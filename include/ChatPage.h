#pragma once
// ---------------------------------------------------------------------------
// ChatPage — the assistant's face: a rail page with a conversation in it.
//
// COMPOSITION (who owns what)
//   ChatPage
//    ├── chat::Transcript   the conversation, BY VALUE (a member, not a ptr)
//    ├── ChatClient*        the wire, a QObject child — Qt deletes it with us
//    └── AppData*           BORROWED, like every other page
//
// The transcript is a plain member because nothing outside this page needs to
// read it. The moment a second surface wants the same conversation — a
// Pomodoro-break check-in, say — it moves up to MainWindow and this page
// takes a pointer, exactly the journey PomodoroEngine already made. Writing
// it as a value now is not a bet that it will never move; it is a refusal to
// pay for a move that has not happened.
//
// WHY NO MODEL/VIEW HERE, three sessions after the model/view arc
// A chat log is append-only, tens of rows long, and every row is a different
// height because it wraps arbitrary prose. QAbstractListModel + delegate buys
// virtualisation (irrelevant at this scale), multiple synchronised views (we
// have one) and cheap in-place edits (a sent message never changes). What it
// COSTS is sizeHint arithmetic for word-wrapped text in a delegate, which is
// the fiddliest paint code in Qt. So: one QLabel per turn in a QVBoxLayout.
// The v20 arc taught what model/view is FOR; knowing when not to reach for it
// is the same lesson finishing.
//
// AND WHY NOT rebuild-on-changed(), the idiom every other page uses
// Because the log only ever grows at one end. Rebuilding the whole widget
// tree per message would throw away scroll position and selection to
// re-derive a list that changed by exactly one row. Append is the honest
// operation; clear() is the only thing that rebuilds.
// ---------------------------------------------------------------------------

#include "AssistantVerbs.h" // v29.0 — the write boundary crosses HERE
#include "Intake.h"          // v29.1 — the interview's brain
#include "ChatSession.h"

#include <QSet>

#include <QDateTime>
#include <QPlainTextEdit>
#include <QWidget>

#include <functional>

class AppData;
class ChatClient;
class IntakeClient;
class ProposalCard;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// The input box. Exists as its own class for ONE behaviour: Enter sends,
// Shift+Enter makes a newline — the convention every chat app trained people
// into, and the thing that makes a chat feel like a chat instead of a form.
// A QPlainTextEdit (not a QLineEdit) because "paste the three bullet points I
// wrote" is a normal thing to want; a single-line field would silently eat
// the structure.
class ChatInput : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit ChatInput(QWidget* parent = nullptr);

signals:
    void submitted();

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(AppData* data, QWidget* parent = nullptr);

    // The clock seam, same doctrine as GlancePanel and TrackerService: the
    // briefing's "past / NOW / upcoming" markers depend on the time, so tests
    // pin it rather than running at whatever o'clock the suite happens to.
    std::function<QDateTime()> nowProvider = [] {
        return QDateTime::currentDateTime();
    };

    // The exact text that would be sent as context right now. Public because
    // the "What can it see?" button shows it verbatim — an inspectable prompt
    // is the difference between a feature people trust and one they suspect.
    QString currentBriefing() const;

    // ---- v29.0: the write boundary ----------------------------------------
    // A proposal enters the transcript as a CARD (§B stage 3) under the
    // role it was issued from; the tap routes through verbs::apply — the
    // card itself decides nothing. Public because proposals are composed
    // OUTSIDE this page (the debug injector today, the intake model in
    // Slice 2) and presented here, where the owner is.
    ProposalCard* presentProposal(const verbs::Proposal& p,
                                  verbs::Role role); // returns the card so
                                                     // flows (intake) can
                                                     // chain on its settle

    // ---- v29.1: the interview (§K) ----------------------------------------
    // Picks the next interview-worthy task from THIS turn's handle world,
    // asks the C++ question (guess folded in), and hands the answer to:
    // crisp parse first, the model second, an honest hint third. Every
    // successful extraction crosses the Slice 1 card — the model is a
    // proposer, nothing more. Returns false when nothing qualifies.
    bool beginIntake();

    // This turn's [T1] → id world, refreshed by every currentBriefing().
    const verbs::HandleMap& handles() const { return m_handles; }

    // Everything validation needs beyond AppData: the clock, the missed-block
    // rule, and the search policy the proposer used. Built fresh on every
    // call rather than cached, because both verdicts (§E) must judge the
    // world as it is at THAT moment — a cached one would make the tap
    // re-validate against the render's world, which is the bug the second
    // verdict exists to catch.
    verbs::World currentWorld() const;

    // Runs immediately before any Apply mutates (§B's cheap insurance —
    // MainWindow copies data.json aside). A std::function member, the
    // nowProvider precedent: the page knows WHEN, the composition root
    // knows WHAT.
    std::function<void()> preApplyHook;

    // v30.0 — this turn's memory band (§L), already trimmed to budget.
    //
    // The same seam as preApplyHook and for the same reason: the page knows
    // WHEN (fire time, every turn), the composition root knows WHAT (which
    // account's file, since login scopes memory exactly as it scopes the
    // planner). ChatPage never learns a username or a path.
    //
    // Called per turn rather than cached, which is the read-at-fire-time
    // doctrine the briefing, the provider and the key already follow — and
    // here it buys something visible: the file is hand-editable, so an edit
    // made in a text editor takes effect on the very next question instead
    // of at the next restart.
    //
    // Unset is normal and means no band at all. Nobody is required to have
    // written anything.
    std::function<QString()> memoryBandProvider;

    // v28.2p2 — the check-in entry: appends the opener bubble (a question
    // with three answer buttons) to the transcript. Public: MainWindow
    // calls it from the toast's action. The whole exchange is localOnly —
    // buttons record the mood through the domain door and no model is in
    // the loop, which is how §E.4's "local, always" is satisfied in part
    // 2: not by routing the conversation locally, but by there being no
    // conversation to route. The model joins when per-role primaries do.
    void beginCheckIn();

    // Test seam: lets a test point the wire at a stub provider (or a keyless
    // one, to walk the fail-fast path offline). Same reason
    // LlmQuickAddClient::setProviderOverride is public.
    ChatClient* client() const { return m_client; }

public slots:
    void sendCurrentInput();
    void startNewConversation();

private:
    void addBubble(const chat::Turn& turn);
    void rebuildLog();
    void setBusy(bool busy);
    void scrollToBottom();
    void showContextDialog();

    void intakeAskCurrent();
    void proposeFromIntake(int minutes, const QDate& due);

    AppData*         m_data;
    IntakeClient*    m_intakeClient = nullptr;
    struct IntakeState
    {
        bool          active = false;
        QString       taskId;
        QString       handle;
        QString       question;
        // Session memory, not domain state: a DISCARDED card must not
        // re-ask instantly (dismissal is the owner's Skip, not ours to
        // infer from a discard), so the loop remembers who it already
        // asked until the conversation restarts.
        QSet<QString> askedThisSession;
    } m_intake;
    // mutable: the handle map is a CACHE of the last-rendered turn's
    // world, not logical state — refreshing it inside a const render is
    // exactly what mutable exists to say out loud.
    mutable verbs::HandleMap m_handles;

    // v30.1 — the single move UndoMove may take back: the ORIGINAL block's
    // id of the last MoveBlock this conversation actually applied. Empty
    // means "nothing of mine to undo", which is the honest default.
    //
    // Here rather than in AppData because it is a fact about a CONVERSATION,
    // not about a planner: the domain has no opinion about who moved a block
    // or how recently. Deliberately not persisted either — the undo is for
    // immediate regret, which is what §B.1's "ask it for something else"
    // actually describes.
    //
    // Two moves in a row: the last one wins. Cleared once used, so asking
    // twice is refused rather than reversing something else.
    QString m_undoableMoveId;

    ChatClient*      m_client   = nullptr;
    chat::Transcript m_transcript;

    QScrollArea* m_scroll    = nullptr;
    QVBoxLayout* m_logLayout = nullptr; // bubbles live here, above a stretch
    QWidget*     m_emptyHint = nullptr; // the suggestion chips; hidden on send
    ChatInput*   m_input     = nullptr;
    QPushButton* m_send      = nullptr;
    QLabel*      m_status    = nullptr;
};
