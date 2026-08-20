#include "SettingsPages.h"

#include "ChatClient.h"
#include "ChatSession.h" // chat::personaCatalog — the Assistant style rows
#include "Event.h"       // plan:: — the domain grid the hours live inside
#include "MissedBlocks.h" // missed::Rule — the catch-up page edits one
#include "Prefs.h"
#include "Widgets.h"     // timeLabel
#include "MemoryStore.h" // the sidecar this page edits (§L)

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimeEdit>
#include <QVBoxLayout>

// std::max, used once for the fallback combo's index. Named explicitly
// rather than relying on it arriving through a Qt header — "include what you
// use" is the rule that stops a build breaking when an unrelated header
// tidies its own includes.
#include <algorithm>
#include <limits>

namespace
{
// "6 AM" for the hour combos; midnight gets its honest name because
// "12 AM" at the END of a list of evening hours reads like a bug.
QString hourName(int hour)
{
    if (hour == 24)
        return QObject::tr("Midnight");
    return timeLabel(hour * 60).remove(QStringLiteral(":00"));
}

// A section heading inside a page. Same objectName the old flat form used,
// so Theme.h's QLabel#h2 rule still styles it — the look is unchanged even
// though the structure around it is not.
QLabel* heading(const QString& text, QWidget* host)
{
    auto* label = new QLabel(text, host);
    label->setObjectName("h2");
    return label;
}

// A quiet explanatory paragraph. Word-wrapped, because these say real
// things and a truncated caveat is worse than none.
QLabel* footnote(const QString& text, QWidget* host)
{
    auto* label = new QLabel(text, host);
    label->setObjectName("sub");
    label->setWordWrap(true);
    return label;
}
} // namespace

// ===========================================================================
// settingsui — shared builders
// ===========================================================================

namespace settingsui
{

QWidget* rowOf(QWidget* host, std::initializer_list<QWidget*> widgets)
{
    auto* row = new QWidget(host);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    for (QWidget* w : widgets)
        layout->addWidget(w);
    // The stretch goes LAST, so the widgets hug the left edge and line up
    // with the single-widget rows above and below them. Without it a
    // two-widget row spreads to fill and the form looks ragged.
    layout->addStretch(1);
    return row;
}

PolicyEditor makePolicyEditor(const ReturnPolicy& current, bool offerEndOfDay,
                              QWidget* host)
{
    // One editor shape for both clocks, because one value type underlies
    // both (needs-a-block §C). The mode combo's userData carries the enum as
    // its string form — the same never-parse-the-label trick as the hour
    // combos, so a translated label can never break a saved setting.
    PolicyEditor e;
    e.mode = new QComboBox(host);
    if (offerEndOfDay)
        e.mode->addItem(QObject::tr("Until end of day"),
                        returnModeToString(ReturnPolicy::Mode::EndOfDay));
    e.mode->addItem(QObject::tr("At a set time"),
                    returnModeToString(ReturnPolicy::Mode::AtTime));
    e.mode->addItem(QObject::tr("After N hours"),
                    returnModeToString(ReturnPolicy::Mode::AfterHours));
    e.mode->setCurrentIndex(
        qMax(0, e.mode->findData(returnModeToString(current.mode))));

    e.time = new QTimeEdit(current.time, host);
    e.time->setDisplayFormat(QStringLiteral("HH:mm"));
    e.hours = new QSpinBox(host);
    e.hours->setRange(1, 48);
    e.hours->setValue(current.hours);
    e.hours->setSuffix(QObject::tr(" h"));

    // Only the parameter the mode USES is visible — the widget choice
    // deletes the "which field wins?" ambiguity instead of validating it.
    const auto sync = [e]() {
        const auto m = returnModeFromString(e.mode->currentData().toString());
        e.time->setVisible(m == ReturnPolicy::Mode::AtTime);
        e.hours->setVisible(m == ReturnPolicy::Mode::AfterHours);
    };
    // `host` is the connection's CONTEXT object: the connection dies when the
    // page does. A free function has no `this` to hand over, which is exactly
    // why the host is a parameter rather than something we reach for.
    QObject::connect(e.mode, &QComboBox::currentIndexChanged, host, sync);
    sync();
    return e;
}

ReturnPolicy readPolicyEditor(const PolicyEditor& e)
{
    ReturnPolicy p;
    p.mode  = returnModeFromString(e.mode->currentData().toString());
    p.time  = e.time->time();
    p.hours = e.hours->value();
    return p;
}

} // namespace settingsui

// ===========================================================================
// Agenda
// ===========================================================================

AgendaSettingsPage::AgendaSettingsPage(QWidget* parent) : SettingsPage(parent)
{
    const auto window = prefs::agendaWindow();

    // Combos, not spin boxes: the legal values are FEW and discrete (18
    // start hours), and a combo can't express 6:07 AM — the widget choice
    // deletes a whole class of validation. Each item carries its hour in
    // userData so nothing ever parses "6 AM" back into a number.
    m_startCombo = new QComboBox(this);
    m_startCombo->setObjectName("startHourCombo");
    for (int h = plan::kDayStartMinutes / 60; h <= 22; ++h)
        m_startCombo->addItem(hourName(h), h);
    m_startCombo->setCurrentIndex(m_startCombo->findData(window.first / 60));

    m_endCombo = new QComboBox(this);
    m_endCombo->setObjectName("endHourCombo");
    rebuildEndChoices();
    m_endCombo->setCurrentIndex(m_endCombo->findData(window.second / 60));

    // The start moved: the set of LEGAL ends changed, so rebuild them —
    // making "end before start" unpickable beats validating it on OK.
    connect(m_startCombo, &QComboBox::currentIndexChanged, this,
            &AgendaSettingsPage::rebuildEndChoices);

    m_weekCombo = new QComboBox(this);
    m_weekCombo->setObjectName("weekStartCombo");
    m_weekCombo->addItem(tr("Monday"), int(Qt::Monday));
    m_weekCombo->addItem(tr("Sunday"), int(Qt::Sunday));
    m_weekCombo->setCurrentIndex(
        m_weekCombo->findData(int(prefs::firstDayOfWeek())));

    // Lives HERE, not on the Pomodoro page: it is the whole agenda's
    // behaviour, while the Pomodoro's toggles sit next to their timer
    // because they change what THAT timer does.
    m_alarmCheck = new QCheckBox(tr("Notify when a planned block starts"), this);
    m_alarmCheck->setObjectName("blockAlarmCheck");
    m_alarmCheck->setChecked(prefs::blockStartNotify());

    auto* form = new QFormLayout(this);
    form->addRow(tr("Day starts at"), m_startCombo);
    form->addRow(tr("Day ends at"), m_endCombo);
    form->addRow(tr("Week starts on"), m_weekCombo);
    form->addRow(QString(), m_alarmCheck);

    // MOVED (v26.1): this note used to sit at the very bottom of the dialog,
    // below every unrelated section, where it read as a statement about the
    // whole of Settings. It is about these three hour controls and nothing
    // else, so it now sits with them. A caveat filed under the wrong heading
    // is a caveat nobody applies correctly.
    form->addRow(footnote(tr("Blocks outside these hours still show — the "
                             "agenda stretches so nothing you planned can "
                             "hide."),
                          this));
}

void AgendaSettingsPage::rebuildEndChoices()
{
    // Keep the user's pick across the rebuild when it's still legal —
    // losing a selection because an UNRELATED combo moved feels broken.
    const QVariant kept = m_endCombo->currentData();
    const int startHour = m_startCombo->currentData().toInt();

    m_endCombo->clear();
    for (int h = startHour + 1; h <= 24; ++h)
        m_endCombo->addItem(hourName(h), h);

    const int keptIndex = kept.isValid() ? m_endCombo->findData(kept) : -1;
    m_endCombo->setCurrentIndex(keptIndex >= 0 ? keptIndex
                                               : m_endCombo->count() - 1);
}

void AgendaSettingsPage::save()
{
    prefs::setAgendaWindow(m_startCombo->currentData().toInt() * 60,
                           m_endCombo->currentData().toInt() * 60);
    prefs::setFirstDayOfWeek(Qt::DayOfWeek(m_weekCombo->currentData().toInt()));
    prefs::setBlockStartNotify(m_alarmCheck->isChecked());
}

// ===========================================================================
// Needs a block
// ===========================================================================

NeedsBlockSettingsPage::NeedsBlockSettingsPage(QWidget* parent)
    : SettingsPage(parent)
{
    const auto rule = prefs::needsBlockRule();

    m_urgent = new QCheckBox(tr("Urgent"), this);
    m_urgent->setChecked(rule.flagUrgent);
    m_medium = new QCheckBox(tr("Medium"), this);
    m_medium->setChecked(rule.flagMedium);
    m_low = new QCheckBox(tr("Low"), this);
    m_low->setChecked(rule.flagLow);

    // The window: FEW discrete legal values -> a combo with userData, the
    // same widget-deletes-the-validation choice as the agenda hours.
    m_window = new QComboBox(this);
    m_window->addItem(tr("Off — priority only"), 0);
    for (int d : {1, 3, 7, 14})
        m_window->addItem(tr("%n day(s)", nullptr, d), d);
    m_window->setCurrentIndex(qMax(0, m_window->findData(rule.dueWithinDays)));

    m_review  = settingsui::makePolicyEditor(prefs::reviewReturnPolicy(),
                                             /*offerEndOfDay=*/false, this);
    m_dismiss = settingsui::makePolicyEditor(prefs::dismissReturnPolicy(),
                                             /*offerEndOfDay=*/true, this);

    const auto esc = prefs::needsBlockEscalation();
    m_escalate = new QSpinBox(this);
    m_escalate->setRange(1, 20);
    m_escalate->setValue(esc.decisionAfter);
    m_escalate->setSuffix(tr(" put-offs"));

    m_escUrgentOnly = new QCheckBox(tr("Only escalate urgent tasks"), this);
    m_escUrgentOnly->setChecked(esc.urgentOnly);

    m_gate = new QCheckBox(tr("Hold today's numbers until I've looked"), this);
    m_gate->setObjectName("needsBlockGateCheck");
    m_gate->setChecked(prefs::needsBlockGateEnabled());

    auto* form = new QFormLayout(this);
    form->addRow(tr("Always flag priorities"),
                 settingsui::rowOf(this, {m_urgent, m_medium, m_low}));
    form->addRow(tr("Also flag anything due within"), m_window);

    // The one rule that is NOT a knob, said where the knobs live (§A):
    // every relaxation of coverage is a way for the app to call a task
    // handled when it isn't.
    form->addRow(footnote(tr("A block only counts if it falls on or before "
                             "the deadline and hasn't already passed. That "
                             "part isn't configurable — it's the failure this "
                             "feature exists to prevent."),
                          this));

    form->addRow(heading(tr("When things come back"), this));
    form->addRow(tr("Review comes back"),
                 settingsui::rowOf(this, {m_review.mode, m_review.time,
                                          m_review.hours}));
    form->addRow(tr("\u201CNot today\u201D lasts"),
                 settingsui::rowOf(this, {m_dismiss.mode, m_dismiss.time,
                                          m_dismiss.hours}));

    form->addRow(heading(tr("Escalation"), this));
    form->addRow(tr("Ask for a decision after"), m_escalate);
    form->addRow(QString(), m_escUrgentOnly);
    form->addRow(QString(), m_gate);
}

void NeedsBlockSettingsPage::save()
{
    coverage::Rule rule;
    rule.flagUrgent    = m_urgent->isChecked();
    rule.flagMedium    = m_medium->isChecked();
    rule.flagLow       = m_low->isChecked();
    rule.dueWithinDays = m_window->currentData().toInt();
    prefs::setNeedsBlockRule(rule);

    coverage::Escalation esc;
    esc.decisionAfter = m_escalate->value();
    esc.urgentOnly    = m_escUrgentOnly->isChecked();
    prefs::setNeedsBlockEscalation(esc);

    prefs::setReviewReturnPolicy(settingsui::readPolicyEditor(m_review));
    prefs::setDismissReturnPolicy(settingsui::readPolicyEditor(m_dismiss));

    // Setter, not raw write: disabling the gate also forgets the last look,
    // so re-enabling re-arms honestly (§E) — behaviour that must live in ONE
    // place, which is exactly what the accessor is for.
    prefs::setNeedsBlockGateEnabled(m_gate->isChecked());
}

// ===========================================================================
// Catch-up
// ===========================================================================

CatchUpSettingsPage::CatchUpSettingsPage(QWidget* parent) : SettingsPage(parent)
{
    const missed::Rule rule = prefs::missedRule();

    // Combos with userData, not spin boxes — the same widget-deletes-the-
    // validation choice as every other page. The threshold's legal values
    // aren't ALL of 0..100: the meaningful settings are a handful of
    // stances, and naming them beats a naked percent field. (The stored
    // value is still a plain int, so a hand-edited 37 in QSettings loads
    // fine — the getter clamps, and the combo lands on the nearest stance
    // via findData's miss falling back to the default row.)
    m_threshold = new QComboBox(this);
    m_threshold->setObjectName("catchUpThresholdCombo");
    // 0 is load-bearing for the first stance: judge() flags NeverStarted on
    // zero focus REGARDLESS of the threshold, and the Partial branch tests
    // focus*100 < minPercent*planned — at 0 that is never true, so partial
    // blocks stop counting entirely, which is exactly what the label says.
    // (At 1, a 30-second dab in a 90-minute block would still flag.)
    m_threshold->addItem(tr("Only if never started"), 0);
    m_threshold->addItem(tr("Under a quarter done (25%)"), 25);
    m_threshold->addItem(tr("Under half done (50%)"), 50);
    m_threshold->addItem(tr("Under three quarters (75%)"), 75);
    m_threshold->addItem(tr("Anything short of the plan (100%)"), 100);
    {
        const int idx = m_threshold->findData(rule.minPercent);
        m_threshold->setCurrentIndex(idx >= 0 ? idx
                                              : m_threshold->findData(50));
    }

    m_lookBack = new QComboBox(this);
    m_lookBack->setObjectName("catchUpLookBackCombo");
    for (int d : {3, 7, 14, 30})
        m_lookBack->addItem(tr("%n day(s)", nullptr, d), d);
    m_lookBack->setCurrentIndex(qMax(0, m_lookBack->findData(rule.lookBackDays)));

    m_onOpen = new QCheckBox(tr("When I open the app (recover the day)"), this);
    m_onOpen->setObjectName("catchUpOnOpenCheck");
    m_onOpen->setChecked(prefs::catchUpOnOpen());
    m_endOfDay = new QCheckBox(tr("At the end of my day (replan what's left)"),
                               this);
    m_endOfDay->setObjectName("catchUpEndOfDayCheck");
    m_endOfDay->setChecked(prefs::catchUpAtEndOfDay());

    m_horizon = new QComboBox(this);
    m_horizon->setObjectName("catchUpHorizonCombo");
    for (int d : {7, 14, 30})
        m_horizon->addItem(tr("%n day(s) ahead", nullptr, d), d);
    m_horizon->setCurrentIndex(
        qMax(0, m_horizon->findData(prefs::catchUpHorizonDays())));

    auto* form = new QFormLayout(this);
    form->addRow(tr("A block counts as missed if"), m_threshold);
    form->addRow(tr("Look back over the last"), m_lookBack);

    form->addRow(heading(tr("When to ask"), this));
    form->addRow(QString(), m_onOpen);
    form->addRow(QString(), m_endOfDay);
    form->addRow(tr("Propose new times up to"), m_horizon);

    // The rule that is NOT a knob, stated where the knobs live — the same
    // habit as the needs-a-block page's coverage note.
    form->addRow(footnote(tr("Only focus time counts toward a block — break "
                             "and distracted time don't. That part isn't "
                             "configurable: it's what keeps a block full of "
                             "procrastination from passing as done."),
                          this));

    // The feature's one promise, said out loud where its knobs are.
    form->addRow(footnote(tr("Nothing is ever moved automatically. The card "
                             "proposes; only you accept."),
                          this));
}

void CatchUpSettingsPage::save()
{
    missed::Rule rule;
    rule.minPercent   = m_threshold->currentData().toInt();
    rule.lookBackDays = m_lookBack->currentData().toInt();
    prefs::setMissedRule(rule);

    prefs::setCatchUpOnOpen(m_onOpen->isChecked());
    prefs::setCatchUpAtEndOfDay(m_endOfDay->isChecked());
    prefs::setCatchUpHorizonDays(m_horizon->currentData().toInt());
}

// ===========================================================================
// Assistant
// ===========================================================================

AssistantSettingsPage::AssistantSettingsPage(QWidget* parent)
    : SettingsPage(parent)
{
    // The combo is populated FROM ai::catalog() rather than from a list
    // written out here. Adding a provider then means editing one function in
    // one file, and this page learns about it for free — the alternative is
    // a second list that drifts from the first.
    m_provider = new QComboBox(this);
    m_provider->setObjectName("aiProviderCombo");
    for (const ai::Provider& p : ai::catalog())
        m_provider->addItem(p.displayName, p.id);

    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setObjectName("aiKeyEdit");
    m_modelEdit = new QLineEdit(this);
    m_modelEdit->setObjectName("aiModelEdit");

    // Custom-endpoint controls live in ONE container widget added as a
    // label-less form row, so showing and hiding them is a single call on a
    // single widget. (A QFormLayout row is a label plus a field; hiding the
    // field alone leaves an orphan label sitting there.)
    m_baseUrl = new QLineEdit(this);
    m_baseUrl->setObjectName("aiBaseUrlEdit");
    m_baseUrl->setPlaceholderText(tr("http://localhost:1234"));
    m_dialect = new QComboBox(this);
    m_dialect->setObjectName("aiDialectCombo");
    m_dialect->addItem(tr("OpenAI-compatible"),
                       ai::dialectToString(ai::Dialect::OpenAi));
    m_dialect->addItem(tr("Anthropic"),
                       ai::dialectToString(ai::Dialect::Anthropic));
    m_customRow = new QWidget(this);
    {
        auto* row = new QHBoxLayout(m_customRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(new QLabel(tr("Address"), m_customRow));
        row->addWidget(m_baseUrl, 1);
        row->addWidget(new QLabel(tr("speaks"), m_customRow));
        row->addWidget(m_dialect);
    }

    {
        QSettings settings;
        m_baseUrl->setText(
            settings.value(QStringLiteral("ai/customBaseUrl")).toString());
        m_dialect->setCurrentIndex(m_dialect->findData(
            ai::dialectToString(ai::dialectFromString(
                settings.value(QStringLiteral("ai/customDialect")).toString()))));

        const QString currentId = ai::configured().id;
        m_provider->setCurrentIndex(m_provider->findData(currentId));
        loadFieldsFor(currentId);
    }

    // Order matters inside the slot, not here: stash-then-load. Connected
    // AFTER the initial load so construction doesn't stash empty fields over
    // the value we just read.
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this]() {
        stashFields();
        loadFieldsFor(m_provider->currentData().toString());
        // A verdict belongs to the provider it tested; carrying "✓ Connected"
        // across a combo switch would vouch for a setup nobody has tried.
        if (m_testResult)
            m_testResult->clear();
    });

    // Test, in place. One row: the button and its verdict side by side,
    // because an answer that appears elsewhere on screen is an answer half
    // the users never find.
    m_probe = new ChatClient(this); // child: dies with the page, and an
                                    // in-flight probe dies with it — a closed
                                    // dialog must never toast
    m_test = new QPushButton(tr("Test"), this);
    m_test->setObjectName("aiTestButton");
    m_test->setCursor(Qt::PointingHandCursor);
    m_testResult = new QLabel(QString(), this);
    m_testResult->setObjectName("aiTestResult");
    m_testResult->setWordWrap(true);

    connect(m_test, &QPushButton::clicked, this,
            &AssistantSettingsPage::runKeyTest);
    connect(m_probe, &ChatClient::replied, this, [this](const QString&) {
        // The reply's CONTENT is irrelevant — a 200 with a well-formed
        // envelope already proves address, dialect, key and model agree.
        m_test->setEnabled(true);
        m_testResult->setStyleSheet("color:#2F7E6E; font-weight:600;");
        m_testResult->setText(
            tr("✓ Connected — %1 answered.").arg(providerFromFields().displayName));
    });
    connect(m_probe, &ChatClient::failed, this, [this](const QString& why) {
        m_test->setEnabled(true);
        m_testResult->setStyleSheet("color:#C25B54;");
        m_testResult->setText(why); // already provider-aware and fix-naming
    });

    // The persona. Combo populated FROM chat::personaCatalog(), the same
    // one-list rule as the provider combo. The free text is a QLineEdit ON
    // PURPOSE, not a QPlainTextEdit: short persona text beats an elaborate
    // character, because long character prompts crowd out the rules. One
    // line invites a note; a text area invites a character sheet. maxLength
    // is the second fence.
    m_persona = new QComboBox(this);
    m_persona->setObjectName("aiPersonaCombo");
    for (const chat::Persona& p : chat::personaCatalog())
        m_persona->addItem(p.displayName, p.id);
    m_personaText = new QLineEdit(this);
    m_personaText->setObjectName("aiPersonaTextEdit");
    m_personaText->setMaxLength(240);
    m_personaText->setPlaceholderText(
        tr("optional — e.g. call me Sam, skip greetings"));
    {
        QSettings settings;
        m_persona->setCurrentIndex(m_persona->findData(
            chat::personaById(settings.value(chat::settingsKeyPersona(),
                                             QStringLiteral("calm"))
                                  .toString())
                .id)); // personaById repairs an unknown id -> the combo always
                       // lands on a real row, never index -1
        m_personaText->setText(
            settings.value(chat::settingsKeyPersonaText()).toString());
    }

    // The fallback seat: the second entry of the chat route. The PRIMARY
    // stays the Provider combo above — one mental model, not two — and this
    // row answers "and if that seat is unreachable?". "Unreachable" is
    // load-bearing: a wrong key or bad address fails loudly and never
    // wanders here (§E). Quick-add gets no row on purpose: its fallback is
    // the deterministic parser it already has.
    m_chatFallback = new QComboBox(this);
    m_chatFallback->setObjectName("aiChatFallbackCombo");
    m_chatFallback->addItem(tr("Nothing — fail fast"), QString());
    for (const ai::Provider& p : ai::catalog())
        m_chatFallback->addItem(ai::seatName(p.id), p.id);
    {
        const QStringList route = ai::configuredRouteIds(ai::Feature::Chat);
        const QString second = route.size() > 1 ? route.at(1) : QString();
        m_chatFallback->setCurrentIndex(
            std::max(0, m_chatFallback->findData(second)));
    }

    auto* form = new QFormLayout(this);
    form->addRow(tr("Provider"), m_provider);
    form->addRow(QString(), m_customRow);
    form->addRow(tr("Model"), m_modelEdit);
    form->addRow(tr("API key"), m_keyEdit);
    // NOT settingsui::rowOf(): that helper left-packs widgets and puts the
    // stretch at the END, which is right for the priority checkboxes and the
    // policy editors. Here the RESULT LABEL is the thing that must expand —
    // it word-wraps a provider error that can run two lines, and a label
    // sized to its hint would clip it. A helper that almost fits is worse
    // than no helper; the four honest lines say what they mean.
    auto* testRow = new QWidget(this);
    auto* testLayout = new QHBoxLayout(testRow);
    testLayout->setContentsMargins(0, 0, 0, 0);
    testLayout->setSpacing(8);
    testLayout->addWidget(m_test);
    testLayout->addWidget(m_testResult, 1); // the 1 is the point
    form->addRow(QString(), testRow);
    form->addRow(tr("If unreachable, try"), m_chatFallback);

    form->addRow(heading(tr("Style"), this));
    form->addRow(tr("Assistant style"), m_persona);
    form->addRow(tr("Extra instructions"), m_personaText);
}

ai::Provider AssistantSettingsPage::providerFromFields() const
{
    // The resolution rules are deliberately identical to ai::configured()
    // (custom takes address + dialect; a non-empty model field overrides the
    // default), so a ✓ here is a promise about what OK would produce.
    ai::Provider p = ai::byId(m_provider->currentData().toString());
    if (p.id == QLatin1String("custom")) {
        const QString url = m_baseUrl->text().trimmed();
        if (!url.isEmpty())
            p.baseUrl = QUrl(url);
        p.dialect = ai::dialectFromString(m_dialect->currentData().toString());
    }
    const QString model = m_modelEdit->text().trimmed();
    if (!model.isEmpty())
        p.model = model;
    return p;
}

void AssistantSettingsPage::runKeyTest()
{
    const ai::Provider p = providerFromFields();

    // Field first, environment second — the same order configuredKey uses,
    // with the on-screen field standing in for the QSettings half. Passed as
    // a FULL override (even when empty): the probe must test this exact
    // composition, not silently fall back to a key saved last week.
    const QString field = m_keyEdit->text().trimmed();
    m_probe->setProviderOverride(p);
    m_probe->setKeyOverride(field.isEmpty() ? ai::envKey(p) : field);

    m_test->setEnabled(false);
    m_testResult->setStyleSheet("color:#616974;");
    m_testResult->setText(tr("Testing…"));

    // The cheapest request that still exercises address, auth, dialect and
    // model: one system line, one word, and the reply is discarded. The
    // fail-fast paths (no key, no address) answer synchronously and offline —
    // which is also what makes this testable without a socket.
    m_probe->send(QStringLiteral("Reply with only the word OK."),
                  {ai::Message{ai::Role::User, QStringLiteral("ping")}});
}

void AssistantSettingsPage::stashFields()
{
    // Nothing shown yet (construction) — there is no owner to attribute the
    // text to, and guessing would attribute it to the wrong provider.
    if (m_shownId.isEmpty())
        return;
    m_keys.insert(m_shownId, m_keyEdit->text().trimmed());
    m_models.insert(m_shownId, m_modelEdit->text().trimmed());
}

void AssistantSettingsPage::loadFieldsFor(const QString& providerId)
{
    const ai::Provider p = ai::byId(providerId);
    QSettings settings;

    // The in-memory buffer wins over disk: it holds what was typed and not
    // yet saved. contains() rather than value().isEmpty() — deliberately
    // cleared to empty is a real edit and must survive a round trip through
    // another provider.
    m_keyEdit->setText(
        m_keys.contains(providerId)
            ? m_keys.value(providerId)
            : settings.value(ai::settingsKeyForKey(providerId)).toString());
    m_modelEdit->setText(
        m_models.contains(providerId)
            ? m_models.value(providerId)
            : settings.value(ai::settingsKeyForModel(providerId)).toString());

    // Placeholders carry the manual: the model box shows what will be used if
    // left blank, and the key box says outright when none is needed — a local
    // model's empty key field would otherwise read as "unconfigured".
    m_modelEdit->setPlaceholderText(p.defaultModel.isEmpty()
                                        ? tr("required for a custom endpoint")
                                        : p.defaultModel);
    m_keyEdit->setPlaceholderText(
        p.needsKey ? tr("%1 (or the %2 environment variable)")
                         .arg(p.keyHint, QString::fromLatin1(p.envVar))
                   : tr("not required"));

    m_customRow->setVisible(providerId == QLatin1String("custom"));
    m_shownId = providerId;
}

void AssistantSettingsPage::save()
{
    // Written raw (no prefs:: wrapper) but through ai::'s key-name functions,
    // so the string "ai/key/<id>" is spelled in exactly one place. Whatever
    // is typed right now belongs to the provider on screen, so stash first.
    stashFields();

    QSettings settings;
    settings.setValue(QStringLiteral("ai/provider"),
                      m_provider->currentData().toString());
    settings.setValue(QStringLiteral("ai/customBaseUrl"),
                      m_baseUrl->text().trimmed());
    settings.setValue(QStringLiteral("ai/customDialect"),
                      m_dialect->currentData().toString());

    // Every provider the user touched this session, not just the selected
    // one: switching away must not silently discard a pasted key.
    for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it)
        settings.setValue(ai::settingsKeyForKey(it.key()), it.value());
    for (auto it = m_models.constBegin(); it != m_models.constEnd(); ++it)
        settings.setValue(ai::settingsKeyForModel(it.key()), it.value());

    settings.setValue(chat::settingsKeyPersona(),
                      m_persona->currentData().toString());
    settings.setValue(chat::settingsKeyPersonaText(),
                      m_personaText->text().trimmed());

    // The chat route, stored as the FULL ordered list even though this page
    // only edits two positions: the storage shape is §E's (route = ordered
    // seats), so a longer route needs no format change, only more UI. A
    // fallback equal to the primary is meaningless and is stored as a
    // one-seat route rather than [x, x].
    const QString primary = m_provider->currentData().toString();
    const QString second  = m_chatFallback->currentData().toString();
    QStringList route{primary};
    if (!second.isEmpty() && second != primary)
        route.append(second);
    settings.setValue(ai::settingsKeyRoute(ai::Feature::Chat), route);
}

// ---------------------------------------------------------------------------
// MemorySettingsPage (v30.0)
// ---------------------------------------------------------------------------

namespace
{

// One entry per line, both ways. Blank lines are dropped rather than stored
// as empty entries — an empty bullet in the file would render as "- " and
// read to a model as a fact it failed to receive.
QStringList linesToEntries(const QPlainTextEdit* edit)
{
    QStringList out;
    const QStringList lines =
        edit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

QPlainTextEdit* sectionEditor(QWidget* parent, const QString& objectName,
                              const QStringList& entries)
{
    auto* edit = new QPlainTextEdit(parent);
    edit->setObjectName(objectName);
    edit->setPlainText(entries.join(QLatin1Char('\n')));
    // Four boxes on one page: each stays small enough that all four are
    // visible together, which is what makes the whole file readable at once.
    edit->setFixedHeight(84);
    return edit;
}

} // namespace

MemorySettingsPage::MemorySettingsPage(QString filePath, QWidget* parent)
    : SettingsPage(parent)
    , m_filePath(filePath.isEmpty() ? MemoryStore::defaultFilePath()
                                    : std::move(filePath))
{
    const memory::File file = MemoryStore(m_filePath).load();

    auto* intro = new QLabel(
        tr("Short, lasting things about you that the planner cannot work out "
           "on its own. The assistant is told this at the start of every "
           "conversation.\n\n"
           "One per line. Replace a line when it changes rather than adding "
           "another — two answers to the same question read as a "
           "contradiction.\n\n"
           "Do NOT put tasks, deadlines or what you did yesterday here. The "
           "assistant already sees all of that, freshly, every time."),
        this);
    intro->setWordWrap(true);
    intro->setObjectName("sub");

    m_routines = sectionEditor(this, QStringLiteral("memoryRoutinesEdit"),
                               file.routines);
    m_preferences = sectionEditor(this, QStringLiteral("memoryPreferencesEdit"),
                                  file.preferences);
    m_situation = sectionEditor(this, QStringLiteral("memorySituationEdit"),
                                file.situation);
    m_people = sectionEditor(this, QStringLiteral("memoryPeopleEdit"),
                             file.people);

    m_cost = new QLabel(this);
    m_cost->setObjectName("memoryCostLabel");
    m_cost->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Routines"), m_routines);
    form->addRow(tr("Preferences"), m_preferences);
    form->addRow(tr("Current situation"), m_situation);
    form->addRow(tr("People"), m_people);

    auto* root = new QVBoxLayout(this);
    root->addWidget(intro);
    root->addSpacing(8);
    root->addLayout(form);
    root->addWidget(m_cost);

    // A file that is preserved but not understood is worth saying out loud,
    // so nobody wonders where their hand-written section went.
    if (!file.preserved.isEmpty()) {
        auto* kept = new QLabel(
            tr("This file also contains text this version did not recognise. "
               "It is kept exactly as written, under \"%1\", and is never sent "
               "to the assistant.")
                .arg(memory::preservedHeading()),
            this);
        kept->setWordWrap(true);
        kept->setObjectName("sub");
        root->addWidget(kept);
    }

    auto* where = new QLabel(tr("Stored at %1").arg(m_filePath), this);
    where->setWordWrap(true);
    where->setObjectName("sub");
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(where);
    root->addStretch(1);

    // Live, because the number is the point: this is the one setting whose
    // cost is paid again on every single turn.
    for (QPlainTextEdit* edit : {m_routines, m_preferences, m_situation,
                                 m_people})
        connect(edit, &QPlainTextEdit::textChanged, this,
                &MemorySettingsPage::refreshCost);
    refreshCost();
}

void MemorySettingsPage::refreshCost()
{
    memory::File current;
    current.routines    = linesToEntries(m_routines);
    current.preferences = linesToEntries(m_preferences);
    current.situation   = linesToEntries(m_situation);
    current.people      = linesToEntries(m_people);

    const int written = current.entryCount();
    const QString band = memory::promptBand(current);
    // Re-parsing the band to count what survived would be guesswork; asking
    // for an unlimited band and comparing sizes is exact.
    const QString untrimmed =
        memory::promptBand(current, std::numeric_limits<int>::max());

    if (written == 0) {
        m_cost->setText(tr("Nothing stored yet — the assistant is told "
                           "nothing about you."));
        return;
    }

    if (band.size() < untrimmed.size()) {
        m_cost->setText(
            tr("%1 of %2 characters — over the limit, so the entries that do "
               "not fit are left out (whole, never cut in half). Shorten or "
               "remove a few.")
                .arg(untrimmed.size())
                .arg(memory::kDefaultBudgetChars));
        return;
    }

    m_cost->setText(tr("%n line(s), %1 of %2 characters, sent every turn.",
                       nullptr, written)
                        .arg(band.size())
                        .arg(memory::kDefaultBudgetChars));
}

void MemorySettingsPage::save()
{
    // Re-read FIRST, and take the preserved half from disk rather than from
    // the copy loaded when this page was built. The file is hand-editable and
    // this dialog may have been open for a while; text added in an editor
    // meanwhile must not be destroyed by an OK here. The four sections are
    // the page's to own — the rest is the owner's, always.
    MemoryStore  store(m_filePath);
    memory::File file = store.load();

    file.routines    = linesToEntries(m_routines);
    file.preferences = linesToEntries(m_preferences);
    file.situation   = linesToEntries(m_situation);
    file.people      = linesToEntries(m_people);

    store.save(file);
}
