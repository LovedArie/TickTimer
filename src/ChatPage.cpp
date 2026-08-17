#include "ChatPage.h"

#include "IntakeClient.h"
#include "ProposalCard.h"
#include "Mood.h"

#include "AppData.h"
#include "ChatClient.h"
#include "DayBriefing.h"
#include "Prefs.h"
#include "LlmProvider.h"
#include "Theme.h"
#include "Widgets.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// ChatInput
// ---------------------------------------------------------------------------

ChatInput::ChatInput(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("chatInput"));
    setPlaceholderText(tr("Ask about your day…  (Enter to send, Shift+Enter for a new line)"));
    // Three lines tall: enough to see a pasted question, small enough that the
    // conversation stays the biggest thing on screen. Auto-growing was
    // considered and skipped — it needs a documentContentsChanged handler and
    // a max-height clamp to avoid the box eating the log, which is more
    // machinery than the feature has earned.
    setFixedHeight(74);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void ChatInput::keyPressEvent(QKeyEvent* event)
{
    const bool isEnter = event->key() == Qt::Key_Return
                         || event->key() == Qt::Key_Enter;
    // Shift+Enter (and Ctrl+Enter, which some people's fingers insist on)
    // fall through to the base class and insert a newline.
    if (isEnter && !(event->modifiers() & Qt::ShiftModifier)
        && !(event->modifiers() & Qt::ControlModifier)) {
        event->accept();
        emit submitted();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// ChatPage
// ---------------------------------------------------------------------------

namespace
{
// Bubble styling, in one place. Two rules, mirrored: the user speaks in the
// app's own focus green on the right, the assistant in a white card on the
// left. Colour is doing the speaker-attribution work that a name label would
// otherwise cost a line of vertical space for, on every single message.
QString bubbleStyle(ai::Role role, bool localOnly)
{
    if (localOnly) {
        return QStringLiteral(
            "background: rgba(194,91,84,0.10); color:#8A3F3A; "
            "border:1px solid rgba(194,91,84,0.30); border-radius:12px; "
            "padding:9px 12px;");
    }
    if (role == ai::Role::User) {
        return QStringLiteral(
            "background: rgba(47,126,110,0.12); color:#245F53; "
            "border:1px solid rgba(47,126,110,0.22); border-radius:12px; "
            "padding:9px 12px;");
    }
    return QStringLiteral(
        "background:#FFFFFF; color:#272C33; border:1px solid #E2E6E0; "
        "border-radius:12px; padding:9px 12px;");
}
} // namespace

ChatPage::ChatPage(AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
{
    m_client = new ChatClient(this); // QObject child: deleted with the page

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 18);
    layout->setSpacing(10);

    // ---- header -----------------------------------------------------------
    auto* headerRow = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    auto* title = new QLabel(tr("Assistant"), this);
    title->setObjectName(QStringLiteral("h1"));
    auto* sub = new QLabel(
        tr("Ask about today. It can see your plan, your tracked time and your "
           "tasks — and it can't change any of them."),
        this);
    sub->setObjectName(QStringLiteral("sub"));
    sub->setWordWrap(true);
    titles->addWidget(title);
    titles->addWidget(sub);
    headerRow->addLayout(titles, 1);

    // "What can it see?" — the transparency button. An LLM feature that
    // quietly ships your data somewhere should be able to show you exactly
    // what it sent, in one click, without a build flag.
    auto* contextBtn = new QPushButton(tr("What can it see?"), this);
    contextBtn->setObjectName(QStringLiteral("chatContext"));
    contextBtn->setCursor(Qt::PointingHandCursor);
    connect(contextBtn, &QPushButton::clicked, this,
            &ChatPage::showContextDialog);
    headerRow->addWidget(contextBtn, 0, Qt::AlignTop);

    auto* newBtn = new QPushButton(tr("New conversation"), this);
    newBtn->setObjectName(QStringLiteral("chatNew"));
    newBtn->setCursor(Qt::PointingHandCursor);
    connect(newBtn, &QPushButton::clicked, this,
            &ChatPage::startNewConversation);
    headerRow->addWidget(newBtn, 0, Qt::AlignTop);
    layout->addLayout(headerRow);

    // ---- the log ----------------------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("chatLog"));
    makeTouchScrollable(m_scroll);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    auto* logHost = new QWidget;
    m_logLayout = new QVBoxLayout(logHost);
    m_logLayout->setContentsMargins(2, 2, 2, 2);
    m_logLayout->setSpacing(8);
    m_logLayout->addStretch(1); // bubbles are inserted BEFORE this, so the
                                // conversation sits at the top and grows down
    m_scroll->setWidget(logHost);
    layout->addWidget(m_scroll, 1);

    // ---- empty state ------------------------------------------------------
    // Three starters, because a blank chat box is a small executive-function
    // tax: you have to invent the question before you can ask it. One click
    // asks it for you. (Supplementary Spec's low-friction rule, applied to a
    // text box.)
    m_emptyHint = new QWidget;
    auto* hintLayout = new QVBoxLayout(m_emptyHint);
    hintLayout->setContentsMargins(0, 0, 0, 0);
    hintLayout->setSpacing(6);
    auto* hintCaption = new QLabel(tr("Try asking:"), m_emptyHint);
    hintCaption->setObjectName(QStringLiteral("sub"));
    hintLayout->addWidget(hintCaption);
    const QStringList starters{tr("How is my day going?"),
                               tr("What should I work on next?"),
                               tr("What's overdue, and what can wait?")};
    for (const QString& starter : starters) {
        auto* chip = new QPushButton(starter, m_emptyHint);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(
            "text-align:left; padding:8px 12px; border-radius:10px; "
            "border:1px dashed #C9D2CB; background:transparent; color:#3C4A44;");
        connect(chip, &QPushButton::clicked, this, [this, starter]() {
            m_input->setPlainText(starter);
            sendCurrentInput();
        });
        hintLayout->addWidget(chip);
    }
    m_logLayout->insertWidget(0, m_emptyHint);

    // ---- composer ---------------------------------------------------------
    m_status = new QLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("chatStatus"));
    m_status->setStyleSheet("color:#616974; font-size:12px;");
    layout->addWidget(m_status);

    auto* composer = new QHBoxLayout;
    composer->setSpacing(8);
    m_input = new ChatInput(this);
    connect(m_input, &ChatInput::submitted, this, &ChatPage::sendCurrentInput);
    composer->addWidget(m_input, 1);

    m_send = new QPushButton(tr("Send"), this);
    m_send->setObjectName(QStringLiteral("chatSend"));
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setFixedHeight(74);
    m_send->setMinimumWidth(96);
    connect(m_send, &QPushButton::clicked, this, [this]() {
        // ONE button, two jobs — Send while idle, Stop while a turn is in
        // flight. A separate always-visible Stop would be dead furniture 99%
        // of the time; the label says which job it is doing right now.
        if (m_client->busy()) {
            m_client->cancel();
            setBusy(false);
            m_status->setText(tr("Stopped."));
            return;
        }
        sendCurrentInput();
    });
    composer->addWidget(m_send, 0, Qt::AlignBottom);
    layout->addLayout(composer);

    // ---- wiring -----------------------------------------------------------
    connect(m_client, &ChatClient::replied, this,
            [this](const QString& text, const QString& seatId,
                   bool viaFallback) {
        m_transcript.append(ai::Role::Assistant, text);
        addBubble(m_transcript.turns().last());
        // §E attribution, as a transcript notice (a per-bubble badge is a
        // recorded follow-up): a fallback answer names its author, because
        // a conversation with two authors of different quality must say
        // which one said the thing you are about to act on.
        if (viaFallback) {
            m_transcript.appendLocal(
                tr("answered by %1").arg(ai::seatName(seatId)));
            addBubble(m_transcript.turns().last());
        }
        setBusy(false);
    });
    connect(m_client, &ChatClient::seatUnreachable, this,
            [this](const QString& seatId, const QString& nextSeatId) {
        // Local-only: the human should see the walk happen; the model must
        // not — a fed-back infrastructure notice is the localOnly flag's
        // whole reason to exist.
        m_transcript.appendLocal(tr("⚠ %1 unreachable — trying %2…")
                                     .arg(ai::seatName(seatId),
                                          ai::seatName(nextSeatId)));
        addBubble(m_transcript.turns().last());
    });
    connect(m_client, &ChatClient::failed, this, [this](const QString& reason) {
        // The failure goes in the LOG, not into a message box: it belongs to
        // the conversation it happened in, and a dialog would interrupt
        // someone who is already mid-thought. localOnly keeps it out of the
        // history the model later sees.
        m_transcript.appendLocal(tr("⚠ %1").arg(reason));
        addBubble(m_transcript.turns().last());
        setBusy(false);
    });
}

QString ChatPage::currentBriefing() const
{
    const QDateTime now = nowProvider();
    // The user's own bar for "missed", so the assistant and the catch-up
    // card never disagree about which blocks count. This is the ONE place
    // the pref crosses into brief::'s world — passed as a value, keeping
    // brief:: itself QSettings-free.
    brief::Options opts;
    opts.missedRule = prefs::missedRule();

    // §E.4, enforced where the text is born (v28.2p2): the MOOD line rides
    // only when EVERY seat the chat route can reach is local — primary AND
    // fallback, because a mid-turn fall-through must not become an
    // exfiltration path. All-remote, mixed, or empty route: silence.
    bool allLocal = true;
    const QStringList route = ai::configuredRouteIds(ai::Feature::Chat);
    for (const QString& id : route)
        if (!ai::isLocal(ai::resolved(id)))
            allLocal = false;
    opts.includeMood = allLocal && !route.isEmpty();

    return brief::dayBriefing(*m_data, now.date(), now, opts,
                              &m_handles); // this turn's handle world
}

ProposalCard* ChatPage::presentProposal(const verbs::Proposal& p,
                                        verbs::Role role)
{
    if (m_emptyHint)
        m_emptyHint->hide(); // a proposal is a conversation now

    // Render-time verdict for DISPLAY (a born-broken card shows its
    // reason and never enables Apply); the tap re-validates regardless —
    // two different moments, two different worlds, checked twice.
    const verbs::Verdict atRender = verbs::validate(*m_data, m_handles,
                                                    role, p);
    auto* card = new ProposalCard(p, p.summary(*m_data, m_handles),
                                  atRender, this);

    connect(card, &ProposalCard::discardRequested, this, [card]() {
        // Nothing happened, so nothing enters the transcript record —
        // a declined proposal should not haunt the log. (Slice 2 will
        // report the decline to the MODEL as a tool_result; that is the
        // proposer's business, not the record's.)
        card->settle(tr("Discarded — nothing changed."));
    });

    connect(card, &ProposalCard::applyRequested, this,
            [this, card, role]() {
        if (preApplyHook)
            preApplyHook(); // the state BEFORE, copied aside first

        const verbs::Verdict v =
            verbs::apply(*m_data, m_handles, role, card->proposal());
        card->settle(v.ok ? tr("Applied.") : v.reason);

        if (v.ok) {
            // The durable receipt: cards are live UI and do not survive
            // rebuildLog — the TRANSCRIPT is the record, so the record
            // gets a localOnly line (never sent to any model; the same
            // privacy stance every notice takes).
            m_transcript.appendLocal(
                tr("✓ Applied: %1")
                    .arg(card->proposal().summary(*m_data, m_handles)));
            addBubble(m_transcript.turns().last());
        }
    });

    m_logLayout->insertWidget(m_logLayout->count() - 1, card);
    scrollToBottom();
    return card;
}

bool ChatPage::beginIntake()
{
    currentBriefing(); // refresh the handle world — proposals speak ITS names
    const QDateTime now = nowProvider();

    for (const QString& id : m_handles.ids) {
        const Task* t = m_data->taskById(id);
        if (!t || m_intake.askedThisSession.contains(id))
            continue;
        if (!intake::worthInterviewing(*m_data, *t, now))
            continue;

        m_intake.active = true;
        m_intake.taskId = id;
        m_intake.handle =
            QStringLiteral("T%1").arg(m_handles.ids.indexOf(id) + 1);
        m_intake.askedThisSession.insert(id); // asked is asked — a later
                                              // discard must not re-loop
        intakeAskCurrent();
        return true;
    }

    if (m_intake.active) {
        // The flow was running and just ran out — close it out loud, so
        // "did it finish or break?" never needs asking.
        m_transcript.appendLocal(
            tr("That's everything that needed sizing."));
        addBubble(m_transcript.turns().last());
    }
    m_intake.active = false;
    return false;
}

void ChatPage::intakeAskCurrent()
{
    const Task* t = m_data->taskById(m_intake.taskId);
    if (!t)
        return;

    if (m_emptyHint)
        m_emptyHint->hide();

    m_intake.question = intake::questionFor(*m_data, *t);
    m_transcript.appendLocal(m_intake.question); // localOnly, like the
                                                 // check-in: this exchange
                                                 // belongs to the human
    addBubble(m_transcript.turns().last());

    // Buttons under the question, the mood-row pattern: the guess (when
    // history offers one) and Skip. Typing in the input box is the third
    // answer, handled in sendCurrentInput.
    auto* host       = new QWidget(this);
    auto* hostLayout = new QHBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(8);

    const intake::Guess g = intake::historyGuess(*m_data, *t);
    if (g.exists()) {
        const int h = g.minutes / 60, m = g.minutes % 60;
        QString span = h > 0 ? (m > 0 ? tr("%1h %2m").arg(h).arg(m)
                                      : tr("%1h").arg(h))
                             : tr("%1m").arg(m);
        auto* yes = new QPushButton(tr("≈ %1 sounds right").arg(span), host);
        yes->setObjectName(QStringLiteral("intakeGuess"));
        yes->setCursor(Qt::PointingHandCursor);
        const int minutes = g.minutes;
        connect(yes, &QPushButton::clicked, this, [this, minutes, host]() {
            host->deleteLater();
            // The guess still crosses the card — EVERY write does, no
            // convenience exceptions: the consistency is the trust story,
            // and it costs one tap.
            proposeFromIntake(minutes, QDate());
        });
        hostLayout->addWidget(yes);
    }

    auto* skip = new QPushButton(tr("Skip this one"), host);
    skip->setObjectName(QStringLiteral("intakeSkip"));
    skip->setCursor(Qt::PointingHandCursor);
    connect(skip, &QPushButton::clicked, this, [this, host]() {
        host->deleteLater();
        // Skip is the OWNER acting, not the assistant proposing — it goes
        // through the domain door directly, like the mood buttons. A year
        // is "ask once" without a year-9999 silliness value; the task
        // outlives neither.
        m_data->dismissTask(m_intake.taskId, nowProvider().addDays(365));
        m_transcript.appendLocal(
            tr("Skipped — I won't ask about that one again."));
        addBubble(m_transcript.turns().last());
        m_intake.active = false;
        QTimer::singleShot(0, this, [this]() { beginIntake(); });
    });
    hostLayout->addWidget(skip);
    hostLayout->addStretch(1);

    m_logLayout->insertWidget(m_logLayout->count() - 1, host);
    scrollToBottom();
}

void ChatPage::proposeFromIntake(int minutes, const QDate& due)
{
    const Task* t = m_data->taskById(m_intake.taskId);

    verbs::Proposal p;
    p.targetHandle    = m_intake.handle;
    p.estimateMinutes = minutes;
    if (due.isValid() && t && !t->dueDate.isValid())
        p.dueDate = due; // additive-safe: never carry a date the task
                         // already answered for itself

    m_intake.active = false; // answered; the card owns what happens next

    ProposalCard* card = presentProposal(p, verbs::Role::Intake);

    // Chain the interview on the card's settle — connected AFTER
    // presentProposal's own handlers, so apply/refusal has finished by
    // the time the next question posts. Deferred a trip through the
    // event loop for the same reason.
    auto continueLater = [this]() {
        QTimer::singleShot(0, this, [this]() { beginIntake(); });
    };
    connect(card, &ProposalCard::applyRequested, this, continueLater);
    connect(card, &ProposalCard::discardRequested, this, continueLater);
}

void ChatPage::beginCheckIn()
{
    m_emptyHint->hide();

    // The opener is C++ and SPECIFIC (§G.3): the facts, then the question.
    const QDateTime now = nowProvider();
    int planned = 0;
    for (const Event& e : m_data->events())
        if (e.date == now.date())
            planned += e.plannedEndMinutes - e.plannedStartMinutes;
    QString opener = tr("Morning.");
    if (planned > 0)
        opener += tr(" Today holds %1 of planned blocks.")
                      .arg(brief::spanLabel(qint64(planned) * 60));
    opener += tr(" How are you doing?");

    // localOnly: this exchange belongs to the human and the domain, never
    // to a future model turn's context window.
    m_transcript.appendLocal(opener);
    addBubble(m_transcript.turns().last());

    // Buttons, not typing: a 07:40 check-in must cost one tap. Recording
    // goes through the domain door — recordMood upserts, so a second tap
    // the same morning simply corrects the first.
    auto* host = new QWidget(this);
    auto* hostLayout = new QHBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(8);
    const struct { Mood::Level level; QString label; } choices[] = {
        {Mood::Level::Rough, tr("Rough")},
        {Mood::Level::Okay,  tr("Okay")},
        {Mood::Level::Good,  tr("Good")},
    };
    for (const auto& choice : choices) {
        auto* button = new QPushButton(choice.label, host);
        button->setCursor(Qt::PointingHandCursor);
        const Mood::Level level = choice.level;
        connect(button, &QPushButton::clicked, this,
                [this, level, host]() {
            m_data->recordMood(nowProvider().date(), level);
            host->deleteLater(); // answered; the question row goes

            // The acknowledgement is C++, specific, non-shaming (§G.3:
            // what lands is evidence, not pep).
            QString ack;
            switch (level) {
            case Mood::Level::Rough:
                ack = tr("Noted — rough. Be kind to yourself today; the "
                         "plan is a tool, not a judge.");
                break;
            case Mood::Level::Okay:
                ack = tr("Noted. The day's laid out whenever you're ready.");
                break;
            case Mood::Level::Good:
                ack = tr("Noted — good. That's worth spending on the hard "
                         "thing first.");
                break;
            }
            m_transcript.appendLocal(ack);
            addBubble(m_transcript.turns().last());

            // §K.1's designed entry, live since v29.1: the check-in is the
            // moment the user already chose to talk — offer the interview
            // HERE, never at capture. One button, one refusal, no nagging
            // (a "Not now" just removes the row; the tasks stay queued for
            // a day the user picks).
            int needing = 0;
            const QDateTime inow = nowProvider();
            for (const Task& task : m_data->tasks())
                if (intake::worthInterviewing(*m_data, task, inow))
                    ++needing;
            if (needing > 0) {
                m_transcript.appendLocal(
                    tr("%n captured task(s) still need sizing — want to go "
                       "through them? One question each.", "", needing));
                addBubble(m_transcript.turns().last());

                auto* offer       = new QWidget(this);
                auto* offerLayout = new QHBoxLayout(offer);
                offerLayout->setContentsMargins(0, 0, 0, 0);
                offerLayout->setSpacing(8);
                auto* go = new QPushButton(tr("Go through them"), offer);
                go->setObjectName(QStringLiteral("intakeOfferGo"));
                go->setCursor(Qt::PointingHandCursor);
                connect(go, &QPushButton::clicked, this, [this, offer]() {
                    offer->deleteLater();
                    beginIntake();
                });
                auto* later = new QPushButton(tr("Not now"), offer);
                later->setObjectName(QStringLiteral("intakeOfferLater"));
                later->setCursor(Qt::PointingHandCursor);
                connect(later, &QPushButton::clicked, this,
                        [offer]() { offer->deleteLater(); });
                offerLayout->addWidget(go);
                offerLayout->addWidget(later);
                offerLayout->addStretch(1);
                m_logLayout->insertWidget(m_logLayout->count() - 1, offer);
                scrollToBottom();
            }
        });
        hostLayout->addWidget(button);
    }
    hostLayout->addStretch(1);
    m_logLayout->insertWidget(m_logLayout->count() - 1, host);
    scrollToBottom();
}

void ChatPage::sendCurrentInput()
{
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() || m_client->busy())
        return; // an empty Enter is a no-op, not an error

    m_input->clear();
    m_emptyHint->hide(); // the starters have done their job

    m_transcript.append(ai::Role::User, text);
    addBubble(m_transcript.turns().last());

    // ---- v29.1: an active interview claims the next answer -----------------
    if (m_intake.active) {
        // Tier 1 — crisp, C++, free, offline: "2h" never needs a model.
        const int crisp = intake::parseDurationAnswer(text);
        if (crisp > 0) {
            proposeFromIntake(crisp, QDate());
            return;
        }

        // Tier 2 — the model reads the prose. Created lazily: a user who
        // only ever taps the guess never pays for the client.
        if (!m_intakeClient) {
            m_intakeClient = new IntakeClient(this);
            connect(m_intakeClient, &IntakeClient::extracted, this,
                    [this](int minutes, const QDate& due) {
                setBusy(false);
                if (m_intake.active)
                    proposeFromIntake(minutes, due);
            });
            connect(m_intakeClient, &IntakeClient::failed, this,
                    [this](const QString& reason) {
                setBusy(false);
                // Tier 3 — the honest hint. The interview stays open;
                // the crisp parser is the path that always works.
                m_transcript.appendLocal(
                    tr("Couldn't read that (%1) — give me something "
                       "like \"2h\" or \"90 min\".")
                        .arg(reason));
                addBubble(m_transcript.turns().last());
            });
        }
        const Task* t = m_data->taskById(m_intake.taskId);
        if (!t) { // the task vanished mid-interview — close quietly
            m_intake.active = false;
            return;
        }
        setBusy(true);
        m_intakeClient->extract(*m_data, *t, nowProvider().date(), text);
        return;
    }

    setBusy(true);

    // Context is built AT FIRE TIME, every single turn — not once per
    // conversation. The user may add a task, track twenty minutes, or cross
    // midnight between two questions, and an assistant answering from a
    // stale snapshot is worse than one with no data at all. Same
    // read-at-fire-time doctrine the provider and key follow.
    m_client->send(chat::systemPrompt(currentBriefing(),
                                      chat::configuredPersonaBand()),
                   m_transcript.window(chat::kDefaultBudgetChars));
}

void ChatPage::startNewConversation()
{
    m_client->cancel();
    m_transcript.clear();
    rebuildLog();
    setBusy(false);
    m_status->clear();
}

void ChatPage::addBubble(const chat::Turn& turn)
{
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);

    auto* bubble = new QLabel(turn.text);
    bubble->setWordWrap(true);
    // v28.10 (field report #1): models SPEAK markdown, and a plain QLabel
    // printed the asterisks and backtick fences literally. MarkdownText
    // renders them — chosen over RichText deliberately, because markdown is
    // the NARROWER surface for text a model wrote: no <img>, no scripts, no
    // font tags to smuggle. Assistant turns only: the user's own asterisks
    // stay asterisks, and local notices (⚠, context chips) stay plain.
    if (turn.role == ai::Role::Assistant && !turn.localOnly)
        bubble->setTextFormat(Qt::MarkdownText);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse); // copyable —
                                                                // an answer you
                                                                // can't paste is
                                                                // half an answer
    bubble->setMaximumWidth(620);
    bubble->setStyleSheet(bubbleStyle(turn.role, turn.localOnly));

    if (turn.role == ai::Role::User && !turn.localOnly) {
        row->addStretch(1);
        row->addWidget(bubble);
    } else {
        row->addWidget(bubble);
        row->addStretch(1);
    }

    // insertLayout at count()-1: the stretch added in the constructor must
    // stay last, or every bubble would be pinned to the bottom of the
    // viewport and the log would grow upward.
    m_logLayout->insertLayout(m_logLayout->count() - 1, row);
    scrollToBottom();
}

void ChatPage::rebuildLog()
{
    // Only clear() comes through here. Walk backwards and take everything
    // except the trailing stretch; deleteLater rather than delete because a
    // button inside one of these rows may be the very thing that called us.
    while (m_logLayout->count() > 1) {
        QLayoutItem* item = m_logLayout->takeAt(0);
        if (QWidget* w = item->widget()) {
            if (w == m_emptyHint) { // keep the starters; re-show them
                m_logLayout->insertWidget(0, m_emptyHint);
                m_emptyHint->show();
                break;
            }
            w->deleteLater();
        } else if (QLayout* child = item->layout()) {
            while (QLayoutItem* sub = child->takeAt(0)) {
                if (QWidget* w = sub->widget())
                    w->deleteLater();
                delete sub;
            }
            delete child;
        }
        delete item;
    }
}

void ChatPage::setBusy(bool busy)
{
    m_send->setText(busy ? tr("Stop") : tr("Send"));
    m_status->setText(busy ? tr("Thinking…") : QString());
}

void ChatPage::scrollToBottom()
{
    // Queued: the layout has not measured the new bubble yet at this point,
    // so scrolling now would land one message short. singleShot(0) runs after
    // the pending layout pass — the standard Qt trick for "do this once the
    // event loop has caught up".
    QTimer::singleShot(0, this, [this]() {
        m_scroll->verticalScrollBar()->setValue(
            m_scroll->verticalScrollBar()->maximum());
    });
}

void ChatPage::showContextDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("What the assistant can see"));
    dialog.resize(620, 520);

    auto* layout = new QVBoxLayout(&dialog);
    auto* caption = new QLabel(
        tr("This is generated fresh from your own data every time you send a "
           "message, and goes to %1. Nothing else is sent — no notes, no "
           "descriptions, no ids.")
            .arg(ai::configured().displayName),
        &dialog);
    caption->setWordWrap(true);
    caption->setObjectName(QStringLiteral("sub"));
    layout->addWidget(caption);

    auto* view = new QPlainTextEdit(currentBriefing(), &dialog);
    view->setReadOnly(true);
    layout->addWidget(view, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}
