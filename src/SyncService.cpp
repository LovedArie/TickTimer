#include "SyncService.h"

#include <QTimer>

#include "AppData.h"
#include "JsonStore.h"
#include "Merge.h"
#include "SyncPlan.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

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
    m_timeKey  = base + QStringLiteral("lastSyncTime");
    m_basePath = JsonStore::basePathForUser(accountKey);

    QSettings settings;
    m_lastRevision = settings.value(m_revKey, 0).toInt();
    m_lastSyncTime = settings.value(m_timeKey).toDateTime();
    // First-run default is DIRTY on purpose: a device that has never synced
    // must assume its local data matters. Consequences: a machine with
    // existing data pushes it up on first sync (right); a brand-new second
    // device meets a one-time conflict prompt and picks "use server version"
    // (safe — one question beats one silent overwrite).
    m_dirty = settings.value(m_dirtyKey, true).toBool();

    // Every domain mutation already announces itself — sync just listens.
    // The m_applying guard keeps our OWN pull-apply from marking us dirty.
    connect(m_data, &AppData::changed, this, [this]() {
        if (m_applying)
            return; // a pull applying itself is not the user editing
        ++m_generation; // every real change gets a number (the race guard)
        setDirty(true);
        // Auto-sync debounce: restart on every change — the timer only
        // fires after the burst goes quiet. Two deliberate silences:
        // while a sync is in flight (finished/conflict will sort it) and
        // while a conflict is HELD — repeatedly ramming an unresolved
        // conflict would spam conflictDetected at a human who already
        // knows. Resolution re-arms naturally on the next change.
        if (m_autoSync && !m_busy && !hasPendingConflict())
            m_autoTimer->start();
    });

    m_autoTimer = new QTimer(this);
    m_autoTimer->setSingleShot(true);
    connect(m_autoTimer, &QTimer::timeout, this, [this]() {
        if (m_busy) {
            m_autoTimer->start(); // still talking — try again in a moment
            return;
        }
        if (m_dirty && !hasPendingConflict())
            syncNow();
    });
}

void SyncService::setAutoSync(bool enabled, int debounceMs)
{
    m_autoSync = enabled;
    m_autoTimer->setInterval(debounceMs);
    if (!enabled)
        m_autoTimer->stop();
    else if (m_dirty && !hasPendingConflict())
        m_autoTimer->start(); // enabling with unsent edits: catch up soon
}

void SyncService::recordSuccess()
{
    // Every finished(true) funnels through here: the four success exits
    // (already-in-sync, pulled, pushed, conflict-resolved) all mean the
    // same human fact — "this device and the server agreed, just now."
    m_lastSyncTime = QDateTime::currentDateTime();
    QSettings().setValue(m_timeKey, m_lastSyncTime);
}

QJsonObject SyncService::loadBase() const
{
    QFile f(m_basePath);
    if (!f.open(QIODevice::ReadOnly))
        return {}; // no base yet, or it went missing — Merge.h fails safe
    return QJsonDocument::fromJson(f.readAll()).object();
}

void SyncService::saveBase(const QJsonObject& doc) const
{
    // QSaveFile, the same write-then-replace JsonStore uses: a crash
    // mid-write must leave the PREVIOUS base intact rather than half a
    // document, because a truncated base is worse than none — it would look
    // valid and claim things were deleted that never were.
    QSaveFile f(m_basePath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
    f.commit();
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

    switch (syncplan::decide(revision, m_lastRevision, m_dirty)) {
    case syncplan::Action::Nothing:
        m_busy = false;
        // Both sides agree RIGHT NOW, so this document is by definition the
        // base. Writing it here is also the repair path for a base that was
        // lost: the next quiet sync restores it.
        saveBase(data);
        recordSuccess();
        emit finished(true, tr("Already in sync (revision %1).")
                                .arg(revision));
        return;

    case syncplan::Action::Push:
        emit statusChanged(tr("Uploading your changes…"));
        m_pushedGeneration = m_generation; // what THIS snapshot contains
        m_pushedDoc = JsonStore::toJsonObject(*m_data);
        m_client->push(m_pushedDoc, /*baseRevision=*/revision,
                       /*force=*/false);
        return;

    case syncplan::Action::Pull:
        applyServerData(data, revision);
        m_busy = false;
        saveBase(data);
        recordSuccess();
        emit finished(true, tr("Updated from the server (revision %1).")
                                .arg(revision));
        return;

    case syncplan::Action::Conflict: {
        // v31.2 — MERGE BEFORE ASKING. "Both sides changed" almost never
        // means both sides changed the SAME thing: the owner's real case was
        // tasks added on a phone and schedules added on a desktop, touching
        // nothing in common. Making a person throw one device's day away for
        // that was the defect.
        //
        // So a three-way merge runs first (Merge.h), against the document
        // both sides last agreed on. Only entities edited on both sides —
        // or edited here and deleted there — survive as questions.
        const QJsonObject base  = loadBase();
        const QJsonObject local = JsonStore::toJsonObject(*m_data);
        const merge::Result mine   = merge::plan(base, local, data, false);
        // IS THERE A QUESTION AT ALL? Not the same as "is anything
        // clashing". When every clash is an edit-vs-delete, plan() ignored
        // preferServer on all of them, so the two candidate documents are
        // byte-identical and the two buttons offer one outcome twice.
        // Raising a modal there asks a question whose answer is thrown away.
        if (!merge::anyDecidedByPreference(mine.clashes)) {
            // Nothing contested: adopt the merge and send it up. This is the
            // path that turns yesterday's "pick a device" into no question
            // at all.
            //
            // AND HOLD NOTHING. The held-conflict fields used to be assigned
            // above this branch, before it was known whether a question
            // would be asked - so a conflict that resolved itself silently
            // still left m_heldServerRevision standing, and
            // hasPendingConflict() reads exactly that field. The service
            // then behaved as though a decision were outstanding forever:
            // auto-sync gated shut, the button stuck on warning, the
            // resolution box on every open. That is the failure
            // clearHeldConflict() exists for, re-entered through a new door,
            // and the fix is the same shape as its lesson - a field with two
            // masters needs ONE janitor, so nothing is held until the path
            // that needs it is the path being taken.
            applyServerData(mine.merged, revision);
            m_pushedGeneration = m_generation;
            m_pushedDoc = mine.merged;
            emit statusChanged(tr("Merging both devices…"));
            m_client->push(m_pushedDoc, /*baseRevision=*/revision,
                           /*force=*/true);
            return;
        }

        // Only now is a conflict genuinely HELD, so only now is the state
        // that describes one worth computing: `theirs` is the second full
        // merge, and nothing above needed it.
        const merge::Result theirs = merge::plan(base, local, data, true);
        m_heldServerData     = data;
        m_heldServerRevision = revision;
        m_mergedPreferringMine   = mine.merged;
        m_mergedPreferringServer = theirs.merged;
        m_clashes = mine.clashes;

        m_busy = false;
        emit conflictDetected(revision);
        return;
    }
    }
}

void SyncService::onPushFinished(SyncClient::Outcome outcome, int revision)
{
    m_busy = false;

    switch (outcome) {
    case SyncClient::Outcome::Success:
        m_lastRevision = revision;
        // The race guard pays off here: only mark clean if the pushed snapshot
        // is STILL the latest generation. An edit that landed while the push
        // was on the wire keeps us dirty — it hasn't left the machine yet,
        // and the auto-sync debounce will carry it on the next beat.
        setDirty(m_generation != m_pushedGeneration);
        // What we just put on the server is now what both sides agree on.
        saveBase(m_pushedDoc);
        // ...and if it DID stay dirty, re-arm the debounce ourselves: the
        // mid-flight change couldn't arm it (the !m_busy guard), and a
        // dirty flag with no timer is a stranded edit.
        if (m_autoSync && m_dirty && !hasPendingConflict())
            m_autoTimer->start();
        persistState();
        recordSuccess();
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

bool SyncService::hasContestedClash() const
{
    return merge::anyDecidedByPreference(m_clashes);
}

void SyncService::clearHeldConflict()
{
    // v31.2: the merged candidates and the clash list are held-conflict
    // state too, and this function exists precisely because a field that
    // outlived its conflict once poisoned the service forever. New state,
    // same janitor.
    m_mergedPreferringMine   = QJsonObject();
    m_mergedPreferringServer = QJsonObject();
    m_clashes.clear();
    // Held-conflict state dies WHOLE. This function exists because it once
    // didn't: both resolve paths cleared the held DATA but left the held
    // REVISION standing (the force-push needed it as its base, and nothing
    // else read it — then hasPendingConflict() was bolted onto that exact
    // field without auditing its writes). One conflict then poisoned the
    // service forever: auto-sync gated shut, ⚠ lit, the resolution box on
    // every open, resolved or not. Lesson: give old state a NEW consumer,
    // audit EVERY write site — a flag with two masters needs one janitor.
    m_heldServerData     = {};
    m_heldServerRevision = 0;
}

void SyncService::resolveUseServer()
{
    const int heldRev = m_heldServerRevision;
    // The MERGED document with the server winning the contested rows — not
    // the server's raw version. Everything both devices added independently
    // survives either way now; the choice is only about what genuinely
    // collided.
    const QJsonObject held = m_mergedPreferringServer.isEmpty()
                                 ? m_heldServerData
                                 : m_mergedPreferringServer;
    clearHeldConflict(); // the decision is made; the conflict is OVER
    applyServerData(held, heldRev);
    saveBase(held);
    recordSuccess();
    emit finished(true, tr("Merged, keeping the server's version of the "
                           "items that clashed (revision %1).")
                            .arg(m_lastRevision));
}

void SyncService::resolveKeepMine()
{
    m_pushedGeneration = m_generation; // force-push snapshots too
    m_busy = true;
    emit statusChanged(tr("Overwriting the server with your version…"));
    // The force push needs the held revision as its base — capture it,
    // THEN clear the conflict whole. (The old order — clear data, keep
    // revision for the push — is exactly how the stale flag was born.)
    const int base = m_heldServerRevision;
    const QJsonObject merged = m_mergedPreferringMine;
    clearHeldConflict();
    // Adopt the merge locally too, so this device keeps what the OTHER one
    // added — "keep mine" now means "mine wins the clashes", not "discard
    // the other device".
    if (!merged.isEmpty()) {
        applyServerData(merged, base);
        m_pushedDoc = merged;
    } else {
        m_pushedDoc = JsonStore::toJsonObject(*m_data);
    }
    // force=true: the ONE deliberate bypass of the revision check — legal
    // only here, because a human just chose it with both versions in view.
    m_client->push(m_pushedDoc, base, /*force=*/true);
}

void SyncService::applyServerData(const QJsonObject& data, int revision)
{
    // The reentrancy guard: replaceAll announces changed() so every screen
    // rebuilds AND the autosave writes the pulled data to disk — but that
    // same announcement must not re-mark us dirty, or every pull would
    // immediately claim there's something new to push.
    m_applying = true;
    const bool applied =
        JsonStore::applyJsonObject(*m_data, data, /*announceChange=*/true);
    m_applying = false;

    if (!applied) {
        // v31, the format floor: the server is holding a planner written by a
        // NEWER TickTimer than this one. Refuse the whole exchange rather
        // than half of it — recording the revision here would tell the next
        // push that we are up to date with a document we never applied, and
        // that push would overwrite it with our older shape. This is the same
        // defect as the one the floor exists for, arriving down the wire.
        emit finished(false,
                      tr("This device is running an older TickTimer than the "
                         "one that last synced. Update TickTimer — nothing "
                         "has been changed here or on the server."));
        return;
    }

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
