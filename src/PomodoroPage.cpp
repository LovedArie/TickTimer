#include "PomodoroPage.h"

#include "Theme.h"
#include "Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
// These are now DEFAULTS, not the law: the classic Pomodoro values that a
// first run starts with and the settings fall back to.
constexpr int kDefaultFocusMin      = 25;
constexpr int kDefaultShortBreakMin = 5;
constexpr int kDefaultLongBreakMin  = 15;
constexpr int kRoundsPerCycle       = 4;   // long break every 4th round
constexpr int kMinMinutes           = 1;   // spin-box bounds: a sane range
constexpr int kMaxMinutes           = 180; // keeps a fat-fingered 9999 out

// The QSettings keys, in one place so a typo can't split the read from the
// write. "pomodoro/" groups them under one section in the settings store.
constexpr auto kKeyFocus = "pomodoro/focusMinutes";
constexpr auto kKeyShort = "pomodoro/shortBreakMinutes";
constexpr auto kKeyLong  = "pomodoro/longBreakMinutes";
} // namespace

PomodoroPage::PomodoroPage(QWidget* parent)
    : QWidget(parent)
{
    // Load the user's remembered durations FIRST — everything below depends
    // on them. A default QSettings() reads the store keyed by the application
    // name set in main() (QApplication::setApplicationName("TickTimer")); the
    // same store the app will use for any future window/UI preference. It is
    // deliberately SEPARATE from data.json: a settings tweak must never dirty
    // the domain file or drag it through migration logic.
    QSettings settings;
    m_focusMinutes      = settings.value(kKeyFocus, kDefaultFocusMin).toInt();
    m_shortBreakMinutes = settings.value(kKeyShort, kDefaultShortBreakMin).toInt();
    m_longBreakMinutes  = settings.value(kKeyLong,  kDefaultLongBreakMin).toInt();
    m_remaining         = m_focusMinutes * 60;

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 40, 26, 26);
    layout->setSpacing(16);
    layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_phaseLabel = new QLabel(this);
    m_phaseLabel->setStyleSheet("font-size:17px; font-weight:800;");
    m_phaseLabel->setAlignment(Qt::AlignCenter);

    m_ring = new PomodoroRing(this);

    // The four round dots.
    auto* dotsRow = new QHBoxLayout;
    dotsRow->setSpacing(8);
    dotsRow->addStretch(1);
    for (int i = 0; i < kRoundsPerCycle; ++i) {
        auto* dot = new QLabel(this);
        dot->setFixedSize(11, 11);
        m_dots.append(dot);
        dotsRow->addWidget(dot);
    }
    dotsRow->addStretch(1);

    m_startBtn = new QPushButton(tr("Start"), this);
    m_startBtn->setObjectName("primary");
    auto* resetBtn = new QPushButton(tr("Reset"), this);
    resetBtn->setObjectName("quiet");
    auto* skipBtn = new QPushButton(tr("Skip"), this);
    skipBtn->setObjectName("quiet");
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    buttons->addWidget(m_startBtn);
    buttons->addWidget(resetBtn);
    buttons->addWidget(skipBtn);
    buttons->addStretch(1);

    // ---- duration controls: adjustable, and remembered across runs -------
    // A small factory so the three spin boxes are configured identically —
    // DRY beats three near-copies that could drift apart.
    const auto makeSpin = [this](int value) {
        auto* spin = new QSpinBox(this);
        spin->setRange(kMinMinutes, kMaxMinutes);
        spin->setValue(value);
        spin->setSuffix(tr(" min"));
        spin->setFixedWidth(88);
        spin->setCursor(Qt::PointingHandCursor);
        return spin;
    };
    const auto makeLabel = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setStyleSheet("color:#616974; font-size:11px; font-weight:700;");
        return l;
    };

    auto* focusSpin = makeSpin(m_focusMinutes);
    auto* shortSpin = makeSpin(m_shortBreakMinutes);
    auto* longSpin  = makeSpin(m_longBreakMinutes);

    // Desktop: one wide "Focus [25] Break [5] Long break [15]" strip.
    // Compact: that strip's minimum (~510px) exceeds a phone's width, so the
    // three pairs stack vertically instead. Same widgets, different QBoxLayout
    // DIRECTION — QBoxLayout's constructor argument is the whole difference,
    // which is why both cases share every line below it.
    auto* settingsRow = new QBoxLayout(isCompactScreen()
                                           ? QBoxLayout::TopToBottom
                                           : QBoxLayout::LeftToRight);
    settingsRow->setSpacing(6);
    const auto addPair = [&](const QString& text, QWidget* spin) {
        auto* pair = new QHBoxLayout;
        pair->addStretch(1);
        pair->addWidget(makeLabel(text));
        pair->addWidget(spin);
        pair->addStretch(1);
        settingsRow->addLayout(pair);
    };
    addPair(tr("Focus"), focusSpin);
    addPair(tr("Break"), shortSpin);
    addPair(tr("Long break"), longSpin);

    // Each control writes ONE setting through, then re-applies. In Qt6
    // QSpinBox::valueChanged is the int overload — the function-pointer form
    // resolves it unambiguously. QSettings coalesces writes cheaply, so at
    // this scale no debounce is needed.
    connect(focusSpin, &QSpinBox::valueChanged, this, [this](int v) {
        m_focusMinutes = v;
        QSettings().setValue(kKeyFocus, v);
        applyDurations();
    });
    connect(shortSpin, &QSpinBox::valueChanged, this, [this](int v) {
        m_shortBreakMinutes = v;
        QSettings().setValue(kKeyShort, v);
        applyDurations();
    });
    connect(longSpin, &QSpinBox::valueChanged, this, [this](int v) {
        m_longBreakMinutes = v;
        QSettings().setValue(kKeyLong, v);
        applyDurations();
    });

    m_hint = new QLabel(this); // text is set in applyDurations() (dynamic)
    m_hint->setObjectName("sub");
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    m_hint->setMaximumWidth(320);

    layout->addWidget(m_phaseLabel);
    layout->addWidget(m_ring, 0, Qt::AlignHCenter);
    layout->addLayout(dotsRow);
    layout->addLayout(buttons);
    layout->addLayout(settingsRow);
    layout->addWidget(m_hint, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &PomodoroPage::tick);
    connect(m_startBtn, &QPushButton::clicked,
            this, &PomodoroPage::toggleRunning);
    connect(resetBtn, &QPushButton::clicked, this, &PomodoroPage::reset);
    connect(skipBtn, &QPushButton::clicked, this, &PomodoroPage::skip);

    applyDurations(); // seeds the hint text and paints the first frame
}

int PomodoroPage::phaseTotalSeconds() const
{
    switch (m_phase) {
    case Phase::Focus:      return m_focusMinutes * 60;
    case Phase::ShortBreak: return m_shortBreakMinutes * 60;
    case Phase::LongBreak:  return m_longBreakMinutes * 60;
    }
    return m_focusMinutes * 60; // unreachable, but compilers want proof
}

void PomodoroPage::applyDurations()
{
    // A duration change only reshapes the CURRENT phase when the clock is
    // idle — we never yank seconds out from under a running countdown; the
    // new length takes effect on the next phase instead. This is the least
    // surprising behaviour: fiddling with settings mid-focus doesn't reset
    // your progress, but a change while stopped is visible immediately.
    if (!m_running)
        m_remaining = phaseTotalSeconds();

    m_hint->setText(
        tr("%1 min focus, %2 min break, a %3 min long break every %4th "
           "round. Skip to preview the next phase.")
            .arg(m_focusMinutes)
            .arg(m_shortBreakMinutes)
            .arg(m_longBreakMinutes)
            .arg(kRoundsPerCycle));

    refresh();
}

void PomodoroPage::advancePhase()
{
    // The transition table, straight from the prototype:
    //   Focus -> Short break (rounds 1–3) or Long break (round 4)
    //   any break -> Focus, and the round counter advances.
    if (m_phase == Phase::Focus) {
        m_phase = (m_round % kRoundsPerCycle == 0) ? Phase::LongBreak
                                                   : Phase::ShortBreak;
    } else {
        m_phase = Phase::Focus;
        ++m_round;
    }
    m_remaining = phaseTotalSeconds();
}

void PomodoroPage::tick()
{
    if (--m_remaining <= 0)
        advancePhase();
    refresh();
}

void PomodoroPage::toggleRunning()
{
    m_running = !m_running;
    if (m_running)
        m_timer.start();
    else
        m_timer.stop();
    refresh();
}

void PomodoroPage::reset()
{
    m_running = false;
    m_timer.stop();
    m_phase = Phase::Focus;
    m_round = 1;
    m_remaining = phaseTotalSeconds(); // now reads the user's focus length
    refresh();
}

void PomodoroPage::skip()
{
    advancePhase();
    refresh();
}

void PomodoroPage::refresh()
{
    const QColor color =
        (m_phase == Phase::Focus) ? theme::focus() : theme::brk();
    const QString phaseName = (m_phase == Phase::Focus) ? tr("Focus")
        : (m_phase == Phase::ShortBreak)                ? tr("Short break")
                                                        : tr("Long break");

    m_phaseLabel->setText(phaseName);
    m_phaseLabel->setStyleSheet(
        QStringLiteral("font-size:17px; font-weight:800; color:%1;")
            .arg(color.name()));

    const int total = phaseTotalSeconds();
    const double progress = 1.0 - double(m_remaining) / double(total);
    const QString timeText =
        QStringLiteral("%1:%2")
            .arg(m_remaining / 60, 2, 10, QChar('0'))
            .arg(m_remaining % 60, 2, 10, QChar('0'));
    m_ring->setState(progress, timeText, tr("Round %1").arg(m_round), color);

    // Dots: done rounds solid green, the current one haloed, future grey.
    const int currentIndex = (m_round - 1) % kRoundsPerCycle;
    for (int i = 0; i < m_dots.size(); ++i) {
        QString style = QStringLiteral("border-radius:5px; background:%1;");
        if (i < currentIndex)
            style = style.arg(theme::focus().name());
        else if (i == currentIndex && m_phase == Phase::Focus)
            style = style.arg(theme::focus().name())
                    + "border: 2px solid rgba(47,126,110,0.35);";
        else
            style = style.arg(theme::track().name());
        m_dots[i]->setStyleSheet(style);
    }

    m_startBtn->setText(m_running ? tr("Pause") : tr("Start"));
}

// ---- PomodoroRing ------------------------------------------------------------

PomodoroRing::PomodoroRing(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void PomodoroRing::setState(double progress, const QString& timeText,
                            const QString& roundText, const QColor& color)
{
    m_progress  = progress;
    m_timeText  = timeText;
    m_roundText = roundText;
    m_color     = color;
    update();
}

void PomodoroRing::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int stroke = 14;
    const QRectF r = QRectF(rect()).adjusted(stroke / 2 + 1, stroke / 2 + 1,
                                             -stroke / 2 - 1, -stroke / 2 - 1);

    // Track circle, then the progress arc on top. Qt measures arcs in
    // 1/16ths of a degree from 3 o'clock, counter-clockwise; we start at
    // 12 o'clock (90*16) and sweep clockwise (negative span).
    QPen pen(theme::track(), stroke, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    p.drawEllipse(r);

    if (m_progress > 0.0) {
        pen.setColor(m_color);
        p.setPen(pen);
        p.drawArc(r, 90 * 16, int(-m_progress * 360.0 * 16.0));
    }

    // A multiplied size needs the same guard as a subtracted one:
    const qreal basePts = font().pointSizeF() > 0 ? font().pointSizeF() : 10.0;
    p.setFont(scaledFont(font(), basePts * 1.6, /*bold=*/true));
    p.setPen(theme::ink());
    p.drawText(rect().adjusted(0, -10, 0, -10), Qt::AlignCenter, m_timeText);

    p.setFont(scaledFont(font(), -1));
    p.setPen(theme::inkSoft());
    p.drawText(rect().adjusted(0, 44, 0, 0), Qt::AlignCenter, m_roundText);
}
