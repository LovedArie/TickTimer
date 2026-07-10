#include "DueDateDialog.h"

#include <QCalendarWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

DueDateDialog::DueDateDialog(QDate initial, QWidget* parent)
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
