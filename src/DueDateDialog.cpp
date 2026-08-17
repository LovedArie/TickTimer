#include "DueDateDialog.h"

#include <QCalendarWidget>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimeEdit>
#include <QVBoxLayout>

DueDateDialog::DueDateDialog(QDate initial, QTime initialTime, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Due date"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("When is this due?"), this);
    title->setObjectName("h2");

    m_calendar = new QCalendarWidget(this);
    m_calendar->setGridVisible(false);
    // A sensible starting point either way: the task's current date, or
    // today for a task that has none yet.
    m_calendar->setSelectedDate(initial.isValid() ? initial
                                                  : QDate::currentDate());
    // Double-clicking a date should just mean "that one, done" — the
    // activated signal fires on double-click/Enter, saving an OK click.
    connect(m_calendar, &QCalendarWidget::activated, this,
            [this](QDate) { accept(); });

    // ---- the time row (v22) -------------------------------------------------
    // Sits UNDER the calendar because it is a refinement of the answer above
    // it, not a peer question: you pick a day, then optionally sharpen it.
    // Reading order and dependency order agree, which is why nobody needs a
    // label explaining the relationship.
    auto* timeRow = new QHBoxLayout;
    timeRow->setSpacing(8);
    auto* timeCap = new QLabel(tr("Time"), this);
    m_time = new QTimeEdit(this);
    m_time->setDisplayFormat(QStringLiteral("HH:mm")); // one clock format app-wide
    // 23:59 is the default a DEADLINE wants: "by the end of that day" is what
    // people mean when they first reach for a time. Starting at 00:00 would
    // make every accidental tick mean "already late".
    m_time->setTime(initialTime.isValid() ? initialTime : QTime(23, 59));
    m_allDay = new QCheckBox(tr("All day"), this);
    m_allDay->setChecked(!initialTime.isValid());
    m_time->setEnabled(initialTime.isValid());
    connect(m_allDay, &QCheckBox::toggled, this,
            [this](bool allDay) { m_time->setEnabled(!allDay); });
    timeRow->addWidget(timeCap);
    timeRow->addWidget(m_time, 1);
    timeRow->addWidget(m_allDay);

    auto* buttons = new QHBoxLayout;
    auto* noDate = new QPushButton(tr("No due date"), this);
    noDate->setObjectName("quiet");
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* ok = new QPushButton(tr("OK"), this);
    ok->setObjectName("primary");
    buttons->addWidget(noDate);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);

    layout->addWidget(title);
    layout->addWidget(m_calendar);
    layout->addLayout(timeRow);
    layout->addLayout(buttons);

    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(noDate, &QPushButton::clicked, this, [this]() {
        m_cleared = true;
        accept(); // clearing is an ANSWER (back to TBD), not a cancel
    });
}

QDate DueDateDialog::chosenDate() const
{
    return m_cleared ? QDate() : m_calendar->selectedDate();
}

QTime DueDateDialog::chosenTime() const
{
    // Two ways to mean "all day": tick the box, or clear the date entirely.
    // Both collapse to an invalid QTime here so the caller never has to ask
    // the question twice.
    if (m_cleared || m_allDay->isChecked())
        return {};
    return m_time->time();
}
