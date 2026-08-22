#include "ActivitiesPage.h"

#include "AppData.h"
#include "CategoryTaskDelegate.h"
#include "CategoryTaskModel.h"
#include "DueDateDialog.h"
#include "QuickAddParser.h"
#include "QuickAddPreview.h"
#include "TaskDetailDialog.h"
#include "Theme.h"
#include "ResponsiveWatcher.h"
#include "Widgets.h"

#include <QAbstractItemView>
#include <QColorDialog>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

// deleteLater every widget in a layout (and recurse into nested layouts), then
// leave the layout itself empty and reusable. deferred, NOT immediate: these
// clears run inside changed(), which can fire from a header/activity button's
// own click — freeing that button synchronously would unwind into freed memory
// (the crash the old rebuildDetail documented). The TASK input escapes this
// entirely now: it's persistent and never cleared.
namespace
{
void clearLayout(QLayout* layout)
{
    if (!layout)
        return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        if (QLayout* child = item->layout())
            clearLayout(child);
        delete item;
    }
}
} // namespace

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
    railPanel->setFixedWidth(250); // narrowed by applyLayoutMode() when tight
    m_railPanel = railPanel;
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
    buildDetailPane(); // the persistent skeleton — built ONCE, never rebuilt

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
    refreshDetail();
}

// ---- responding to the container's size class --------------------------------

bool ActivitiesPage::event(QEvent* e)
{
    if (e->type() == ResponsiveModeEvent::type())
        applyLayoutMode(static_cast<ResponsiveModeEvent*>(e)->mode());

    return QWidget::event(e);
}

void ActivitiesPage::applyLayoutMode(responsive::Mode mode)
{
    // Narrowing the rail buys the detail pane 82px. Worth having, and NOT a
    // fix: this page is a permanent two-column split, and two columns on a
    // 384px screen is the wrong shape no matter how the width is divided. The
    // master/detail rework belongs to the phone-navigation stage; the honest
    // thing to record here is that this line is a mitigation.
    m_railPanel->setFixedWidth(mode == responsive::Mode::Compact ? 168 : 250);
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
            if (c.folderId == folder.id && !c.archived)
                addCategoryItem(folderItem, c);

        folderItem->setExpanded(!m_collapsedFolders.contains(folder.id));
    }

    for (const Category& c : m_data->categories())
        if (c.folderId.isEmpty() && !c.archived)
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
    refreshDetail(); // selection changed, not the data — just re-point the model
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

void ActivitiesPage::buildDetailPane()
{
    m_detailStack = new QStackedWidget;

    // ---- page 0: nothing selected -----------------------------------------
    auto* emptyWrap = new QWidget;
    auto* emptyLayout = new QVBoxLayout(emptyWrap);
    emptyLayout->setContentsMargins(14, 12, 14, 12);
    auto* empty = new QLabel(tr("Add a life area on the left to get started."),
                             emptyWrap);
    empty->setObjectName("sub");
    emptyLayout->addWidget(empty);
    emptyLayout->addStretch(1);
    m_detailStack->addWidget(emptyWrap);

    // ---- page 1: the persistent content -----------------------------------
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    // header (dot, name, delete/archive) — refilled in place by refreshHeader()
    m_headerHost = new QWidget(content);
    auto* headerLayout = new QHBoxLayout(m_headerHost);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_headerHost);

    // ---- TASKS: persistent input + a model/view list ----------------------
    auto* tasksTitle = new QLabel(tr("TASKS"), content);
    tasksTitle->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    layout->addSpacing(6);
    layout->addWidget(tasksTitle);

    auto* addTaskRow = new QHBoxLayout;
    m_taskInput = new QLineEdit(content);
    // The placeholder teaches the grammar by example — cheaper than a manual.
    m_taskInput->setPlaceholderText(tr("+ Add a task…  (\"lab 4 friday urgent weekly #school\")"));
    auto* addTaskBtn = new QPushButton(tr("Add"), content);
    addTaskBtn->setObjectName("primary");
    // The input is PERSISTENT, so its handler reads the CURRENT selection at
    // click time (not a captured category) — and, crucially, adding a task can
    // no longer destroy the very widget whose returnPressed is mid-flight.
    //
    // v21: the text goes through nlp::parseQuickAdd first. One line in, a
    // fully-dressed task out: title + due date + priority + repeat, and a
    // '#tag' can even re-route the task to another life area. The parse is the
    // same pure function the live preview below runs, so what you SAW is what
    // gets committed — no second interpretation at Enter time.
    const auto addTask = [this]() {
        if (m_selectedCategoryId.isEmpty())
            return;
        const nlp::ParsedTask p =
            nlp::parseQuickAdd(m_taskInput->text(), QDate::currentDate());
        if (p.title.isEmpty())
            return; // all facets, no words — nothing to name the task
        const QString catId = resolveCategoryHint(p.categoryHint);
        const QString id = m_data->addTask(p.title, catId, p.dueDate, p.dueTime);
        // addTask covers title/category/date; priority and repeat ride in via
        // the same updateTask the detail dialog uses. Two changed() signals,
        // but the second is a single-row dataChanged under the granular model
        // — the v20.3 diff makes the extra hop visually free.
        if (!id.isEmpty()
            && (p.priority != Task::Priority::Medium
                || p.repeat != Task::Repeat::None)) {
            m_data->updateTask(id, p.title, QString(), p.dueDate, p.dueTime, p.repeat,
                               p.priority);
        }
        m_taskInput->clear(); // also hides the preview via textChanged
    };
    connect(addTaskBtn, &QPushButton::clicked, this, addTask);
    connect(m_taskInput, &QLineEdit::returnPressed, this, addTask);
    addTaskRow->addWidget(m_taskInput, 1);
    addTaskRow->addWidget(addTaskBtn);
    layout->addLayout(addTaskRow);

    // The live preview: natural-language input is only trustworthy when you
    // can SEE what it understood before you commit. Re-parses on every
    // keystroke (the parser is microseconds) and shows the exact commit.
    m_quickAddPreview = new QLabel(content);
    m_quickAddPreview->setTextFormat(Qt::RichText);
    m_quickAddPreview->setStyleSheet("font-size:11px; padding-left:2px;");
    m_quickAddPreview->hide(); // empty input -> no preview, no reserved space
    connect(m_taskInput, &QLineEdit::textChanged,
            this, &ActivitiesPage::updateQuickAddPreview);
    layout->addWidget(m_quickAddPreview);

    m_taskModel = new CategoryTaskModel(m_data, this);
    m_taskDelegate = new CategoryTaskDelegate(this);
    // The delegate reports intent; the page performs the domain call — the same
    // thin-shell contract TaskRow had, now for a painted row.
    connect(m_taskDelegate, &CategoryTaskDelegate::doneToggled, this,
            [this](const QString& id, bool on) { m_data->setTaskDone(id, on); });
    connect(m_taskDelegate, &CategoryTaskDelegate::deleteRequested, this,
            [this](const QString& id) { m_data->removeTask(id); });
    connect(m_taskDelegate, &CategoryTaskDelegate::archiveRequested, this,
            [this](const QString& id) { m_data->setTaskArchived(id, true); });
    connect(m_taskDelegate, &CategoryTaskDelegate::editRequested,
            this, &ActivitiesPage::editTask);
    connect(m_taskDelegate, &CategoryTaskDelegate::dueDateRequested,
            this, &ActivitiesPage::chooseDueDate);

    m_taskView = new QListView(content);
    m_taskView->setModel(m_taskModel);
    m_taskView->setItemDelegate(m_taskDelegate);
    m_taskView->setFrameShape(QFrame::NoFrame);
    m_taskView->setSelectionMode(QAbstractItemView::NoSelection);
    // v28.7 — right-click a task: the TickTick door for pieces. The menu
    // is built per-click from the row under the cursor; "Add a piece" is
    // offered only for parents (one level — the domain guard in
    // addSubtask is the real wall, this is just honest chrome).
    m_taskView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_taskView, &QListView::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const QModelIndex index = m_taskView->indexAt(pos);
                if (!index.isValid())
                    return;
                const QString id =
                    index.data(cattask::IdRole).toString();
                if (index.data(cattask::IsPieceRole).toBool())
                    return; // a piece has no pieces — no menu to offer yet
                QMenu menu(m_taskView);
                QAction* addPiece =
                    menu.addAction(tr("Add a piece"));
                connect(addPiece, &QAction::triggered, this,
                        [this, id]() { startPieceUnder(id); });
                menu.exec(m_taskView->viewport()->mapToGlobal(pos));
            });
    m_taskView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_taskView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_taskView->viewport()->setAutoFillBackground(false);
    // The list is sized to its FULL content and never scrolls itself — the
    // detail pane's own QScrollArea owns scrolling. (A QListView wants to own
    // scrolling; when it's one section of a larger scroll page you either box it
    // at a fixed viewport or, as here, let it report its full height.)
    // The list's height follows its row COUNT, which changes on reset (a
    // category swap) AND on the granular insert/remove signals the model now
    // emits (v20.3). dataChanged doesn't change the count, so it isn't wired.
    connect(m_taskModel, &QAbstractItemModel::modelReset,
            this, &ActivitiesPage::updateTaskViewHeight);
    connect(m_taskModel, &QAbstractItemModel::rowsInserted,
            this, &ActivitiesPage::updateTaskViewHeight);
    connect(m_taskModel, &QAbstractItemModel::rowsRemoved,
            this, &ActivitiesPage::updateTaskViewHeight);
    layout->addWidget(m_taskView);

    // ---- ACTIVITIES: persistent input + rows refilled in place ------------
    auto* actsTitle = new QLabel(tr("ACTIVITIES"), content);
    actsTitle->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    layout->addSpacing(10);
    layout->addWidget(actsTitle);

    auto* addActRow = new QHBoxLayout;
    m_actInput = new QLineEdit(content);
    m_actInput->setPlaceholderText(tr("+ Add an activity…"));
    auto* addActBtn = new QPushButton(tr("Add"), content);
    addActBtn->setObjectName("primary");
    const auto addActivity = [this]() {
        if (m_selectedCategoryId.isEmpty())
            return;
        m_data->addActivity(m_actInput->text(), m_selectedCategoryId);
        m_actInput->clear();
    };
    connect(addActBtn, &QPushButton::clicked, this, addActivity);
    connect(m_actInput, &QLineEdit::returnPressed, this, addActivity);
    addActRow->addWidget(m_actInput, 1);
    addActRow->addWidget(addActBtn);
    layout->addLayout(addActRow);

    m_actHost = new QWidget(content);
    auto* actHostLayout = new QVBoxLayout(m_actHost);
    actHostLayout->setContentsMargins(0, 0, 0, 0);
    actHostLayout->setSpacing(8);
    layout->addWidget(m_actHost);

    layout->addStretch(1);
    m_detailStack->addWidget(content);

    m_detail->setWidget(m_detailStack);
}

void ActivitiesPage::refreshDetail()
{
    const Category* category = m_data->categoryById(m_selectedCategoryId);
    if (!category) {
        m_taskModel->setCategoryId(QString()); // clear the list
        m_detailStack->setCurrentIndex(0);     // show the empty message
        return;
    }
    m_detailStack->setCurrentIndex(1);
    refreshHeader();
    m_taskModel->setCategoryId(category->id); // re-point the model (no-op if same)
    updateTaskViewHeight();
    refreshActivities();
}

void ActivitiesPage::refreshHeader()
{
    clearLayout(m_headerHost->layout());
    const Category* category = m_data->categoryById(m_selectedCategoryId);
    if (!category)
        return;

    auto* hl = static_cast<QHBoxLayout*>(m_headerHost->layout());
    auto* dot = new QLabel(m_headerHost);
    dot->setPixmap(colorDot(category->color, 12));
    auto* name = new QLabel(category->name, m_headerHost);
    name->setObjectName("h2");
    hl->addWidget(dot);
    hl->addWidget(name);
    hl->addStretch(1);

    const QString categoryId = category->id;
    const bool holdsNothing = m_data->activityCountIn(categoryId) == 0
                              && m_data->taskCountIn(categoryId) == 0;
    if (holdsNothing) {
        // Delete only when COMPLETELY empty (same rule as before). This button
        // rebuilds via deleteLater (clearLayout), so clicking it can't free
        // itself mid-signal.
        auto* del = new QPushButton(tr("Delete"), m_headerHost);
        del->setObjectName("danger");
        del->setCursor(Qt::PointingHandCursor);
        connect(del, &QPushButton::clicked, this,
                [this, categoryId]() { m_data->removeCategory(categoryId); });
        hl->addWidget(del);
    } else {
        // A life area with content can't be deleted, but it can archive whole.
        auto* arch = new QPushButton(tr("Archive area"), m_headerHost);
        arch->setCursor(Qt::PointingHandCursor);
        arch->setStyleSheet(
            "background:#EEF0ED; border:none; border-radius:8px; "
            "padding:5px 10px; color:#616974; font-weight:600;");
        connect(arch, &QPushButton::clicked, this, [this, categoryId]() {
            m_data->setCategoryArchived(categoryId, true);
        });
        hl->addWidget(arch);
    }
}

void ActivitiesPage::refreshActivities()
{
    clearLayout(m_actHost->layout());
    const Category* category = m_data->categoryById(m_selectedCategoryId);
    if (!category)
        return;
    auto* host = static_cast<QVBoxLayout*>(m_actHost->layout());
    const QString categoryId = category->id;

    // The reusable types — no checkbox, ever. Still widget-built rows (a
    // different shape than tasks, and not the lesson here), just refilled in
    // place instead of via a wholesale panel rebuild.
    for (const Activity& a : m_data->activities()) {
        if (a.categoryId != categoryId || a.archived)
            continue; // archived activities: Archive page only
        auto* row = new QHBoxLayout;
        row->setSpacing(9);
        auto* aDot = new QLabel(m_actHost);
        aDot->setPixmap(colorDot(category->color, 9));
        auto* aName = new QLabel(a.name, m_actHost);
        row->addWidget(aDot);
        row->addWidget(aName, 1);

        const int used = m_data->eventCountUsing(a.id);
        const QString activityId = a.id;
        if (used > 0) {
            auto* tag = new QLabel(tr("in use (%1)").arg(used), m_actHost);
            tag->setStyleSheet("color:#616974; font-size:11px;");
            row->addWidget(tag);
            auto* arch = new QPushButton(tr("Archive"), m_actHost);
            arch->setCursor(Qt::PointingHandCursor);
            arch->setStyleSheet(
                "background:#EEF0ED; border:none; border-radius:8px; "
                "padding:4px 9px; color:#616974; font-weight:600;");
            connect(arch, &QPushButton::clicked, this, [this, activityId]() {
                m_data->setActivityArchived(activityId, true);
            });
            row->addWidget(arch);
        } else {
            auto* x = new QPushButton(QStringLiteral("\u00D7"), m_actHost);
            x->setObjectName("danger");
            x->setFixedWidth(24);
            x->setCursor(Qt::PointingHandCursor);
            connect(x, &QPushButton::clicked, this, [this, activityId]() {
                m_data->removeActivity(activityId);
            });
            row->addWidget(x);
        }
        host->addLayout(row);
    }
}

void ActivitiesPage::updateTaskViewHeight()
{
    // Sum the delegate's row heights so the list shows every task with no inner
    // scrollbar; the surrounding QScrollArea handles overflow for the whole pane.
    int h = 0;
    for (int i = 0; i < m_taskModel->rowCount(); ++i)
        h += m_taskView->sizeHintForRow(i);
    m_taskView->setFixedHeight(h); // 0 rows -> collapses to nothing
}

void ActivitiesPage::startPieceUnder(const QString& parentTaskId)
{
    // Create first, name second — TickTick's exact order (their inline
    // "No Title" row IS a created subtask). Creating up front means the
    // panel edits a real task through the standard door, and a walk-away
    // leaves an honest "New piece" row to keep or ✕ — never lost typing.
    const QString pieceId =
        m_data->addSubtask(parentTaskId, tr("New piece"));
    if (pieceId.isEmpty())
        return; // the domain said no (piece parent / bad id) — trust it
    runTaskDetailNaming(*m_data, pieceId, window());
}

void ActivitiesPage::editTask(const QString& taskId)
{
    // v28.5: seed/exec/apply — and piece navigation — all live in
    // runTaskDetail now; it re-reads the task by id and handles a vanished
    // one. Window parenting per the ownership rule, unchanged.
    runTaskDetail(*m_data, taskId, window());
}

void ActivitiesPage::chooseDueDate(const QString& taskId)
{
    const Task* task = m_data->taskById(taskId);
    if (!task)
        return;
    DueDateDialog dialog(task->dueDate, task->dueTime, window());
    if (dialog.exec() == QDialog::Accepted)
        m_data->setTaskDueDate(taskId, dialog.chosenDate(), dialog.chosenTime());
}

QString ActivitiesPage::resolveCategoryHint(const QString& hint) const
{
    // The parser hands back a HINT ("school"), never an id — it knows no
    // categories, which is what keeps it pure. Resolution is the UI's job:
    // exact name match, case-insensitive. No match (or no hint) falls back to
    // the selected life area, so a typo'd tag degrades gracefully instead of
    // inventing a category or blocking the add.
    if (!hint.isEmpty()) {
        const QString byName = m_data->categoryIdByName(hint); // domain query
        if (!byName.isEmpty())
            return byName;
    }
    return m_selectedCategoryId;
}

void ActivitiesPage::updateQuickAddPreview()
{
    const QString text = m_taskInput->text();
    if (text.trimmed().isEmpty()) {
        m_quickAddPreview->hide(); // no reserved blank line under the input
        return;
    }

    // Same pure function the commit runs — the preview cannot drift from what
    // Enter will actually do, because there is only one interpretation.
    const nlp::ParsedTask p = nlp::parseQuickAdd(text, QDate::currentDate());

    // Chip only when a '#tag' was typed (the rail already SHOWS the selected
    // area — repeating it every keystroke would be noise). Resolved: the real
    // name, focus green. Unresolved: the raw hint + '?', grey.
    QString chip;
    bool resolved = false;
    if (!p.categoryHint.isEmpty()) {
        const Category* c =
            m_data->categoryById(resolveCategoryHint(p.categoryHint));
        resolved =
            c && c->name.compare(p.categoryHint, Qt::CaseInsensitive) == 0;
        chip = resolved ? c->name : p.categoryHint;
    }

    m_quickAddPreview->setText(quickAddPreviewHtml(p, chip, resolved));
    m_quickAddPreview->show();
}
