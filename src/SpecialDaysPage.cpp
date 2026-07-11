#include "SpecialDaysPage.h"

#include "AppData.h"
#include "Theme.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
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

        // Accent colour: the OWNER'S choice wins when one was made (item 8);
        // otherwise the old urgency colouring stands — today green, this
        // week warm, later calm. A valid QColor IS the "choice was made"
        // state, the same absence-as-default trick as the TBD due date.
        const QString accent =
            day->color.isValid() ? day->color.name()
            : (in == 0)          ? theme::focus().name()
            : (in <= 7)          ? QStringLiteral("#D9873B")
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

        // Edit (item 8): name, date, yearly, colour — one dialog, one
        // updateSpecialDay. Snapshot by value first: `day` points into the
        // vector, and the edit's rebuild moves the ground under it.
        auto* edit = new QPushButton(tr("Edit"), card);
        edit->setCursor(Qt::PointingHandCursor);
        edit->setStyleSheet(
            "background:#EEF0ED; border:none; border-radius:8px; "
            "padding:5px 10px; color:#616974; font-weight:600;");
        const SpecialDay snapshot = *day;
        connect(edit, &QPushButton::clicked, this, [this, snapshot]() {
            editDay(snapshot);
        });

        cardRow->addWidget(dot);
        cardRow->addLayout(textCol, 1);
        cardRow->addWidget(countdown);
        cardRow->addWidget(edit);
        cardRow->addWidget(x);

        layout->addSpacing(4);
        layout->addWidget(card);
    }

    layout->addStretch(1);
    return wrapLeft(panel);
}


void SpecialDaysPage::editDay(const SpecialDay& snapshot)
{
    // A pure-question dialog, the house contract: gather answers, mutate
    // nothing; ONE updateSpecialDay on accept. Built inline because this
    // page is its only caller (no speculative dialog class).
    QDialog dialog(window()); // window-parented: the save rebuilds this page
    dialog.setWindowTitle(tr("Edit special day"));
    auto* form = new QVBoxLayout(&dialog);
    form->setSpacing(8);

    auto* name = new QLineEdit(snapshot.title, &dialog);
    auto* date = new QDateEdit(snapshot.date, &dialog);
    date->setCalendarPopup(true);
    auto* yearly = new QCheckBox(tr("repeats yearly"), &dialog);
    yearly->setChecked(snapshot.repeatsYearly);

    // The colour: a swatch button opening QColorDialog, plus "automatic"
    // to hand colouring back to the urgency rules (invalid QColor).
    QColor chosen = snapshot.color;
    auto* colorBtn = new QPushButton(&dialog);
    const auto paintSwatch = [&]() {
        colorBtn->setText(chosen.isValid() ? tr("Colour: %1").arg(chosen.name())
                                           : tr("Colour: automatic"));
        colorBtn->setStyleSheet(
            chosen.isValid()
                ? QStringLiteral("background:%1; color:white; border:none; "
                                 "border-radius:8px; padding:6px 10px; "
                                 "font-weight:600;").arg(chosen.name())
                : QStringLiteral("background:#EEF0ED; border:none; "
                                 "border-radius:8px; padding:6px 10px; "
                                 "color:#616974; font-weight:600;"));
    };
    paintSwatch();
    connect(colorBtn, &QPushButton::clicked, &dialog, [&]() {
        const QColor picked =
            QColorDialog::getColor(chosen.isValid() ? chosen : QColor("#534AB7"),
                                   &dialog, tr("Pick a colour"));
        if (picked.isValid()) { chosen = picked; paintSwatch(); }
    });
    auto* autoBtn = new QPushButton(tr("Back to automatic"), &dialog);
    autoBtn->setFlat(true);
    autoBtn->setCursor(Qt::PointingHandCursor);
    connect(autoBtn, &QPushButton::clicked, &dialog, [&]() {
        chosen = QColor(); // invalid = "no choice" = urgency colours again
        paintSwatch();
    });

    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"), &dialog);
    auto* save   = new QPushButton(tr("Save"), &dialog);
    save->setObjectName("primary");
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save,   &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);

    form->addWidget(name);
    form->addWidget(date);
    form->addWidget(yearly);
    form->addWidget(colorBtn);
    form->addWidget(autoBtn, 0, Qt::AlignLeft);
    form->addLayout(buttons);

    if (dialog.exec() == QDialog::Accepted)
        m_data->updateSpecialDay(snapshot.id, name->text(), date->date(),
                                 yearly->isChecked(), chosen);
}
