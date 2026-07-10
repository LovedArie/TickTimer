#pragma once

#include "ShareClient.h"

#include <QDialog>

class AppData;
class QLabel;
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
// It holds a const AppData* — MY data — purely to hand it onward to the
// CompareDialog, which needs both sides of the comparison. const because a
// sharing screen has no business mutating the planner; the compiler enforces
// what the design intends (the A2 lesson, applied again).
// ---------------------------------------------------------------------------

class SharingDialog : public QDialog
{
    Q_OBJECT
public:
    SharingDialog(ShareClient* client, const AppData* myData,
                  QWidget* parent = nullptr);

private:
    void refresh();                 // ask the server for both lists
    void rebuildLists(const QStringList& iShareWith,
                      const QStringList& sharedWithMe);
    void openCompare(const QString& user, const QJsonObject& peerBlob);
    static void clearLayout(QVBoxLayout* layout);

    ShareClient*   m_client;
    const AppData* m_myData;

    QLineEdit*   m_nameEdit    = nullptr;
    QPushButton* m_shareBtn    = nullptr;
    QVBoxLayout* m_iShareList  = nullptr; // rows: name + Remove
    QVBoxLayout* m_withMeList  = nullptr; // rows: name + Compare
    QLabel*      m_status      = nullptr;
};
