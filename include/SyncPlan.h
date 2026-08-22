#pragma once

// ---------------------------------------------------------------------------
// syncplan::decide — the ENTIRE sync brain, as a pure function.
//
// Sync engines have a scary reputation, but with full-document sync the
// decision reduces to two questions:
//
//   1. Did the SERVER move since we last synced?   (serverRev != lastSynced)
//   2. Did WE change anything locally since then?   (dirty)
//
// Two booleans → a four-row truth table:
//
//   server moved | local dirty | action
//   -------------+-------------+---------------------------------
//        no      |     no      | Nothing   — already in sync
//        no      |     yes     | Push      — upload our changes
//        yes     |     no      | Pull      — take the server's version
//        yes     |     yes     | Conflict  — a HUMAN must choose
//
// Pure function of its inputs (the isLiveAt / nowProvider lesson again):
// no network, no files, no clock — so the test is the table itself, all
// four rows, and the messy async plumbing in SyncService stays thin wiring
// around a decision that can never surprise us.
// ---------------------------------------------------------------------------

namespace syncplan
{

enum class Action { Nothing, Push, Pull, Conflict };

inline Action decide(int serverRevision, int lastSyncedRevision,
                     bool dirtyLocal)
{
    const bool serverMoved = serverRevision != lastSyncedRevision;
    if (!serverMoved)
        return dirtyLocal ? Action::Push : Action::Nothing;
    return dirtyLocal ? Action::Conflict : Action::Pull;
}

} // namespace syncplanplan
