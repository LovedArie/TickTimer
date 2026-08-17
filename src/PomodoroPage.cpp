#include "PomodoroPage.h"

#include "AppData.h"
#include "PomodoroEngine.h"
#include "PomodoroLink.h"
#include "PomodoroMiniWindow.h"
#include "Prefs.h"
#include "Stats.h"
#include "Theme.h"
#include "TrackerService.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
// These are DEFAULTS, not the law: the classic Pomodoro values that a
// first run starts with and the settings fall back to.
constexpr int kDefaultFocusMin      = 25;
constexpr int kDefaultShortBreakMin = 5;
constexpr int kDefaultLongBreakMin  = 15;
constexpr int kMinMinutes           = 1;   // spin-box bounds: a sane range
constexpr int kMaxMinutes           = 180; // keeps a fat-fingered 9999 out

// The QSettings keys, in one place so a typo can't split the read from the
// write. "pomodoro/" groups them under one section in the settings store.
// (The notify / link / mini-position keys live in Prefs.h — they have
// consumers OUTSIDE this page, and shared keys need a shared home.)
constexpr auto kKeyFocus = "pomodoro/focusMinutes";
constexpr auto kKeyShort = "pomodoro/shortBreakMinutes";
constexpr auto kKeyLong  = "pomodoro/longBreakMinutes";
} // namespace

PomodoroPage::PomodoroPage(PomodoroEngine* engine, PomodoroLink* link,
                           TrackerService* tracker, const AppData* data,
                           QWidget* parent)
    : QWidget(parent)
    , m_engine(engine)
    , m_link(link)
    , m_tracker(tracker)
    , m_data(data)
{
    // Load remembered settings FIRST and TELL the services — the page is
    // the doctrine's reader; the engine and the link stay QSettings-free.
    QSettings settings;
    const int focusMin = settings.value(kKeyFocus, kDefaultFocusMin).toInt();
    const int shortMin = settings.value(kKeyShort, kDefaultShortBreakMin).toInt();
    const int longMin  = settings.value(kKeyLong,  kDefaultLongBreakMin).toInt();
    m_engine->setDurations(focusMin, shortMin, longMin);
    m_link->setEnabled(prefs::pomodoroDrivesTracker());

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
    for (int i = 0; i < PomodoroEngine::kRoundsPerCycle; ++i) {
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
    auto* miniBtn = new QPushButton(tr("Mini timer"), this);
    miniBtn->setObjectName("quiet");
    miniBtn->setToolTip(
        tr("A small always-on-top timer you can park over other apps"));
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    buttons->addWidget(m_startBtn);
    buttons->addWidget(resetBtn);
    buttons->addWidget(skipBtn);
    buttons->addWidget(miniBtn);
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

    auto* focusSpin = makeSpin(focusMin);
    auto* shortSpin = makeSpin(shortMin);
    auto* longSpin  = makeSpin(longMin);

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

    // Each control writes ONE setting through, then re-tells the engine.
    // In Qt6 QSpinBox::valueChanged is the int overload — the function-
    // pointer form resolves it unambiguously.
    const auto pushDurations = [this, focusSpin, shortSpin, longSpin]() {
        m_engine->setDurations(focusSpin->value(), shortSpin->value(),
                               longSpin->value());
        refreshHint();
    };
    connect(focusSpin, &QSpinBox::valueChanged, this, [pushDurations](int v) {
        QSettings().setValue(kKeyFocus, v);
        pushDurations();
    });
    connect(shortSpin, &QSpinBox::valueChanged, this, [pushDurations](int v) {
        QSettings().setValue(kKeyShort, v);
        pushDurations();
    });
    connect(longSpin, &QSpinBox::valueChanged, this, [pushDurations](int v) {
        QSettings().setValue(kKeyLong, v);
        pushDurations();
    });

    // ---- the two behaviour toggles ----------------------------------------
    // Checkboxes, not dialog items: they change what the RUNNING timer does,
    // so they belong next to it (the Settings dialog keeps only what has no
    // natural home — scope rule from the settings addendum §F).
    auto* notifyCheck = new QCheckBox(tr("Notify when a phase ends"), this);
    notifyCheck->setChecked(prefs::pomodoroNotify());
    connect(notifyCheck, &QCheckBox::toggled, this,
            [](bool on) { prefs::setPomodoroNotify(on); });

    auto* linkCheck = new QCheckBox(
        tr("Drive the tracked block (focus → focus, break → break, "
           "paused → distracted)"),
        this);
    linkCheck->setToolTip(
        tr("While you're tracking a block on the agenda, the Pomodoro's "
           "phases switch that block's timer kind for you. It never picks "
           "or stops a block by itself."));
    linkCheck->setChecked(prefs::pomodoroDrivesTracker());
    connect(linkCheck, &QCheckBox::toggled, this, [this](bool on) {
        prefs::setPomodoroDrivesTracker(on);
        m_link->setEnabled(on); // told, not read — the page is the reader
        refreshLinkStatus();
    });

    // The status line UNDER the checkbox (v19.6): the link used to work
    // invisibly — right rules, zero acknowledgement — and invisible
    // correctness reads as broken (owner report). One live sentence now
    // says what the link is doing and, when it's doing nothing, exactly
    // which human act it is waiting for.
    m_linkStatus = new QLabel(this);
    m_linkStatus->setObjectName("sub");
    m_linkStatus->setAlignment(Qt::AlignCenter);
    m_linkStatus->setWordWrap(true);
    m_linkStatus->setMaximumWidth(420);
    // The sentence depends on BOTH machines, so it listens to both — the
    // test caught the half-wired version: press Start while the tracker
    // is already Focusing and no tracker signal fires (the link sees
    // desired == state and rightly does nothing), leaving a stale "press
    // Start" until the next tick bailed it out.
    connect(m_tracker, &TrackerService::stateChanged,
            this, &PomodoroPage::refreshLinkStatus);
    connect(m_tracker, &TrackerService::tick,
            this, &PomodoroPage::refreshLinkStatus);
    connect(m_engine, &PomodoroEngine::modeChanged,
            this, &PomodoroPage::refreshLinkStatus);

    auto* toggles = new QVBoxLayout;
    toggles->setSpacing(4);
    toggles->addWidget(notifyCheck, 0, Qt::AlignHCenter);
    toggles->addWidget(linkCheck, 0, Qt::AlignHCenter);
    toggles->addWidget(m_linkStatus, 0, Qt::AlignHCenter);

    m_hint = new QLabel(this);
    m_hint->setObjectName("sub");
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    m_hint->setMaximumWidth(360);

    layout->addWidget(m_phaseLabel);
    layout->addWidget(m_ring, 0, Qt::AlignHCenter);
    layout->addLayout(dotsRow);
    layout->addLayout(buttons);
    layout->addLayout(settingsRow);
    layout->addLayout(toggles);
    layout->addWidget(m_hint, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    // The view's whole contract: user gestures go IN as engine calls,
    // repaints come OUT of engine signals. No state lives here.
    connect(m_startBtn, &QPushButton::clicked,
            m_engine, &PomodoroEngine::toggle);
    connect(resetBtn, &QPushButton::clicked, m_engine, &PomodoroEngine::reset);
    connect(skipBtn, &QPushButton::clicked, m_engine, &PomodoroEngine::skip);
    connect(miniBtn, &QPushButton::clicked, this, &PomodoroPage::showMini);
    connect(m_engine, &PomodoroEngine::changed,
            this, &PomodoroPage::refresh);

    refreshHint();
    refreshLinkStatus();
    refresh();
}

void PomodoroPage::refreshLinkStatus()
{
    // Every branch names its ACTOR: what the machine is doing, or which
    // human act it is waiting for — never just "inactive".
    if (!m_link->isEnabled()) {
        m_linkStatus->setText(
            tr("Link off — the Pomodoro and the block timer run "
               "independently."));
        return;
    }
    if (m_tracker->state() == TrackerService::State::Idle) {
        // v19.7: the offer is concrete when it can be — if a block is
        // planned for RIGHT NOW, name it and name the one action needed.
        const QString liveId = m_tracker->liveEventNow();
        if (const Event* live = m_data->eventById(liveId)) {
            m_linkStatus->setText(
                tr("Link on: press Start and the Pomodoro will drive "
                   "“%1” (planned for right now).")
                    .arg(m_data->eventLabel(*live)));
        } else {
            m_linkStatus->setText(
                tr("Link on, waiting: no block is planned for right now — "
                   "start tracking one on the Planner, or press Start "
                   "during a planned block's hours."));
        }
        return;
    }

    const Event* e = m_data->eventById(m_tracker->trackedEventId());
    const QString name = e ? m_data->eventLabel(*e) : tr("a block");

    if (!m_engine->engaged()) {
        m_linkStatus->setText(
            tr("Tracking “%1” — press Start and the Pomodoro takes the "
               "wheel.").arg(name));
        return;
    }

    QString state = tr("recording distracted time (Pomodoro paused)");
    if (m_tracker->state() == TrackerService::State::Focusing)
        state = tr("recording focus");
    else if (m_tracker->state() == TrackerService::State::OnBreak)
        state = tr("recording break time");
    m_linkStatus->setText(
        tr("Driving “%1” — %2 · %3 this interval.")
            .arg(name, state,
                 stats::formatSeconds(m_tracker->liveSeconds())));
}

PomodoroPage::~PomodoroPage()
{
    // The one manual delete this codebase allows itself, and the header
    // says why: the card must be parentless (Windows hides owned windows
    // with their minimized owner), so Qt's parent tree cannot clean it up.
    delete m_mini;
}

void PomodoroPage::showMini()
{
    if (!m_mini) {
        m_mini = new PomodoroMiniWindow(m_engine);
        // ⤢ on the card brings the real window back (and hides the card).
        connect(m_mini, &PomodoroMiniWindow::expandRequested, this, [this]() {
            window()->showNormal();
            window()->raise();
            window()->activateWindow();
        });
    }
    m_mini->show();
    m_mini->raise();
}

void PomodoroPage::refreshHint()
{
    // Re-read from the spin values' single source of truth (QSettings) so
    // this text can never drift from what the engine was told.
    QSettings settings;
    m_hint->setText(
        tr("%1 min focus, %2 min break, a %3 min long break every %4th "
           "round. Skip to preview the next phase.")
            .arg(settings.value(kKeyFocus, kDefaultFocusMin).toInt())
            .arg(settings.value(kKeyShort, kDefaultShortBreakMin).toInt())
            .arg(settings.value(kKeyLong, kDefaultLongBreakMin).toInt())
            .arg(PomodoroEngine::kRoundsPerCycle));
}

void PomodoroPage::refresh()
{
    const bool focusPhase = (m_engine->phase() == PomodoroEngine::Phase::Focus);
    const QColor color = focusPhase ? theme::focus() : theme::brk();

    m_phaseLabel->setText(m_engine->phaseName());
    m_phaseLabel->setStyleSheet(
        QStringLiteral("font-size:17px; font-weight:800; color:%1;")
            .arg(color.name()));

    const int total = m_engine->phaseTotalSeconds();
    const double progress = 1.0 - double(m_engine->remaining()) / double(total);
    m_ring->setState(progress, m_engine->timeText(),
                     tr("Round %1").arg(m_engine->round()), color);

    // Dots: done rounds solid green, the current one haloed, future grey.
    const int currentIndex =
        (m_engine->round() - 1) % PomodoroEngine::kRoundsPerCycle;
    for (int i = 0; i < m_dots.size(); ++i) {
        QString style = QStringLiteral("border-radius:5px; background:%1;");
        if (i < currentIndex)
            style = style.arg(theme::focus().name());
        else if (i == currentIndex && focusPhase)
            style = style.arg(theme::focus().name())
                    + "border: 2px solid rgba(47,126,110,0.35);";
        else
            style = style.arg(theme::track().name());
        m_dots[i]->setStyleSheet(style);
    }

    m_startBtn->setText(m_engine->running() ? tr("Pause") : tr("Start"));
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
