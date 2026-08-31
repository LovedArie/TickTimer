#include "ProbeOverlay.h"

#include "Widgets.h" // isCompactScreen — the rule this probe reports on

#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QVBoxLayout>

namespace probe
{

bool isRequested()
{
    // Deliberately "is it SET", not "is it 1" — matching how the screenshot
    // tool has always asked. A diagnostic switch should turn on when you
    // mention it; making people remember a value is how a switch gets typed
    // wrong on a phone keyboard and reported as "the probe doesn't work".
    return qEnvironmentVariableIsSet("TICKTIMER_PROBE");
}

// ---------------------------------------------------------------------------
// The pure half. Formatting only — no QScreen, no environment, no verdict of
// its own. Everything it prints was handed to it.
// ---------------------------------------------------------------------------
QStringList lines(const QRect& screenGeometry,
                  const QRect& availableGeometry,
                  qreal devicePixelRatio,
                  qreal logicalDpi,
                  bool compactVerdict,
                  const QSize& windowSize)
{
    const auto rect = [](const QRect& r) {
        return QStringLiteral("%1x%2 at (%3,%4)")
            .arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y());
    };

    // The short side is the number isCompactScreen() actually compares, so
    // it gets its own line rather than being left as arithmetic for someone
    // squinting at a photograph.
    const int shortSide =
        qMin(availableGeometry.width(), availableGeometry.height());

    QStringList out;
    out << QStringLiteral("TickTimer layout probe");
    out << QStringLiteral("screen geometry     %1").arg(rect(screenGeometry));
    out << QStringLiteral("available geometry  %1").arg(rect(availableGeometry));
    out << QStringLiteral("  short side        %1").arg(shortSide);
    out << QStringLiteral("device pixel ratio  %1")
               .arg(devicePixelRatio, 0, 'f', 2);
    out << QStringLiteral("logical DPI         %1").arg(logicalDpi, 0, 'f', 1);
    out << QStringLiteral("window size         %1")
               .arg(windowSize.isEmpty() ? QStringLiteral("(no window yet)")
                                         : rect(QRect(QPoint(), windowSize)));
    out << QString();
    // The verdict, spelled out with the rule beside it. A reader who has
    // never opened Widgets.h can still tell whether the answer follows from
    // the numbers above it — which is the difference between a probe and a
    // number to be taken on faith.
    out << QStringLiteral("isCompactScreen()   %1   (short side %2 %3 %4)")
               .arg(compactVerdict ? QStringLiteral("YES - phone layout")
                                   : QStringLiteral("no  - desktop layout"))
               .arg(shortSide)
               .arg(shortSide < kCompactThreshold ? QStringLiteral("<")
                                                  : QStringLiteral(">="))
               .arg(kCompactThreshold);

    // If those two disagree, the environment forced the answer and the
    // numbers no longer explain it. Say so, or the screenshot lies.
    if (compactVerdict != (shortSide < kCompactThreshold))
        out << QStringLiteral("  ** FORCED by TICKTIMER_COMPACT - "
                              "this verdict does not follow from the screen **");

    out << QString();
    out << QStringLiteral("(tap to dismiss)");
    return out;
}

// ---------------------------------------------------------------------------
// The impure half: ask the real screen, then hand everything to the pure one.
// ---------------------------------------------------------------------------
QStringList lines(const QSize& windowSize)
{
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        // Headless. Worth reporting rather than crashing or inventing zeros —
        // isCompactScreen() answers "desktop" in exactly this case, and a
        // reader should be able to see that that is why.
        return {QStringLiteral("TickTimer layout probe"),
                QStringLiteral("no primary screen (headless platform)"),
                QStringLiteral("isCompactScreen() answers 'no' by default"),
                QString(),
                QStringLiteral("(tap to dismiss)")};
    }

    return lines(screen->geometry(),
                 screen->availableGeometry(),
                 screen->devicePixelRatio(),
                 screen->logicalDotsPerInch(),
                 isCompactScreen(), // the ONE implementation of the rule
                 windowSize);
}

} // namespace probe

// ---------------------------------------------------------------------------

ProbeOverlay* ProbeOverlay::showIfRequested(const QSize& windowSize)
{
    if (!probe::isRequested())
        return nullptr; // nothing constructed, nothing shown, nothing to pay

    auto* overlay = new ProbeOverlay(windowSize);
    overlay->show();
    overlay->raise();
    return overlay;
}

ProbeOverlay::ProbeOverlay(const QSize& windowSize)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_DeleteOnClose);
    // Never take the keyboard. The probe is shown before the login dialog,
    // and a diagnostic that steals focus from a password field is a worse
    // bug than the one it was built to find.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName(QStringLiteral("probeOverlay"));

    m_text = new QLabel(probe::lines(windowSize).join(QLatin1Char('\n')), this);
    m_text->setObjectName(QStringLiteral("probeOverlayText"));
    // Monospace so the columns line up in a photograph, and selectable so
    // the numbers can be copied on a platform where that is possible.
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(9);
    m_text->setFont(mono);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Its own colours, not the theme's: this must stay legible over whatever
    // is behind it, including a dialog that has not been styled yet.
    m_text->setStyleSheet(QStringLiteral(
        "QLabel#probeOverlayText {"
        "  background: #101418;"
        "  color: #e8f0f8;"
        "  border: 1px solid #4c9aff;"
        "  border-radius: 6px;"
        "  padding: 10px;"
        "}"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_text);

    // Top-left, inset. Not centred: the probe must not cover the thing it is
    // describing, and on a phone the top-left corner is the one region no
    // layout in this app puts a control in.
    const QScreen* screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    adjustSize();
    move(area.x() + 8, area.y() + 8);
}

void ProbeOverlay::mousePressEvent(QMouseEvent* event)
{
    close(); // WA_DeleteOnClose does the rest
    event->accept();
}
