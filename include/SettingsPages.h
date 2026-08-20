#pragma once
// ---------------------------------------------------------------------------
// SettingsPages — the individual panes of the preferences dialog.
//
// WHY THIS FILE EXISTS (v26.1)
// SettingsDialog was one 579-line constructor building one flat QFormLayout,
// with bold QLabels faking section headers. Its own header comment warned
// against exactly the thing it had become:
//
//     "A settings dialog that hoards every knob in the app becomes the junk
//      drawer nobody can find anything in."
//
// The fix is not more sections in the same form — it is giving each concern
// its own WIDGET that owns its own save(). The dialog then knows nothing
// about agenda hours or API keys; it knows only that it holds a list of
// pages and that OK means "tell each one to save".
//
// THE PAYOFF, concretely: the catch-up feature (missed blocks) adds a page
// by writing one class and one addPage() line. Under the old shape it would
// have meant editing a 200-line save() that every other feature also edits —
// the same merge-conflict magnet that AppData avoids by having one named
// door per operation instead of one addEvent(everything).
//
// WHY ONE HEADER FOR THREE CLASSES (and not six files)
// These pages are never used apart from this dialog, they are small, and
// they share the PolicyEditor helper below. ReviewWidgets.h sets the
// precedent: several tiny collaborating widgets, one file. One class per
// file is a good default, not a law — the law is "things that change
// together live together".
//
// WHAT DID NOT CHANGE, deliberately:
//   - every QSettings key, default, and clamp (prefs:: still owns those)
//   - every objectName — test_ui.cpp finds widgets by name, recursively,
//     so the tests pass untouched. A refactor that needs its tests rewritten
//     hasn't been proven to be a refactor.
//   - the persistence model: widgets edit local state, OK writes once,
//     Cancel writes nothing. Now spread across pages, still exactly once.
// ---------------------------------------------------------------------------

#include "LlmProvider.h"  // ai::Provider — aiProviderFromFields returns one
#include "ReturnPolicy.h" // the value type PolicyEditor edits

#include <QHash>
#include <QString>
#include <QWidget>

#include <initializer_list>

class ChatClient;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimeEdit;

// ---------------------------------------------------------------------------
// The contract every pane honours.
//
// TWO virtuals, and note what is NOT here: there is no load(). Pages load in
// their constructors, because a page is built exactly once — MainWindow
// constructs a fresh SettingsDialog on every ⚙ click (see MainWindow.cpp).
// A load() would be ceremony for a lifecycle this dialog doesn't have; add
// it the day a page needs re-reading, not before.
//
// `title()` is virtual rather than a constructor argument so the string
// lives NEXT TO the widgets it describes, and so each page's tr() context
// is its own class — which is what Q_OBJECT in every subclass buys us.
// ---------------------------------------------------------------------------
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr) : QWidget(parent) {}

    // The label shown in the nav list.
    virtual QString title() const = 0;

    // Widgets -> QSettings. Called by SettingsDialog on OK, never otherwise.
    virtual void save() = 0;
};

// ---------------------------------------------------------------------------
// settingsui — small builders shared by more than one page.
//
// Free functions in a namespace, not protected members of SettingsPage:
// these are helpers, not behaviour a page inherits. Inheritance is for "is
// a", and a page is not "a policy editor". (The same instinct that keeps
// coverage:: a namespace of pure functions instead of a base class.)
// ---------------------------------------------------------------------------
namespace settingsui
{

// A ReturnPolicy editor: a mode combo plus the ONE parameter widget that
// mode needs, the others hidden. Built twice today (the review clock and
// the dismissal clock) and a third time when catch-up ships — which is why
// it was already extracted before this refactor and stays extracted now.
struct PolicyEditor
{
    QComboBox* mode  = nullptr;
    QTimeEdit* time  = nullptr;
    QSpinBox*  hours = nullptr;
};

PolicyEditor makePolicyEditor(const ReturnPolicy& current, bool offerEndOfDay,
                              QWidget* host);
ReturnPolicy readPolicyEditor(const PolicyEditor& e);

// Pack widgets into one left-aligned horizontal row.
//
// Extracted on the rule of three: the old constructor spelled out
// "new QWidget + new QHBoxLayout + margins(0) + addWidget xN + addStretch(1)"
// four separate times. Four copies of a five-line incantation is four
// chances to forget the zero margins and wonder why one row sits lower than
// its neighbours. A QFormLayout row is ONE widget, so composite rows must be
// wrapped — this is the wrapper, spelled once.
QWidget* rowOf(QWidget* host, std::initializer_list<QWidget*> widgets);

} // namespace settingsui

// ---------------------------------------------------------------------------
// Agenda — the shape of the day.
//
// Hours, week start, and the block-start alarm. The alarm belongs HERE and
// not on a page of its own: a page holding a single checkbox is worse than
// no page at all, and the alarm is genuinely a property of the agenda ("tell
// me when a planned block starts"). The junk-drawer rule cuts both ways —
// it forbids one giant pane AND a pane per checkbox.
// ---------------------------------------------------------------------------
class AgendaSettingsPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit AgendaSettingsPage(QWidget* parent = nullptr);

    QString title() const override { return tr("Agenda"); }
    void    save() override;

private:
    void rebuildEndChoices(); // legal ends depend on the chosen start

    QComboBox* m_startCombo = nullptr; // day starts at … (6 AM – 10 PM)
    QComboBox* m_endCombo   = nullptr; // day ends at …   (start+1 h – midnight)
    QComboBox* m_weekCombo  = nullptr; // week starts on Monday / Sunday
    QCheckBox* m_alarmCheck = nullptr; // toast when a planned block starts
};

// ---------------------------------------------------------------------------
// Needs a block — the flag rule, the escalation ladder, the two clocks.
//
// All of it is TASTE (what deserves a nudge, this machine's rhythm), so all
// of it is prefs::. The FACTS the feature produces (Task::dismissedUntil,
// dismissCount) live on Task and sync; nothing on this page does.
// ---------------------------------------------------------------------------
class NeedsBlockSettingsPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit NeedsBlockSettingsPage(QWidget* parent = nullptr);

    QString title() const override { return tr("Needs a block"); }
    void    save() override;

private:
    QCheckBox* m_urgent = nullptr; // the always-flag priority set
    QCheckBox* m_medium = nullptr;
    QCheckBox* m_low    = nullptr;
    QComboBox* m_window = nullptr; // due-within window (Off/1/3/7/14)

    settingsui::PolicyEditor m_review;  // when the review re-arms
    settingsui::PolicyEditor m_dismiss; // how long "Not today" lasts

    QSpinBox*  m_escalate       = nullptr; // decisions demanded after N put-offs
    QCheckBox* m_escUrgentOnly  = nullptr;
    QCheckBox* m_gate           = nullptr; // hold the numbers until reviewed
};

// ---------------------------------------------------------------------------
// Catch-up — missed blocks: what counts, how far back, and when to ask.
//
// The v26.1 refactor's promised test case: one class, one addPage() line,
// zero edits to SettingsDialog::save(). All four knobs are TASTE and live in
// QSettings via prefs::; the facts the feature produces (Event::outcome,
// movedToId) are in data.json and sync.
// ---------------------------------------------------------------------------
class CatchUpSettingsPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit CatchUpSettingsPage(QWidget* parent = nullptr);

    QString title() const override { return tr("Catch-up"); }
    void    save() override;

private:
    QComboBox* m_threshold = nullptr; // what share counts as "it happened"
    QComboBox* m_lookBack  = nullptr; // how far back the card looks
    QCheckBox* m_onOpen    = nullptr; // the morning moment
    QCheckBox* m_endOfDay  = nullptr; // the evening moment
    QComboBox* m_horizon   = nullptr; // proposer reach with no deadline
};

// ---------------------------------------------------------------------------
// Assistant — provider, credentials, persona, fallback seat.
//
// The heaviest page, and the one that most justifies the split: it owns an
// in-memory edit buffer (per-provider keys and models), a live network probe,
// and three-way visibility rules. None of that has any business sitting in
// the same function as "which hour does the agenda start".
// ---------------------------------------------------------------------------
class AssistantSettingsPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit AssistantSettingsPage(QWidget* parent = nullptr);

    QString title() const override { return tr("Assistant"); }
    void    save() override;

private:
    // Two functions because switching a combo is TWO jobs in the wrong order
    // if written as one: first remember what's currently typed (it belongs to
    // the provider you are LEAVING), then load the fields of the provider you
    // are arriving at. Fuse them and the user's Anthropic key gets saved
    // under "openai".
    void stashFields();
    void loadFieldsFor(const QString& providerId);

    // The mirror of ai::configured(), reading the SCREEN instead of
    // QSettings — Test must try what is typed, and Cancel-writes-nothing
    // forbids the shortcut of saving first.
    ai::Provider providerFromFields() const;
    void         runKeyTest();

    QComboBox*   m_provider     = nullptr; // which vendor
    QWidget*     m_customRow    = nullptr; // address + dialect; custom only
    QLineEdit*   m_baseUrl      = nullptr; // custom endpoint address
    QComboBox*   m_dialect      = nullptr; // custom endpoint protocol
    QLineEdit*   m_modelEdit    = nullptr; // model override (blank = default)
    QLineEdit*   m_keyEdit      = nullptr; // API key for the selected provider
    QPushButton* m_test         = nullptr; // fire one tiny request
    QLabel*      m_testResult   = nullptr; // ✓ / the provider-aware error
    ChatClient*  m_probe        = nullptr; // reused wire; overrides do the rest
    QComboBox*   m_persona      = nullptr; // assistant style preset
    QComboBox*   m_chatFallback = nullptr; // 2nd seat of ai/route/chat
    QLineEdit*   m_personaText  = nullptr; // free text appended to the preset

    QString                 m_shownId; // whose values the fields hold now
    QHash<QString, QString> m_keys;    // providerId -> key, as edited
    QHash<QString, QString> m_models;  // providerId -> model override, as edited
};

// ---------------------------------------------------------------------------
// Memory — what the assistant knows about you (§L, v30.0).
//
// The ONE page whose save() does not write QSettings. Memory is neither a
// preference nor planner data: it is a third lifetime, a sidecar Markdown
// file the owner may also edit in any text editor. The base class contract
// says "widgets -> QSettings" because that is what every page before this
// one did; what it actually means is "commit my widgets", and where they
// land is the page's own business.
//
// FOUR EDITORS, ONE PER SECTION, one entry per line — because §L.3 says
// entries are REPLACED, never appended. A line editor replaces; there is no
// "add" button here, and that absence is the rule made physical.
//
// The character counter is not decoration. Memory is billed on EVERY turn
// forever, whether or not it was relevant, so the one number worth showing
// is how much of the budget this costs — and which entries are being dropped
// because they no longer fit.
//
// WHAT THIS PAGE MUST NEVER DO is lose text it did not understand. The file
// belongs to its owner; unrecognised sections are re-read at save time and
// written back untouched.
// ---------------------------------------------------------------------------
class MemorySettingsPage : public SettingsPage
{
    Q_OBJECT
public:
    // An empty path means the global memory file — the same convention
    // JsonStore::filePathForUser() already uses for an empty username, so
    // tests and a login-less build behave the way they always have.
    explicit MemorySettingsPage(QString filePath, QWidget* parent = nullptr);

    QString title() const override { return tr("Memory"); }
    void    save() override;

private:
    void refreshCost();

    QString m_filePath;

    QPlainTextEdit* m_routines    = nullptr;
    QPlainTextEdit* m_preferences = nullptr;
    QPlainTextEdit* m_situation   = nullptr;
    QPlainTextEdit* m_people      = nullptr;
    QLabel*         m_cost        = nullptr;
};
