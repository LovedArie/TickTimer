#include "EventDialog.h"

#include "Theme.h"
#include "Widgets.h"
#include "AppData.h"
#include "Stats.h"
#include "TrackerService.h"

#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QTimeEdit>
#include <QPainter>
#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTimer>
#include <functional>
#include <QLineEdit>
#include <QStandardItemModel>

#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// LabelEdit — the block-label box, grown from one line to a real multiline
// editor (owner request: "a bigger box so I can add more").
//
// Why a subclass exists at all: QCompleter plugs into a QLineEdit for free
// (setCompleter and done), but for text edits Qt makes YOU do the wiring —
// this follows Qt's documented "Custom Completer" example. Two overrides:
//
//   keyPressEvent — drive the popup from what's typed, and while the popup
//   is open, refuse to act on the keys that belong to it (Enter must pick
//   the suggestion, not insert a newline).
//
//   focusOutEvent — a multiline edit has NO editingFinished signal, because
//   Enter stopped meaning "done" the moment it started meaning "new line".
//   So "done" becomes "clicked away": commit on focus-out, via a callback
//   the dialog plugs in. (No Q_OBJECT / custom signal needed — the class
//   stays file-local and moc-free, same practice as StatBox.)
// ---------------------------------------------------------------------------
class LabelEdit : public QPlainTextEdit
{
public:
    using QPlainTextEdit::QPlainTextEdit;

    QCompleter* completer = nullptr;      // owned by the dialog
    std::function<void()> onCommit;       // "the user is done editing"

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (completer && completer->popup()->isVisible()) {
            // While the popup is open these keys are ITS interface —
            // ignore them here so Enter selects instead of newlining.
            switch (e->key()) {
            case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Escape:
            case Qt::Key_Tab:   case Qt::Key_Backtab:
                e->ignore();
                return;
            default:
                break;
            }
        }

        QPlainTextEdit::keyPressEvent(e);
        if (!completer)
            return;

        // simplified() collapses newlines/runs of spaces — a multiline
        // draft can still match a task title, which is stored single-line.
        const QString prefix = toPlainText().simplified();
        if (prefix.isEmpty()) {
            completer->popup()->hide();
            return;
        }
        completer->setCompletionPrefix(prefix);
        if (completer->completionCount() == 0) {
            completer->popup()->hide();
            return;
        }
        completer->popup()->setCurrentIndex(
            completer->completionModel()->index(0, 0));
        QRect cr = cursorRect();
        cr.setWidth(completer->popup()->sizeHintForColumn(0)
                    + completer->popup()->verticalScrollBar()
                          ->sizeHint().width());
        completer->complete(cr); // popup under the text cursor
    }

    void focusOutEvent(QFocusEvent* e) override
    {
        QPlainTextEdit::focusOutEvent(e);
        // Interacting with the completer's popup steals focus for a moment;
        // that is not "done editing" — don't commit mid-selection.
        if (e->reason() == Qt::PopupFocusReason
            || (completer && completer->popup()->isVisible()))
            return;
        if (onCommit)
            onCommit();
    }
};


EventDialog::EventDialog(AppData* data, TrackerService* tracker,
                         const QString& eventId, QWidget* parent)
    : QDialog(parent)
    , m_data(data)
    , m_tracker(tracker)
    , m_eventId(eventId)
{
    setWindowTitle(tr("Planned block"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    // Header: colour swatch + activity name.
    m_swatch = new QLabel(this);
    m_swatch->setFixedSize(15, 15);
    m_title = new QLabel(this);
    m_title->setObjectName("h2");
    auto* headRow = new QHBoxLayout;
    headRow->setSpacing(9);
    headRow->addWidget(m_swatch);
    headRow->addWidget(m_title, 1);

    m_plannedLine = new QLabel(this);
    m_plannedLine->setObjectName("sub");

    // Reschedule row — the four nudge buttons from the prototype. Each is
    // wired to the same slot with a different delta; the domain's isFree()
    // decides (via refresh) which are enabled.
    auto* moveRow = new QHBoxLayout;
    moveRow->setSpacing(6);
    auto* moveCaption = new QLabel(tr("Reschedule"), this);
    moveCaption->setObjectName("sub");
    const struct { const char* text; int delta; } moves[] = {
        {"\u25B2 1h", -2}, {"\u25B2 30m", -1},
        {"30m \u25BC", +1}, {"1h \u25BC", +2},
    };
    for (const auto& m : moves) {
        auto* b = new QPushButton(QString::fromUtf8(m.text), this);
        const int delta = m.delta;
        connect(b, &QPushButton::clicked, this,
                [this, delta]() { moveBySlots(delta); });
        m_moveButtons.append(b);
        moveRow->addWidget(b);
    }

    m_pva = new PvaBar(this);
    m_legend = new QLabel(this);
    m_legend->setObjectName("sub");

    m_stateLabel = new QLabel(this);

    m_focusBtn = new QPushButton(tr("Start focus"), this);
    m_focusBtn->setObjectName("primary");
    m_breakBtn = new QPushButton(tr("Take a break"), this);
    m_breakBtn->setObjectName("breakBtn");
    m_distractedBtn = new QPushButton(tr("Distracted"), this);
    m_distractedBtn->setObjectName("danger"); // lost time wears the danger hue
    m_stopBtn = new QPushButton(tr("Stop"), this);
    m_stopBtn->setObjectName("quiet");
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_focusBtn);
    btnRow->addWidget(m_breakBtn);
    btnRow->addWidget(m_distractedBtn);
    btnRow->addWidget(m_stopBtn);

    // The block label — one line, because it must FIT on a block. Contrast
    // with m_note below: note = private reflection (multi-line, never
    // painted), title = public face (one line, painted on the agenda).
    // Two fields because they are two different facts with two audiences.
    m_titleEdit = new LabelEdit(this);
    m_titleEdit->setPlaceholderText(
        tr("Label shown on the block — or type to find a task…"));
    // ~3 text lines, same visual weight as the note box below. Tab moves
    // focus instead of inserting \t — this is a form field, not a code
    // editor, and keyboard users need a way OUT of a multiline box.
    m_titleEdit->setFixedHeight(72);
    m_titleEdit->setTabChangesFocus(true);

    // The SAME field also finds your tasks (owner request): QCompleter is
    // Qt's suggest-while-typing machinery — hand it a model, it pops
    // filtered matches under the field. Picking a match LINKS the task
    // (a reference, not text); typed text that matches nothing stays a
    // plain label. The user's next gesture disambiguates, just like the
    // picker's one-field design.
    auto* taskModel = new QStandardItemModel(this);
    for (const Task& t : m_data->tasks()) {
        if (t.done)
            continue; // you don't link finished work
        auto* item = new QStandardItem(t.title);
        item->setData(t.id, Qt::UserRole); // the id rides inside the row
        taskModel->appendRow(item);
    }
    auto* completer = new QCompleter(taskModel, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains); // "lab" finds "Lab 4 — …"
    // setWidget, not QLineEdit::setCompleter — the manual half of the
    // custom-completer pattern; LabelEdit's keyPressEvent does the rest.
    completer->setWidget(m_titleEdit);
    m_titleEdit->completer = completer;
    // The QModelIndex overload, not the QString one: the index carries the
    // task ID in UserRole. Matching titles back to ids by string would break
    // the moment two tasks share a title.
    connect(completer, QOverload<const QModelIndex&>::of(&QCompleter::activated),
            this, &EventDialog::linkTaskFromCompleter);

    // Link status: visible only while a task is linked; Unlink undoes it.
    m_taskLine = new QLabel(this);
    m_taskLine->setObjectName("sub");
    m_unlinkBtn = new QPushButton(tr("Unlink"), this);
    m_unlinkBtn->setObjectName("quiet");
    m_unlinkBtn->setCursor(Qt::PointingHandCursor);
    auto* taskRow = new QHBoxLayout;
    taskRow->setContentsMargins(0, 0, 0, 0);
    taskRow->addWidget(m_taskLine, 1);
    taskRow->addWidget(m_unlinkBtn);

    m_note = new QPlainTextEdit(this);
    m_note->setPlaceholderText(
        tr("A quick note… (how did it go? did anxiety creep in?)"));
    m_note->setFixedHeight(64);

    // ---- tracked time — the honest-tracking editor (item 2) ----------------
    // The timers write facts; this section lets the HUMAN write them too.
    // Forgot to press focus? Add the 9:00–9:45 you really studied. Left the
    // timer running into lunch? Retract it and add the true one. The domain
    // treats a manual segment exactly like a timer's (appendSegment is the
    // same door) — a fact is a fact, whoever typed it. Stats, glance, and
    // compare all update instantly because they derive, never store.
    auto* segCaption = new QLabel(tr("TRACKED TIME"), this);
    segCaption->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    m_segList = new QVBoxLayout;
    m_segList->setSpacing(2);

    auto* addSegRow = new QHBoxLayout;
    m_segKind = new QComboBox(this);
    m_segKind->addItem(tr("Focus"));       // == SegmentKind::Focus (0)
    m_segKind->addItem(tr("Break"));       // == SegmentKind::Break (1)
    m_segKind->addItem(tr("Distracted"));  // == SegmentKind::Distracted (2)
    m_segStart = new QTimeEdit(this);
    m_segEnd   = new QTimeEdit(this);
    m_segStart->setDisplayFormat(QStringLiteral("HH:mm"));
    m_segEnd->setDisplayFormat(QStringLiteral("HH:mm"));
    auto* addSegBtn = new QPushButton(tr("Add"), this);
    addSegBtn->setCursor(Qt::PointingHandCursor);
    addSegRow->addWidget(m_segKind);
    addSegRow->addWidget(m_segStart);
    addSegRow->addWidget(new QLabel(QStringLiteral("→"), this));
    addSegRow->addWidget(m_segEnd);
    addSegRow->addWidget(addSegBtn);
    addSegRow->addStretch(1);

    auto* deleteBtn = new QPushButton(tr("Delete this block"), this);
    deleteBtn->setObjectName("danger");
    deleteBtn->setCursor(Qt::PointingHandCursor);

    layout->addLayout(headRow);
    layout->addWidget(m_plannedLine);
    layout->addWidget(m_titleEdit);
    layout->addLayout(taskRow);
    layout->addWidget(moveCaption);
    layout->addLayout(moveRow);
    layout->addWidget(m_pva);
    layout->addWidget(m_legend);
    layout->addWidget(m_stateLabel);
    layout->addLayout(btnRow);
    layout->addWidget(segCaption);
    layout->addLayout(m_segList);
    layout->addLayout(addSegRow);
    layout->addWidget(m_note);
    layout->addWidget(deleteBtn, 0, Qt::AlignLeft);

    // Wiring. Note the shape of every connection: UI event -> service call.
    // The dialog never edits its own labels in these slots — it lets the
    // services change reality, hears about it, and re-reads it in refresh().
    connect(m_focusBtn, &QPushButton::clicked, this,
            [this]() { m_tracker->startFocus(m_eventId); });
    connect(m_breakBtn, &QPushButton::clicked, this,
            [this]() { m_tracker->startBreak(m_eventId); });
    connect(m_distractedBtn, &QPushButton::clicked, this,
            [this]() { m_tracker->startDistracted(m_eventId); });
    connect(m_stopBtn, &QPushButton::clicked,
            m_tracker, &TrackerService::stop);
    connect(addSegBtn, &QPushButton::clicked, this, [this]() {
        const Event* e = m_data->eventById(m_eventId);
        if (!e)
            return;
        Segment seg;
        seg.kind  = static_cast<SegmentKind>(m_segKind->currentIndex());
        // The times are ON THE BLOCK'S DATE — you're correcting that day's
        // record, so the dialog never asks which day (it knows).
        seg.start = QDateTime(e->date, m_segStart->time());
        seg.end   = QDateTime(e->date, m_segEnd->time());
        // appendSegment refuses end <= start (zero/negative facts are
        // noise); the UI simply relays reality, it doesn't pre-argue.
        m_data->appendSegment(m_eventId, seg);
    });
    connect(m_note, &QPlainTextEdit::textChanged,
            this, &EventDialog::saveNote);
    // Commit on focus-out (LabelEdit's callback), NOT per keystroke: the
    // domain REFUSES an empty title on an ad-hoc block, and refusing every
    // keystroke of a rewrite-in-progress would fight the user mid-edit.
    // Multiline killed editingFinished — Enter means "new line" now — so
    // "done" is "clicked away"; refresh() restores the field if the domain
    // said no. Validate-on-commit, new trigger.
    m_titleEdit->onCommit = [this]() { saveTitle(); };
    connect(m_unlinkBtn, &QPushButton::clicked,
            this, &EventDialog::unlinkTask);
    connect(deleteBtn, &QPushButton::clicked,
            this, &EventDialog::deleteEvent);

    connect(m_tracker, &TrackerService::stateChanged,
            this, &EventDialog::refresh);
    // tick() only pulses WHILE tracking — an idle dialog sitting open on
    // a future block would never notice its window arriving (or a live
    // one expiring). A coarse 30-second timer re-runs refresh(); block
    // boundaries are minute-granular, so this is plenty, and refresh() is
    // cheap (labels and enable flags).
    auto* liveGate = new QTimer(this);
    liveGate->setInterval(30 * 1000);
    connect(liveGate, &QTimer::timeout, this, &EventDialog::refresh);
    liveGate->start();

    connect(m_tracker, &TrackerService::tick,
            this, &EventDialog::refresh); // live numbers every second
    connect(m_data, &AppData::changed,
            this, &EventDialog::refresh);

    // Seed the add-time row from the PLAN — the likeliest correction is
    // "I did what I planned, the timer just wasn't running."
    if (const Event* e0 = m_data->eventById(m_eventId)) {
        m_segStart->setTime(QTime(0, 0).addSecs(e0->plannedStartMinutes * 60));
        m_segEnd->setTime(QTime(0, 0).addSecs(e0->plannedEndMinutes * 60));
    }

    // Seed the note ONCE — refresh() never touches it again, or every
    // keystroke would trigger changed() -> refresh() -> setPlainText() and
    // fight the user for the cursor. Feedback loops are THE classic
    // signals-and-slots trap; the m_updatingUi guard is the standard cure.
    if (const Event* e = m_data->eventById(m_eventId)) {
        m_updatingUi = true;
        m_note->setPlainText(e->note);
        m_titleEdit->setPlainText(e->title);
        m_updatingUi = false;
    }

    refresh();
}

void EventDialog::refresh()
{
    const Event* e = m_data->eventById(m_eventId);
    if (!e) {         // deleted while we were open (or vanished on load)
        reject();
        return;
    }

    // Identity and colour come from the SAME resolvers the agenda paints
    // with — a task block or an ad-hoc block opens looking exactly like its
    // block, because there is only one resolution rule to agree with.
    const Category* c = m_data->categoryById(m_data->eventCategoryId(*e));
    const QColor color = c ? c->color : theme::inkSoft();

    m_swatch->setStyleSheet(
        QStringLiteral("background:%1; border-radius:5px;").arg(color.name()));
    m_title->setText(m_data->eventLabel(*e));

    // Keep the label field honest with reality — but never while it has
    // focus (the user is typing) and never re-entrantly (m_updatingUi).
    // This is how the field snaps back when the domain refuses an edit.
    if (!m_titleEdit->hasFocus() && m_titleEdit->toPlainText() != e->title) {
        // refresh() can run INSIDE a guarded save (changed() is a direct
        // connection) — so restore the flag's previous value instead of
        // forcing false, or we'd disarm the caller's guard mid-flight.
        const bool wasUpdating = m_updatingUi;
        m_updatingUi = true;
        m_titleEdit->setPlainText(e->title);
        m_updatingUi = wasUpdating;
    }

    // The linked-task row: shown only while a link exists. Unlink is
    // DISABLED when the task is the block's only identity — the UI makes
    // the illegal click unreachable; setEventTask would refuse it anyway.
    const Task* linked = m_data->taskById(e->taskId);
    m_taskLine->setVisible(linked != nullptr);
    m_unlinkBtn->setVisible(linked != nullptr);
    if (linked) {
        QString line = tr("Linked task: %1%2")
                           .arg(linked->done ? QStringLiteral("✓ ") : QString(),
                                linked->title);
        if (linked->dueDate.isValid())
            line += tr(" · due %1")
                        .arg(linked->dueDate.toString(QStringLiteral("MMM d")));
        m_taskLine->setText(line);
        m_unlinkBtn->setEnabled(!e->activityId.isEmpty() || !e->title.isEmpty());
    }

    const int slotCount = (e->plannedEndMinutes - e->plannedStartMinutes)
                      / plan::kSlotMinutes;
    m_plannedLine->setText(QStringLiteral("%1 · planned %2 – %3 (%4)")
                               .arg(c ? c->name : QString(),
                                    timeLabel(e->plannedStartMinutes),
                                    timeLabel(e->plannedEndMinutes),
                                    durationLabel(slotCount)));

    // Enable each nudge only if the landing spot is free — the UI makes
    // illegal moves unreachable; the domain makes them impossible.
    const int deltas[] = {-2, -1, +1, +2};
    for (int i = 0; i < m_moveButtons.size(); ++i) {
        const int newStart =
            e->plannedStartMinutes + deltas[i] * plan::kSlotMinutes;
        m_moveButtons[i]->setEnabled(m_data->isFree(
            e->date, newStart,
            newStart + slotCount * plan::kSlotMinutes, e->id));
    }

    stats::Totals t = stats::eventTotals(*e);
    const bool trackingThis = m_tracker->isTrackingEvent(m_eventId);
    if (trackingThis) {
        // The live, uncommitted second goes to the bucket matching the CURRENT
        // state — three-way, not an `else` that would dump distracted time into
        // break (the same trap eventTotals had).
        const qint64 live = m_tracker->liveSeconds();
        switch (m_tracker->state()) {
        case TrackerService::State::Focusing:   t.focusSeconds      += live; break;
        case TrackerService::State::OnBreak:    t.breakSeconds      += live; break;
        case TrackerService::State::Distracted: t.distractedSeconds += live; break;
        case TrackerService::State::Idle:       break; // not tracking; no add
        }
    }

    m_pva->setValues(t.focusSeconds, t.breakSeconds, t.distractedSeconds,
                     e->plannedSeconds());
    m_legend->setText(
        QStringLiteral("%1 focused · %2 on break · %3 distracted · %4 untracked")
            .arg(stats::formatSeconds(t.focusSeconds),
                 stats::formatSeconds(t.breakSeconds),
                 stats::formatSeconds(t.distractedSeconds),
                 stats::formatSeconds(
                     qMax<qint64>(0, e->plannedSeconds() - t.total()))));

    const bool live = m_tracker->canTrackNow(m_eventId);
    if (!trackingThis) {
        m_stateLabel->setObjectName("stateIdle");
        if (live) {
            m_stateLabel->setText(tr("Idle — not tracking"));
        } else {
            // Say WHY the buttons are grey — a disabled control with no
            // explanation reads as a bug. Future block: name the opening
            // time. Past block: say it closed.
            // THE TRACKER'S clock, not the wall clock: the verdict (live?)
            // and its explanation (future or past?) must come from the
            // SAME "now", or they can disagree — this line originally read
            // currentDateTime() and the gated-buttons test failed the
            // moment the real clock crossed the test block's 5 PM start:
            // injected clock said "future", wall clock said "passed".
            // Two clocks in one decision is a race; it fired within hours.
            const QDateTime now = m_tracker->nowProvider();
            const bool isFuture =
                e->date > now.date()
                || (e->date == now.date()
                    && now.time().hour() * 60 + now.time().minute()
                           < e->plannedStartMinutes);
            m_stateLabel->setText(
                isFuture ? tr("Not live yet — tracking opens at %1")
                               .arg(timeLabel(e->plannedStartMinutes))
                         : tr("This block has passed — tracking is closed"));
        }
    } else if (m_tracker->state() == TrackerService::State::Focusing) {
        m_stateLabel->setObjectName("stateFocusing");
        m_stateLabel->setText(tr("● Focusing"));
    } else if (m_tracker->state() == TrackerService::State::Distracted) {
        m_stateLabel->setObjectName("stateOnBreak"); // reuse the muted style
        m_stateLabel->setText(tr("● Distracted"));
    } else {
        m_stateLabel->setObjectName("stateOnBreak");
        m_stateLabel->setText(tr("● On a break"));
    }
    // Changing objectName after the fact needs a style nudge:
    m_stateLabel->style()->unpolish(m_stateLabel);
    m_stateLabel->style()->polish(m_stateLabel);

    // Braces to the domain's belt (§3.38): the start buttons exist only
    // while the block is LIVE — the illegal click made unreachable, while
    // TrackerService's guard would refuse it anyway. Stop is NEVER gated:
    // stopping writes the truth of what already happened.
    m_focusBtn->setEnabled(live && !(trackingThis
        && m_tracker->state() == TrackerService::State::Focusing));
    m_breakBtn->setEnabled(live && !(trackingThis
        && m_tracker->state() == TrackerService::State::OnBreak));
    m_distractedBtn->setEnabled(live && !(trackingThis
        && m_tracker->state() == TrackerService::State::Distracted));
    m_stopBtn->setEnabled(trackingThis);
    // Rebuild the tracked-time rows ONLY when the segment list actually
    // changed — refresh() also fires on every live-timer tick (once per
    // second), and rebuilding widgets at 1 Hz means flicker, a growing
    // deleteLater queue, and a ✕ button replaced under the cursor
    // mid-click. Size is a sufficient fingerprint here because segments
    // only ever get appended or removed, and each mutation emits its own
    // changed() → its own refresh().
    if (e->segments.size() != m_renderedSegments) {
        m_renderedSegments = e->segments.size();
        rebuildSegmentList();
    }
}

void EventDialog::moveBySlots(int deltaSlots)
{
    const Event* e = m_data->eventById(m_eventId);
    if (!e)
        return;
    m_data->moveEvent(m_eventId, e->plannedStartMinutes
                                     + deltaSlots * plan::kSlotMinutes);
    // No manual UI update: moveEvent emits changed(), changed() calls
    // refresh(). One path for updates, always.
}

void EventDialog::saveNote()
{
    if (m_updatingUi)
        return;
    m_updatingUi = true; // our own setEventNote fires changed()->refresh()
    m_data->setEventNote(m_eventId, m_note->toPlainText());
    m_updatingUi = false;
}

void EventDialog::saveTitle()
{
    if (m_updatingUi)
        return;
    m_updatingUi = true; // setEventTitle fires changed()->refresh()

    // The domain may refuse (emptying an ad-hoc block's only identity).
    // On refusal, changed() never fires and refresh() won't run — put the
    // stored value back ourselves. The field asked; the domain answered.
    if (!m_data->setEventTitle(m_eventId, m_titleEdit->toPlainText()))
        if (const Event* e = m_data->eventById(m_eventId))
            m_titleEdit->setPlainText(e->title);

    m_updatingUi = false;
}

void EventDialog::linkTaskFromCompleter(const QModelIndex& index)
{
    const QString taskId = index.data(Qt::UserRole).toString();
    if (taskId.isEmpty())
        return;

    m_updatingUi = true;
    // Link the task and NOTHING else. This used to also clear the label
    // ("let the task show on the block") — which silently destroyed any
    // comments the user had written. The two are independent facts now:
    // the task is the structured "what", the label is the free-text "and
    // also", and the block paints both. The text sitting in the box was
    // your SEARCH QUERY, not a comment — so the box snaps back to the
    // stored label (uncommitted typing is discarded, committed comments
    // survive; the popup guard in focusOutEvent is why the search typing
    // never got committed in the first place).
    m_data->setEventTask(m_eventId, taskId);
    if (const Event* e = m_data->eventById(m_eventId))
        m_titleEdit->setPlainText(e->title);
    m_updatingUi = false;
    refresh();
}

void EventDialog::unlinkTask()
{
    // The button is disabled when the domain would refuse (task-only block),
    // but the call still goes through the guarded door — belt and braces.
    m_data->setEventTask(m_eventId, QString());
}

void EventDialog::deleteEvent()
{
    // Order matters: stop (and commit) the live interval BEFORE the event
    // disappears, or the segment would have nowhere to land.
    if (m_tracker->isTrackingEvent(m_eventId))
        m_tracker->stop();
    m_data->removeEvent(m_eventId);
    // refresh() sees the event is gone and reject()s the dialog.
}

// ---- PvaBar ------------------------------------------------------------------

PvaBar::PvaBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(20);
}

void PvaBar::setValues(qint64 focusSecs, qint64 breakSecs, qint64 distractedSecs,
                       qint64 plannedSecs)
{
    m_focus      = focusSecs;
    m_break      = breakSecs;
    m_distracted = distractedSecs;
    m_planned    = qMax<qint64>(1, plannedSecs);
    update();
}

void PvaBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    p.setBrush(theme::track());
    p.drawRoundedRect(rect(), 7, 7);

    const int fw = int(qint64(width()) * qMin(m_focus, m_planned) / m_planned);
    qint64 room = m_planned - qMin(m_focus, m_planned);
    const int bw = int(qint64(width()) * qMin(m_break, room) / m_planned);
    room -= qMin(m_break, room);
    const int dw = int(qint64(width()) * qMin(m_distracted, room) / m_planned);

    if (fw > 0) {
        p.setBrush(theme::focus());
        p.drawRoundedRect(QRect(0, 0, fw, height()), 7, 7);
    }
    if (bw > 0) {
        p.setBrush(theme::brk());
        p.drawRect(QRect(fw, 0, bw, height()));
    }
    if (dw > 0) {
        // Distraction fills after focus and break, in the danger hue: it's
        // real time that ate into the plan, shown honestly as lost.
        p.setBrush(theme::danger());
        p.drawRect(QRect(fw + bw, 0, dw, height()));
    }
}

void EventDialog::rebuildSegmentList()
{
    // Empty-and-refill, the UpcomingPage idiom: rows are cheap, bookkeeping
    // isn't. deleteLater because the × that triggered this rebuild is one
    // of the widgets being discarded (the J-section rule, once more).
    while (QLayoutItem* item = m_segList->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const Event* e = m_data->eventById(m_eventId);
    if (!e)
        return;

    if (e->segments.isEmpty()) {
        auto* none = new QLabel(
            tr("Nothing tracked yet — the timers write here, and so can "
               "you (forgot to press focus? add the time below)."),
            this);
        none->setStyleSheet("color:#8A9098; font-size:11px;");
        none->setWordWrap(true);
        m_segList->addWidget(none);
        return;
    }

    const QString kindNames[] = {tr("Focus"), tr("Break"), tr("Distracted")};
    for (int i = 0; i < e->segments.size(); ++i) {
        const Segment& seg = e->segments[i];
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);

        auto* text = new QLabel(
            QStringLiteral("%1  %2 → %3   (%4)")
                .arg(kindNames[int(seg.kind)],
                     seg.start.time().toString(QStringLiteral("HH:mm")),
                     seg.end.time().toString(QStringLiteral("HH:mm")),
                     stats::formatSeconds(seg.seconds())),
            row);
        text->setStyleSheet("font-size:12px; color:#4A505A;");

        auto* x = new QPushButton(QStringLiteral("\u00D7"), row);
        x->setObjectName("danger");
        x->setFixedWidth(22);
        x->setCursor(Qt::PointingHandCursor);
        x->setToolTip(tr("This never happened — remove it"));
        const int index = i;
        connect(x, &QPushButton::clicked, this, [this, index]() {
            m_data->removeSegment(m_eventId, index);
        });

        h->addWidget(text, 1);
        h->addWidget(x);
        m_segList->addWidget(row);
    }
}
