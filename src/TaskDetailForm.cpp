#include "TaskDetailForm.h"

#include "Widgets.h" // minutesLabel — the one duration formatter

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimeEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
// A tiny section caption, reused for each field — the uppercase grey label
// idiom already used across the app's panels. (Moved with the form; the
// dialog wrapper builds no fields of its own any more.)
QLabel* caption(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    return l;
}
} // namespace

TaskDetailForm::TaskDetailForm(const QString& title,
                               const QString& description, QDate dueDate,
                               QTime dueTime, Task::Repeat repeat,
                               Task::Priority priority, int estimateMinutes,
                               bool chunkable, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ---- title -------------------------------------------------------------
    layout->addWidget(caption(tr("TITLE"), this));
    m_title = new QLineEdit(title, this);
    m_title->setPlaceholderText(tr("What needs doing?"));
    layout->addWidget(m_title);

    // ---- description (the multi-line notes field) --------------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("DESCRIPTION"), this));
    m_notes = new QPlainTextEdit(this);
    m_notes->setPlainText(description);
    m_notes->setPlaceholderText(
        tr("Notes, links, a checklist — anything that helps future you."));
    m_notes->setFixedHeight(96);
    layout->addWidget(m_notes);

    // ---- deadline: the date, then optionally the clock ---------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("DEADLINE"), this));
    auto* dateRow = new QHBoxLayout;
    dateRow->setSpacing(8);
    m_date = new QDateEdit(this);
    m_date->setCalendarPopup(true);
    m_date->setDisplayFormat("MMM d, yyyy");
    // A sensible starting point either way: the task's current date, or
    // today for a task that has none yet.
    m_date->setDate(dueDate.isValid() ? dueDate : QDate::currentDate());
    m_noDate = new QCheckBox(tr("No due date"), this);
    m_noDate->setChecked(!dueDate.isValid()); // TBD tasks start ticked
    dateRow->addWidget(m_date, 1);
    dateRow->addWidget(m_noDate);
    layout->addLayout(dateRow);

    // The time row mirrors the date row exactly — editor + "this answer is
    // off" checkbox — so the second question is learnable from the first.
    auto* timeRow = new QHBoxLayout;
    timeRow->setSpacing(8);
    m_time = new QTimeEdit(this);
    m_time->setDisplayFormat(QStringLiteral("HH:mm"));
    // 23:59, not 00:00: a deadline time means "by then", and end-of-day is
    // what people mean the first time they reach for one. Midnight would
    // silently make every new timed task already late.
    m_time->setTime(dueTime.isValid() ? dueTime : QTime(23, 59));
    m_allDay = new QCheckBox(tr("All day"), this);
    m_allDay->setChecked(!dueTime.isValid());
    timeRow->addWidget(m_time, 1);
    timeRow->addWidget(m_allDay);
    layout->addLayout(timeRow);

    // ONE rule, two triggers. Ticking "no due date" also greys the time,
    // because a clock with no calendar is a state the domain refuses — the
    // UI's job is to never let the user compose it in the first place.
    syncDeadlineEnabled();
    connect(m_noDate, &QCheckBox::toggled,
            this, &TaskDetailForm::syncDeadlineEnabled);
    connect(m_allDay, &QCheckBox::toggled,
            this, &TaskDetailForm::syncDeadlineEnabled);

    // ---- repeat ------------------------------------------------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("REPEAT"), this));
    m_repeat = new QComboBox(this);
    // The item ORDER matches the enum's order, so the enum's integer value
    // IS the combo index — no lookup table, no chance of drift. (If the
    // enum ever gains a value mid-list, this assumption is where you'd fix
    // it; the comment is the tripwire.)
    m_repeat->addItem(tr("Does not repeat")); // Repeat::None  == 0
    m_repeat->addItem(tr("Daily"));           // Repeat::Daily == 1
    m_repeat->addItem(tr("Weekly"));          // Repeat::Weekly== 2
    m_repeat->addItem(tr("Monthly"));         // Repeat::Monthly==3
    m_repeat->addItem(tr("Yearly"));          // Repeat::Yearly ==4
    m_repeat->setCurrentIndex(static_cast<int>(repeat));
    layout->addWidget(m_repeat);

    // ---- priority (v7) ------------------------------------------------------
    // Same combo-index-equals-enum-value trick as repeat.
    layout->addSpacing(4);
    layout->addWidget(caption(tr("PRIORITY"), this));
    m_priority = new QComboBox(this);
    m_priority->addItem(tr("Urgent — do this first"));   // Priority::Urgent == 0
    m_priority->addItem(tr("Medium — the default"));     // Priority::Medium == 1
    m_priority->addItem(tr("Low — when there's time"));  // Priority::Low    == 2
    m_priority->setCurrentIndex(static_cast<int>(priority));
    layout->addWidget(m_priority);

    // ---- size (v28.3, §J.1) -------------------------------------------------
    // The two sizing facts on one row, because they are one question: "how
    // big is this, and what shape?". specialValueText makes the minimum (0)
    // display as words — "0 min" would read as "instant", and 0 means
    // "unanswered" (Task::hasEstimate). Step 15: estimates are
    // quarter-hour-grained things.
    layout->addSpacing(4);
    layout->addWidget(caption(tr("SIZE"), this));
    auto* sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(8);
    // v28.8 — a DROPDOWN, owner request ("720 min" is unreadable; hours
    // past the hour mark). Non-uniform steps, the duration-picker trick:
    // fine where tasks live, coarse where they don't — 15/30/45m (15
    // stays although the ask was 30-steps: "Fits short gaps" is built on
    // 15-minute holes, and the picker must be able to say the app's own
    // number), 1h–8h by half hours, 9h–16h by whole hours. The 16h CAP is
    // the answer to "how long should the list be": past two workdays an
    // estimate shouldn't grow — the task should break into pieces.
    // itemData carries the minutes; the label is minutesLabel's, so the
    // dropdown, the piece chip, and the planner all speak one dialect.
    m_estimate = new QComboBox(this);
    m_estimate->setObjectName(QStringLiteral("estimateCombo"));
    // v28.9.1 — 26 rungs is a useful LADDER, not a useful POPUP: cap the
    // open list at 6 rows and let it scroll (owner request; the full-height
    // popup covered most of the panel). Six ≈ the sub-hour rungs plus a
    // couple of hours — the zone most picks live in — one flick from the
    // rest. (Respected because the app styles its comboboxes; a fully
    // native popup style may ignore this hint — the QA line checks it on
    // real Windows.)
    m_estimate->setMaxVisibleItems(6);
    m_estimate->addItem(tr("No estimate"), 0);
    for (int m : {15, 30, 45})
        m_estimate->addItem(minutesLabel(m), m);
    for (int m = 60; m <= 480; m += 30)
        m_estimate->addItem(minutesLabel(m), m);
    for (int m = 540; m <= 960; m += 60)
        m_estimate->addItem(minutesLabel(m), m);

    // Values OFF the ladder exist (the old spinbox took anything, quick
    // capture can parse "~25m") and must survive the control swap: a
    // picker that snapped would corrupt data just by OPENING the panel.
    // An absent value is inserted at its sorted spot, same label style —
    // the ladder simply grows one honest rung for this task.
    int seedIndex = m_estimate->findData(estimateMinutes);
    if (seedIndex < 0) {
        int at = m_estimate->count();
        for (int i = 1; i < m_estimate->count(); ++i)
            if (m_estimate->itemData(i).toInt() > estimateMinutes) {
                at = i;
                break;
            }
        m_estimate->insertItem(at, minutesLabel(estimateMinutes),
                               estimateMinutes);
        seedIndex = at;
    }
    m_estimate->setCurrentIndex(seedIndex);
    m_chunkable = new QCheckBox(tr("Fits short gaps"), this);
    m_chunkable->setToolTip(
        tr("Can you chip at this in a spare 15 minutes,\n"
           "or does it need a real run at it?"));
    m_chunkable->setChecked(chunkable);
    sizeRow->addWidget(m_estimate, 1);
    sizeRow->addWidget(m_chunkable);
    layout->addLayout(sizeRow);

    // ---- edited() wiring (v28.6) -------------------------------------------
    // Every answer-bearing widget reports through ONE signal, so the
    // panel's Save button has one thing to listen to. Deliberately coarse:
    // edited() means "worth re-asking isDirty()", not "is dirty" — the
    // compare stays in one place (isDirty), the notifications in another.
    connect(m_title, &QLineEdit::textChanged, this, &TaskDetailForm::edited);
    connect(m_notes, &QPlainTextEdit::textChanged,
            this, &TaskDetailForm::edited);
    connect(m_date, &QDateEdit::dateChanged, this, &TaskDetailForm::edited);
    connect(m_noDate, &QCheckBox::toggled, this, &TaskDetailForm::edited);
    connect(m_time, &QTimeEdit::timeChanged, this, &TaskDetailForm::edited);
    connect(m_allDay, &QCheckBox::toggled, this, &TaskDetailForm::edited);
    connect(m_repeat, &QComboBox::currentIndexChanged,
            this, &TaskDetailForm::edited);
    connect(m_priority, &QComboBox::currentIndexChanged,
            this, &TaskDetailForm::edited);
    connect(m_estimate, &QComboBox::currentIndexChanged,
            this, &TaskDetailForm::edited);
    connect(m_chunkable, &QCheckBox::toggled, this, &TaskDetailForm::edited);

    // The construction state IS the saved truth until a container says
    // otherwise (seeding pieces re-marks; see seedPieces).
    markClean();
}

// ---- the breadcrumb (v28.5; policy moved out in v28.6) ---------------------

void TaskDetailForm::setBreadcrumb(const QString& parentId,
                                   const QString& parentTitle)
{
    // "‹ LOG410 FINAL" — a flat text button, styled like every clickable
    // title in the app, inserted ABOVE the fields at index 0. What a click
    // MEANS is no longer decided here: the form reports the wish and the
    // container (modal dialog vs docked panel) applies its own policy.
    auto* crumb = new QPushButton(
        QStringLiteral("\u2039 ") + parentTitle, this);
    crumb->setObjectName(QStringLiteral("pieceBreadcrumb"));
    crumb->setCursor(Qt::PointingHandCursor);
    crumb->setToolTip(tr("Back to the parent task"));
    crumb->setStyleSheet(QStringLiteral(
        "QPushButton { border:none; background:transparent; text-align:left; "
        "padding:0; color:#616974; font-size:12px; } "
        "QPushButton:hover { color:#2F7E6E; }"));
    connect(crumb, &QPushButton::clicked, this, [this, parentId]() {
        emit navigateRequested(parentId);
    });
    static_cast<QVBoxLayout*>(layout())->insertWidget(0, crumb);
    m_hasBreadcrumb = true;
}

// ---- pieces (v28.3, §I) ----------------------------------------------------

void TaskDetailForm::seedPieces(const QVector<Piece>& pieces)
{
    // Build the whole section here rather than in the constructor, so a
    // form that never gets pieces (a piece's own form) never shows the
    // machinery.
    auto* layout = static_cast<QVBoxLayout*>(this->layout());

    auto* section = new QVBoxLayout;
    section->setSpacing(4);
    section->addSpacing(4);
    section->addWidget(caption(tr("PIECES"), this));

    m_piecesLayout = new QVBoxLayout;
    m_piecesLayout->setSpacing(2);
    section->addLayout(m_piecesLayout);

    // The add-row: type, Enter, it becomes a line. A QLineEdit and not a
    // button-plus-popup, because capture must stay cheap — the quick-add
    // lesson (§K.1), applied to a checklist.
    m_newPiece = new QLineEdit(this);
    m_newPiece->setPlaceholderText(tr("Add a piece — e.g. \"read the spec\""));
    connect(m_newPiece, &QLineEdit::returnPressed, this,
            [this]() { commitNewPiece(); });
    connect(m_newPiece, &QLineEdit::textChanged,
            this, &TaskDetailForm::edited); // pending text is an answer too
    section->addWidget(m_newPiece);

    layout->addLayout(section);

    for (const Piece& piece : pieces)
        appendPieceRow(piece);

    // The seeded checklist joins the saved truth — without this, a form
    // with pieces would open already "dirty".
    markClean();
}

void TaskDetailForm::appendPieceRow(const Piece& piece)
{
    const int rowIndex = m_pieceRows.size();

    // v28.5 split the row's two jobs into two targets, TickTick-style:
    // the CHECKBOX ticks the piece done (one click, the reward stays
    // cheap), the TITLE asks to open the piece's own panel.
    auto* rowWidget = new QWidget(this);
    auto* row       = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    auto* box = new QCheckBox(rowWidget);
    box->setChecked(piece.done);
    connect(box, &QCheckBox::toggled, this, [this, rowIndex](bool on) {
        m_pieceRows[rowIndex].piece.done = on;
        emit edited();
    });
    row->addWidget(box);

    if (!piece.id.isEmpty()) {
        // A REAL piece: its title is a door — but the form only ASKS. The
        // dialog answers accept-and-hop; the panel answers guarded swap.
        auto* open = new QPushButton(piece.title, rowWidget);
        open->setObjectName(QStringLiteral("pieceOpenButton"));
        open->setCursor(Qt::PointingHandCursor);
        open->setToolTip(tr("Open this piece — set its date and size"));
        open->setStyleSheet(QStringLiteral(
            "QPushButton { border:none; background:transparent; "
            "text-align:left; padding:2px 0; color:#2B2F36; } "
            "QPushButton:hover { color:#2F7E6E; }"));
        const QString pieceId = piece.id;
        connect(open, &QPushButton::clicked, this, [this, pieceId]() {
            emit navigateRequested(pieceId);
        });
        row->addWidget(open, 1);
    } else {
        // A NEWBORN (typed this sitting, id still empty): no door until it
        // exists. Navigation targets are ids, and this line has none yet.
        auto* label = new QLabel(piece.title, rowWidget);
        row->addWidget(label, 1);
    }

    // The scheduled-work chip: "Aug 8 · 45 min", muted, display-only.
    QStringList chipParts;
    if (piece.dueDate.isValid())
        chipParts << piece.dueDate.toString(QStringLiteral("MMM d"));
    if (piece.estimateMinutes > 0)
        chipParts << minutesLabel(piece.estimateMinutes); // "12h", not "720 min"
    if (!chipParts.isEmpty()) {
        auto* chip = new QLabel(chipParts.join(QStringLiteral(" \u00B7 ")),
                                rowWidget);
        chip->setObjectName(QStringLiteral("pieceChip"));
        chip->setStyleSheet(
            QStringLiteral("color:#8A93A0; font-size:11px;"));
        row->addWidget(chip);
    }

    // The ✕ records an ARCHIVE wish, never a delete — same vocabulary as
    // everywhere else in the app.
    auto* remove = new QToolButton(rowWidget);
    remove->setText(QStringLiteral("\u2715"));
    remove->setAutoRaise(true);
    remove->setToolTip(tr("Remove from this list (archives the piece)"));
    connect(remove, &QToolButton::clicked, this, [this, rowIndex, rowWidget]() {
        m_pieceRows[rowIndex].piece.archived = true;
        rowWidget->hide();
        emit edited();
    });
    row->addWidget(remove);

    m_piecesLayout->addWidget(rowWidget);
    m_pieceRows.append(PieceRow{piece, box, rowWidget});
}

void TaskDetailForm::commitNewPiece()
{
    const QString title = m_newPiece->text().trimmed();
    if (title.isEmpty())
        return;
    Piece piece;
    piece.title = title; // id stays empty: "born here"
    appendPieceRow(piece);
    m_newPiece->clear();
    emit edited();
}

// ---- answers ---------------------------------------------------------------

QString TaskDetailForm::chosenTitle() const { return m_title->text(); }

QString TaskDetailForm::chosenDescription() const
{
    return m_notes->toPlainText();
}

void TaskDetailForm::syncDeadlineEnabled()
{
    const bool hasDate = !m_noDate->isChecked();
    m_date->setEnabled(hasDate);
    m_time->setEnabled(hasDate && !m_allDay->isChecked());
    m_allDay->setEnabled(hasDate);
}

QDate TaskDetailForm::chosenDueDate() const
{
    return m_noDate->isChecked() ? QDate() : m_date->date();
}

QTime TaskDetailForm::chosenDueTime() const
{
    if (m_noDate->isChecked() || m_allDay->isChecked())
        return {};
    return m_time->time();
}

Task::Priority TaskDetailForm::chosenPriority() const
{
    return static_cast<Task::Priority>(m_priority->currentIndex());
}

Task::Repeat TaskDetailForm::chosenRepeat() const
{
    return static_cast<Task::Repeat>(m_repeat->currentIndex());
}

int TaskDetailForm::chosenEstimateMinutes() const
{
    return m_estimate->currentData().toInt(); // the minutes ride itemData
}

bool TaskDetailForm::chosenChunkable() const
{
    return m_chunkable->isChecked();
}

QVector<TaskDetailForm::Piece> TaskDetailForm::chosenPieces() const
{
    QVector<Piece> result;
    for (const PieceRow& row : m_pieceRows)
        result.append(row.piece);
    // Text sitting in the add-row counts: the user typed a line and
    // reached for Save instead of Enter — losing it would punish them for
    // skipping a keystroke they didn't know they owed.
    if (m_newPiece) {
        const QString pending = m_newPiece->text().trimmed();
        if (!pending.isEmpty()) {
            Piece piece;
            piece.title = pending;
            result.append(piece);
        }
    }
    return result;
}

void TaskDetailForm::selectTitleForNaming()
{
    m_title->setFocus();
    m_title->selectAll();
}

// ---- dirty tracking (v28.6) ------------------------------------------------

TaskDetailForm::Answers TaskDetailForm::currentAnswers() const
{
    Answers a;
    a.title       = chosenTitle();
    a.description = chosenDescription();
    a.date        = chosenDueDate();
    a.time        = chosenDueTime();
    a.repeat      = chosenRepeat();
    a.priority    = chosenPriority();
    a.estimate    = chosenEstimateMinutes();
    a.chunkable   = chosenChunkable();
    a.pieces      = chosenPieces();
    return a;
}

void TaskDetailForm::markClean()
{
    m_clean = currentAnswers();
}

bool TaskDetailForm::isDirty() const
{
    const Answers now = currentAnswers();
    if (now.title != m_clean.title || now.description != m_clean.description
        || now.date != m_clean.date || now.time != m_clean.time
        || now.repeat != m_clean.repeat || now.priority != m_clean.priority
        || now.estimate != m_clean.estimate
        || now.chunkable != m_clean.chunkable
        || now.pieces.size() != m_clean.pieces.size())
        return true;
    for (int i = 0; i < now.pieces.size(); ++i) {
        const Piece &a = now.pieces[i], &b = m_clean.pieces[i];
        if (a.id != b.id || a.title != b.title || a.done != b.done
            || a.archived != b.archived)
            return true;
    }
    return false;
}
