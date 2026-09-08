#pragma once
// ---------------------------------------------------------------------------
// ArchivePage — the quiet room (items 3–5). Everything the rest of the app
// deliberately hides — archived tasks, archived activities — lives here,
// visible ONLY when you come looking.
//
// The design intent is the placement: it sits at the BOTTOM of the nav,
// styled like Sync/Share, below the stretch — furniture, not a destination.
// Archive is where things go to stop demanding attention; a page that
// demanded attention would defeat itself.
//
// Four lists, and only one of them has a second exit:
//   Folders:    Restore only (v31). A whole retired semester, widest thing
//               on the page, so it reads first — a user hunting a missing
//               course should meet the folder hiding it before the empty
//               life-areas list.
//   Life areas: Restore only.
//   Tasks:      Restore (back to its category, un-archived) or Delete
//               forever (removeTask — tasks reference nothing, safe).
//   Activities: Restore only. No delete here on purpose — an archived
//               activity is usually archived BECAUSE it's in past events,
//               and AppData::removeActivity would refuse anyway. The page
//               doesn't offer buttons the domain will bounce. The same
//               reasoning silences delete on folders: removeFolder refuses
//               a folder that still holds areas.
//
// Same rebuild-on-changed() pattern as every page: the lists re-derive from
// AppData, nothing is hand-maintained.
// ---------------------------------------------------------------------------

#include <QWidget>

class AppData;
class QScrollArea;

class ArchivePage : public QWidget
{
    Q_OBJECT

public:
    explicit ArchivePage(AppData* data, QWidget* parent = nullptr);

public slots:
    void rebuild();

private:
    QWidget* buildContent();

    AppData*     m_data;
    QScrollArea* m_scroll = nullptr;
};
