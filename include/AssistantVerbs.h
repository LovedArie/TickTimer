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

#include "MissedBlocks.h" // missed::Rule — validation judges the target
#include "Reschedule.h"   // reschedule::Context — and re-runs the search

#include <QDate>
#include <QDateTime>
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

    MoveBlock,      // v29.2 — propose a new home for a block that ALREADY
                    // DIDN'T HAPPEN. Two fences make this small: the target
                    // must be one the domain judges missed (a block you may
                    // still do is a plan you are living inside, and an
                    // assistant that may move it is a calendar editor), and
                    // the placement must be one reschedule::propose() just
                    // offered — the model selects, it never invents a time.
                    // Not additive, and it doesn't need to be: the door it
                    // uses APPENDS a replacement and annotates the original,
                    // so nothing is overwritten (addendum §E).

    UndoMove,       // v30.1 — MoveBlock's inverse, and the reason §B.1 can
                    // promise no undo button: "every verb has an inverse it
                    // can ALSO call". AppData::undoReschedule existed from
                    // v29.2 and had no caller until now, which made that
                    // promise false for two versions.
                    //
                    // IT CARRIES NO TARGET. The model asks for the undo; C++
                    // decides which move, from World::undoableMoveId. That is
                    // not convenience — a moved block is no longer missed, so
                    // it has no handle at all (the block namespace is exactly
                    // the set of legal MoveBlock targets, DayBriefing.cpp),
                    // and inventing a way to name one would have widened that
                    // namespace to make an undo expressible. The narrower
                    // reading is also the safer one: the model cannot aim
                    // this verb, by construction rather than by rule.
};

// role → allowed verbs. The whole allow-list, readable in one screen.
QVector<Verb> verbsFor(Role role);

// Per-turn handle registry, built in briefing print order and deduplicated
// (a thing printed in two sections keeps one handle).
//
// TWO NAMESPACES, not one counter (v29.2). Tasks are "T{i+1}", blocks are
// "B{i+1}", and they are separate vectors because a handle that could name
// either kind is precisely the ambiguity §B.2 exists to prevent: the model
// would see [T4] with no way to know whether it named a task or a planned
// block, and resolution would have to guess. Which namespace a proposal
// resolves in is decided by its VERB, never by parsing — so the two can
// never be confused at a call site either.
struct HandleMap
{
    QVector<QString> taskIds;
    QVector<QString> blockIds;

    // Register (or find) an id; returns its handle — "T3", "B2".
    QString addTask(const QString& id);
    QString addBlock(const QString& id);

    // "" for anything unknown — the fail-safe §B.2 exists for. Each accepts
    // exactly the shape it prints and nothing else, so an invented handle,
    // a handle from the other namespace, or a malformed one all resolve to
    // "" and die in validate() with a readable reason.
    QString taskIdFor(const QString& handle) const;
    QString blockIdFor(const QString& handle) const;

    bool isEmpty() const { return taskIds.isEmpty() && blockIds.isEmpty(); }

    // Forget everything. A door rather than a field poke because the map is
    // rebuilt per turn and MUST NOT leak the last one's world — with two
    // vectors, clearing "the ids" by hand is one namespace away from a
    // handle that resolves to a block nobody mentioned this turn.
    void clear() { taskIds.clear(); blockIds.clear(); }
};

struct World; // defined below; summary() and validate() both need it

// One proposed change. Absent fields use the domain's own absence idioms
// (0-estimate, invalid QDate) — the same convention Task itself follows,
// so "not proposed" and "not set" read identically everywhere.
struct Proposal
{
    Verb    verb = Verb::SetTaskDetails;
    // "T1" for task verbs, "B1" for block verbs — resolved via HandleMap,
    // never an id. WHICH namespace is decided by `verb`, never by parsing
    // the string, so the two can't be confused at a call site.
    QString targetHandle;

    // ---- SetTaskDetails ----
    int   estimateMinutes = 0; // 0 = not proposed
    QDate dueDate;             // invalid = not proposed

    // ---- MoveBlock ----
    // Where the block would go. Deliberately the CONCRETE placement rather
    // than an index into the offered list: an index is a handle with no
    // fail-safe property, and the list is recomputed at the tap (§E), so
    // index 2 in the new list may name a different option than index 2 in
    // the old one — a silent wrong-target write instead of a refusal. A
    // placement fails CLOSED: no longer offered, no longer valid.
    QDate newDate;
    int   newStartMinutes = 0;
    int   newEndMinutes   = 0; // exclusive, same convention as Event

    // The card's text, composed HERE from the structured fields — never
    // from the proposer's prose. What you approve is what will run, not
    // what the proposer claims will run.
    //
    // Takes the World (v30.1) because UndoMove's target is not in the
    // Proposal at all — it is the caller's, so naming the block the card
    // will put back requires the same value validate() judges against. A
    // card that could not name its target would be asking for a blank
    // cheque.
    QString summary(const AppData& data, const HandleMap& handles,
                    const World& world) const;
};

// What validation needs to know beyond AppData (v29.2).
//
// It exists because MoveBlock's central check — "is this placement one the
// search would actually offer?" — cannot be answered from stored data alone:
// it needs the clock, the rule for judging a block missed, and the same
// search constraints the proposer used. Bundled into one value rather than
// three trailing parameters so that adding a fourth later is not another
// signature change at every call site.
//
// The `reschedule` context must match the one the PROPOSER searched with, or
// the two will compute different option sets and honest proposals will be
// refused. The caller owning both is what keeps them in step — which is why
// this is passed in rather than defaulted here: the agenda window is a
// preference, and preferences are not domain knowledge.
// Note what is NOT the caller's to set: `reschedule.deadline` is derived from
// the target block's own task during validation. Policy comes from the caller
// (the agenda window, the horizon — preferences); facts come from the domain.
struct World
{
    QDateTime           now;
    missed::Rule        missedRule;
    reschedule::Context reschedule;

    // v30.1 — which move UndoMove would take back: the original block's id,
    // or empty for "there is nothing to undo".
    //
    // IT LIVES HERE, and not on Proposal, deliberately. Proposal is built
    // from the model's own reply by scrub::, so an id field on it would sit
    // one careless edit away from being model-aimable. World is assembled by
    // the CALLER, every time, out of what the caller itself did — so no
    // reply, however constructed, can influence this. The model asks for an
    // undo; it never says of what.
    //
    // The caller also owns the SCOPE (ChatPage: the last MoveBlock this
    // conversation applied, cleared when used). Kept out of the domain on
    // purpose — "what the assistant just did" is a fact about a
    // conversation, not about a planner.
    QString             undoableMoveId;
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
                 const Proposal& p, const World& world);

// The only mutator. RE-VALIDATES before touching anything: the world can
// change between the card rendering and the tap (the owner may have filled
// the estimate by hand meanwhile), and a stale Apply must refuse politely
// rather than overwrite. On ok, funnels through AppData's existing doors
// (setTaskSize preserving the task's current chunkable; setTaskDueDate) —
// this file adds NO new mutation capability to the domain.
Verdict apply(AppData& data, const HandleMap& handles, Role role,
              const Proposal& p, const World& world);

} // namespace verbs
