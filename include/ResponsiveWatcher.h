#pragma once
// ---------------------------------------------------------------------------
// ResponsiveWatcher — the impure half of Responsive.h.
//
// Responsive.h answers "which size class is this width?" and knows nothing
// else. This file does the two things that judgement cannot do for itself:
// notice that a real widget changed size, and tell a subtree about it.
//
// ---- WHY AN EVENT AND NOT A SIGNAL ----------------------------------------
//
// Qt already has this exact pattern, three times over: LanguageChange,
// StyleChange and LayoutDirectionChange are all ambient facts about the
// environment that ANY widget in a tree may or may not care about. All three
// are delivered as events, ignored by default, and handled by overriding
// event(). Layout mode is the fourth member of that family, so it uses the
// framework's own channel rather than inventing a fifth mechanism.
//
// The decisive practical reason is local to this codebase: half our widgets
// have no Q_OBJECT macro, deliberately (Widgets.h says so out loud; StatBox
// and PomodoroRing are both like that). Event delivery works on any QWidget.
// A signal-based design would force Q_OBJECT — and therefore a moc pass and a
// vtable — onto every plain helper widget that ever wanted to know its mode.
// PomodoroRing needing to shrink on a phone is a live requirement, and with
// events it costs one event() override and zero build-system churn.
//
// Rejected alternatives, briefly, because the reasons matter more than the
// choice:
//   * An abstract base class with virtual applyLayoutMode(). Costs seven page
//     headers to introduce and STILL does not reach NeedsBlockCard,
//     TaskDetailPanel, PomodoroRing or any dialog unless each of those
//     inherits it too — at which point the base class means only "a QWidget
//     that can hear about modes", which QWidget::event() already is, free.
//   * A process-wide policy singleton. It contradicts the whole point: two
//     containers may legitimately disagree (a docked detail panel is narrow
//     while the page behind it is wide). We already have that cautionary
//     tale — ai::breaker() is process-wide, and test_ui warns that its
//     verdicts poison unrelated tests.
//
// ---- THE RULE FOR WIDGETS THAT WANT TO KNOW: PULL AT BIRTH, PUSH ON CHANGE -
//
// A widget built AFTER the last dispatch never saw it, so it must ask once,
// in its constructor:
//
//     const auto mode = responsive::modeOf(parentWidget());
//
// and thereafter it is told, by overriding event():
//
//     bool MyWidget::event(QEvent* e) {
//         if (e->type() == ResponsiveModeEvent::type())
//             applyMode(static_cast<ResponsiveModeEvent*>(e)->mode());
//         return QWidget::event(e);   // ALWAYS chain
//     }
//
// This is exactly how Qt handles palette inheritance, and it is the only part
// of this file a page author has to learn.
//
// NOTE: registerEventType() returns a value chosen at RUNTIME, so type() can
// never be a `case` label. Handlers must use `if`, always.
//
// ---- WHAT A HANDLER MAY DO --------------------------------------------------
//
// **A ResponsiveModeEvent handler may not create or destroy widgets.** Only
// reversible operations: setVisible, QBoxLayout::setDirection, setFixedWidth /
// setMaximumWidth / setMinimumWidth, setFixedSize, setSizePolicy, setWordWrap,
// setText, QStackedLayout::setCurrentIndex, or adding a widget that already
// exists to a layout.
//
// Two reasons, and the second is the one that survives review. First, this
// codebase has already paid for deleting a widget inside a signal it was
// delivering — hence the deleteLater() discipline everywhere. Second, and
// independently: rebuilding on a mode change silently destroys user state
// nobody thinks about — scroll position, keyboard focus, half-typed text, and
// QTreeWidget expansion (ActivitiesPage::m_collapsedFolders exists precisely
// because that state is precious). Building both arrangements once and
// swapping between them is not a compromise here; it is strictly better.
// ---------------------------------------------------------------------------

#include "Responsive.h"

#include <QEvent>
#include <QObject>

class QWidget;

// The notification. Carries the width that decided it as well as the mode,
// because a handler that wants to interpolate (rather than switch) should not
// have to go asking its parent how wide it is.
class ResponsiveModeEvent : public QEvent
{
public:
    // Registered once, lazily. See the runtime-value note above.
    static QEvent::Type type();

    ResponsiveModeEvent(responsive::Mode mode, int widthPx);

    responsive::Mode mode() const { return m_mode; }
    int widthPx() const { return m_widthPx; }

private:
    responsive::Mode m_mode;
    int m_widthPx;
};

// Watches ONE container and tells its subtree when the size class changes.
//
// WHERE TO ATTACH IT — and this is the whole argument for container-driven
// layout in one sentence: toggling the nav rail (Ctrl+B / the hamburger)
// changes the page's width by 190px and produces no MainWindow resize event
// at all, because hiding the rail merely re-runs the body layout. A watcher
// on the window would miss the single most common width change in the app.
// So it attaches to the widget whose width actually governs the content —
// MainWindow's page stack.
//
// LIFETIME: the watcher parents itself to the container, so it dies with it.
class ResponsiveWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ResponsiveWatcher(QWidget* container);

    // The container's current class. Expanded until the first real layout.
    responsive::Mode mode() const { return m_mode; }

signals:
    // For the watcher's OWNER only — chrome that is NOT inside the watched
    // container and so cannot receive the event (MainWindow's header is a
    // sibling of the page stack, not a descendant of it).
    void modeChanged(responsive::Mode mode);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reclassify();
    void deliver();

    QWidget* m_container = nullptr;
    responsive::Mode m_mode = responsive::Mode::Expanded;

    // Sentinel, and it earns its keep: at construction the container's width
    // is 0, which classifies as Compact. Without "I have never answered yet",
    // a desktop launch whose real class is Expanded would compare the first
    // real answer against a bogus Compact, dispatch, and leave every handler
    // having run once with the wrong value in between.
    bool m_everClassified = false;

    // At most one queued dispatch in flight; a resize drag would otherwise
    // post hundreds.
    bool m_deliveryQueued = false;
};

namespace responsive {

// The PULL half of "pull at birth, push on change": which mode governs the
// surface this widget sits on? Walks up the parent chain to the nearest
// watched container. Answers Expanded when there is none, which is the right
// default for a bare widget in a test or a tool.
Mode modeOf(const QWidget* w);

// The dynamic property a watcher stamps on its container. Exposed because it
// is also the hook a stylesheet can select on — with the sharp edge that a
// QSS property selector only re-evaluates after unpolish()/polish(), and
// forgetting that looks like "the style silently did nothing".
extern const char* const kModeProperty;

// ---- dialogs on a phone -----------------------------------------------------
//
// A QDialog is its OWN top-level window with its own minimum, so none of the
// machinery above reaches it: MainWindow's watcher governs MainWindow's page
// stack and nothing else. On a desktop that is fine — a dialog floats at a
// comfortable size over the window. On a phone a floating panel sized for a
// desktop is simply a panel with its right-hand side off the screen.
//
// installCompactDialogFitter() is the one place that decides otherwise: on a
// compact DEVICE — and "device" is the honest question here, because a
// top-level window's container IS the screen — every QDialog is given the
// screen when it is shown, and Android's Back key is mapped to reject().
//
// Back matters MORE once dialogs fill the screen, not less: Qt maps Esc to
// reject() for you but knows nothing about Qt::Key_Back, and a full-screen
// modal with no visible way out is a soft-lock rather than a dialog. That is
// already a reported bug on SyncDialog.
//
// HOW A DIALOG ASKS FOR SOMETHING ELSE (v30.7): one dynamic property,
// `compactFit`, set by the dialog on itself. Absent means "screen", so a
// dialog that has never heard of it behaves exactly as it did in v30.5.
//
//   (absent) / "screen" — the whole available screen. The default, and right
//                     for a dialog that REPLACES the app while it is up:
//                     login, settings, sync.
//   "card"          — natural size, inset a little, centred. For a modal
//                     that sits OVER content the user is still reading —
//                     the block picker over its planner. Seeing what you are
//                     deciding about is the point.
//   "sheet"         — full WIDTH, natural HEIGHT, left where the caller put
//                     it vertically. For dialogs you type one line into
//                     rather than work inside; filling the screen for those
//                     wastes the whole page on a single field.
//   "none"          — leave it entirely alone (a deliberately small floater).
//
// WHY A DECLARATION AND NOT A MEASUREMENT, since the measurement is the
// obvious idea: "give a dialog the smaller of what it asks for and what
// fits" sounds strictly better and regresses the login screen. Its minimum
// is 234px, and a 234px panel adrift on a 360px phone is the exact bug
// full-screen fitting was introduced to cure (see the test that pins it).
// What separates login from the block picker is not size, it is whether the
// app is still behind them — and no size hint can answer that.
//
// One property with a closed set of names, not three booleans: at most one
// answer can be given, so "screen and also none" cannot be written down.
void installCompactDialogFitter(QObject* owner);

// PRECONDITION FOR NESTING, recorded now and deliberately not yet exercised:
// a watcher's dispatch prunes at any descendant that carries kModeProperty,
// so a nested watcher governs its own subtree and the outer one stops at its
// boundary. Without that rule two watchers would deliver contradictory modes
// to the same widget. The pruning is implemented; nesting is not yet used.

} // namespace responsive
