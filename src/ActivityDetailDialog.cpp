#include "ActivityDetailDialog.h"

#include "AppData.h"     // the free helpers only — the dialogs never use it
#include "Recurrence.h"  // recur::summary, recur::problemWith — one home
#include "Widgets.h"     // isCompactScreen

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace
{
// The same small caption TaskDetailForm draws over each field. Copied rather
// than shared because it is three lines of styling with no behaviour; the
// moment it grows a rule it belongs in Widgets.h with the others.
QLabel* caption(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    return l;
}

// Minutes-after-midnight is the domain's unit (Event, Schedule); QTime is the
// widget's. The conversion lives in exactly these two functions so it cannot
// be spelled differently in two places — off-by-an-hour bugs live in
// duplicated unit conversions.
QTime toClock(int minutes) { return QTime(minutes / 60, minutes % 60); }
int fromClock(QTime t) { return t.isValid() ? t.hour() * 60 + t.minute() : 0; }

// The reminder choices, as a table rather than five hand-written addItem
// calls plus a switch to read them back. The value rides in the item's data,
// so the order on screen and the meaning cannot drift apart.
struct ReminderChoice {
    int minutes;
    const char* label;
};
const ReminderChoice kReminders[] = {
    {0,  QT_TRANSLATE_NOOP("ScheduleEditDialog", "At the start")},
    {5,  QT_TRANSLATE_NOOP("ScheduleEditDialog", "5 minutes before")},
    {10, QT_TRANSLATE_NOOP("ScheduleEditDialog", "10 minutes before")},
    {15, QT_TRANSLATE_NOOP("ScheduleEditDialog", "15 minutes before")},
    {30, QT_TRANSLATE_NOOP("ScheduleEditDialog", "30 minutes before")},
    {60, QT_TRANSLATE_NOOP("ScheduleEditDialog", "1 hour before")},
};
} // namespace

// ---------------------------------------------------------------------------
// ScheduleEditDialog
// ---------------------------------------------------------------------------

ScheduleEditDialog::ScheduleEditDialog(const Schedule& seed, QWidget* parent)
    : QDialog(parent)
    , m_seed(seed)
{
    setWindowTitle(tr("When does this happen?"));
    setMinimumWidth(isCompactScreen() ? 0 : 380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    // ---- repeat -----------------------------------------------------------
    layout->addWidget(caption(tr("REPEATS"), this));
    m_repeat = new QComboBox(this);
    m_repeat->setObjectName(QStringLiteral("scheduleRepeatCombo"));
    // Items in enum order so currentIndex IS the enum — the same trick
    // TaskDetailForm and EventDialog use, so all three read alike.
    m_repeat->addItem(tr("Does not repeat")); // Repeat::None   == 0
    m_repeat->addItem(tr("Daily"));           // Repeat::Daily  == 1
    m_repeat->addItem(tr("Weekly"));          // Repeat::Weekly == 2
    m_repeat->addItem(tr("Monthly"));         // Repeat::Monthly== 3
    m_repeat->addItem(tr("Yearly"));          // Repeat::Yearly == 4
    m_repeat->setCurrentIndex(int(seed.repeat));
    layout->addWidget(m_repeat);

    // ---- WHICH DAYS (v31.1) ------------------------------------------------
    // Reported by the owner: a class every Wednesday, in a term that starts
    // on a Monday. The start date used to decide the weekday, so the term's
    // first day silently became the class's day. These checkboxes are the
    // fact that used to be smuggled inside startDate.
    layout->addSpacing(4);
    auto* daysCaption = caption(tr("ON THESE DAYS"), this);
    m_daysRow = new QWidget(this);
    // A GRID, four across, not a single row: seven checkboxes side by side
    // measured 361px on their own and pushed this dialog to 397 against a
    // 360dp phone. Caught by dialogsFitAPhoneScreen in the same drop that
    // added them, which is the only reason it is not shipping.
    //
    // Four columns rather than single-letter labels, because M/T/W/T/F/S/S
    // has two Ts and two Ss — narrower, and ambiguous exactly where a
    // mis-tap costs you a term of Thursdays.
    auto* daysLayout = new QGridLayout(m_daysRow);
    daysLayout->setContentsMargins(0, 0, 0, 0);
    daysLayout->setHorizontalSpacing(2);
    daysLayout->setVerticalSpacing(2);
    const QLocale loc;
    // Seeded through effectiveWeekdays, so a rule written before this field
    // existed opens with its CURRENT behaviour ticked rather than blank —
    // the old meaning made visible instead of implied.
    const QList<int> seeded = recur::effectiveWeekdays(seed);
    for (int day = 1; day <= 7; ++day) {
        auto* box = new QCheckBox(loc.dayName(day, QLocale::ShortFormat),
                                  m_daysRow);
        box->setObjectName(QStringLiteral("day%1").arg(day));
        box->setChecked(seeded.contains(day));
        m_days.append(box);
        daysLayout->addWidget(box, (day - 1) / 4, (day - 1) % 4);
    }
    layout->addWidget(daysCaption);
    layout->addWidget(m_daysRow);

    // Only weekly consults them (Schedule.h), so only weekly shows them: a
    // control that cannot affect anything is worse than an absent one.
    const auto syncDays = [this, daysCaption]() {
        const bool weekly =
            m_repeat->currentIndex() == int(Task::Repeat::Weekly);
        daysCaption->setVisible(weekly);
        m_daysRow->setVisible(weekly);
    };
    connect(m_repeat, &QComboBox::currentIndexChanged, this, syncDays);
    syncDays();

    // ---- first date -------------------------------------------------------
    // Labelled "first date" because that is now all it is (v31.1). It used
    // to carry the weekday too, which is precisely the bug the checkboxes
    // above fix: a term beginning on a Monday turned a Wednesday class into
    // a Monday one. The first occurrence is the first CHOSEN day on or
    // after this date.
    layout->addSpacing(4);
    layout->addWidget(caption(tr("FIRST DATE"), this));
    m_startDate = new QDateEdit(this);
    m_startDate->setObjectName(QStringLiteral("scheduleStartDate"));
    m_startDate->setCalendarPopup(true);
    m_startDate->setDisplayFormat(QStringLiteral("MMM d, yyyy"));
    m_startDate->setDate(seed.startDate.isValid() ? seed.startDate
                                                  : QDate::currentDate());
    layout->addWidget(m_startDate);

    // ---- time of day ------------------------------------------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("TIME"), this));
    auto* timeRow = new QHBoxLayout;
    timeRow->setSpacing(8);
    m_startTime = new QTimeEdit(this);
    m_startTime->setObjectName(QStringLiteral("scheduleStartTime"));
    m_startTime->setDisplayFormat(QStringLiteral("HH:mm"));
    m_startTime->setTime(toClock(seed.startMinutes ? seed.startMinutes
                                                   : 9 * 60));
    m_endTime = new QTimeEdit(this);
    m_endTime->setObjectName(QStringLiteral("scheduleEndTime"));
    m_endTime->setDisplayFormat(QStringLiteral("HH:mm"));
    m_endTime->setTime(toClock(seed.endMinutes ? seed.endMinutes : 10 * 60));
    timeRow->addWidget(m_startTime);
    timeRow->addWidget(new QLabel(QStringLiteral("–"), this));
    timeRow->addWidget(m_endTime);
    timeRow->addStretch(1);
    layout->addLayout(timeRow);

    // ---- how long the rule runs -------------------------------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("REPEAT UNTIL"), this));
    m_noEnd = new QCheckBox(tr("No end date"), this);
    m_noEnd->setObjectName(QStringLiteral("scheduleNoEnd"));
    m_noEnd->setChecked(!seed.endDate.isValid());
    m_endDate = new QDateEdit(this);
    m_endDate->setObjectName(QStringLiteral("scheduleEndDate"));
    m_endDate->setCalendarPopup(true);
    m_endDate->setDisplayFormat(QStringLiteral("MMM d, yyyy"));
    m_endDate->setDate(seed.endDate.isValid()
                           ? seed.endDate
                           : QDate::currentDate().addMonths(4));
    m_endDate->setEnabled(!m_noEnd->isChecked());
    connect(m_noEnd, &QCheckBox::toggled, m_endDate, &QWidget::setDisabled);
    layout->addWidget(m_noEnd);
    layout->addWidget(m_endDate);

    // ---- reminder ---------------------------------------------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("REMIND ME"), this));
    m_reminder = new QComboBox(this);
    m_reminder->setObjectName(QStringLiteral("scheduleReminderCombo"));
    for (const ReminderChoice& c : kReminders)
        m_reminder->addItem(tr(c.label), c.minutes);
    const int at = m_reminder->findData(qMax(0, seed.reminderMinutes));
    m_reminder->setCurrentIndex(at >= 0 ? at : 0);
    layout->addWidget(m_reminder);

    layout->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* save = new QPushButton(tr("Save"), this);
    save->setObjectName("primary");
    save->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    // NOT straight to accept(): a rule the domain will refuse must not be
    // able to close this dialog. Before v31.1 it could — AppData returned
    // false, the dialog was already gone, and the edit vanished with nothing
    // said. Whoever refuses should be the one who explains.
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString why = problem();
        if (why.isEmpty()) {
            accept();
            return;
        }
        QMessageBox::warning(const_cast<ScheduleEditDialog*>(this),
                             tr("That rule cannot work"), why);
    });
}

QString ScheduleEditDialog::problem() const
{
    // The domain's own definition, asked here so the two cannot drift.
    return recur::problemWith(chosen());
}

Schedule ScheduleEditDialog::chosen() const
{
    Schedule out = m_seed; // carries id, activityId, title and skipDates
    out.repeat          = Task::Repeat(m_repeat->currentIndex());
    out.startDate       = m_startDate->date();
    out.startMinutes    = fromClock(m_startTime->time());
    out.endMinutes      = fromClock(m_endTime->time());
    out.endDate         = m_noEnd->isChecked() ? QDate() : m_endDate->date();
    out.reminderMinutes = m_reminder->currentData().toInt();

    out.weekdays.clear();
    if (out.repeat == Task::Repeat::Weekly)
        for (int i = 0; i < m_days.size(); ++i)
            if (m_days[i]->isChecked())
                out.weekdays.append(i + 1); // index+1 IS the Qt day number
    // Every box cleared is not "no days", it is "I did not choose" — and an
    // empty list already means "the weekday of startDate", which is the
    // behaviour this rule had before. Falling back to it beats saving a rule
    // that can never produce anything.
    return out;
    // NO validation here, deliberately. AppData::scheduleIsWellFormed is the
    // rule — a second, kinder copy of it in a dialog is how two answers to
    // "is this legal?" get born. An impossible rule is simply refused by the
    // apply step and the old one stands.
}

// ---------------------------------------------------------------------------

Schedule newScheduleSeed(const QString& activityId, QDate today)
{
    Schedule seed;
    // The link, first and not last. Without it the draft is nameless and the
    // dialog's own Save refuses it — see the header.
    seed.activityId   = activityId;
    // A new rule opens on something plausible rather than on midnight:
    // today, 9-10, weekly. Every field is still editable - this is a
    // starting point, not a decision made for the user.
    seed.startDate    = today;
    seed.startMinutes = 9 * 60;
    seed.endMinutes   = 10 * 60;
    seed.repeat       = Task::Repeat::Weekly;
    return seed;
}

// ---------------------------------------------------------------------------
// ActivityDetailDialog
// ---------------------------------------------------------------------------

ActivityDetailDialog::ActivityDetailDialog(const QString& activityId,
                                           const QString& name,
                                           const QString& description,
                                           QWidget* parent)
    : QDialog(parent), m_activityId(activityId)
{
    setWindowTitle(tr("Activity details"));
    // 0 on a phone, because a 380 minimum on a 360dp screen is a dialog that
    // pans sideways — the same number, and the same reason, as
    // TaskDetailDialog.
    setMinimumWidth(isCompactScreen() ? 0 : 380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    layout->addWidget(caption(tr("NAME"), this));
    m_name = new QLineEdit(name, this);
    m_name->setObjectName(QStringLiteral("activityNameEdit"));
    m_name->setPlaceholderText(tr("What is this kind of session called?"));
    layout->addWidget(m_name);

    layout->addSpacing(4);
    layout->addWidget(caption(tr("DESCRIPTION"), this));
    m_notes = new QPlainTextEdit(this);
    m_notes->setObjectName(QStringLiteral("activityNotesEdit"));
    m_notes->setPlainText(description);
    m_notes->setPlaceholderText(
        tr("What this session actually involves — equipment, room, a warm-up."));
    m_notes->setFixedHeight(96);
    layout->addWidget(m_notes);

    // ---- WHEN: the timetable ----------------------------------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("WHEN"), this));
    auto* hint = new QLabel(
        tr("Put this on the calendar. Each entry fills the agenda ahead on "
           "every date it names."),
        this);
    hint->setObjectName("sub");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_scheduleRows = new QVBoxLayout;
    m_scheduleRows->setContentsMargins(0, 0, 0, 0);
    m_scheduleRows->setSpacing(6);
    layout->addLayout(m_scheduleRows);

    auto* addTime = new QPushButton(tr("+ Add a time"), this);
    addTime->setObjectName(QStringLiteral("addScheduleButton"));
    addTime->setCursor(Qt::PointingHandCursor);
    connect(addTime, &QPushButton::clicked, this,
            [this]() { editScheduleAt(-1); });
    layout->addWidget(addTime);

    layout->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* save = new QPushButton(tr("Save"), this);
    save->setObjectName("primary");
    save->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);

    rebuildScheduleRows();

    // Opening the editor on an activity you meant to rename should let you
    // type immediately — the overwhelmingly common reason to be here.
    m_name->setFocus();
    m_name->selectAll();
}

void ActivityDetailDialog::seedSchedules(const QVector<Schedule>& schedules)
{
    m_schedules = schedules;
    rebuildScheduleRows();
}

void ActivityDetailDialog::rebuildScheduleRows()
{
    // deleteLater, not delete: a row's own ✕ triggers this rebuild, and
    // freeing that button synchronously inside its own click would unwind
    // into freed memory. The house rule, same as ActivitiesPage's clearLayout.
    //
    // AND hide() FIRST, which the page's version does not need and this one
    // does. Taking a widget out of a LAYOUT does not take it out of the
    // widget TREE: it keeps its parent, so it keeps painting — at (0,0),
    // because nothing is positioning it any more. On a page that is
    // invisible, since the next trip through the event loop deletes it.
    // Here it is not, and the reason is the one Qt documents and everybody
    // meets once: DeferredDelete is only processed by the event loop LEVEL
    // that posted it, and this rebuild runs before exec() opens a NESTED
    // loop. So the discarded widget survives for the whole life of the
    // dialog, and the phone showed it as a clipped "Not on the calenda"
    // stuck to the top-left corner over the real content.
    const auto discard = [](QWidget* w) {
        w->hide();
        w->deleteLater();
    };
    while (QLayoutItem* item = m_scheduleRows->takeAt(0)) {
        if (QWidget* w = item->widget())
            discard(w);
        if (QLayout* child = item->layout()) {
            while (QLayoutItem* sub = child->takeAt(0)) {
                if (QWidget* w = sub->widget())
                    discard(w);
                delete sub;
            }
        }
        delete item;
    }

    if (m_schedules.isEmpty()) {
        auto* none = new QLabel(tr("Not on the calendar yet."), this);
        none->setObjectName("sub");
        m_scheduleRows->addWidget(none);
        return;
    }

    for (int i = 0; i < m_schedules.size(); ++i) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        // The summary is the button, the same "put the control ON the thing
        // it affects" rule the activity rows and the area switcher follow.
        //
        // TWO LINES, and an explicit minimum of zero. A QPushButton reports
        // its whole text as its minimum width and cannot wrap, so one long
        // summary measured 476dp and pushed this dialog to 546 against a
        // 360dp phone — caught by dialogsFitAPhoneScreen in the same drop
        // that added it, which is the only reason it is not shipping.
        // summaryLines() breaks the sentence at its natural seam; the
        // explicit minimum lets the layout shrink the button below even the
        // longer half rather than force the dialog wider (qSmartMinSize
        // honours an explicit minimumSize over minimumSizeHint).
        auto* open = new QPushButton(
            recur::summaryLines(m_schedules[i]).join(QLatin1Char('\n')), this);
        open->setObjectName(QStringLiteral("scheduleSummaryButton"));
        open->setCursor(Qt::PointingHandCursor);
        open->setMinimumWidth(0);
        open->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        open->setToolTip(recur::summary(m_schedules[i])); // the full sentence
        open->setStyleSheet(
            "QPushButton { background:#EEF0ED; border:none; border-radius:8px;"
            " padding:7px 10px; color:#2B2F36; text-align:left; }");
        connect(open, &QPushButton::clicked, this,
                [this, i]() { editScheduleAt(i); });

        auto* remove = new QPushButton(QStringLiteral("×"), this);
        remove->setObjectName("danger");
        remove->setFixedWidth(28);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setToolTip(tr("Stop this schedule"));
        connect(remove, &QPushButton::clicked, this, [this, i]() {
            m_schedules.removeAt(i);
            rebuildScheduleRows();
        });

        row->addWidget(open, 1);
        row->addWidget(remove);
        m_scheduleRows->addLayout(row);
    }
}

void ActivityDetailDialog::editScheduleAt(int index)
{
    const Schedule seed = (index >= 0 && index < m_schedules.size())
                              ? m_schedules[index]
                              : newScheduleSeed(m_activityId,
                                                QDate::currentDate());

    ScheduleEditDialog dialog(seed, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (index >= 0 && index < m_schedules.size())
        m_schedules[index] = dialog.chosen();
    else
        m_schedules.append(dialog.chosen()); // empty id == new
    rebuildScheduleRows();
}

QString ActivityDetailDialog::chosenName() const { return m_name->text(); }

QString ActivityDetailDialog::chosenDescription() const
{
    return m_notes->toPlainText();
}

// ---------------------------------------------------------------------------

void applyActivityDetailAnswers(AppData& data, const QString& activityId,
                                const ActivityDetailDialog& dialog,
                                QDate today)
{
    // ONE changed() for what the user experienced as one Save. Without the
    // batch, a dialog holding three schedules would fire five or six
    // repaints and five or six autosaves on the way out.
    AppData::Batch batch(data);

    // Two guarded doors for the text, not one coarse updateActivity, because
    // the two fields have genuinely different rules: a blank name is
    // refused, a blank description is normal. Folding them into one setter
    // would mean one of those two truths had to be re-decided at the call
    // site.
    //
    // renameActivity's refusal is what makes an emptied name field a no-op
    // rather than a wipe — the old name survives, which is the only
    // non-destructive answer available to a dialog with no validation of
    // its own.
    data.renameActivity(activityId, dialog.chosenName());
    data.setActivityDescription(activityId, dialog.chosenDescription());

    // ---- reconcile the timetable ------------------------------------------
    // Update / add / remove, decided by comparing ids — the same shape
    // TaskDetailForm's pieces use. Removals are computed FIRST, against the
    // domain's current list, so a rule the user deleted in the dialog is
    // stopped even though the dialog no longer holds anything to point at.
    const QVector<Schedule> wanted = dialog.chosenSchedules();
    QStringList keptIds;
    for (const Schedule& s : wanted)
        if (!s.id.isEmpty())
            keptIds << s.id;

    QStringList doomed;
    for (const Schedule* existing : data.schedulesFor(activityId))
        if (!keptIds.contains(existing->id))
            doomed << existing->id;
    for (const QString& id : doomed)
        data.removeSchedule(id, today);

    for (const Schedule& s : wanted) {
        Schedule out = s;
        out.activityId = activityId; // the dialog never invents this link
        out.title.clear();           // an activity-backed rule needs no title
        if (out.id.isEmpty()) {
            const QString id = data.addSchedule(out);
            if (!id.isEmpty())
                data.syncSchedules(today);
        } else {
            data.updateSchedule(out, today); // re-materialises internally
        }
    }
}

void runActivityDetail(AppData& data, const QString& activityId,
                       QWidget* windowParent)
{
    const Activity* activity = data.activityById(activityId);
    if (!activity)
        return;

    // Seed by VALUE. The pointer above is valid only until the next
    // mutation (AppData's lifetime rule), and exec() below runs an event
    // loop in which anything — a sync apply, an assistant write — may
    // mutate. Copy what we need out now and let the pointer go.
    ActivityDetailDialog dialog(activityId, activity->name,
                                activity->description, windowParent);
    QVector<Schedule> seeds;
    for (const Schedule* s : data.schedulesFor(activityId))
        seeds.append(*s);
    dialog.seedSchedules(seeds);

    if (dialog.exec() == QDialog::Accepted)
        applyActivityDetailAnswers(data, activityId, dialog,
                                   QDate::currentDate());
}
