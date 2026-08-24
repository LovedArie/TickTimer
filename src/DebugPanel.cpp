#include "DebugPanel.h"

#include "AffordabilityService.h"
#include "AlarmService.h"
#include "CheckInService.h"

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
// Buttons carry objectNames because test_ui drives this panel by findChild,
// the same contract the Settings pages defend. Text is for humans and may
// be reworded; names are for tests and may not.
QPushButton* button(const QString& text, const char* name)
{
    auto* b = new QPushButton(text);
    b->setObjectName(QLatin1String(name));
    return b;
}
} // namespace

DebugPanel::DebugPanel(
    AffordabilityService* afford, CheckInService* checkIn,
    AlarmService* alarms,
    std::function<QString()> briefing,
    std::function<void(const std::optional<QDateTime>&)> applyClock,
    std::function<QString()> injectProposal,
    std::function<QString()> startIntake,
    std::function<QString()> showSchedule,
    QWidget* parent)
    : QDialog(parent)
    , m_afford(afford)
    , m_checkIn(checkIn)
    , m_alarms(alarms)
    , m_briefing(std::move(briefing))
    , m_applyClock(std::move(applyClock))
    , m_injectProposal(std::move(injectProposal))
    , m_startIntake(std::move(startIntake))
    , m_showSchedule(std::move(showSchedule))
{
    setWindowTitle(tr("TickTimer — debug seams"));
    setModal(false); // the whole point is watching the app react beside it

    auto* root = new QVBoxLayout(this);

    // ---- the clock ---------------------------------------------------------
    // Frozen, not offset: "pretend it is exactly this moment" is the honest
    // contract (services re-ask their provider each sweep, so a frozen
    // answer means every sweep judges the same instant — which is precisely
    // what reproducing a bug wants).
    {
        auto* box    = new QGroupBox(tr("Clock (affordability, check-in, "
                                        "chat briefing)"));
        auto* layout = new QVBoxLayout(box);

        m_clockEdit = new QDateTimeEdit(QDateTime::currentDateTime());
        m_clockEdit->setCalendarPopup(true);
        m_clockEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
        layout->addWidget(m_clockEdit);

        auto* row   = new QHBoxLayout;
        auto* apply = button(tr("Pretend it is this moment"),
                             "debugApplyClock");
        auto* real  = button(tr("Back to real time"), "debugRealClock");
        row->addWidget(apply);
        row->addWidget(real);
        layout->addLayout(row);

        m_clockState = new QLabel(tr("Using the real clock."));
        m_clockState->setWordWrap(true);
        layout->addWidget(m_clockState);

        connect(apply, &QPushButton::clicked, this, [this]() {
            const QDateTime t = m_clockEdit->dateTime();
            m_applyClock(t);
            m_clockState->setText(
                tr("FROZEN at %1 — sweeps and the briefing all judge this "
                   "moment until \"Back to real time\".")
                    .arg(t.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        });
        connect(real, &QPushButton::clicked, this, [this]() {
            m_applyClock(std::nullopt);
            m_clockState->setText(tr("Using the real clock."));
        });

        root->addWidget(box);
    }

    // ---- the deadline heads-up (v28.0/28.1) --------------------------------
    {
        auto* box    = new QGroupBox(tr("Deadline heads-up"));
        auto* layout = new QVBoxLayout(box);

        auto* row   = new QHBoxLayout;
        auto* sweep = button(tr("Sweep now"), "debugAffordSweep");
        auto* reset = button(tr("Forget manners"), "debugAffordForget");
        row->addWidget(sweep);
        row->addWidget(reset);
        layout->addLayout(row);

        auto* hint = new QLabel(
            tr("A nudge only re-speaks when a verdict TURNS Tight — "
               "\"Forget manners\" clears last-spoken verdicts and today's "
               "cap so the next sweep may speak again."));
        hint->setWordWrap(true);
        layout->addWidget(hint);

        connect(sweep, &QPushButton::clicked, m_afford,
                &AffordabilityService::sweep);
        connect(reset, &QPushButton::clicked, this,
                [] { AffordabilityService::forgetManners(); });

        root->addWidget(box);
    }

    // ---- the morning check-in (v28.2) --------------------------------------
    {
        auto* box    = new QGroupBox(tr("Morning check-in"));
        auto* layout = new QVBoxLayout(box);

        auto* row   = new QHBoxLayout;
        auto* sweep = button(tr("Sweep now"), "debugCheckInSweep");
        auto* force = button(tr("Offer now (skip the gate)"),
                             "debugForceCheckIn");
        auto* clear = button(tr("Clear today's ask"), "debugCheckInClear");
        row->addWidget(sweep);
        row->addWidget(force);
        row->addWidget(clear);
        layout->addLayout(row);

        auto* hint = new QLabel(
            tr("\"Offer now\" rehearses the full flow — toast, tap, chat — "
               "without spending the morning's one real ask. \"Sweep now\" "
               "runs the honest gate (morning window, heavy day, once a "
               "day)."));
        hint->setWordWrap(true);
        layout->addWidget(hint);

        connect(sweep, &QPushButton::clicked, m_checkIn,
                &CheckInService::sweep);
        connect(force, &QPushButton::clicked, m_checkIn,
                &CheckInService::forceOffer);
        connect(clear, &QPushButton::clicked, this,
                [] { CheckInService::clearTodaysAsk(); });

        root->addWidget(box);
    }

    // ---- what the model is told --------------------------------------------
    {
        auto* box    = new QGroupBox(tr("The assistant's context"));
        auto* layout = new QVBoxLayout(box);

        auto* show = button(tr("Show the briefing"), "debugShowBriefing");
        layout->addWidget(show);

        auto* hint = new QLabel(
            tr("The exact text every chat turn carries. When the assistant "
               "is wrong, read this FIRST — the field report's lesson was "
               "that every wrong answer traced to a fact this text failed "
               "to state, not to the model or the prompt."));
        hint->setWordWrap(true);
        layout->addWidget(hint);

        connect(show, &QPushButton::clicked, this, &DebugPanel::showBriefing);

        root->addWidget(box);
    }

    // ---- the write boundary (v29.0) ----------------------------------------
    // The panel plays the model: one press composes a real Proposal for
    // the first unsized task in the current briefing turn and presents it
    // in the chat — card, tap, boundary, receipt, copy-aside, the whole
    // road. TESTING.md's v29 recipe starts here.
    {
        auto* box    = new QGroupBox(tr("The write boundary"));
        auto* layout = new QVBoxLayout(box);

        auto* inject = button(tr("Inject sample proposal"),
                              "debugInjectProposal");
        layout->addWidget(inject);

        auto* status = new QLabel(
            tr("Presents a card in the Assistant chat — nothing changes "
               "until you tap Apply there."));
        status->setObjectName(QStringLiteral("debugInjectStatus"));
        status->setWordWrap(true);
        layout->addWidget(status);

        connect(inject, &QPushButton::clicked, this, [this, status]() {
            status->setText(m_injectProposal());
        });

        // v29.1 — the interview, forced by hand: same entry the check-in
        // offer and (later) the badge use, so the recipe and the product
        // walk the same road.
        auto* interview = button(tr("Start intake interview"),
                                 "debugStartIntake");
        layout->addWidget(interview);
        connect(interview, &QPushButton::clicked, this, [this, status]() {
            status->setText(m_startIntake());
        });

        root->addWidget(box);
    }

    // ---- the AI switch -----------------------------------------------------
    {
        auto* box    = new QGroupBox(tr("AI"));
        auto* layout = new QVBoxLayout(box);

        auto* down = new QCheckBox(
            tr("All providers down this run (the C++ voices speak)"));
        down->setObjectName(QStringLiteral("debugAiDown"));
        // Read the live env, don't assume: a dev who launched with
        // TICKTIMER_AI_DOWN=* already set should see the box agree.
        down->setChecked(
            qEnvironmentVariable("TICKTIMER_AI_DOWN").trimmed()
            == QLatin1String("*"));
        layout->addWidget(down);

        auto* hint = new QLabel(
            tr("Sets TICKTIMER_AI_DOWN=* for this process only — nothing "
               "persists, because a debug state that survives a restart is "
               "a support ticket. Applies to requests fired after the "
               "toggle; unchecking clears the variable entirely."));
        hint->setWordWrap(true);
        layout->addWidget(hint);

        connect(down, &QCheckBox::toggled, this, [](bool on) {
            if (on)
                qputenv("TICKTIMER_AI_DOWN", "*");
            else
                qunsetenv("TICKTIMER_AI_DOWN");
        });

        root->addWidget(box);
    }

    // ---- block alarms (v30.6) ----------------------------------------------
    // The seam the fake clock deliberately does NOT reach. AlarmService
    // reads its clock in the constructor — the high-water mark is born at
    // "now" — so there is no now-provider left to rewire afterwards. The
    // hint says so, rather than the panel offering a control that would
    // quietly do nothing, which is the one thing a debug panel must never
    // do.
    {
        auto* box    = new QGroupBox(tr("Block alarms"));
        auto* layout = new QVBoxLayout(box);

        auto* row       = new QHBoxLayout;
        auto* pollNow   = button(tr("Poll now"), "debugAlarmPoll");
        auto* rebuild   = button(tr("Republish"), "debugAlarmRepublish");
        auto* showSched = button(tr("Show the schedule"), "debugAlarmSchedule");
        row->addWidget(pollNow);
        row->addWidget(rebuild);
        row->addWidget(showSched);
        layout->addLayout(row);

        auto* hint = new QLabel(
            tr("\"Poll now\" asks whether anything came due, without waiting "
               "out the timer. \"Show the schedule\" prints the forward "
               "window exactly as it is handed to the platform — on a phone "
               "that list IS what Android holds, so a block missing from it "
               "is a block that will not ring. This group runs on the wall "
               "clock: the fake clock above does not reach it."));
        hint->setWordWrap(true);
        layout->addWidget(hint);

        connect(pollNow, &QPushButton::clicked, m_alarms, &AlarmService::poll);
        connect(rebuild, &QPushButton::clicked, m_alarms,
                &AlarmService::republish);
        connect(showSched, &QPushButton::clicked, this,
                &DebugPanel::showSchedule);

        root->addWidget(box);
    }

    root->addStretch(1);
}

void DebugPanel::showSchedule()
{
    // Fetched per press, for the same reason the briefing is: a schedule is
    // derived state, and the entire question being asked here is "what does
    // it say RIGHT NOW".
    auto* view = new QDialog(this);
    view->setWindowTitle(tr("The forward schedule, right now"));
    view->setAttribute(Qt::WA_DeleteOnClose);
    view->resize(620, 460);

    auto* layout = new QVBoxLayout(view);
    auto* text   = new QPlainTextEdit(m_showSchedule());
    text->setReadOnly(true);
    layout->addWidget(text);

    view->show();
}

void DebugPanel::showBriefing()
{
    // A fresh dialog per press, fetched per press: the briefing is derived
    // state, and a cached copy would be exactly the stale-print bug the
    // catch-up drawer already taught us about (v26.7).
    auto* view = new QDialog(this);
    view->setWindowTitle(tr("What the assistant is told, right now"));
    view->setAttribute(Qt::WA_DeleteOnClose);
    view->resize(560, 520);

    auto* layout = new QVBoxLayout(view);
    auto* text   = new QPlainTextEdit(m_briefing());
    text->setReadOnly(true);
    layout->addWidget(text);

    view->show();
}
