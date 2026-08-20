#pragma once
// ---------------------------------------------------------------------------
// scrub — pulling a structured proposal out of a conversational reply (v29.2).
//
// Slice 2's intake extraction could not be reused here, and the reason is
// worth stating: intake is a MODE. The question is asked, the whole reply is
// expected to be JSON, and prose would be a malfunction. Chat is a
// conversation — the model has to answer in sentences AND, sometimes, ask to
// change something. So the proposal rides inside the reply and this file
// takes it back out.
//
// THE SHAPE:
//
//     Sure — that Tuesday slot is the cleanest swap, it keeps the
//     block before your Thursday deadline.
//
//     {"move": {"block": "B1", "date": "2026-07-21",
//               "start": "09:00", "end": "10:00"}}
//
// The object is removed; the sentences become the chat bubble; the fields
// become a verbs::Proposal that must still cross the card. Precedent for
// removing a structured region before display is the <think> scrub that
// reasoning models already get — this is the same move on the other end of
// the reply.
//
// WHY IT IS ITS OWN FILE, not part of AssistantVerbs.h: that header's value
// is that a diff of it IS the complete security review. A reply parser
// grants no capability — it can only ever produce a Proposal that validate()
// then judges exactly as it judges a C++-composed one — so keeping it out
// preserves the signal in the file that matters.
//
// EVERY FAILURE DEGRADES TO "no proposal". Malformed JSON, a missing field,
// an unparseable clock, an object that isn't about a move: all of them mean
// the reply was just conversation. Nothing here can fail INTO a write, which
// is the property that lets a parser sit on the model's side of the boundary
// at all.
//
// Pure: a string in, values out. No AppData, no network, no clock — so the
// whole contract is asserted offline in microseconds.
// ---------------------------------------------------------------------------

#include <QDate>
#include <QString>

namespace scrub
{

struct MoveReply
{
    // The reply with the proposal object removed, ready to display. Always
    // set — when there is no proposal this is the input, trimmed.
    QString prose;

    bool    hasMove = false;
    QString blockHandle;      // "B1" — resolved later, against the turn's map
    QDate   date;
    int     startMinutes = 0; // minutes after midnight, Event's convention
    int     endMinutes   = 0; // exclusive

    // v30.1 — `{"undo_move": {}}`. Note what is NOT beside it: no handle, no
    // date, no fields of any kind, because UndoMove has no target the model
    // may name. C++ decides which move from verbs::World, so there is
    // deliberately nowhere here for a reply to put one. This struct keeps
    // its name: an undo is still a statement about a move.
    bool    hasUndo = false;

    // True only if every field a Proposal needs actually arrived. A partial
    // object is not half a proposal; it is prose that happened to contain
    // braces. An undo needs nothing, so its presence IS its completeness.
    bool complete() const
    {
        if (hasUndo)
            return true;
        return hasMove && !blockHandle.isEmpty() && date.isValid()
               && endMinutes > startMinutes;
    }
};

// Find the LAST proposal object in `reply` (models sometimes restate; the
// final word is the one they meant), strip it, and read its fields.
MoveReply moveFromReply(const QString& reply);

} // namespace scrub
