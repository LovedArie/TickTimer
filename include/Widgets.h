#pragma once
// ---------------------------------------------------------------------------
// Widgets — tiny shared UI pieces used by several pages.
//
// The rule of thumb applied here: the moment two screens hand-build the
// same little cluster of labels, extract a widget. "Don't Repeat Yourself"
// applies to UI code exactly as it does to logic.
// ---------------------------------------------------------------------------

#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QHBoxLayout>
#include <QPixmap>
#include <QVBoxLayout>

// A small stat card: a muted caption over a big coloured value —
// the "FOCUSED / 2h 05m" boxes from the prototype's sidebar.
class StatBox : public QFrame
{
    // No Q_OBJECT macro: this class declares no signals, slots, or
    // properties of its own, so it doesn't need Qt's meta-object machinery.
    // Leaving the macro out when unneeded is normal Qt practice.
public:
    explicit StatBox(const QString& caption, const QColor& valueColor,
                     QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName("panel"); // reuse the card styling from the theme

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 9, 12, 9);
        layout->setSpacing(2);

        auto* captionLabel = new QLabel(caption.toUpper(), this);
        captionLabel->setStyleSheet(
            "color:#616974; font-size:10px; font-weight:600; "
            "letter-spacing:1px; border:none; background:transparent;");

        m_value = new QLabel("0m", this);
        m_value->setStyleSheet(
            QString("color:%1; font-size:19px; font-weight:800; "
                    "border:none; background:transparent;")
                .arg(valueColor.name()));

        layout->addWidget(captionLabel);
        layout->addWidget(m_value);
    }

    void setValue(const QString& text) { m_value->setText(text); }

private:
    QLabel* m_value = nullptr; // child widget: Qt's parent-child ownership
                               // deletes it with us — no delete needed, ever
};

// A round colour swatch as a QPixmap — used as the icon for category dots
// in lists and legends (the prototype's ubiquitous ".dot").
inline QPixmap colorDot(const QColor& color, int diameter = 10)
{
    QPixmap pm(diameter, diameter);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, diameter, diameter);
    return pm;
}

// Font-size tweaks — through a helper, never by raw arithmetic. On some
// platforms the default QFont is PIXEL-sized and pointSizeF() returns -1;
// "pointSizeF() - 1.5" then yields an invalid font and a runtime warning.
// A guard in one place beats the same guard remembered in eight places.
inline QFont scaledFont(const QFont& base, qreal deltaPoints,
                        bool bold = false)
{
    QFont f = base;
    qreal pts = f.pointSizeF();
    if (pts <= 0)
        pts = 10.0; // pixel-sized font: adopt a sane point baseline
    f.setPointSizeF(qMax<qreal>(6.0, pts + deltaPoints));
    f.setBold(bold);
    return f;
}

// "9:00 AM" from minutes-after-midnight (handles 1440 -> 12:00 AM).
inline QString timeLabel(int minutesAfterMidnight)
{
    const int mm = minutesAfterMidnight % (24 * 60);
    int hour = mm / 60;
    const int minute = mm % 60;
    const bool am = hour < 12;
    hour %= 12;
    if (hour == 0)
        hour = 12;
    return QStringLiteral("%1:%2 %3")
        .arg(hour)
        .arg(minute, 2, 10, QChar('0'))
        .arg(am ? "AM" : "PM");
}

// "1h 30m" from a slot count.
// NAMING TRAP (learned the hard way): the parameter must NOT be called
// `slots` — Qt defines `slots` (and `signals`, `emit`) as macros for its
// signal/slot syntax, so the name would be macro-erased and the file
// wouldn't compile. Never use those three words as identifiers in Qt code.
inline QString durationLabel(int slotCount)
{
    const int mins = slotCount * 30;
    const int h = mins / 60, m = mins % 60;
    if (h == 0) return QStringLiteral("%1m").arg(m);
    if (m == 0) return QStringLiteral("%1h").arg(h);
    return QStringLiteral("%1h %2m").arg(h).arg(m);
}

// Host a max-width panel at the top-left with breathing room to its
// right — the standard framing for single-panel pages (Upcoming,
// Special days). Without the wrapper, a QScrollArea would stretch the
// panel to full width and the max-width would be ignored.
inline QWidget* wrapLeft(QWidget* panel)
{
    auto* host = new QWidget;
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(panel, 0, Qt::AlignTop);
    layout->addStretch(1);
    return host;
}

// ---- mobile / compact-screen support (Android addendum) --------------------

#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>
#include <QScroller>

// "Is this a phone-sized screen?" — asked ONCE per launch, by geometry, not
// by platform. Why not #ifdef Q_OS_ANDROID: a 10-inch Android tablet has
// desktop-class room and should get the desktop layout; a future tiny
// Windows tablet should get the compact one. The QUESTION is "how much
// space?", so the code asks about space. (Threshold: a phone's short side
// is ~360-450 logical px; small laptops start well above 600.)
inline bool isCompactScreen()
{
    // Test hook: TICKTIMER_COMPACT=1 forces compact mode regardless of the
    // real screen — the screenshot tool uses it to render "the phone layout"
    // on a desktop-sized virtual display. Same philosophy as the tool
    // itself: if a layout mode can't be produced on demand, it can't be
    // verified or documented repeatably.
    if (qEnvironmentVariableIsSet("TICKTIMER_COMPACT"))
        return qEnvironmentVariable("TICKTIMER_COMPACT") != QLatin1String("0");

    const QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen)
        return false; // headless/offscreen: behave like desktop
    const QRect g = screen->availableGeometry();
    return qMin(g.width(), g.height()) < 600;
}

// Make a QScrollArea flick-scrollable by finger. QScroller is Qt's kinetic-
// scrolling engine; grabbing the TouchGesture on the VIEWPORT (the widget
// that actually clips and pans the content) makes touch-drags scroll and
// leaves taps to pass through to children as clicks.
//
// Deliberately TouchGesture, NOT LeftMouseButtonGesture: mouse drags keep
// their existing desktop meanings (the agenda's edge-resize drag would be
// stolen by a mouse-gesture scroller). Tradeoff accepted: on a touchscreen
// the agenda scrolls instead of edge-resizing — blocks are still resized
// through the block dialog. Scrolling is the more common gesture by far.
inline void makeTouchScrollable(QScrollArea* area)
{
    QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
}
