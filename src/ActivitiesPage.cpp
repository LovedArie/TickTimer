#include "ActivitiesPage.h"

#include "AppData.h"
#include "TaskRow.h"
#include "Theme.h"
#include "Widgets.h"

#include <QColorDialog>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>

// The role keys now live in ActivitiesPage.h (namespace acts) so CategoryTree
// and this file share one definition. Using acts::kCategoryIdRole below.

// ---- CategoryTree ----------------------------------------------------------

CategoryTree::CategoryTree(QWidget* parent)
    : QTreeWidget(parent)
{
    // The four switches that turn an ordinary tree into a drag-drop surface:
    setDragEnabled(true);        // items with ItemIsDragEnabled can be picked up
    setAcceptDrops(true);        // the tree will receive drops
    setDropIndicatorShown(true); // the blue line showing where a drop lands
    // DragDrop (not InternalMove): InternalMove would let Qt reparent items
    // ITSELF, which for us would allow nonsense like a category nested under a
    // category, or a folder inside a folder — states our domain forbids. By
    // handling the drop ourselves and rebuilding from AppData, the domain's
    // rules stay the only rules.
    setDragDropMode(QAbstractItemView::DragDrop);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void CategoryTree::dropEvent(QDropEvent* event)
{
    // What is being dragged? For a single-selection internal drag, the
    // dragged item is the current one. We only ever move CATEGORIES, so a
    // missing category id means "not our gesture" — ignore and bail.
    QTreeWidgetItem* dragged = currentItem();
    if (!dragged) {
        event->ignore();
        return;
    }
    const QString catId = dragged->data(0, acts::kCategoryIdRole).toString();
    if (catId.isEmpty()) {
        event->ignore(); // a folder was dragged — folders don't move into things
        return;
    }

    // Where did it land? Resolve the drop position to a target FOLDER id.
    QString folderId; // "" == top level, the default if we drop into space
    if (QTreeWidgetItem* target = itemAt(event->position().toPoint())) {
        const QString targetFolder = target->data(0, acts::kFolderIdRole).toString();
        if (!targetFolder.isEmpty()) {
            folderId = targetFolder; // dropped directly ON a folder
        } else if (QTreeWidgetItem* parent = target->parent()) {
            // Dropped ON a category that sits inside a folder: adopt that
            // folder. (A top-level category has no parent -> stays "".)
            folderId = parent->data(0, acts::kFolderIdRole).toString();
        }
    }

    // Accept the gesture but DO NOT call QTreeWidget::dropEvent — we move the
    // data ourselves via the signal, then rebuild. Letting the base class run
    // would reparent the item visually and fight the rebuild.
    event->acceptProposedAction();
    emit categoryDropped(catId, folderId);
}

// ---- ActivitiesPage --------------------------------------------------------

ActivitiesPage::ActivitiesPage(AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 22);
    layout->setSpacing(18);

    // ---- the master: category rail, now a tree -----------------------------
    auto* railPanel = new QFrame(this);
    railPanel->setObjectName("panel");
    railPanel->setFixedWidth(isCompactScreen() ? 168 : 250);
    auto* railLayout = new QVBoxLayout(railPanel);
    railLayout->setContentsMargins(12, 12, 12, 12);
    railLayout->setSpacing(8);

    auto* railHead = new QHBoxLayout;
    auto* railTitle = new QLabel(tr("Life areas"), railPanel);
    railTitle->setObjectName("h2");
    auto* addFolderBtn = new QPushButton(tr("+ Folder"), railPanel);
    addFolderBtn->setObjectName("quiet");
    addFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(addFolderBtn, &QPushButton::clicked, this, [this]() {
        // QInputDialog: the one-question dialog Qt ships, for when a whole
        // custom dialog class would be ceremony.
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New folder"), tr("Folder name:"),
            QLineEdit::Normal, QString(), &ok);
        if (ok)
            m_data->addFolder(name);
    });
    railHead->addWidget(railTitle);
    railHead->addStretch(1);
    railHead->addWidget(addFolderBtn);

    auto* railHint = new QLabel(
        tr("Drag a life area onto a folder, or right-click it, to organise."),
        railPanel);
    railHint->setObjectName("sub");
    railHint->setWordWrap(true);

    m_rail = new CategoryTree(railPanel);
    m_rail->setObjectName("railTree");
    m_rail->setHeaderHidden(true);
    m_rail->setIndentation(16);
    m_rail->setCursor(Qt::PointingHandCursor);
    m_rail->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_rail, &QTreeWidget::itemClicked,
            this, &ActivitiesPage::onRailItemClicked);
    connect(m_rail, &QTreeWidget::customContextMenuRequested,
            this, &ActivitiesPage::onRailContextMenu);
    // The drag-drop payoff: the tree reports the gesture, the DOMAIN performs
    // the move. setCategoryFolder already enforces every rule (one level deep,
    // valid ids); its changed() triggers the rebuild that redraws the tree.
    // The page is a thin translator between a UI event and a use-case step.
    connect(m_rail, &CategoryTree::categoryDropped, this,
            [this](const QString& categoryId, const QString& folderId) {
                m_data->setCategoryFolder(categoryId, folderId);
            });
    // Collapse state is PRESENTATION: tracked per session, never saved.
    // The m_rebuilding guard matters — rebuildRail() re-expands items
    // programmatically, and without the guard those programmatic events
    // would overwrite what the user actually chose.
    connect(m_rail, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem* item) {
                const QString id = item->data(0, acts::kFolderIdRole).toString();
                if (!m_rebuilding && !id.isEmpty())
                    m_collapsedFolders.insert(id);
            });
    connect(m_rail, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem* item) {
                const QString id = item->data(0, acts::kFolderIdRole).toString();
                if (!m_rebuilding && !id.isEmpty())
                    m_collapsedFolders.remove(id);
            });

    auto* newCatName = new QLineEdit(railPanel);
    newCatName->setPlaceholderText(tr("New life area…"));
    auto* colorBtn = new QPushButton(railPanel);
    colorBtn->setFixedSize(34, 32);
    colorBtn->setCursor(Qt::PointingHandCursor);
    const auto paintSwatch = [colorBtn](const QColor& c) {
        colorBtn->setStyleSheet(
            QStringLiteral("background:%1; border:1px solid #E2E6E0; "
                           "border-radius:8px;").arg(c.name()));
    };
    paintSwatch(m_newCategoryColor);
    connect(colorBtn, &QPushButton::clicked, this, [this, paintSwatch]() {
        const QColor picked = QColorDialog::getColor(
            m_newCategoryColor, this, tr("Life area colour"));
        if (picked.isValid()) {
            m_newCategoryColor = picked;
            paintSwatch(picked);
        }
    });
    auto* addCatBtn = new QPushButton(tr("Add"), railPanel);
    addCatBtn->setObjectName("primary");
    const auto addCategory = [this, newCatName]() {
        const QString id =
            m_data->addCategory(newCatName->text(), m_newCategoryColor);
        if (!id.isEmpty())
            m_selectedCategoryId = id; // jump to what you just created
    };
    connect(addCatBtn, &QPushButton::clicked, this, addCategory);
    connect(newCatName, &QLineEdit::returnPressed, this, addCategory);

    auto* addRow = new QHBoxLayout;
    addRow->setSpacing(6);
    addRow->addWidget(newCatName, 1);
    addRow->addWidget(colorBtn);
    addRow->addWidget(addCatBtn);

    railLayout->addLayout(railHead);
    railLayout->addWidget(railHint);
    railLayout->addWidget(m_rail, 1);
    railLayout->addLayout(addRow);

    // ---- the detail pane -------------------------------------------------------
    auto* detailPanel = new QFrame(this);
    detailPanel->setObjectName("panel");
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(6, 6, 6, 6);
    m_detail = new QScrollArea(detailPanel);
    makeTouchScrollable(m_detail); // finger-flick on touch screens
    m_detail->setWidgetResizable(true);
    detailLayout->addWidget(m_detail);

    layout->addWidget(railPanel);
    layout->addWidget(detailPanel, 1);

    connect(m_data, &AppData::changed, this, &ActivitiesPage::rebuild);
    rebuild();
}

void ActivitiesPage::rebuild()
{
    if (!m_data->categoryById(m_selectedCategoryId)) {
        m_selectedCategoryId = m_data->categories().isEmpty()
                                   ? QString()
                                   : m_data->categories().first().id;
    }
    rebuildRail();
    rebuildDetail();
}

void ActivitiesPage::rebuildRail()
{
    m_rebuilding = true; // programmatic expand/collapse follows — see ctor
    m_rail->clear();
    QTreeWidgetItem* toSelect = nullptr;

    const auto addCategoryItem = [&](QTreeWidgetItem* parentItem,
                                     const Category& c) {
        const int n =
            m_data->activityCountIn(c.id) + m_data->taskCountIn(c.id);
        auto* item = parentItem
                         ? new QTreeWidgetItem(parentItem)
                         : new QTreeWidgetItem(m_rail);
        item->setText(0, n > 0
                             ? QStringLiteral("%1  ·  %2").arg(c.name).arg(n)
                             : c.name);
        item->setIcon(0, QIcon(colorDot(c.color, 10)));
        item->setData(0, acts::kCategoryIdRole, c.id);
        // A category is the thing you DRAG, and also a valid drop target
        // (dropping onto a category means "same folder as this one"). It
        // stays selectable — the detail pane follows the selection.
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                       | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        if (c.id == m_selectedCategoryId)
            toSelect = item;
    };

    for (const Folder& folder : m_data->folders()) {
        auto* folderItem = new QTreeWidgetItem(m_rail);
        const int inside = m_data->categoryCountInFolder(folder.id);
        folderItem->setText(0, inside > 0
                                   ? QStringLiteral("%1  ·  %2")
                                         .arg(folder.name).arg(inside)
                                   : folder.name);
        folderItem->setData(0, acts::kFolderIdRole, folder.id);
        // Structure, not a choice: a folder can expand and RECEIVE drops, but
        // is never selectable (the detail pane always shows a category) and
        // never draggable (you don't move a folder into things — no nesting).
        folderItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
        QFont bold = m_rail->font();
        bold.setBold(true);
        folderItem->setFont(0, bold);

        for (const Category& c : m_data->categories())
            if (c.folderId == folder.id)
                addCategoryItem(folderItem, c);

        folderItem->setExpanded(!m_collapsedFolders.contains(folder.id));
    }

    for (const Category& c : m_data->categories())
        if (c.folderId.isEmpty())
            addCategoryItem(nullptr, c);

    if (toSelect)
        m_rail->setCurrentItem(toSelect);
    m_rebuilding = false;
}

void ActivitiesPage::onRailItemClicked(QTreeWidgetItem* item, int)
{
    const QString categoryId = item->data(0, acts::kCategoryIdRole).toString();
    if (categoryId.isEmpty()) {
        // A folder: single click toggles it — friendlier than Qt's
        // default double-click-or-tiny-arrow.
        item->setExpanded(!item->isExpanded());
        return;
    }
    if (categoryId == m_selectedCategoryId)
        return;
    m_selectedCategoryId = categoryId;
    rebuildDetail(); // the data didn't change — only the view did
}

void ActivitiesPage::onRailContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_rail->itemAt(pos);
    if (!item)
        return;
    const QString categoryId = item->data(0, acts::kCategoryIdRole).toString();
    const QString folderId   = item->data(0, acts::kFolderIdRole).toString();

    QMenu menu(this);

    if (!categoryId.isEmpty()) {
        QMenu* move = menu.addMenu(tr("Move to folder"));
        for (const Folder& folder : m_data->folders()) {
            QAction* action = move->addAction(folder.name);
            const QString targetId = folder.id;
            connect(action, &QAction::triggered, this,
                    [this, categoryId, targetId]() {
                        m_data->setCategoryFolder(categoryId, targetId);
                    });
        }
        move->addSeparator();
        QAction* none = move->addAction(tr("No folder (top level)"));
        connect(none, &QAction::triggered, this, [this, categoryId]() {
            m_data->setCategoryFolder(categoryId, QString());
        });
        if (m_data->folders().isEmpty())
            move->setEnabled(false); // nowhere to move to yet
    } else if (!folderId.isEmpty()) {
        QAction* rename = menu.addAction(tr("Rename…"));
        connect(rename, &QAction::triggered, this, [this, folderId]() {
            bool ok = false;
            const Folder* folder = m_data->folderById(folderId);
            const QString name = QInputDialog::getText(
                this, tr("Rename folder"), tr("Folder name:"),
                QLineEdit::Normal, folder ? folder->name : QString(), &ok);
            if (ok)
                m_data->renameFolder(folderId, name);
        });
        QAction* remove = menu.addAction(tr("Delete folder"));
        // UI mirrors the rule; AppData enforces it — as always.
        remove->setEnabled(m_data->categoryCountInFolder(folderId) == 0);
        connect(remove, &QAction::triggered, this, [this, folderId]() {
            m_data->removeFolder(folderId);
        });
    }

    if (!menu.isEmpty())
        menu.exec(m_rail->viewport()->mapToGlobal(pos));
}

void ActivitiesPage::rebuildDetail()
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
    if (QWidget* old = m_detail->takeWidget())
        old->deleteLater();
    m_detail->setWidget(buildDetailContent());
}

QWidget* ActivitiesPage::buildDetailContent()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    const Category* category = m_data->categoryById(m_selectedCategoryId);
    if (!category) {
        auto* empty = new QLabel(
            tr("Add a life area on the left to get started."), content);
        empty->setObjectName("sub");
        layout->addWidget(empty);
        layout->addStretch(1);
        return content;
    }

    // -- header: dot, name, and Delete only when COMPLETELY empty ----------
    auto* head = new QHBoxLayout;
    auto* dot = new QLabel(content);
    dot->setPixmap(colorDot(category->color, 12));
    auto* name = new QLabel(category->name, content);
    name->setObjectName("h2");
    head->addWidget(dot);
    head->addWidget(name);
    head->addStretch(1);
    const bool holdsNothing = m_data->activityCountIn(category->id) == 0
                              && m_data->taskCountIn(category->id) == 0;
    if (holdsNothing) {
        auto* del = new QPushButton(tr("Delete"), content);
        del->setObjectName("danger");
        del->setCursor(Qt::PointingHandCursor);
        const QString categoryId = category->id;
        connect(del, &QPushButton::clicked, this, [this, categoryId]() {
            m_data->removeCategory(categoryId);
        });
        head->addWidget(del);
    }
    layout->addLayout(head);

    const QString categoryId = category->id;

    // ---- TASKS: add at the top, rows via the shared TaskRow ----------------
    auto* tasksTitle = new QLabel(tr("TASKS"), content);
    tasksTitle->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    layout->addSpacing(6);
    layout->addWidget(tasksTitle);

    auto* addTaskRow = new QHBoxLayout;
    auto* taskInput = new QLineEdit(content);
    taskInput->setPlaceholderText(tr("+ Add a task…"));
    auto* addTaskBtn = new QPushButton(tr("Add"), content);
    addTaskBtn->setObjectName("primary");
    const auto addTask = [this, categoryId, taskInput]() {
        m_data->addTask(taskInput->text(), categoryId); // born as "TBD"
    };
    connect(addTaskBtn, &QPushButton::clicked, this, addTask);
    connect(taskInput, &QLineEdit::returnPressed, this, addTask);
    addTaskRow->addWidget(taskInput, 1);
    addTaskRow->addWidget(addTaskBtn);
    layout->addLayout(addTaskRow);

    for (const Task* task : m_data->tasksIn(categoryId)) {
        if (task->archived)
            continue; // archived tasks live on the Archive page now (item 4)
        layout->addWidget(new TaskRow(m_data, *task,
                                      /*showCategoryDot=*/false, content));
    }

    // ---- ACTIVITIES: the reusable types — no checkbox, ever (§3.9) ---------
    auto* actsTitle = new QLabel(tr("ACTIVITIES"), content);
    actsTitle->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    layout->addSpacing(10);
    layout->addWidget(actsTitle);

    auto* addActRow = new QHBoxLayout;
    auto* actInput = new QLineEdit(content);
    actInput->setPlaceholderText(tr("+ Add an activity…"));
    auto* addActBtn = new QPushButton(tr("Add"), content);
    addActBtn->setObjectName("primary");
    const auto addActivity = [this, categoryId, actInput]() {
        m_data->addActivity(actInput->text(), categoryId);
    };
    connect(addActBtn, &QPushButton::clicked, this, addActivity);
    connect(actInput, &QLineEdit::returnPressed, this, addActivity);
    addActRow->addWidget(actInput, 1);
    addActRow->addWidget(addActBtn);
    layout->addLayout(addActRow);

    for (const Activity& a : m_data->activities()) {
        if (a.categoryId != categoryId || a.archived)
            continue; // archived activities: Archive page only (item 3)
        auto* row = new QHBoxLayout;
        row->setSpacing(9);
        auto* aDot = new QLabel(content);
        aDot->setPixmap(colorDot(category->color, 9));
        auto* aName = new QLabel(a.name, content);
        row->addWidget(aDot);
        row->addWidget(aName, 1);

        const int used = m_data->eventCountUsing(a.id);
        if (used > 0) {
            auto* tag = new QLabel(tr("in use (%1)").arg(used), content);
            tag->setStyleSheet("color:#616974; font-size:11px;");
            row->addWidget(tag);
            // In-use = undeletable (history would dangle), which used to
            // mean UNRETIRABLE. Archive is the missing exit (item 3):
            // gone from every list and picker, history intact, reversible.
            auto* arch = new QPushButton(tr("Archive"), content);
            arch->setCursor(Qt::PointingHandCursor);
            arch->setStyleSheet(
                "background:#EEF0ED; border:none; border-radius:8px; "
                "padding:4px 9px; color:#616974; font-weight:600;");
            const QString activityId = a.id;
            connect(arch, &QPushButton::clicked, this, [this, activityId]() {
                m_data->setActivityArchived(activityId, true);
            });
            row->addWidget(arch);
        } else {
            auto* x = new QPushButton(QStringLiteral("\u00D7"), content);
            x->setObjectName("danger");
            x->setFixedWidth(24);
            x->setCursor(Qt::PointingHandCursor);
            const QString activityId = a.id;
            connect(x, &QPushButton::clicked, this, [this, activityId]() {
                m_data->removeActivity(activityId);
            });
            row->addWidget(x);
        }
        layout->addLayout(row);
    }

    layout->addStretch(1);
    return content;
}
