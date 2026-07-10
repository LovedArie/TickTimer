#include "TaskDetailDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
// A tiny section caption, reused for each field — the uppercase grey label
// idiom already used across the app's panels.
QLabel* caption(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    return l;
}
} // namespace

TaskDetailDialog::TaskDetailDialog(const QString& title,
                                   const QString& description, QDate dueDate,
                                   Task::Repeat repeat, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Task details"));
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
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

    // ---- due date ----------------------------------------------------------
    layout->addSpacing(4);
    layout->addWidget(caption(tr("DUE DATE"), this));
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
    // The checkbox owns the date field's enabled state: ticking "no date"
    // greys the calendar, because a disabled control says "this answer is
    // off" far more clearly than a value you must remember to ignore.
    m_date->setEnabled(dueDate.isValid());
    connect(m_noDate, &QCheckBox::toggled, this,
            [this](bool noDate) { m_date->setEnabled(!noDate); });
    dateRow->addWidget(m_date, 1);
    dateRow->addWidget(m_noDate);
    layout->addLayout(dateRow);

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

    // ---- buttons -----------------------------------------------------------
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

    m_title->setFocus(); // land the cursor where editing usually starts
}

QString TaskDetailDialog::chosenTitle() const
{
    return m_title->text();
}

QString TaskDetailDialog::chosenDescription() const
{
    return m_notes->toPlainText();
}

QDate TaskDetailDialog::chosenDueDate() const
{
    // The checkbox is the source of truth for "has a date at all".
    return m_noDate->isChecked() ? QDate() : m_date->date();
}

Task::Repeat TaskDetailDialog::chosenRepeat() const
{
    return static_cast<Task::Repeat>(m_repeat->currentIndex());
}
