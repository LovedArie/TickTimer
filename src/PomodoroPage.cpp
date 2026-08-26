#include "PomodoroPage.h"

#include "AppData.h"
#include "PomodoroEngine.h"
#include "PomodoroLink.h"
#include "PomodoroMiniWindow.h"
#include "Prefs.h"
#include "Stats.h"
#include "Theme.h"
#include "TrackerService.h"
#include "ResponsiveWatcher.h"
#include "Widgets.h"
#include "Touch.h" // v30.7 — the 48dp minimum, for the stepper buttons

#include <QCheckBox>
#include <QScrollArea>
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
    // Asked once, about the DEVICE: which of two presentations this page is
    // built as. Not a container-width question — the cards and the desktop
    // strip are different widget trees, and a ResponsiveModeEvent handler may
    // not create or destroy widgets (ResponsiveWatcher.h's rule).
    m_phoneShell = isCompactScreen();
    // Load remembered settings FIRST and TELL the services — the page is
    // the doctrine's reader; the engine and the link stay QSettings-free.
    QSettings settings;
    const int focusMin = settings.value(kKeyFocus, kDefaultFocusMin).toInt();
    const int shortMin = settings.value(kKeyShort, kDefaultShortBreakMin).toInt();
    const int longMin  = settings.value(kKeyLong,  kDefaultLongBreakMin).toInt();
    m_engine->setDurations(focusMin, shortMin, longMin);
    m_link->setEnabled(prefs::pomodoroDrivesTracker());

    // The page's content lives on an inner widget so the page itself can be
    // a QScrollArea. See the wrap at the end of this constructor for why.
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(26, 40, 26, 26);
    layout->setSpacing(16);
    layout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_phaseLabel = new QLabel(content);
    m_phaseLabel->setStyleSheet("font-size:17px; font-weight:800;");
    m_phaseLabel->setAlignment(Qt::AlignCenter);

    m_ring = new PomodoroRing(content);

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
    // NOT OFFERED ON A PHONE (v30.7), because the sentence in that tooltip
    // is the whole feature and Android will not honour it. An ordinary app
    // cannot draw over other apps there without the SYSTEM_ALERT_WINDOW
    // overlay permission, so what the card actually floats over is
    // TickTimer itself — including its own modal dialogs. It was found
    // sitting exactly on top of the block picker's "Plan it" button, which
    // is how a working control came to look like a missing one.
    //
    // Hidden rather than deleted: on a desktop it does exactly what it
    // promises, and this is a phone-shaped device question, not a
    // container-width one.
    miniBtn->setVisible(!isCompactScreen());
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    buttons->addWidget(m_startBtn);
    buttons->addWidget(resetBtn);
    buttons->addWidget(skipBtn);
    buttons->addWidget(miniBtn);
    buttons->addStretch(1);

    // ---- the phone's settings-row vocabulary (v30.7) -----------------------
    // Three pieces, used by everything below: a hairline, a row that carries
    // a label on the left and a control on the right, and a stepper. Android
    // settings screens are built from exactly this and nothing else, which is
    // most of why they read as one system rather than as a form.
    const auto makeDivider = [content]() {
        auto* line = new QFrame(content);
        line->setFixedHeight(1);
        line->setStyleSheet(
            QStringLiteral("background:%1;").arg(theme::line().name()));
        return line;
    };

    const auto makeRow = [content](const QString& text, QWidget* control) {
        auto* row = new QWidget(content);
        // 56, not 48: the row is not itself a target, it HOLDS one, and a
        // 48dp control needs somewhere to sit without touching the divider.
        row->setMinimumHeight(56);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(14, 4, 8, 4);
        h->setSpacing(8);
        auto* label = new QLabel(text, row);
        // Wrapping matters here and nowhere on the desktop version: these
        // labels are full sentences on a 360dp screen, and an unwrapped
        // QLabel's minimum width is its whole text.
        label->setWordWrap(true);
        h->addWidget(label, 1);
        h->addWidget(control, 0);
        return row;
    };

    // − 25 min + . The buttons drive the hidden QSpinBox through stepDown()
    // and stepUp(), so the range, the clamping and every connect() already
    // wired to it keep working untouched; the value label just listens.
    const auto makeStepperRow = [&](const QString& text, QSpinBox* spin) {
        auto* box = new QWidget(content);
        auto* h   = new QHBoxLayout(box);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);

        const auto stepBtn = [&](const QString& glyph) {
            auto* b = new QPushButton(glyph, box);
            b->setObjectName("quiet");
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedSize(touch::kMinTarget, touch::kMinTarget);
            return b;
        };
        auto* minus = stepBtn(QStringLiteral("−")); // U+2212, a real minus
        auto* value = new QLabel(box);
        value->setAlignment(Qt::AlignCenter);
        value->setMinimumWidth(76);
        value->setStyleSheet(QStringLiteral("font-weight:700;"));
        auto* plus = stepBtn(QStringLiteral("+"));

        const auto show = [value, spin]() { value->setText(spin->text()); };
        show();
        connect(spin, &QSpinBox::valueChanged, value, [show](int) { show(); });
        connect(minus, &QPushButton::clicked, spin, &QSpinBox::stepDown);
        connect(plus, &QPushButton::clicked, spin, &QSpinBox::stepUp);

        h->addWidget(minus);
        h->addWidget(value);
        h->addWidget(plus);
        return makeRow(text, box);
    };

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
    //
    // v30.5 — this is THE reference conversion from the responsive addendum.
    // It used to read isCompactScreen(), asked once, about the SCREEN. Now the
    // direction is a function of the width this page was actually handed, and
    // it can change while the app runs: rotate the phone, or open the nav rail
    // on a narrow desktop window, and the strip folds or unfolds to match.
    // Nothing is rebuilt to do it — one call to setDirection() moves the same
    // three pairs, which is why the handler is allowed to run at any moment.
    auto* settingsRow = new QBoxLayout(QBoxLayout::LeftToRight);
    m_settingsRow = settingsRow;
    settingsRow->setSpacing(6);
    const auto addPair = [&](const QString& text, QWidget* spin) {
        auto* pair = new QHBoxLayout;
        pair->addStretch(1);
        pair->addWidget(makeLabel(text));
        pair->addWidget(spin);
        pair->addStretch(1);
        settingsRow->addLayout(pair);
    };
    if (!m_phoneShell) {
        addPair(tr("Focus"), focusSpin);
        addPair(tr("Break"), shortSpin);
        addPair(tr("Long break"), longSpin);
    } else {
        // ---- the phone presents these as Android list rows (v30.7) ---------
        // A QSpinBox is a desktop control end to end: its value is a text
        // field you are expected to type into, and its up/down arrows are
        // each half of a ~30dp box — about 15dp of thumb, a third of the
        // guideline. Reported as the page "not feeling like a modern Android
        // UI", and this is the largest single reason why.
        //
        // The spin boxes SURVIVE, hidden. They still hold the value, the
        // range, the clamping and every connect() that writes the setting
        // through to QSettings and the engine — so the visible stepper below
        // drives them rather than replacing them, and there is still exactly
        // one place that knows what a duration is. Two controls writing the
        // same setting is how they drift.
        focusSpin->hide();
        shortSpin->hide();
        longSpin->hide();
        m_settingsCard = new QFrame(content);
        m_settingsCard->setObjectName("panel");
        auto* cardV = new QVBoxLayout(m_settingsCard);
        cardV->setContentsMargins(0, 0, 0, 0);
        cardV->setSpacing(0);
        cardV->addWidget(makeStepperRow(tr("Focus"), focusSpin));
        cardV->addWidget(makeDivider());
        cardV->addWidget(makeStepperRow(tr("Break"), shortSpin));
        cardV->addWidget(makeDivider());
        cardV->addWidget(makeStepperRow(tr("Long break"), longSpin));
    }

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

    // The label is SHORT on purpose, and this is a width fix as much as a
    // copy one. A QCheckBox cannot word-wrap, so its label is a hard promise
    // about width: the old sentence — "(focus → focus, break → break, paused
    // → distracted)" — measured 476px on its own and, because a
    // QStackedWidget's minimum is the max over all its pages, single-handedly
    // stopped the WHOLE WINDOW from fitting on a phone, even while the
    // Planner was the page on screen.
    //
    // Nothing is lost by cutting it. That parenthetical is still in the
    // tooltip below, and m_linkStatus narrates the live mapping in a
    // word-wrapped label a few lines further down. It was a width contract
    // paying for information the page already gave twice.
    auto* linkCheck = new QCheckBox(tr("Drive the tracked block"), content);
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
    if (!m_phoneShell) {
        toggles->addWidget(notifyCheck, 0, Qt::AlignHCenter);
        toggles->addWidget(linkCheck, 0, Qt::AlignHCenter);
        toggles->addWidget(m_linkStatus, 0, Qt::AlignHCenter);
    } else {
        // A TICK MEANS "I HAVE CHOSEN"; A SWITCH MEANS "THIS IS ON" (v30.7).
        // Both of these take effect the instant they are touched — there is
        // no OK button on this page — which on Android is a switch, not a
        // checkbox. The shape is the part that says whether anything has
        // happened yet, so the wrong one is a lie about state rather than a
        // style preference.
        //
        // The QCheckBoxes stay alive and hidden, exactly like the spin boxes
        // above, because they carry the prefs wiring. The switch mirrors and
        // drives; the checkbox remains the one thing that writes.
        notifyCheck->hide();
        linkCheck->hide();

        const auto mirror = [this](QCheckBox* box) {
            auto* sw = new ToggleSwitch(this);
            sw->setChecked(box->isChecked());
            sw->setToolTip(box->toolTip());
            connect(sw, &ToggleSwitch::toggled, box, &QCheckBox::setChecked);
            // Both directions: something else may flip the preference, and a
            // switch showing the opposite of the truth is worse than no
            // switch. setChecked on an unchanged value emits nothing, so this
            // cannot loop.
            connect(box, &QCheckBox::toggled, sw, &ToggleSwitch::setChecked);
            return sw;
        };

        m_togglesCard = new QFrame(content);
        m_togglesCard->setObjectName("panel");
        auto* cardV = new QVBoxLayout(m_togglesCard);
        cardV->setContentsMargins(0, 0, 0, 0);
        cardV->setSpacing(0);
        cardV->addWidget(makeRow(notifyCheck->text(), mirror(notifyCheck)));
        cardV->addWidget(makeDivider());
        cardV->addWidget(makeRow(linkCheck->text(), mirror(linkCheck)));
        // The live sentence belongs UNDER the row it explains, indented to
        // read as its subtitle rather than as a new setting.
        auto* statusWrap = new QWidget(content);
        auto* statusH    = new QHBoxLayout(statusWrap);
        statusH->setContentsMargins(14, 0, 14, 12);
        m_linkStatus->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        statusH->addWidget(m_linkStatus);
        cardV->addWidget(statusWrap);
    }

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
    if (m_settingsCard)
        layout->addWidget(m_settingsCard);
    layout->addLayout(toggles);
    if (m_togglesCard)
        layout->addWidget(m_togglesCard);
    layout->addWidget(m_hint, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    // ---- THE OVERLAPPING SENTENCES (v30.7) ---------------------------------
    // On the phone the link-status line and the hint below it were drawn on
    // top of each other, both cut off mid-word. Neither was too long: both
    // are word-wrapped, and both carried a setMaximumWidth from the desktop
    // layout (420 and 360).
    //
    // That maximum is the bug. Inside a QScrollArea with widgetResizable the
    // content's height comes from the layout's sizeHint, and a wrapped
    // QLabel's height is only knowable once its WIDTH is fixed — heightForWidth.
    // A label narrower than the column it sits in, centred, gives the layout
    // two different widths to reason about, and it reserves height for the
    // wrong one. The text then wraps to the real width and runs past the
    // space allotted, straight over its neighbour.
    //
    // Letting them be exactly as wide as the column removes the ambiguity.
    // The desktop keeps its maximums, where they stop a sentence sprawling
    // across a 1400px window.
    if (m_phoneShell) {
        m_linkStatus->setMaximumWidth(QWIDGETSIZE_MAX);
        m_hint->setMaximumWidth(QWIDGETSIZE_MAX);
    }

    // The view's whole contract: user gestures go IN as engine calls,
    // repaints come OUT of engine signals. No state lives here.
    connect(m_startBtn, &QPushButton::clicked,
            m_engine, &PomodoroEngine::toggle);
    connect(resetBtn, &QPushButton::clicked, m_engine, &PomodoroEngine::reset);
    connect(skipBtn, &QPushButton::clicked, m_engine, &PomodoroEngine::skip);
    connect(miniBtn, &QPushButton::clicked, this, &PomodoroPage::showMini);
    connect(m_engine, &PomodoroEngine::changed,
            this, &PomodoroPage::refresh);

    // ---- the wrap ---------------------------------------------------------
    // A QScrollArea's minimum IGNORES its content, which is the only
    // construction that makes a page's width promise structurally safe: no
    // future long label here can pin the window again, the way the link
    // checkbox above did. It is the same recipe SpecialDaysPage and
    // ArchivePage already use, and it earns its keep vertically too — the
    // ring, the buttons, the three duration rows and two checkboxes do not
    // fit in a landscape phone's 384px of height, and now they scroll.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Vertical only. Horizontal scrolling would HIDE a width overflow rather
    // than fix it, and hiding it is how 522px went unnoticed for four
    // versions.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    // setWidget() silently turns on the child's autoFillBackground — the
    // exact mechanism that once painted the agenda black (Theme.h). Undo it
    // AFTER setWidget, or the page gets a slab of palette colour over the
    // themed background.
    content->setAutoFillBackground(false);
    makeTouchScrollable(scroll);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

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
    // The second lock on the same door. Hiding the button is what a user
    // sees; this is what makes it true — a shortcut, a restored session or
    // a future caller cannot conjure a card that the platform will only let
    // float over our own dialogs.
    if (isCompactScreen())
        return;

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

// ---- responding to the container's size class --------------------------------

bool PomodoroPage::event(QEvent* e)
{
    // registerEventType() picks its value at RUNTIME, so this can never be a
    // switch case — see ResponsiveWatcher.h.
    if (e->type() == ResponsiveModeEvent::type())
        applyLayoutMode(static_cast<ResponsiveModeEvent*>(e)->mode());

    return QWidget::event(e); // always chain: Qt still needs every other event
}

void PomodoroPage::applyLayoutMode(responsive::Mode mode)
{
    const bool compact = mode == responsive::Mode::Compact;

    m_settingsRow->setDirection(compact ? QBoxLayout::TopToBottom
                                        : QBoxLayout::LeftToRight);

    // The ring was a hard 230px square in every mode — 230 of a phone's 384
    // logical px, before the page's own 26px margins. It is the one item here
    // whose size is purely decorative, so it is also the one that should
    // yield first.
    m_ring->setDiameter(compact ? 160
                                : mode == responsive::Mode::Medium ? 200
                                                                   : 230);
}

// ---- PomodoroRing ------------------------------------------------------------

PomodoroRing::PomodoroRing(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void PomodoroRing::setDiameter(int px)
{
    // The ring paints into whatever square it is given (see paintEvent), so
    // resizing it needs no other change. Kept as a setter rather than an
    // event handler on purpose: the RING should not know that layout modes
    // exist — the page it belongs to does, and tells it. Glass decides
    // nothing, one layer down as well as one layer up.
    setFixedSize(px, px);
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
