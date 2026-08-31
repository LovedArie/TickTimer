#pragma once
// ---------------------------------------------------------------------------
// ProbeOverlay — TICKTIMER_PROBE, drawn, for the platforms with no stdout.
//
// WHY THIS EXISTS. This codebase already believes that a behaviour you
// cannot produce on demand is a behaviour you cannot verify:
// TICKTIMER_COMPACT forces the phone layout, TICKTIMER_AI_DOWN forces a
// dead provider, TICKTIMER_PROBE prints layout minimums, and Ctrl+Shift+D
// presses every injection seam by hand. All of those answer through a
// console.
//
// The WebAssembly build broke that assumption, and it broke it on the one
// platform it was written for. **Safari on iOS has no developer console.**
// Reaching one needs a Mac, a cable and Safari's Web Inspector — precisely
// the dependency the whole WebAssembly approach exists to avoid. So on the
// device this app is being carried to, qInfo() goes nowhere a human can
// read. A diagnosis you cannot READ on the device is not a diagnosis.
//
// This is therefore not a second probe. It is the same switch with a second
// renderer: `tools/screenshot.cpp` prints layout minimums to stdout for the
// platform that has one, and this draws the screen readings onto the screen
// for the platform that does not. `web/index.html` turns `?probe` in the URL
// into TICKTIMER_PROBE=1 inside the process, because a browser tab has no
// environment for a person to set one in.
//
// THE QUESTION IT WAS BUILT TO ANSWER. `isCompactScreen()` decides the whole
// phone layout by comparing the short side of
// QScreen::availableGeometry() against 600. On Android that input turned out
// to be PHYSICAL pixels on a 1080x2400 device, so the check answered
// "desktop" on a phone and the app opened with its content clipped off the
// right edge. The cause was recorded in docs as *suspected, not measured* —
// nothing had ever printed the value from inside the running app. Nothing has
// printed it from inside a browser either.
//
// TICKTIMER_COMPACT cannot settle that. Forcing the mode proves what the mode
// DOES; it says nothing about what the real input IS. **A test that supplies
// its own input can never validate that input.** One screengrab of this
// overlay is the whole answer, on any of the three platforms.
//
// SHAPE. Pure policy, impure adapter — the same split as
// overlapsAnyScreen()/availableScreenRects() in Widgets.h, and for the same
// reason. `probe::lines(...)` takes plain values and returns the text, so
// what the probe REPORTS can be pinned by a test against any screen anyone
// can imagine, including ones nobody owns. The overload that asks
// QGuiApplication for today's real screen is the only impure part, and it
// does no formatting of its own.
//
// The overlay itself is deliberately dumb glass: it shows those lines and
// closes when tapped. It decides nothing, and with TICKTIMER_PROBE unset it
// is never constructed at all — so the desktop app is byte-for-byte the app
// it was unless someone asks for this.
// ---------------------------------------------------------------------------

#include <QRect>
#include <QSize>
#include <QStringList>
#include <QWidget>

class QLabel;

namespace probe
{

// Has someone asked for the probe? One place asks, so the answer cannot
// drift between the overlay and anything that later wants the same switch.
// On desktop and Android the environment carries it; on WebAssembly
// web/index.html sets the same variable from `?probe` before main() runs.
bool isRequested();

// The compact-layout threshold, named once so the overlay can SHOW the rule
// it is reporting against rather than leaving the reader to remember it.
// This is a display constant only: the verdict below is never re-derived
// here, it is passed in from isCompactScreen() so there is exactly one
// implementation of the rule.
inline constexpr int kCompactThreshold = 600;

// PURE. The readings as text, from values a caller supplies.
//
// Every line is one fact with its name attached, because the person reading
// this is holding a phone and photographing it — an unlabelled number in a
// screenshot is a number nobody can use a week later.
QStringList lines(const QRect& screenGeometry,
                  const QRect& availableGeometry,
                  qreal devicePixelRatio,
                  qreal logicalDpi,
                  bool compactVerdict,
                  const QSize& windowSize);

// The impure half: today's real primary screen, asked once. `windowSize` is
// whatever the caller considers the app's window — it may legitimately be
// empty when the probe runs before any window exists, which is the normal
// case on a phone (the point is to read the screen before logging in).
QStringList lines(const QSize& windowSize);

} // namespace probe

// ---------------------------------------------------------------------------
// The glass. Parentless Qt::Tool + frameless + stays-on-top: the same recipe
// NotificationToast and PomodoroMiniWindow use, and for the same reason —
// it must sit above whatever is showing, including the login dialog, which
// on a phone is the first and sometimes only thing anyone sees. Parentless
// specifically, because on Windows a parent is an OWNER and an owned window
// hides with a minimized owner (the v19.5.1 scar).
// ---------------------------------------------------------------------------
class ProbeOverlay : public QWidget
{
public:
    // The only entry point callers need. Returns nullptr — having built
    // nothing at all — unless TICKTIMER_PROBE is set. Deleting itself on
    // close is why the return value can be safely ignored.
    static ProbeOverlay* showIfRequested(const QSize& windowSize = QSize());

    explicit ProbeOverlay(const QSize& windowSize);

protected:
    // Tap anywhere to dismiss. There is no close button, because on a phone
    // a close button is another 48dp thing to get right and the whole
    // surface is a bigger target than any button could be.
    void mousePressEvent(QMouseEvent* event) override;

private:
    QLabel* m_text = nullptr;
};
