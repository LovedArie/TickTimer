#include "ActivitiesPage.h"


#include "SlidePanel.h"

#include <QStyle>

#include "ActivityDetailDialog.h"
#include "ActivityListModel.h"
#include "ActivityRowDelegate.h"
#include "AppData.h"
#include "CategoryTaskDelegate.h"
#include "CategoryTaskModel.h"
#include "DueDateDialog.h"
#include "QuickAddParser.h"
#include "ReorderListView.h"
#include "QuickAddPreview.h"
#include "TaskDetailDialog.h"
#include "Theme.h"
#include "ResponsiveWatcher.h"
#include "Widgets.h"
#include "Touch.h" // v30.7 — the 48dp minimum, as a value

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
    // ---- DRAG TO ORGANISE IS A MOUSE AFFORDANCE (v30.7) --------------------
    // On a touchscreen a press-and-move means SCROLL, and there is no second
    // gesture to spend: whichever widget claims it, the other loses. This tree
    // claimed it, so on the phone the life-areas list could not be scrolled at
    // all — every attempt picked an area up and carried it. Reported exactly
    // that way: "when I want to scroll down, it selects and hold the item and
    // moves it. So it's impossible to navigate."
    //
    // So drag-and-drop is desktop-only now. The capability is not lost on the
    // phone, it moves to a gesture that costs nothing: LONG-PRESS opens the
    // context menu (Qt synthesises it from a touch hold), and that menu has
    // carried "Move to folder" since folders existed. One organiser per input
    // device, and the common gesture goes to the common task.
    //
    // Same shape as the agenda's edge-resize decision (§3.31): where a touch
    // gesture and a mouse gesture collide, the touchscreen keeps scrolling and
    // the rarer action finds another door.
    const bool mouseDriven = !isCompactScreen();
    setDragEnabled(mouseDriven);   // items with ItemIsDragEnabled can be picked up
    setAcceptDrops(mouseDriven);   // the tree will receive drops
    setDropIndicatorShown(mouseDriven); // the blue line showing where a drop lands
    // DragDrop (not InternalMove): InternalMove would let Qt reparent items
    // ITSELF, which for us would allow nonsense like a category nested under a
    // category, or a folder inside a folder — states our domain forbids. By
    // handling the drop ourselves and rebuilding from AppData, the domain's
    // rules stay the only rules.
    setDragDropMode(mouseDriven ? QAbstractItemView::DragDrop
                                : QAbstractItemView::NoDragDrop);
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
    m_phoneShell = isCompactScreen();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 22);
    m_outerLayout = layout;
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
    m_railTitle = railTitle;
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
    // The desktop sentence names two gestures a phone does not have: there is
    // no right-click, and a drag is a scroll. Swapped rather than hidden,
    // because after v30.7 the phone's organiser is a LONG PRESS and that is
    // exactly as undiscoverable as press-and-hold-to-plan was on the agenda.
    // The one instruction a surface needs is the one for the input device in
    // front of it.
    if (m_phoneShell)
        railHint->setText(
            tr("Press and hold an area to move it, or a folder to archive "
               "it. Hold a row's handle to reorder."));

    m_rail = new CategoryTree(railPanel);
    m_rail->setObjectName("railTree");
    m_rail->setHeaderHidden(true);
    m_rail->setIndentation(16);
    m_rail->setCursor(Qt::PointingHandCursor);
    m_rail->setContextMenuPolicy(Qt::CustomContextMenu);
    // ---- THE SCROLLER GOES WHERE THE PRESSES LAND (v30.7) ------------------
    // QScroller::grabGesture installs an event filter on the widget you name.
    // A mouse press over this tree is delivered straight to this tree — it
    // never travels through the enclosing sheet's viewport — so a grab on the
    // sheet, which is what the previous two attempts did, could not possibly
    // see it. The list kept highlighting rows under a dragging finger because
    // the tree was still the only thing receiving the drag.
    //
    // makeTouchScrollable's QAbstractItemView overload is the right tool and
    // was sitting here unused: it sets ScrollPerPixel and grabs
    // LeftMouseButtonGesture, which DELAYS the press — panning if the finger
    // travels, replaying press-and-release to the row if it does not. That is
    // the whole difference between "scrolls" and "selects whatever you touch".
    makeTouchScrollable(m_rail);
    // And no bar: on a touchscreen the gesture is the affordance, and this
    // stripe down the side of the sheet is what the owner asked to remove.
    m_rail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    m_detailPanel = detailPanel;
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(6, 6, 6, 6);
    m_detailLayout = detailLayout;
    m_detail = new QScrollArea(detailPanel);
    makeTouchScrollable(m_detail); // finger-flick on touch screens
    // Same reasoning as the drawer's and the agenda's (v30.7): where the
    // gesture is the affordance the bar is only noise, and on this page it
    // was a stub of grey hanging off the right edge beside the task rows.
    if (m_phoneShell)
        m_detail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detail->setWidgetResizable(true);
    detailLayout->addWidget(m_detail);
    buildDetailPane(); // the persistent skeleton — built ONCE, never rebuilt

    // ---- where the rail LIVES, decided once ---------------------------------
    // A permanent second column costs 168 of a phone's 360px and leaves the
    // thing you came for in a sliver. On a phone the AREA fills the screen and
    // the list of areas becomes a sheet you summon — TickTick's shape, and the
    // one the owner asked for after living with the split.
    //
    // Decided at construction and never revisited, the same rule the glance
    // drawer follows: nothing moves at a mode change, so there is no
    // "does it come back?" case to get wrong.
    if (m_phoneShell) {
        m_areaDrawer = new SlidePanel(this);
        m_areaDrawer->setTitle(tr("Life areas"));
        railPanel->setMinimumWidth(0);
        railPanel->setMaximumWidth(QWIDGETSIZE_MAX);
        // Stretch 1, so the list fills the sheet instead of sitting in the
        // top third with grey below it. SlidePanel's content layout ends in a
        // trailing stretch, which otherwise eats everything left over.
        // NEVER clearContent() this panel — it deletes what it holds.
        QVBoxLayout* sheet = m_areaDrawer->contentLayout();
        sheet->insertWidget(0, railPanel, 1);
        // SlidePanel ends its content with addStretch(1) so short rows pool
        // their slack at the bottom. Two stretch-1 items split the space
        // evenly, which left the list filling exactly half the sheet with grey
        // beneath it. This panel is not a stack of rows — it IS the content —
        // so the pooled slack is given to it.
        sheet->setStretch(sheet->count() - 1, 0);
        // THE CORNER GOES BACK TO BEING A CLOSE BUTTON (v30.7).
        //
        // It used to carry "Archive area", on the reasoning that the ≡ which
        // opened the sheet also closes it, so the ✕ was a spare. The premise
        // was true and the conclusion was still wrong: the top-right of a
        // sheet is where every phone user reaches to dismiss it, so what sat
        // there was not a spare corner but the most-aimed-at pixel on the
        // screen — and it held an action that ARCHIVES a whole life area.
        // A destructive verb parked in the muscle-memory position of "close"
        // is a trap regardless of how many other exits exist.
        //
        // Archiving moves to the area's own header, next to the name it acts
        // on, which is where the desktop has always kept it.
        m_areaDrawer->setHeaderButton(QString(), tr("Close"));
        // The sheet's own header already says "Life areas".
        if (m_railTitle)
            m_railTitle->hide();
        connect(m_areaDrawer, &SlidePanel::openStateChanged,
                this, &ActivitiesPage::overlayOpenChanged);
    } else {
        layout->addWidget(railPanel);
    }
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
    // Not on a phone: there the rail is IN A SHEET and must fill it. A fixed
    // width here would fight that — setFixedWidth pins minimum AND maximum, so
    // it silently re-squeezed the panel to 168px inside a 336px drawer on
    // every mode dispatch, truncating every label in it.
    if (!m_phoneShell)
        m_railPanel->setFixedWidth(mode == responsive::Mode::Compact ? 168 : 250);

    // ---- edge to edge, same treatment as the calendar -----------------------
    // With the rail in a sheet, the area IS the screen — so the card around it
    // is a frame drawn around the whole display, and the nested margins
    // (26 page + 6 panel + 14 column) were 46px of a 360px width spent on
    // whitespace three times over.
    const bool compact = mode == responsive::Mode::Compact;
    if (m_detailPanel && m_detailPanel->property("flat").toBool() != compact) {
        m_detailPanel->setProperty("flat", compact);
        // A QSS property selector never re-evaluates on its own.
        m_detailPanel->style()->unpolish(m_detailPanel);
        m_detailPanel->style()->polish(m_detailPanel);
    }
    if (m_outerLayout)
        m_outerLayout->setContentsMargins(compact ? 0 : 26, compact ? 0 : 22,
                                          compact ? 0 : 26, compact ? 0 : 22);
    if (m_detailLayout)
        m_detailLayout->setContentsMargins(compact ? 0 : 6, compact ? 0 : 6,
                                           compact ? 0 : 6, compact ? 0 : 6);
    if (m_contentLayout)
        m_contentLayout->setContentsMargins(compact ? 12 : 14, 12,
                                            compact ? 12 : 14, 12);
}

void ActivitiesPage::openAreaDrawer()
{
    if (!m_areaDrawer)
        return;
    // A toggle, not an opener. The button that summoned the sheet is the
    // obvious thing to press to dismiss it, which is also why the sheet can
    // afford to give its ✕ corner away.
    if (m_areaDrawer->isOpen())
        m_areaDrawer->closePanel();
    else
        m_areaDrawer->open();
}

void ActivitiesPage::archiveSelectedArea()
{
    const Category* category = m_data->categoryById(m_selectedCategoryId);
    if (!category)
        return; // nothing chosen; the button has nothing to act on
    m_data->setCategoryArchived(category->id, true);
    if (m_areaDrawer)
        m_areaDrawer->closePanel();
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
        if (folder.archived)
            continue; // v31: retired semesters live on the Archive page
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
            if (c.folderId == folder.id && !m_data->categoryHidden(c))
                addCategoryItem(folderItem, c);

        folderItem->setExpanded(!m_collapsedFolders.contains(folder.id));
    }

    for (const Category& c : m_data->categories())
        if (c.folderId.isEmpty() && !m_data->categoryHidden(c))
            addCategoryItem(nullptr, c);

    if (toSelect)
        m_rail->setCurrentItem(toSelect);
    m_rebuilding = false;
}

void ActivitiesPage::onRailItemClicked(QTreeWidgetItem* item, int)
{
    // Choosing an area is the whole reason the sheet was open, so it closes
    // itself. Leaving it up would put the thing you just asked for behind the
    // thing you asked with.
    if (m_areaDrawer && m_areaDrawer->isOpen())
        m_areaDrawer->closePanel();

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
        // ---- retire the whole semester (v31) ------------------------------
        // The action a non-empty folder actually needs. Before this, the only
        // thing offered was a Delete that could never fire, so the honest
        // answer to "the session ended" was to move four areas out by hand
        // and archive each one.
        QAction* archive = menu.addAction(tr("Archive folder"));
        connect(archive, &QAction::triggered, this, [this, folderId]() {
            m_data->setFolderArchived(folderId, true);
        });

        menu.addSeparator();

        // ---- delete, and WHY it is grey when it is grey --------------------
        // The rule is unchanged (AppData::removeFolder refuses a folder that
        // still holds areas, archived ones included) and it is the rule the
        // owner asked for. What was missing was the reason: a disabled item
        // labelled "Delete folder" is indistinguishable from a broken one,
        // which is exactly how it was reported.
        //
        // The reason goes in the LABEL, not a tooltip. A tooltip needs a
        // hovering pointer, and this menu is opened by long-press on the
        // phone where there is no such thing — a tooltip there is a message
        // written in ink only a desktop can see.
        const int inside = m_data->categoryCountInFolder(folderId);
        QAction* remove = menu.addAction(
            inside == 0
                ? tr("Delete folder")
                : tr("Delete folder — move out %n life area(s) first", nullptr,
                     inside));
        remove->setEnabled(inside == 0);
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
    m_contentLayout = layout;
    layout->setSpacing(8);

    // header (dot, name, delete/archive) — refilled in place by refreshHeader()
    m_headerHost = new QWidget(content);
    auto* headerLayout = new QHBoxLayout(m_headerHost);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_headerHost);

    // ---- TASKS: persistent input + a model/view list ----------------------
    auto* tasksHead = new QHBoxLayout;
    auto* tasksTitle = new QLabel(tr("TASKS"), content);
    tasksTitle->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    tasksHead->addWidget(tasksTitle);
    tasksHead->addStretch(1);

    // ---- the way BACK out of hand order (v31) -----------------------------
    // Dragging a row flips this area to Manual, and without this button that
    // would be a one-way decision: the only route back to "soonest deadline
    // first" would be dragging every row into place by hand, which is the
    // opposite of what the deadline sort is for.
    //
    // It sits on the TASKS caption because that is the list it governs — the
    // same "controls near their effect need no label" rule the area switcher
    // and the task-notes toggle already follow. It appears ONLY while the
    // area is Manual: a control offering to undo something that has not
    // happened is noise on every one of the areas that never get dragged.
    // The reorder toggle, on the caption of the list it governs — the same
    // "control on the thing it affects" rule the area switcher follows.
    m_taskReorderBtn = makeReorderToggle(content);
    connect(m_taskReorderBtn, &QPushButton::clicked, this, [this]() {
        setTaskReorderMode(!m_taskDelegate->reorderMode());
    });
    tasksHead->addWidget(m_taskReorderBtn);

    m_sortResetBtn = new QPushButton(tr("Sorted by hand · use deadlines"),
                                     content);
    m_sortResetBtn->setObjectName(QStringLiteral("sortResetButton"));
    m_sortResetBtn->setCursor(Qt::PointingHandCursor);
    m_sortResetBtn->setStyleSheet(
        QStringLiteral("QPushButton { background:#EEF0ED; border:none; "
                       "border-radius:8px; padding:4px 9px; color:#616974; "
                       "font-size:11px; font-weight:600; }%1")
            .arg(m_phoneShell ? QStringLiteral("QPushButton { min-height:40px; }")
                              : QString()));
    m_sortResetBtn->hide();
    connect(m_sortResetBtn, &QPushButton::clicked, this, [this]() {
        m_data->setCategorySortMode(m_selectedCategoryId,
                                    Category::SortMode::Smart);
    });
    tasksHead->addWidget(m_sortResetBtn);

    layout->addSpacing(6);
    layout->addLayout(tasksHead);

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
    connect(m_taskDelegate, &CategoryTaskDelegate::moveUpRequested, this,
            [this](const QString& id) {
                moveRowBy(displayedTaskIds(), id, -1);
            });
    connect(m_taskDelegate, &CategoryTaskDelegate::moveDownRequested, this,
            [this](const QString& id) {
                moveRowBy(displayedTaskIds(), id, +1);
            });

    m_taskView = new ReorderListView(cattask::IdRole, content);
    m_taskView->setModel(m_taskModel);
    // The reorder payoff, and it is the same three-line shape the rail's
    // drag already has: the view reports the gesture, the DOMAIN performs the
    // move (flipping the area to Manual as it goes), and its changed()
    // re-snapshots the model. The page is a thin translator, nothing more.
    connect(m_taskView, &ReorderListView::reordered, this,
            [this](const QString& movedId, const QString& beforeId) {
                m_data->moveTaskBefore(movedId, beforeId);
            });
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
                showTaskRowMenu(index.data(cattask::IdRole).toString(),
                                m_taskView->viewport()->mapToGlobal(pos));
            });
    // The touchscreen's door into the SAME menu. The view detects the hold
    // itself — see ReorderListView.h for why a synthesised context-menu
    // event never arrives here.
    connect(m_taskView, &ReorderListView::rowLongPressed, this,
            &ActivitiesPage::showTaskRowMenu);
    // Holding the HANDLE means "I am moving this one": the list enters
    // reorder mode, so the arrows are there when the finger lifts, and the
    // same finger can drag the row straight away.
    connect(m_taskView, &ReorderListView::gripHeld, this,
            [this](const QString&) { setTaskReorderMode(true); });
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
    auto* actsHead = new QHBoxLayout;
    auto* actsTitle = new QLabel(tr("ACTIVITIES"), content);
    actsTitle->setStyleSheet(
        "color:#616974; font-size:10px; font-weight:700; letter-spacing:1px;");
    actsHead->addWidget(actsTitle);
    actsHead->addStretch(1);
    m_actReorderBtn = makeReorderToggle(content);
    connect(m_actReorderBtn, &QPushButton::clicked, this, [this]() {
        setActivityReorderMode(!m_actDelegate->reorderMode());
    });
    actsHead->addWidget(m_actReorderBtn);
    layout->addSpacing(10);
    layout->addLayout(actsHead);

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

    // ---- the activity list, now model/view (v31) --------------------------
    // It was a stack of hand-built QHBoxLayouts refilled on every changed().
    // Reordering is what forced the conversion: widgets in a layout have no
    // drag machinery, and Qt's item views do. See ActivityListModel.h for why
    // this model resets where CategoryTaskModel diffs.
    m_actModel    = new ActivityListModel(m_data, this);
    m_actDelegate = new ActivityRowDelegate(this);
    connect(m_actDelegate, &ActivityRowDelegate::editRequested, this,
            [this](const QString& id) {
                // window(), not `this`: a changed()-driven rebuild must not
                // be able to delete a live dialog's parent.
                runActivityDetail(*m_data, id, window());
            });
    connect(m_actDelegate, &ActivityRowDelegate::archiveRequested, this,
            [this](const QString& id) { m_data->setActivityArchived(id, true); });
    connect(m_actDelegate, &ActivityRowDelegate::deleteRequested, this,
            [this](const QString& id) { m_data->removeActivity(id); });
    connect(m_actDelegate, &ActivityRowDelegate::moveUpRequested, this,
            [this](const QString& id) {
                moveRowBy(displayedActivityIds(), id, -1);
            });
    connect(m_actDelegate, &ActivityRowDelegate::moveDownRequested, this,
            [this](const QString& id) {
                moveRowBy(displayedActivityIds(), id, +1);
            });

    m_actView = new ReorderListView(actrow::IdRole, content);
    m_actView->setModel(m_actModel);
    m_actView->setItemDelegate(m_actDelegate);
    m_actView->setFrameShape(QFrame::NoFrame);
    m_actView->setSelectionMode(QAbstractItemView::NoSelection);
    m_actView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_actView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_actView->viewport()->setAutoFillBackground(false);
    connect(m_actView, &ReorderListView::reordered, this,
            [this](const QString& movedId, const QString& beforeId) {
                m_data->moveActivityBefore(movedId, beforeId);
            });

    // The same door the task rows get, for the same reason — and it carries
    // Edit as well, because on a phone the row's tap target is the whole row
    // and a long press should still be able to say what it is offering.
    m_actView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_actView, &QListView::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const QModelIndex index = m_actView->indexAt(pos);
                if (!index.isValid())
                    return;
                showActivityRowMenu(index.data(actrow::IdRole).toString(),
                                    m_actView->viewport()->mapToGlobal(pos));
            });
    connect(m_actView, &ReorderListView::rowLongPressed, this,
            &ActivitiesPage::showActivityRowMenu);
    connect(m_actView, &ReorderListView::gripHeld, this,
            [this](const QString&) { setActivityReorderMode(true); });

    // Same "report your full height, let the pane scroll" contract the task
    // list keeps — two lists inside one QScrollArea, neither scrolling itself.
    connect(m_actModel, &QAbstractItemModel::modelReset,
            this, &ActivitiesPage::updateActivityViewHeight);
    layout->addWidget(m_actView);

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
    // A mode is a state, and a state that survives a CONTEXT SWITCH is a
    // trap: you pick a different life area and its list is mysteriously in
    // arrows. So both drop when the selected area changes — and ONLY then.
    //
    // The distinction matters more than it looks. refreshDetail() also runs
    // on every changed(), and a move IS a change, so resetting here
    // unconditionally kicked the user out of the mode on their first tap —
    // you could move one row and then had to press Reorder again for the
    // next. Caught by the suite before the phone saw it, and only because
    // the ejected mode let a row tap open a modal that blocks headlessly.
    if (m_reorderAreaId != category->id) {
        if (m_taskDelegate->reorderMode())
            setTaskReorderMode(false);
        if (m_actDelegate->reorderMode())
            setActivityReorderMode(false);
        m_reorderAreaId = category->id;
    }

    // The hand-order escape hatch shows itself only where it applies.
    m_sortResetBtn->setVisible(category->sortMode == Category::SortMode::Manual);
    m_taskModel->setCategoryId(category->id); // re-point the model (no-op if same)
    updateTaskViewHeight();
    refreshActivities();
}

// ---------------------------------------------------------------------------
// THE OTHER DOOR (v31.0.1). A grip drag is a mouse gesture and a phone has
// none — QScroller owns press-and-move there, and two attempts to take it
// back both lost (ReorderListView.h records why). So the same capability is
// offered through the row's long-press menu, which is exactly what
// CategoryTree did in v30.7 when its drag collided with scrolling.
//
// Both lists share this one function because both reorder through the same
// pair of domain doors, and "move up" is the same idea in both: find the row
// I am currently ABOVE or BELOW on screen, and go before it.
//
// The neighbour is read from the DISPLAYED order, not from sortKey, for the
// same reason moveTaskBefore seeds from the display: before the first move
// every key is 0, and "the row above this one" is a question about what the
// user can see.
void ActivitiesPage::moveRowBy(const QStringList& order, const QString& id,
                               int delta)
{
    const int at = order.indexOf(id);
    if (at < 0)
        return;
    const int to = at + delta;
    if (to < 0 || to >= order.size())
        return; // already at the end it is being pushed towards

    // Moving DOWN means landing before whatever follows the row we are
    // swapping with — which for the last position is nothing, i.e. the end.
    const QString beforeId =
        delta < 0 ? order[to]
                  : (to + 1 < order.size() ? order[to + 1] : QString());

    if (m_taskModel && m_data->taskById(id))
        m_data->moveTaskBefore(id, beforeId);
    else
        m_data->moveActivityBefore(id, beforeId);
}

// ---------------------------------------------------------------------------
// REORDER MODE (v31.0.3) — the door that does not depend on a gesture.
//
// Three gesture designs lost to QScroller on a real phone; the delegates'
// headers carry the full account. A TAP has never failed on these rows, so
// the reorder is built on taps, and a MODE is what buys the room: with the
// due badge, the archive pill and the × out of the way, the arrows get a
// full 48dp each instead of squeezing into a row that was already over
// budget.
// ---------------------------------------------------------------------------

QPushButton* ActivitiesPage::makeReorderToggle(QWidget* parent)
{
    auto* b = new QPushButton(tr("Reorder"), parent);
    b->setObjectName(QStringLiteral("reorderToggle"));
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(
        QStringLiteral(
            "QPushButton { background:#EEF0ED; border:none; border-radius:8px; "
            "padding:4px 10px; color:#616974; font-size:11px; "
            "font-weight:600; }"
            "QPushButton:checked { background:#2F7E6E; color:#FFFFFF; }%1")
            .arg(m_phoneShell ? QStringLiteral("QPushButton { min-height:40px; }")
                              : QString()));
    return b;
}

void ActivitiesPage::setTaskReorderMode(bool on)
{
    m_taskDelegate->setReorderMode(on);
    m_taskReorderBtn->setChecked(on);
    m_taskReorderBtn->setText(on ? tr("Done") : tr("Reorder"));
    // Leaving the other list in its normal state is deliberate: the two
    // lists are separate questions, and a mode that silently changed a list
    // you were not looking at would be a surprise.
    m_taskView->viewport()->update();
}

void ActivitiesPage::setActivityReorderMode(bool on)
{
    m_actDelegate->setReorderMode(on);
    m_actReorderBtn->setChecked(on);
    m_actReorderBtn->setText(on ? tr("Done") : tr("Reorder"));
    m_actView->viewport()->update();
}

// The row menus. ONE builder each, reached by two doors — right-click on a
// desktop, a held finger on a phone — because a menu that differs by how it
// was opened is two menus that will drift.
void ActivitiesPage::showTaskRowMenu(const QString& id, const QPoint& globalPos)
{
    if (!m_data->taskById(id))
        return;
    QMenu menu(m_taskView);

    // v28.7 — the TickTick door for pieces. Offered only for parents (one
    // level — the domain guard in addSubtask is the real wall, this is just
    // honest chrome).
    const Task* task = m_data->taskById(id);
    if (task && !task->isPiece()) {
        QAction* addPiece = menu.addAction(tr("Add a piece"));
        connect(addPiece, &QAction::triggered, this,
                [this, id]() { startPieceUnder(id); });
        menu.addSeparator();
    }

    const QStringList order = displayedTaskIds();
    addMoveActions(menu, order, id);
    menu.exec(globalPos);
}

void ActivitiesPage::showActivityRowMenu(const QString& id,
                                         const QPoint& globalPos)
{
    if (!m_data->activityById(id))
        return;
    QMenu menu(m_actView);
    QAction* edit = menu.addAction(tr("Edit…"));
    connect(edit, &QAction::triggered, this,
            [this, id]() { runActivityDetail(*m_data, id, window()); });
    menu.addSeparator();
    addMoveActions(menu, displayedActivityIds(), id);
    menu.exec(globalPos);
}

// Move up / Move down, disabled at the ends. Shared so the two lists cannot
// end up wording or bounding the same idea differently.
void ActivitiesPage::addMoveActions(QMenu& menu, const QStringList& order,
                                    const QString& id)
{
    const int at = order.indexOf(id);
    QAction* up = menu.addAction(tr("Move up"));
    up->setEnabled(at > 0);
    connect(up, &QAction::triggered, this,
            [this, order, id]() { moveRowBy(order, id, -1); });
    QAction* down = menu.addAction(tr("Move down"));
    down->setEnabled(at >= 0 && at < order.size() - 1);
    connect(down, &QAction::triggered, this,
            [this, order, id]() { moveRowBy(order, id, +1); });
}

QStringList ActivitiesPage::displayedTaskIds() const
{
    QStringList ids;
    for (int i = 0; i < m_taskModel->rowCount(); ++i) {
        const QModelIndex ix = m_taskModel->index(i, 0);
        if (!ix.data(cattask::IsPieceRole).toBool()) // pieces have no position
            ids << ix.data(cattask::IdRole).toString();
    }
    return ids;
}

QStringList ActivitiesPage::displayedActivityIds() const
{
    QStringList ids;
    for (int i = 0; i < m_actModel->rowCount(); ++i)
        ids << m_actModel->index(i, 0).data(actrow::IdRole).toString();
    return ids;
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
    hl->addWidget(dot);

    // ---- the area name IS the area switcher, on a phone (v30.7) -----------
    // Reported as "when I try to change the life area, it's very difficult to
    // navigate", and the screenshot says why: the area you are looking at was
    // static text at the top LEFT, while the only control that changed it was
    // an unlabelled ≡ at the top right of the APP header — a different bar,
    // belonging to a different thing, with nothing tying the two together.
    //
    // This file already argued the principle, for the Task-notes toggle:
    // "Controls near their effect need no label explaining what they affect;
    // distance is what creates that need." It was never applied to the
    // biggest control on the page.
    //
    // So the name becomes a button with a ▾ and tapping it opens the sheet.
    // That is also what Google Calendar and TickTick do — you tap the list
    // you are in to change which list you are in — so it needs no teaching.
    // The ≡ still works; it is simply no longer the only way in.
    if (m_phoneShell) {
        // U+25BC (▼), not the prettier small U+25BE: the small one
        // renders as an empty box on this device, exactly as U+2715 did
        // for SlidePanel's close button. Android's default font has a
        // narrower glyph set than a desktop's, and the rule this keeps
        // proving is to reuse a codepoint the app already draws somewhere
        // rather than pick the nicest-looking one. EventDialog's nudge
        // buttons have been drawing this one since v19.
        auto* switcher = new QPushButton(
            category->name + QStringLiteral("  ▼"), m_headerHost);
        switcher->setObjectName("areaSwitcher");
        switcher->setCursor(Qt::PointingHandCursor);
        switcher->setToolTip(tr("Switch life area"));
        connect(switcher, &QPushButton::clicked,
                this, &ActivitiesPage::openAreaDrawer);
        hl->addWidget(switcher);
    } else {
        auto* name = new QLabel(category->name, m_headerHost);
        name->setObjectName("h2");
        hl->addWidget(name);
    }
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
        // v30.7 brings this back to the phone too: it briefly lived in the
        // area sheet's corner, which is the position a phone user reaches for
        // to CLOSE a sheet. An archive button there is a trap. Beside the name
        // it acts on, it is just a button.
        auto* arch = new QPushButton(tr("Archive area"), m_headerHost);
        arch->setCursor(Qt::PointingHandCursor);
        // A widget's OWN stylesheet beats the application one outright, so the
        // compact min-height in Theme.h cannot reach an inline sheet like this
        // — it has to carry its own. 5px of padding either side, so 38 lands
        // on 48 (v30.7; caught by the touch gate, not by reading).
        arch->setStyleSheet(
            QStringLiteral(
                "background:#EEF0ED; border:none; border-radius:8px; "
                "padding:5px 10px; color:#616974; font-weight:600;%1")
                .arg(m_phoneShell ? " min-height:38px;" : ""));
        connect(arch, &QPushButton::clicked, this, [this, categoryId]() {
            m_data->setCategoryArchived(categoryId, true);
        });
        hl->addWidget(arch);
    }
}

void ActivitiesPage::refreshActivities()
{
    const Category* category = m_data->categoryById(m_selectedCategoryId);
    m_actModel->setCategoryId(category ? category->id : QString());
    m_actModel->refresh(); // no-op-cheap when the id was already current
    updateActivityViewHeight();
}

void ActivitiesPage::updateActivityViewHeight()
{
    int h = 0;
    for (int i = 0; i < m_actModel->rowCount(); ++i)
        h += m_actView->sizeHintForRow(i);
    m_actView->setFixedHeight(h); // 0 rows -> collapses to nothing
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
