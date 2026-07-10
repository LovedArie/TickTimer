#pragma once

#include "SyncClient.h"

#include <QJsonObject>
#include <QObject>

class AppData;

// ---------------------------------------------------------------------------
// SyncService — the POLICY layer of sync. SyncClient moves bytes; this class
// decides what they mean, using two pieces of local state:
//
//   - lastSyncedRevision  — the server revision this device last agreed with
//   - dirty               — has AppData changed since then?
//
// Those two feed sync::decide()'s truth table; everything here is the thin
// async wiring around that pure decision. Both are persisted in QSettings —
// they are DEVICE state ("where does this machine stand relative to the
// server"), not domain data, exactly the settings-vs-domain split the app
// already lives by.
//
// The dirty flag rides AppData::changed() — every mutation already announces
// itself, so sync gets change-tracking for free. One guard: while we APPLY a
// pull, changed() fires too (replaceAll announces so the UI rebuilds), and
// without the m_applying flag the pull would mark itself dirty — an infinite
// "always out of sync" loop. Classic reentrancy guard, same family as the
// PopupFocusReason check in LabelEdit.
//
// Conflicts are NEVER resolved silently: decide() says Conflict → we hold the
// server's version aside, emit conflictDetected, and wait for a human to call
// resolveUseServer() or resolveKeepMine(). Losing data is a choice someone
// makes, not something that happens.
// ---------------------------------------------------------------------------

class SyncService : public QObject
{
    Q_OBJECT
public:
    // `accountKey` namespaces this device's sync state per account, so two
    // users on one machine don't share a revision counter or dirty flag.
    // Empty → the legacy global keys (unchanged behaviour for callers that
    // don't pass one).
    SyncService(AppData* data, SyncClient* client,
                const QString& accountKey = QString(),
                QObject* parent = nullptr);

    // One button, one entry point: pull the server's revision, run the truth
    // table, act. Ignored while a sync is already in flight.
    void syncNow();

    // The two conflict resolutions, called by the dialog's buttons.
    void resolveUseServer(); // replace local with the held server version
    void resolveKeepMine();  // force-push local, overwriting the server

    int  lastRevision() const { return m_lastRevision; }
    bool dirty() const        { return m_dirty; }

signals:
    void statusChanged(const QString& message);  // running commentary
    void finished(bool ok, const QString& message);
    void conflictDetected(int serverRevision);

private:
    void onPullFinished(SyncClient::Outcome outcome, int revision,
                        const QJsonObject& data);
    void onPushFinished(SyncClient::Outcome outcome, int revision);
    void applyServerData(const QJsonObject& data, int revision);
    void setDirty(bool dirty);
    void persistState() const;

    // Per-account QSettings prefixes; see the ctor. Held so save and load
    // use the SAME keys.
    QString     m_revKey;
    QString     m_dirtyKey;

    AppData*    m_data;
    SyncClient* m_client;

    int         m_lastRevision = 0;
    bool        m_dirty        = true; // see ctor note on the first-run default
    bool        m_applying     = false;
    bool        m_busy         = false;

    QJsonObject m_heldServerData;      // stashed during a conflict
    int         m_heldServerRevision = 0;
};
