#pragma once
// ---------------------------------------------------------------------------
// ActivitiesPage v3 — the rail becomes a TREE: folders holding life areas,
// one level deep, like the owner's TickTick (addendum §3.12).
//
//   ┌ category rail ──┐
//   │ ▾ School  · 2   │   folders: bold, expandable, NOT selectable —
//   │    ● LOG410     │   they are structure, not a choice
//   │    ● GTI350     │
//   │ ● Health   · 3  │   loose categories stay at top level
//   │ [+ Folder] [new]│
//   └─────────────────┘
//
// Widget upgrade: QListWidget -> QTreeWidget — Qt's widget for exactly
// this shape, with expand/collapse handling for free. Organising happens
// via a right-click menu (move to folder, rename, delete); drag-and-drop
// is the noted exercise.
//
// One classifier call worth seeing in code: which folders are COLLAPSED
// is presentation (session-only, m_collapsedFolders, deliberately never
// saved), while which folder a category LIVES IN is domain (survives
// restart, guarded by rules). Same feature, both bins, side by side.
// ---------------------------------------------------------------------------

#include <QColor>
#include <QSet>
#include <QTreeWidget> // subclassed below (CategoryTree) — needs the full type
#include "Responsive.h"

#include <QWidget>

class AppData;
class QScrollArea;
class QTreeWidgetItem;
class QDropEvent;
class QStackedWidget;
class QLineEdit;
class QListView;
class QLabel;
class CategoryTaskModel;
class CategoryTaskDelegate;

// Item-data role keys, shared between CategoryTree (drops) and ActivitiesPage
// (building the tree): read the role to learn what an item IS. Declared in the
// header so both classes agree on the exact numbers.
namespace acts
{
constexpr int kCategoryIdRole = Qt::UserRole;     // a life area lives here
constexpr int kFolderIdRole   = Qt::UserRole + 1; // a folder lives here
} // namespace acts

// ---------------------------------------------------------------------------
// CategoryTree — a QTreeWidget that turns a DRAG-AND-DROP gesture into a
// domain INTENT and nothing more. On a valid drop it emits
// categoryDropped(categoryId, folderId) and lets the page perform the one
// true mutation; it never touches AppData itself. Same model/view split the
// whole app follows: the widget reports what the user did, the domain decides
// what it means and whether it is allowed.
//
// It does NOT chain to QTreeWidget's own dropEvent, so Qt never reparents
// items behind our back. The data moves in AppData, changed() fires, and the
// tree is rebuilt from truth — one source of truth, even for a drag.
// ---------------------------------------------------------------------------
class CategoryTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit CategoryTree(QWidget* parent = nullptr);

signals:
    void categoryDropped(const QString& categoryId, const QString& folderId);

protected:
    void dropEvent(QDropEvent* event) override;
};

class ActivitiesPage : public QWidget
{
    Q_OBJECT

public:
    explicit ActivitiesPage(AppData* data, QWidget* parent = nullptr);

public slots:
    void rebuild();
    // v28.7 — the TickTick door: create a piece under this task and open
    // the panel on it, title pre-selected for naming. PUBLIC because it
    // is the behavior the right-click menu merely triggers — the test
    // drives this directly instead of simulating a QMenu exec (untestable
    // chrome). Domain-guarded: addSubtask refuses a piece parent, so the
    // one-level rule holds even if a menu ever offers this on a piece.
    void startPieceUnder(const QString& parentTaskId);

private slots:
    // A task row's affordances, wired from CategoryTaskDelegate's signals.
    void editTask(const QString& taskId);      // opens TaskDetailDialog
    void chooseDueDate(const QString& taskId); // opens DueDateDialog
    void updateTaskViewHeight();               // fit the list to its rows
    void updateQuickAddPreview();              // live parse of the task input

protected:
    bool event(QEvent* e) override; // the container's size class arrives here

private:
    void applyLayoutMode(responsive::Mode mode);

    void rebuildRail();
    void buildDetailPane();  // ONCE, in the ctor — the persistent skeleton
    void refreshDetail();    // update in place: header + task model + activities
    void refreshHeader();
    void refreshActivities();
    void onRailItemClicked(QTreeWidgetItem* item, int column);
    void onRailContextMenu(const QPoint& pos);
    // '#school' -> the matching category id (exact name, case-insensitive),
    // or the currently selected life area when there is no/unknown hint.
    QString resolveCategoryHint(const QString& hint) const;

    AppData*     m_data;
    CategoryTree* m_rail  = nullptr;
    QScrollArea* m_detail = nullptr;
    // Held so applyLayoutMode() can narrow it. NOTE this page is still a
    // permanent two-column split, which is wrong on a phone however narrow
    // the rail gets — the master/detail rework is a later stage. Its minimum
    // is already inside the phone budget, so the core does not force it.
    class QFrame* m_railPanel = nullptr;
    QString      m_selectedCategoryId;   // domain selection: rebuild-proof id
    QSet<QString> m_collapsedFolders;    // presentation: session-only, unsaved
    bool         m_rebuilding = false;   // guards collapse tracking during rebuild
    QColor       m_newCategoryColor{"#4C6FE0"};

    // Persistent detail-pane widgets — built once, updated in place. This is the
    // heart of the v20.2 conversion: the task input is never destroyed, so it
    // can no longer be freed mid-signal (the crash rebuildDetail guarded with
    // deleteLater). The task list itself is a model/view QListView, not a stack
    // of rebuilt TaskRow widgets.
    QStackedWidget*       m_detailStack  = nullptr; // [0] empty · [1] content
    QWidget*              m_headerHost   = nullptr;  // dot/name/button, refilled
    QLineEdit*            m_taskInput    = nullptr;
    QLabel*               m_quickAddPreview = nullptr; // live parse readout
    CategoryTaskModel*    m_taskModel    = nullptr;
    CategoryTaskDelegate* m_taskDelegate = nullptr;
    QListView*            m_taskView     = nullptr;
    QLineEdit*            m_actInput     = nullptr;
    QWidget*              m_actHost      = nullptr;  // activity rows, refilled
};
