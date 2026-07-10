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
#include <QWidget>

class AppData;
class QScrollArea;
class QTreeWidgetItem;
class QDropEvent;

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

private:
    void rebuildRail();
    void rebuildDetail();
    QWidget* buildDetailContent();
    void onRailItemClicked(QTreeWidgetItem* item, int column);
    void onRailContextMenu(const QPoint& pos);

    AppData*     m_data;
    CategoryTree* m_rail  = nullptr;
    QScrollArea* m_detail = nullptr;
    QString      m_selectedCategoryId;   // domain selection: rebuild-proof id
    QSet<QString> m_collapsedFolders;    // presentation: session-only, unsaved
    bool         m_rebuilding = false;   // guards collapse tracking during rebuild
    QColor       m_newCategoryColor{"#4C6FE0"};
};
