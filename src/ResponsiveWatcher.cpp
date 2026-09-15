#include "ResponsiveWatcher.h"

#include "Widgets.h"

#include <QApplication> // topLevelWidgets, focusChanged
#include <QCoreApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLayout>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QStyle> // alignedRect — centring the "card" fit
#include <QMetaObject>
#include <QVariant>
#include <QWidget>
#include <QWindow> // the platform window's flags, not the widget's

namespace responsive {
const char* const kModeProperty = "responsiveMode";
} // namespace responsive

// ---- the event --------------------------------------------------------------

QEvent::Type ResponsiveModeEvent::type()
{
    // Registered once, on first use. A function-local static is initialised
    // exactly once and thread-safely, which is what makes this safe to call
    // from anywhere without an init step somewhere else that could be
    // forgotten.
    static const QEvent::Type id =
        static_cast<QEvent::Type>(QEvent::registerEventType());
    return id;
}

ResponsiveModeEvent::ResponsiveModeEvent(responsive::Mode mode, int widthPx)
    : QEvent(type()), m_mode(mode), m_widthPx(widthPx)
{
}

// ---- the pull ---------------------------------------------------------------

responsive::Mode responsive::modeOf(const QWidget* w)
{
    for (const QWidget* p = w; p; p = p->parentWidget()) {
        const QVariant v = p->property(kModeProperty);
        if (v.isValid())
            return static_cast<Mode>(v.toInt());
    }
    return Mode::Expanded;
}

// ---- the watcher ------------------------------------------------------------

ResponsiveWatcher::ResponsiveWatcher(QWidget* container)
    : QObject(container), m_container(container)
{
    container->installEventFilter(this);
    // Do NOT classify here. The container's width is 0 until its first
    // layout pass, and answering now would only mean answering wrong; the
    // first Resize or Show settles it (see m_everClassified).
}

bool ResponsiveWatcher::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_container
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        reclassify();

    return QObject::eventFilter(watched, event); // never consume; only observe
}

void ResponsiveWatcher::reclassify()
{
    int w = m_container->width();

    // On a phone-sized device, judge only the width a person can reach
    // (Responsive.h::reachableWidth). Gated on the DEVICE, deliberately: a
    // desktop window dragged wider than its monitor is a choice its owner
    // made, and this project's rule is that phone work leaves the desktop
    // exactly as it was.
    if (isCompactScreen()) {
        if (const QScreen* screen = QGuiApplication::primaryScreen())
            w = responsive::reachableWidth(
                w, screen->availableGeometry().width());
    }

    // Hysteresis needs a previous answer; the very first call has none, so it
    // asks the memoryless overload. Everything after feeds its own last
    // answer back in, which is what makes a slow drag across a breakpoint
    // settle instead of rattle.
    const responsive::Mode next = m_everClassified
                                      ? responsive::modeFor(w, m_mode)
                                      : responsive::modeFor(w);

    // The coalescer. Resize events arrive in floods during a drag, but mode
    // changes are rare BY CONSTRUCTION, so this one line does the work a
    // debounce timer would — and unlike a timer it costs nothing and delays
    // nothing. (The window-geometry save genuinely needed a timer, because
    // there every event described a decision worth writing. Different
    // problem, different tool.)
    if (m_everClassified && next == m_mode)
        return;

    m_mode = next;
    m_everClassified = true;

    // Stamped before delivery so that anything constructed by a handler —
    // and anything that pulls with modeOf() — sees the new answer, not the
    // old one.
    m_container->setProperty(responsive::kModeProperty, int(m_mode));

    if (m_deliveryQueued)
        return; // already scheduled; it will read the latest m_mode
    m_deliveryQueued = true;

    // QUEUED, NOT DIRECT, and this is the important line in the file.
    //
    // eventFilter runs inside QWidget::setGeometry, inside a layout
    // activation — the layout engine is part-way through arranging children
    // and is walking structures a handler could disturb. Running arbitrary
    // page code there is the same hazard family as the delete-inside-a-signal
    // crash this project already paid for. One hop through the event loop
    // means handlers run when the loop owns the stack and the layout engine
    // has finished.
    //
    // THE COST, so nobody has to rediscover it: a test that resizes and then
    // asserts immediately reads the state BEFORE delivery. Spin the loop
    // first — QCoreApplication::processEvents() or QTest::qWait(0).
    QMetaObject::invokeMethod(this, [this] { deliver(); },
                              Qt::QueuedConnection);
}

namespace {

// Depth-first dispatch, written by hand because Qt does not propagate custom
// events to children the way it does LanguageChange.
//
// The prune is the nesting precondition from the header: a descendant that
// carries the property is itself a watched container, so it owns its subtree
// and this walk stops at its edge. Without it, a widget under two watchers
// would receive two contradictory modes and the last one delivered would win
// at random.
// A HIDDEN WIDGET IS NEVER RE-LAID-OUT, AND ITS MINIMUM STILL COUNTS (31.2.1).
//
// Qt drops a hidden widget's layout work on the floor: updateGeometry()
// skips a hidden widget's parent, and QLayout ignores a LayoutRequest for a
// widget that is not visible. Sensible — until the numbers are read. A
// QStackedWidget's minimum is the largest of ALL its pages, current or not,
// and a top-level window is clamped up to that minimum. So a page built in
// Expanded, then hidden, then told it is Compact keeps the minimum it had
// under the DESKTOP stylesheet: UpcomingPage's four filter chips reported a
// 339px row that was really 210 once recomputed, and the window's minimum
// carried the stale figure. On a 375px iPhone that is the whole clip.
//
// invalidate() then activate(), in that order, on every hidden widget's
// layout in the subtree. activate() alone returns early on a layout that
// still believes it is activated; invalidate() clears that belief, and
// activate() then walks every nested layout (activateRecursiveHelper) and
// recomputes. It does not check visibility — only the event path does. Run
// AFTER the mode signal, because the compact stylesheet is applied by a
// handler of that signal and a recomputation before it would cache the old
// padding a second time.
void relayoutHidden(QWidget* parent)
{
    const QObjectList& children = parent->children();
    for (QObject* child : children) {
        auto* w = qobject_cast<QWidget*>(child);
        if (!w)
            continue;
        if (!w->isVisible() && w->layout()) {
            w->layout()->invalidate();
            w->layout()->activate();
        }
        relayoutHidden(w);
    }
}

void dispatchTo(QWidget* parent, ResponsiveModeEvent& ev)
{
    const QObjectList& children = parent->children();
    for (QObject* child : children) {
        auto* w = qobject_cast<QWidget*>(child);
        if (!w)
            continue;
        if (w->property(responsive::kModeProperty).isValid())
            continue; // governed by its own watcher
        QCoreApplication::sendEvent(w, &ev);
        dispatchTo(w, ev);
    }
}

} // namespace

void ResponsiveWatcher::deliver()
{
    m_deliveryQueued = false;

    ResponsiveModeEvent ev(m_mode, m_container->width());

    // The container hears it too: it is a surface in its own right, and a
    // page stack may well want to know.
    QCoreApplication::sendEvent(m_container, &ev);
    dispatchTo(m_container, ev);

    emit modeChanged(m_mode);

    // Last, once every handler — including the stylesheet swap — has run.
    relayoutHidden(m_container);
}

// ---- dialogs on a phone -----------------------------------------------------

namespace {

// The fit itself: one dialog, one room. Pulled out of the event filter in
// 31.2.1 because it gained a second caller. It used to run once, when a
// dialog was shown, and that was only right while the room could not change
// under an open dialog. The iPhone keyboard changes it: the page now shrinks
// the app to what is visible (web/index.html), and a dialog fitted to the old
// height keeps its bottom half under the keys.
void fitToRoom(QDialog* dialog, const QRect& room)
{
    const QString fit = dialog->property("compactFit").toString();
    if (fit == QLatin1String("none"))
        return;

    // ---- "card": a modal you can see the app BEHIND -----------------------
    // For dialogs that sit OVER content the user is still reading — the block
    // picker over its planner. Filling the screen for those hides the very
    // thing being edited and reads as a page, not a decision.
    //
    // Why this is a declaration and not a size test: the obvious rule is
    // "give a dialog the smaller of what it asks for and what fits", and it
    // is wrong. LoginDialog's minimum is 234px, and shrinking it to that is
    // precisely the "small floating panel adrift on a phone" bug the
    // full-screen fit was introduced to cure. The difference between the two
    // is not how big they are, it is what they ARE: login REPLACES the app
    // for its duration, the picker sits over it. No sizeHint can answer that,
    // so the dialog says.
    if (fit == QLatin1String("card")) {
        if (QLayout* l = dialog->layout())
            l->activate();
        const QSize want =
            dialog->sizeHint().expandedTo(dialog->minimumSizeHint());
        // The inset is what makes it read as a card. It is a nicety, though,
        // and never worth clipping for: a dialog whose hard minimum needs the
        // whole width gets the whole width.
        const int inset = 16;
        const QSize box(qMax(1, room.width() - 2 * inset),
                        qMax(1, room.height() - 2 * inset));
        QSize size = want.boundedTo(box);
        size = size.expandedTo(dialog->minimumSizeHint().boundedTo(room.size()));
        dialog->setGeometry(
            QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size, room));
        return;
    }

    if (fit != QLatin1String("sheet")) {
        // NO TITLE BAR on a dialog that IS the screen (31.2.1). Android never
        // drew one, so nobody noticed that Qt for WebAssembly does: it paints
        // its own frame, with a close button, on every top-level window that
        // is not frameless. On the login screen that × was a trap — closing
        // the gate ends main(), and the tab shows "TickTimer closed".
        //
        // Set on the QWindow, not with QWidget::setWindowFlags(). The widget
        // call goes through setParent(), which on an already-created window
        // marks it uncreated and hidden, and this runs after show. QWindow::
        // setFlags() hands the change straight to the platform window, which
        // on WebAssembly is a CSS class and on Android nothing at all.
        if (QWindow* handle = dialog->windowHandle())
            handle->setFlag(Qt::FramelessWindowHint, true);

        // setGeometry, not showFullScreen(): the dialog stays an ordinary
        // dialog — exec(), reject(), the caller's result handling all
        // unchanged — and only its rectangle differs. A window STATE would
        // have to be undone on a tablet, and "remember to undo the platform
        // tweak" is where these things rot.
        dialog->setGeometry(room);
        return;
    }
    // FULL WIDTH, NATURAL HEIGHT, pinned to the top. Filling the screen is
    // right for a dialog you work inside and wrong for one you type a line
    // into: quick capture became a screenful of white with its hint floating
    // in the middle, spending the whole page on one text field.
    //
    // TAKE THE WIDTH, LEAVE THE VERTICAL POSITION ALONE.
    //
    // Pinning the sheet to the top looked obvious and was wrong twice. Qt
    // reports availableGeometry().y() as 0 on this device — and the parent
    // window's geometry as 0 too — while Android actually draws its status
    // bar over that strip. Anchoring to either put the sheet under the clock
    // and sliced the text field in half. Qt Widgets has no safe-area API to
    // ask, so the honest move is not to pretend we know where the top is.
    //
    // The caller already chose a good y (popup() puts it in the parent's
    // upper third — palette position, clear of any system bar), and that
    // choice needs no help from a phone. Only the WIDTH was ever wrong here.
    dialog->resize(room.width(), dialog->height());
    if (QLayout* l = dialog->layout())
        l->activate();
    const int needed =
        qMax(dialog->sizeHint().height(), dialog->minimumSizeHint().height());
    dialog->setGeometry(room.x(), dialog->y(), room.width(),
                        qMin(needed, room.height()));
}

class CompactDialogFitter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (!dialog || !dialog->isWindow())
            return QObject::eventFilter(watched, event);

        // ONE property, named values, at most one answer (v30.7). This used
        // to be two independent booleans — noCompactFit and compactTopSheet —
        // and adding "card" would have made three mutually-exclusive flags
        // where setting two is legal and means nothing. A closed set of names
        // cannot express the contradiction. Absent reads as "screen", so
        // every dialog that never heard of this property keeps the v30.5
        // behaviour exactly.
        const QString fit = dialog->property("compactFit").toString();
        if (fit == QLatin1String("none"))
            return QObject::eventFilter(watched, event);

        // Android delivers its Back gesture as Qt::Key_Back. QDialog maps only
        // Qt::Key_Escape to reject(), so without this a phone has no way out
        // of a modal at all — the SyncDialog soft-lock. Handled before the fit
        // so it works even for dialogs that opt out of resizing.
        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Back) {
            dialog->reject();
            return true; // consumed: do not also let it close the activity
        }

        if (event->type() == QEvent::Show && isCompactScreen()) {
            // QUEUED — the third time this project has learned the same rule.
            // A Show event arrives BEFORE the widget has been polished and
            // laid out, so sizeHint() still reflects an unstyled widget: the
            // capture field's stylesheet padding had not been applied yet, its
            // hint came back too short, and the sheet squeezed the field until
            // its text was clipped. One hop through the event loop and every
            // size is the real one.
            //
            // QPointer, not a raw pointer: a dialog can be shown and destroyed
            // within one turn of the loop, and this runs after.
            QPointer<QDialog> guarded(dialog);
            QMetaObject::invokeMethod(
                dialog,
                [guarded]() {
                    if (!guarded || !guarded->isVisible())
                        return;
                    if (const QScreen* screen = QGuiApplication::primaryScreen())
                        fitToRoom(guarded, screen->availableGeometry());
                },
                Qt::QueuedConnection);
        }
        return QObject::eventFilter(watched, event);
    }
};

// Scroll the focused widget into view inside every QScrollArea that holds it,
// innermost first. The loop walks the PARENT chain, so an outer area is asked
// to reveal the inner area rather than the field — which is exactly what
// ensureWidgetVisible() needs, since it measures against its own content.
void keepInView(QWidget* focused)
{
    if (!focused || !focused->isVisible())
        return;
    QWidget* target = focused;
    for (QWidget* p = focused->parentWidget(); p; p = p->parentWidget()) {
        auto* area = qobject_cast<QScrollArea*>(p);
        if (area && area->widget() && area->widget()->isAncestorOf(target)) {
            area->ensureWidgetVisible(target);
            target = area;
        }
    }
}

} // namespace

void responsive::installCompactDialogFitter(QObject* owner)
{
    // One filter on the application object sees every widget's events, so a
    // dialog written next year is covered without anyone remembering to call
    // anything. The alternative — a line in each of eleven constructors — is
    // eleven chances to forget and no way to notice.
    auto* fitter = new CompactDialogFitter(owner);
    QCoreApplication::instance()->installEventFilter(fitter);

    // ...and again whenever the room changes under a dialog that is already
    // up: the iPhone keyboard, a phone turned sideways. The rect comes from
    // the SIGNAL rather than from asking the screen again, so a test can hand
    // it a room no offscreen screen will ever report.
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QObject::connect(screen, &QScreen::availableGeometryChanged, fitter,
                         [](const QRect& room) {
                             if (!isCompactScreen())
                                 return;
                             const QWidgetList tops =
                                 QApplication::topLevelWidgets();
                             for (QWidget* w : tops) {
                                 auto* dialog = qobject_cast<QDialog*>(w);
                                 if (dialog && dialog->isVisible())
                                     fitToRoom(dialog, room);
                             }
                         });
    }
}

void responsive::installCompactFocusKeeper(QObject* owner)
{
    // Two moments put a focused field out of sight, and they arrive in either
    // order on a phone: the tap that gives it focus, and the keyboard that
    // opens because of that tap and takes the bottom of the screen away.
    //
    // BOTH QUEUED, for the reason the fitter above learned: the keyboard's
    // room change is followed by the refit and the layout pass, and a scroll
    // computed before those finish is computed against the old height.
    // QPointer because focus can leave, and its widget can die, in one turn.
    auto later = [owner](QWidget* w) {
        if (!w || !isCompactScreen())
            return;
        QPointer<QWidget> guarded(w);
        QMetaObject::invokeMethod(
            owner, [guarded]() { keepInView(guarded); }, Qt::QueuedConnection);
    };

    QObject::connect(qApp, &QApplication::focusChanged, owner,
                     [later](QWidget*, QWidget* now) { later(now); });

    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QObject::connect(screen, &QScreen::availableGeometryChanged, owner,
                         [later](const QRect&) {
                             later(QApplication::focusWidget());
                         });
    }
}
