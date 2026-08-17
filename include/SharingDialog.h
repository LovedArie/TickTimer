#pragma once

#include "ShareClient.h"

#include <QDialog>

class AppData;
class QLabel;
class TrackerService;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// ---------------------------------------------------------------------------
// SharingDialog — the one place sharing is managed: grant someone read access
// to your planner, see who you've granted (with a Remove per name), see who
// has granted YOU (with a Compare per name), and jump into a comparison.
//
// Same shape as SyncDialog: the dialog owns NO policy. It renders whatever
// ShareClient's signals report and forwards button presses; every decision
// (may you read this? does that user exist?) is the SERVER's, which is the
// only honest place for it — a permission checked only in the UI isn't a
// permission, it's a suggestion.
//
// It carries the live AppData* and the TrackerService onward to
// CompareDialog. (v1 held a const pointer — "a sharing screen has no
// business mutating the planner" — and that was true until comparing
// turned out to BE planning. The requirement moved; the const moved with
// it, to the peer snapshot where it still tells the truth. Constness is a
// design statement, and design statements get revised like any other.)
// ---------------------------------------------------------------------------

class SharingDialog : public QDialog
{
    Q_OBJECT
public:
    // `myName`: the logged-in account, passed through to CompareDialog's
    // pinned column headers. This dialog itself never shows it — it is a
    // courier, and the parameter documents the delivery route.
    SharingDialog(ShareClient* client, AppData* myData,
                  TrackerService* tracker, const QString& myName,
                  QWidget* parent = nullptr);

private:
    void refresh();                 // ask the server for both lists
    void rebuildLists(const QStringList& iShareWith,
                      const QStringList& sharedWithMe);
    void openCompare(const QString& user, const QJsonObject& peerBlob);
    static void clearLayout(QVBoxLayout* layout);

    ShareClient*     m_client;
    AppData*         m_myData;  // live: CompareDialog v2 PLANS on it
    TrackerService*  m_tracker; // rides along for EventDialog inside compare
    QString          m_myName;  // rides along for the compare headers

    QLineEdit*   m_nameEdit    = nullptr;
    QPushButton* m_shareBtn    = nullptr;
    QVBoxLayout* m_iShareList  = nullptr; // rows: name + Remove
    QVBoxLayout* m_withMeList  = nullptr; // rows: name + Compare
    QLabel*      m_status      = nullptr;
};
