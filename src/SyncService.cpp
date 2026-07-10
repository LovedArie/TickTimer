#include "SyncService.h"

#include "AppData.h"
#include "JsonStore.h"
#include "SyncPlan.h"

#include <QSettings>

SyncService::SyncService(AppData* data, SyncClient* client,
                         const QString& accountKey, QObject* parent)
    : QObject(parent)
    , m_data(data)
    , m_client(client)
{
    // "sync/lastRevision" globally, or "sync/<user>/lastRevision" per account.
    const QString base = accountKey.trimmed().isEmpty()
                             ? QStringLiteral("sync/")
                             : QStringLiteral("sync/") + accountKey.trimmed().toLower()
                                   + QStringLiteral("/");
    m_revKey   = base + QStringLiteral("lastRevision");
    m_dirtyKey = base + QStringLiteral("dirty");

    QSettings settings;
    m_lastRevision = settings.value(m_revKey, 0).toInt();
    // First-run default is DIRTY on purpose: a device that has never synced
    // must assume its local data matters. Consequences: a machine with
    // existing data pushes it up on first sync (right); a brand-new second
    // device meets a one-time conflict prompt and picks "use server version"
    // (safe — one question beats one silent overwrite).
    m_dirty = settings.value(m_dirtyKey, true).toBool();

    // Every domain mutation already announces itself — sync just listens.
    // The m_applying guard keeps our OWN pull-apply from marking us dirty.
    connect(m_data, &AppData::changed, this, [this]() {
        if (!m_applying)
            setDirty(true);
    });
}

void SyncService::syncNow()
{
    if (m_busy)
        return; // one sync at a time; the button is disabled anyway (braces)
    m_busy = true;

    emit statusChanged(tr("Contacting server…"));
    // Every sync STARTS with a pull, whatever ends up happening — the truth
    // table needs the server's revision before it can say anything.
    connect(m_client, &SyncClient::pullFinished,
            this, &SyncService::onPullFinished,
            Qt::UniqueConnection);
    connect(m_client, &SyncClient::pushFinished,
            this, &SyncService::onPushFinished,
            Qt::UniqueConnection);
    m_client->pull();
}

void SyncService::onPullFinished(SyncClient::Outcome outcome, int revision,
                                 const QJsonObject& data)
{
    if (outcome == SyncClient::Outcome::NetworkError) {
        m_busy = false;
        emit finished(false, tr("Can't reach the server. Is it running?"));
        return;
    }
    if (outcome == SyncClient::Outcome::AuthError) {
        m_busy = false;
        emit finished(false, tr("Session expired — restart the app and log "
                                "in again."));
        return;
    }

    switch (sync::decide(revision, m_lastRevision, m_dirty)) {
    case sync::Action::Nothing:
        m_busy = false;
        emit finished(true, tr("Already in sync (revision %1).")
                                .arg(revision));
        return;

    case sync::Action::Push:
        emit statusChanged(tr("Uploading your changes…"));
        m_client->push(JsonStore::toJsonObject(*m_data),
                       /*baseRevision=*/revision, /*force=*/false);
        return;

    case sync::Action::Pull:
        applyServerData(data, revision);
        m_busy = false;
        emit finished(true, tr("Updated from the server (revision %1).")
                                .arg(revision));
        return;

    case sync::Action::Conflict:
        // Hold the server's version so resolveUseServer() doesn't need a
        // second network round-trip, then hand the decision to a human.
        m_heldServerData     = data;
        m_heldServerRevision = revision;
        m_busy = false;
        emit conflictDetected(revision);
        return;
    }
}

void SyncService::onPushFinished(SyncClient::Outcome outcome, int revision)
{
    m_busy = false;

    switch (outcome) {
    case SyncClient::Outcome::Success:
        m_lastRevision = revision;
        setDirty(false);
        persistState();
        emit finished(true, tr("Saved to the server (revision %1).")
                                .arg(revision));
        return;
    case SyncClient::Outcome::Conflict:
        // A race: the server moved between our pull and our push (another
        // device synced in that window). Rare on a home setup; the honest
        // cheap handling is "run the sync again" — the next pull will see
        // the new revision and the truth table takes it from there.
        emit finished(false, tr("The server changed mid-sync — press Sync "
                                "now again."));
        return;
    case SyncClient::Outcome::AuthError:
        emit finished(false, tr("Session expired — restart the app and log "
                                "in again."));
        return;
    case SyncClient::Outcome::NetworkError:
        emit finished(false, tr("Can't reach the server. Is it running?"));
        return;
    }
}

void SyncService::resolveUseServer()
{
    applyServerData(m_heldServerData, m_heldServerRevision);
    m_heldServerData = {};
    emit finished(true, tr("Replaced local data with the server's version "
                           "(revision %1).").arg(m_lastRevision));
}

void SyncService::resolveKeepMine()
{
    m_busy = true;
    emit statusChanged(tr("Overwriting the server with your version…"));
    m_heldServerData = {};
    // force=true: the ONE deliberate bypass of the revision check — legal
    // only here, because a human just chose it with both versions in view.
    m_client->push(JsonStore::toJsonObject(*m_data),
                   m_heldServerRevision, /*force=*/true);
}

void SyncService::applyServerData(const QJsonObject& data, int revision)
{
    // The reentrancy guard: replaceAll announces changed() so every screen
    // rebuilds AND the autosave writes the pulled data to disk — but that
    // same announcement must not re-mark us dirty, or every pull would
    // immediately claim there's something new to push.
    m_applying = true;
    JsonStore::applyJsonObject(*m_data, data, /*announceChange=*/true);
    m_applying = false;

    m_lastRevision = revision;
    setDirty(false);
    persistState();
}

void SyncService::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    persistState();
}

void SyncService::persistState() const
{
    QSettings settings;
    settings.setValue(m_revKey, m_lastRevision);
    settings.setValue(m_dirtyKey, m_dirty);
}
