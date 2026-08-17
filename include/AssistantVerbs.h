#pragma once
// ---------------------------------------------------------------------------
// AssistantVerbs — the write boundary (v29.0, Slice 1: model-less).
//
// The assistant addendum §B, made code. Four stages: PROPOSAL (a value of
// the closed type below — anything not expressible here cannot be asked
// for), VALIDATION (this file, funnelling into AppData's already-guarded
// doors), CONFIRMATION (ProposalCard — the owner's tap), EFFECT (apply(),
// which is the only function here that mutates, and only through doors 86+
// domain tests already defend).
//
// Slice 1 ships ONE verb and NO model: every Proposal in this version is
// composed by C++ (the debug panel's injector) — so the boundary machinery
// is built, wired, and tested before any model output ever enters it. The
// model, when it arrives (Slice 2), is a new CALLER of an old, guarded
// path — not a new path.
//
// THE PER-ROLE RULE (§B.4): verbs are scoped per role, born that way
// because retrofitting costs what the flat shape saves. Nudge and check-in
// get EMPTY lists — "the 08:00 toast rearranged my afternoon" is
// structurally impossible, and a prompt-injection landing in a nudge has
// nothing to reach for. Note Role here is NOT ai::Feature: Feature is
// ROUTING (which seats may answer) and deliberately lacks Nudge; Role is
// TRUST (which call sites may write). Different axes, different enums —
// folding them would let a routing edit widen a trust scope by accident.
//
// HANDLES, NOT IDS (§B.2): the briefing strips UUIDs and must keep doing
// so — models invent plausible-looking UUIDs, but they do not invent "T7"
// when only T1–T3 exist, and if one does, the lookup fails SAFELY (empty
// id, refused proposal) instead of hitting the wrong task. HandleMap is
// per-briefing-turn and lives with the turn, never persisted.
// ---------------------------------------------------------------------------

#include <QDate>
#include <QString>
#include <QVector>

class AppData;

namespace verbs
{

// The trust axis. One scope per PLACE the assistant speaks from.
enum class Role
{
    Chat,    // conversational turns — no write verbs in Slice 1 (§K:
             // intake is the first, and it is its own role below)
    Nudge,   // the deadline heads-up — observes and phrases, forever
    CheckIn, // the morning knock — observes and phrases, forever
    Intake,  // the detail interview (§K) — the FIRST writing role
};

// The closed verb set. Growing this enum is the ONLY way the assistant
// gains a capability, which is what makes a diff of this file the complete
// security review.
enum class Verb
{
    SetTaskDetails, // additive fill of a task's ABSENT sizing facts:
                    // estimate (0 = unset) and/or due date (invalid =
                    // "DATE TBD"). Priority is deliberately NOT here — it
                    // has no absence state (Medium is a value, not a
                    // blank), so "additive" is undefined for it.
};

// role → allowed verbs. The whole allow-list, readable in one screen.
QVector<Verb> verbsFor(Role role);

// Per-turn handle registry: index i ↔ "T{i+1}", built in briefing print
// order, deduplicated (a task printed in two sections keeps one handle).
struct HandleMap
{
    QVector<QString> ids;

    // Registers (or finds) an id; returns its handle ("T3").
    QString add(const QString& id);

    // "" for anything unknown — the fail-safe §B.2 exists for. Accepts
    // exactly the shape we print: 'T' + 1-based index.
    QString idFor(const QString& handle) const;

    bool isEmpty() const { return ids.isEmpty(); }
};

// One proposed change. Absent fields use the domain's own absence idioms
// (0-estimate, invalid QDate) — the same convention Task itself follows,
// so "not proposed" and "not set" read identically everywhere.
struct Proposal
{
    Verb    verb = Verb::SetTaskDetails;
    QString targetHandle;        // "T1" — resolved via HandleMap, never an id
    int     estimateMinutes = 0; // 0 = not proposed
    QDate   dueDate;             // invalid = not proposed

    // The card's text, composed HERE from the structured fields — never
    // from the proposer's prose. What you approve is what will run, not
    // what the proposer claims will run.
    QString summary(const AppData& data, const HandleMap& handles) const;
};

// Validation verdict, reason in the owner's language (it goes on the card).
struct Verdict
{
    bool    ok = false;
    QString reason;
};

// The gate. Checks, in order: the role may use this verb; the handle
// resolves; the target is open (not done, not archived); at least one
// field is proposed; every proposed field is currently ABSENT (the
// additive rule, §K.5); proposed values are sane. Pure — mutates nothing.
Verdict validate(const AppData& data, const HandleMap& handles, Role role,
                 const Proposal& p);

// The only mutator. RE-VALIDATES before touching anything: the world can
// change between the card rendering and the tap (the owner may have filled
// the estimate by hand meanwhile), and a stale Apply must refuse politely
// rather than overwrite. On ok, funnels through AppData's existing doors
// (setTaskSize preserving the task's current chunkable; setTaskDueDate) —
// this file adds NO new mutation capability to the domain.
Verdict apply(AppData& data, const HandleMap& handles, Role role,
              const Proposal& p);

} // namespace verbs
