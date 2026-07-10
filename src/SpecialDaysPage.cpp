#include "SpecialDaysPage.h"

#include "AppData.h"
#include "Theme.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

SpecialDaysPage::SpecialDaysPage(AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 22);

    m_scroll = new QScrollArea(this);
    makeTouchScrollable(m_scroll); // finger-flick on touch screens
    m_scroll->setWidgetResizable(true);
    layout->addWidget(m_scroll);

    connect(m_data, &AppData::changed, this, &SpecialDaysPage::rebuild);
    rebuild();
}

void SpecialDaysPage::rebuild()
{
    // deleteLater, NOT delete — the fix for a real crash (see test_ui.cpp).
    // This rebuild runs INSIDE changed(), a direct connection — meaning the
    // widget being discarded may be the very one whose signal handler is
    // still on the call stack (the add-task input's returnPressed, a
    // TaskRow checkbox, a delete button...). A plain `delete` frees it
    // mid-signal and control unwinds into freed memory. deleteLater defers
    // destruction until the event loop is back in charge and nobody is
    // executing inside the widget — Qt's documented cure, and the same one
    // the due strip already used. One pattern, now everywhere.
    if (QWidget* old = m_scroll->takeWidget())
        old->deleteLater();
    m_scroll->setWidget(buildContent());
}

QWidget* SpecialDaysPage::buildContent()
{
    auto* panel = new QFrame;
    panel->setObjectName("panel");
    panel->setMaximumWidth(720);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("Special days"), panel);
    title->setObjectName("h2");
    auto* sub = new QLabel(
        tr("Birthdays, holidays, vacations — days worth looking forward "
           "to, sorted by whichever comes next."),
        panel);
    sub->setObjectName("sub");
    sub->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(sub);

    // ---- add form -----------------------------------------------------------
    auto* addRow = new QHBoxLayout;
    addRow->setSpacing(8);
    auto* nameInput = new QLineEdit(panel);
    nameInput->setPlaceholderText(tr("+ Add a special day…"));
    auto* dateInput = new QDateEdit(QDate::currentDate(), panel);
    dateInput->setCalendarPopup(true); // click opens a calendar — one line
    dateInput->setDisplayFormat("MMM d, yyyy");
    auto* yearly = new QCheckBox(tr("Repeats yearly"), panel);
    auto* addBtn = new QPushButton(tr("Add"), panel);
    addBtn->setObjectName("primary");
    const auto addDay = [this, nameInput, dateInput, yearly]() {
        m_data->addSpecialDay(nameInput->text(), dateInput->date(),
                              yearly->isChecked());
    };
    connect(addBtn, &QPushButton::clicked, this, addDay);
    connect(nameInput, &QLineEdit::returnPressed, this, addDay);
    addRow->addWidget(nameInput, 1);
    addRow->addWidget(dateInput);
    addRow->addWidget(yearly);
    addRow->addWidget(addBtn);
    layout->addLayout(addRow);

    // ---- the list, soonest first ----------------------------------------------
    const QDate today = QDate::currentDate();
    const auto days = m_data->specialDaysSorted(today);

    if (days.isEmpty()) {
        auto* empty = new QLabel(
            tr("Nothing here yet. Add a birthday or a holiday above — "
               "future you will enjoy the countdown."),
            panel);
        empty->setObjectName("encourage");
        empty->setWordWrap(true);
        layout->addSpacing(6);
        layout->addWidget(empty);
    }

    // Each special day is a CARD now, not a thin row: a coloured accent bar,
    // a larger title, and the countdown as the headline on the right. The
    // visual weight was the whole request — a birthday shouldn't read like a
    // list item. This is pure PRESENTATION: not one line of domain code
    // changed, only how the same SpecialDay data is drawn.
    for (const SpecialDay* day : days) {
        const QDate  next = day->nextOccurrence(today);
        const qint64 in   = today.daysTo(next);

        // Accent + countdown colour by urgency: today is the focus green,
        // the next week warms up, everything further out stays calm.
        const QString accent = (in == 0) ? theme::focus().name()
                             : (in <= 7)  ? QStringLiteral("#D9873B")
                                          : QStringLiteral("#B7BEC6");

        auto* card = new QFrame(panel);
        card->setObjectName("dayCard");
        card->setStyleSheet(QStringLiteral(
            "#dayCard { background:#FFFFFF; border:1px solid #E6E9E4; "
            "border-left:4px solid %1; border-radius:12px; }").arg(accent));
        auto* cardRow = new QHBoxLayout(card);
        cardRow->setContentsMargins(16, 14, 14, 14);
        cardRow->setSpacing(12);

        auto* dot = new QLabel(card);
        dot->setPixmap(colorDot(QColor(accent), 14)); // bigger than the old 9

        // Title + date, stacked: the name is the headline, the date its quiet
        // subtitle underneath.
        auto* textCol = new QVBoxLayout;
        textCol->setSpacing(2);
        auto* name = new QLabel(day->title, card);
        name->setStyleSheet("font-size:16px; font-weight:700; color:#2B2F36;");
        auto* dateLine = new QLabel(
            day->repeatsYearly
                ? tr("%1 · repeats yearly").arg(next.toString("MMMM d"))
                : next.toString("dddd, MMMM d, yyyy"),
            card);
        dateLine->setStyleSheet("font-size:12px; color:#7A828C;");
        textCol->addWidget(name);
        textCol->addWidget(dateLine);

        // The countdown is the headline on the right — big, coloured, and it
        // still says "Today!"/"Tomorrow" for the days that deserve words.
        auto* countdown = new QLabel(card);
        if (in == 0)
            countdown->setText(tr("Today!"));
        else if (in == 1)
            countdown->setText(tr("Tomorrow"));
        else
            countdown->setText(tr("in <b>%1</b> days").arg(in));
        countdown->setTextFormat(Qt::RichText);
        countdown->setStyleSheet(
            QStringLiteral("font-size:%1px; font-weight:800; color:%2;")
                .arg(in <= 1 ? 18 : 15)
                .arg(accent));

        auto* x = new QPushButton(QStringLiteral("\u00D7"), card);
        x->setObjectName("danger");
        x->setFixedSize(26, 26);
        x->setCursor(Qt::PointingHandCursor);
        const QString dayId = day->id;
        connect(x, &QPushButton::clicked, this,
                [this, dayId]() { m_data->removeSpecialDay(dayId); });

        cardRow->addWidget(dot);
        cardRow->addLayout(textCol, 1);
        cardRow->addWidget(countdown);
        cardRow->addWidget(x);

        layout->addSpacing(4);
        layout->addWidget(card);
    }

    layout->addStretch(1);
    return wrapLeft(panel);
}
