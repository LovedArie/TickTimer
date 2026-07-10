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
};
