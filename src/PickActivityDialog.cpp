#include "PickActivityDialog.h"

#include "Theme.h"
#include "Widgets.h"
#include "AppData.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

PickActivityDialog::PickActivityDialog(const AppData* data, QDate date,
                                       int slotIndex, QWidget* parent)
    : QDialog(parent)
    , m_data(data)
    , m_date(date)
    , m_slotIndex(slotIndex)
{
    setWindowTitle(tr("Plan a block"));
    // Same reasoning as QuickCaptureOverlay's input: 400 is a good desktop
    // shape and an impossible one on a 360px phone, where it pushed the
    // duration chips and the "What exactly?" field off the right edge.
    setMinimumWidth(isCompactScreen() ? 0 : 400);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);

    // A title ROW, so the close affordance has somewhere to live. Android
    // gives a frameless dialog no title bar and no ✕, and Esc does not exist
    // on a phone — without this the only way out was the Back gesture, which
    // is not discoverable and which the owner reported being trapped by.
    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel(
        tr("What are you doing at %1?").arg(timeLabel(chosenStartMinutes())),
        this);
    title->setObjectName("h2");
    auto* sub = new QLabel(
        tr("Pick how long, then choose an activity or a task — "
           "or just type what you're doing."), this);
    sub->setObjectName("sub");
    // An unwrapped QLabel's minimum width IS its text width, and this sentence
    // is ~330px of it. Wrapping is right on every screen; it only became
    // VISIBLE as a bug on a phone, where it pushed the whole dialog off the
    // right edge.
    sub->setWordWrap(true);

    // The duration pills are one per free slot, so the row is as wide as the
    // day is empty — up to eight or more of them. That is a width this dialog
    // cannot promise on a phone, and shortening the list would remove real
    // choices. A scroll area SEVERS the promise instead: the row keeps every
    // pill and simply scrolls when there is no room, which on a touchscreen is
    // a gesture people already have.
    auto* pillRow = new QWidget(this);
    buildDurationPills(pillRow);
    auto* pillHost = new QScrollArea(this);
    pillHost->setWidget(pillRow);
    pillHost->setWidgetResizable(true);
    pillHost->setFrameShape(QFrame::NoFrame);
    pillHost->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pillHost->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // setWidget() turns on the child's autoFillBackground behind your back —
    // the mechanism that once painted the agenda black (Theme.h).
    pillRow->setAutoFillBackground(false);
    // A scroll area's sizeHint is a cached guess; pin the height to the row it
    // carries so the dialog does not grow a tall empty band.
    pillHost->setFixedHeight(pillRow->sizeHint().height());
    makeTouchScrollable(pillHost);

    // ONE text field, two jobs — deliberately (block-labels addendum):
    //   type + click an activity  ->  activity block WITH that label
    //   type + press Enter        ->  spontaneous ad-hoc block
    // Two separate fields ("label" and "or something else?") would ask the
    // user to classify their own intent before typing; one field lets them
    // just say what they're doing and decide with the NEXT gesture.
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(
        tr("What exactly? (shows on the block — or press Enter to plan it as-is)"));
    m_titleEdit->setClearButtonEnabled(true);
    connect(m_titleEdit, &QLineEdit::returnPressed,
            this, &PickActivityDialog::onTitleReturnPressed);

    m_list = new QListWidget(this);
    // Draggable by thumb, and no bar: the gesture IS the affordance on a
    // touchscreen, and the bar was the only way to scroll this list before.
    makeTouchScrollable(m_list);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    // Tall enough to show a dozen-ish rows before scrolling — a short list
    // was half the reason user-added categories at the bottom went unseen.
    m_list->setMinimumHeight(300);
    buildChoiceList();
    connect(m_list, &QListWidget::itemClicked,
            this, &PickActivityDialog::onItemClicked);

    auto* closeBtn = new QPushButton(QString(QChar(0x00D7)), this);
    closeBtn->setObjectName(QStringLiteral("pickClose"));
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(tr("Close"));
    // U+00D7, not the prettier U+2715 — that one renders as an empty box on
    // Android, which is how SlidePanel's ✕ was found.
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleRow->addWidget(title, 1);
    titleRow->addWidget(closeBtn, 0, Qt::AlignTop);
    layout->addLayout(titleRow);
    layout->addWidget(sub);
    layout->addWidget(pillHost);
    layout->addWidget(m_titleEdit);
    layout->addWidget(m_list, /*stretch=*/1);
}

QString PickActivityDialog::enteredTitle() const
{
    return m_titleEdit->text().trimmed();
}

void PickActivityDialog::onTitleReturnPressed()
{
    // Enter with an empty field is a no-op, not an error dialog: the field's
    // placeholder already explains itself, and nothing was lost.
    if (enteredTitle().isEmpty())
        return;
    m_kind = Kind::AdHoc;
    accept();
}

int PickActivityDialog::chosenStartMinutes() const
{
    return plan::kDayStartMinutes + m_slotIndex * plan::kSlotMinutes;
}

int PickActivityDialog::chosenEndMinutes() const
{
    return chosenStartMinutes() + m_durationSlots * plan::kSlotMinutes;
}

int PickActivityDialog::maxFreeSlots() const
{
    // Walk downward from the clicked slot while it stays free — that run,
    // capped at 2 h, is how long the new block may be. The check delegates
    // to AppData::isFree: the overlap rule exists in ONE place only.
    int run = 0;
    for (int i = m_slotIndex;
         i < plan::kSlotsPerDay && run < plan::kMaxSlotsPerEvent; ++i) {
        const int start = plan::kDayStartMinutes + i * plan::kSlotMinutes;
        if (!m_data->isFree(m_date, start, start + plan::kSlotMinutes))
            break;
        ++run;
    }
    return qMax(1, run);
}

void PickActivityDialog::buildDurationPills(QWidget* host)
{
    auto* row = new QHBoxLayout(host);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    const int cap = maxFreeSlots();
    m_durationSlots = qMin(m_durationSlots, cap);

    for (int slotCount = 1; slotCount <= cap; ++slotCount) {
        auto* pill = new QPushButton(durationLabel(slotCount), host);
        pill->setObjectName("segment");
        pill->setCheckable(true);
        pill->setChecked(slotCount == m_durationSlots);
        pill->setCursor(Qt::PointingHandCursor);

        // Lambda capturing `slotCount` BY VALUE: each button remembers its own
        // number. Capture by reference here would be a classic bug — all
        // pills would share the loop variable's final value.
        connect(pill, &QPushButton::clicked, this, [this, slotCount]() {
            m_durationSlots = slotCount;
            for (int i = 0; i < m_pills.size(); ++i)
                m_pills[i]->setChecked(i + 1 == slotCount);
        });
        m_pills.append(pill);
        row->addWidget(pill);
    }
}

void PickActivityDialog::buildChoiceList()
{
    // Show activities AND open tasks grouped by category, in the SAME order
    // as the Activities panel's rail: categories inside folders first
    // (folder by folder), then top-level categories.
    //
    // WHY the order matters (this was a real bug): the picker used to walk
    // categories in raw creation order. The seeded defaults were created
    // first, so any category YOU added later — especially one tucked in a
    // folder — sorted to the very bottom, below the dialog's visible area.
    // It looked "missing" though it was there. Mirroring the rail keeps the
    // two views consistent and surfaces your own categories where you expect
    // them. One ordering rule, shared by both screens.
    const auto addCategory = [this](const Category& c) {
        bool headerAdded = false;
        const auto ensureHeader = [&]() {
            if (headerAdded)
                return;
            auto* header = new QListWidgetItem(c.name.toUpper(), m_list);
            header->setFlags(Qt::NoItemFlags); // not selectable/clickable
            header->setFont(scaledFont(m_list->font(), -2, /*bold=*/true));
            header->setForeground(theme::inkSoft());
            headerAdded = true;
        };

        for (const Activity& a : m_data->activities()) {
            if (a.archived)
                continue; // retired types never appear in the picker (item 3)
            if (a.categoryId != c.id)
                continue;
            ensureHeader();
            auto* item = new QListWidgetItem(QIcon(colorDot(c.color)), a.name,
                                             m_list);
            // The id rides along INSIDE the item (Qt::UserRole) — no parallel
            // array to keep in sync with the visible list. A SECOND role says
            // what KIND of row this is; two ids from two id spaces must never
            // be told apart by string-sniffing.
            item->setData(Qt::UserRole, a.id);
            item->setData(Qt::UserRole + 1, QStringLiteral("activity"));
        }

        // Open tasks of this life area, after its activities — the picker
        // mirrors what the rail already teaches: activities and tasks live
        // side by side under a category. tasksIn() is already sorted by
        // urgency; done tasks are skipped (you don't PLAN finished work —
        // and the domain wouldn't stop you, but the picker shouldn't offer it).
        for (const Task* t : m_data->tasksIn(c.id)) {
            if (t->done)
                continue;
            ensureHeader();
            QString text = QStringLiteral("☐ ") + t->title;
            if (t->dueDate.isValid())
                text += QStringLiteral("  ·  due %1")
                            .arg(t->dueDate.toString(QStringLiteral("MMM d")));
            auto* item = new QListWidgetItem(QIcon(colorDot(c.color)), text,
                                             m_list);
            item->setData(Qt::UserRole, t->id);
            item->setData(Qt::UserRole + 1, QStringLiteral("task"));
        }
    };

    // Folders first, each folder's categories — exactly the rail's order.
    for (const Folder& f : m_data->folders())
        for (const Category& c : m_data->categories())
            if (c.folderId == f.id && !c.archived)
                addCategory(c); // retired life areas: not plannable
    // Then the top-level (folder-less) categories.
    for (const Category& c : m_data->categories())
        if (c.folderId.isEmpty() && !c.archived)
            addCategory(c);

    if (m_list->count() == 0) {
        auto* empty = new QListWidgetItem(
            tr("No activities yet — add some in the Activities tab first."),
            m_list);
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(theme::inkSoft());
    }
}

void PickActivityDialog::onItemClicked(QListWidgetItem* item)
{
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return; // a header or the empty-state row

    if (item->data(Qt::UserRole + 1).toString() == QLatin1String("task")) {
        m_kind   = Kind::Task;
        m_taskId = id;
    } else {
        m_kind       = Kind::Activity;
        m_activityId = id;
    }
    accept(); // one click chooses AND confirms — prototype behaviour
}
