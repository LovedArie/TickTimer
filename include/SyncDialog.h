#pragma once

#include <QDialog>

class SyncService;
class QLabel;
class QPushButton;
class QWidget;

// ---------------------------------------------------------------------------
// SyncDialog — one primary button ("Sync now") plus a status line, and a
// conflict box that only exists when a human decision is required. Purely
// reactive to SyncService's signals, the same widget-reports/page-decides
// split as everywhere else — the dialog owns NO sync logic, it just renders
// the service's state and forwards button presses.
// ---------------------------------------------------------------------------

class SyncDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SyncDialog(SyncService* sync, QWidget* parent = nullptr);

private:
    void refreshInfo();

    SyncService* m_sync;

    QLabel*      m_status     = nullptr;
    QLabel*      m_info       = nullptr;
    QPushButton* m_syncBtn    = nullptr;
    QWidget*     m_conflictBox = nullptr;

    // v31.2 — the conflict box has TWO halves, because a conflict has two
    // kinds of row and only one of them is a question.
    //
    //   m_contestedBox  rows edited on both sides. The two buttons live
    //                   here, because these are the only rows they decide.
    //   m_settledBox    rows where one side edited and the other deleted.
    //                   merge::plan keeps the edit whichever button is
    //                   pressed, so listing them among the choices asked a
    //                   question and then ignored the answer. They are
    //                   reported, not asked.
    //
    // Each half hides when its list is empty.
    QWidget*     m_contestedBox = nullptr;
    QWidget*     m_settledBox   = nullptr;

    // WHAT clashed, named, and — the point of the split above — which SIDE
    // did what. Held as members because the box is built once and filled
    // later: the text is only knowable when the conflict arrives, and there
    // are two arrival paths (the signal, and the state asked on open for a
    // conflict raised before this dialog existed).
    QLabel*      m_clashList    = nullptr; // the contested rows
    QLabel*      m_settledList  = nullptr; // the already-decided rows

    // Fills both lists from the service. Called on BOTH arrival paths, so
    // a conflict raised before this dialog opened is described the same way
    // as one that arrives while it is up.
    void showClashes();
};
