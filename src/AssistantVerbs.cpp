#include "AssistantVerbs.h"

#include "AppData.h"
#include "Task.h"

#include <QObject> // QObject::tr for owner-facing verdict reasons

namespace verbs
{

QVector<Verb> verbsFor(Role role)
{
    // The entire allow-list. That this switch fits on one screen is the
    // point: reviewing the assistant's reach is reading one function.
    switch (role) {
    case Role::Intake:
        return { Verb::SetTaskDetails };
    case Role::Chat:
        // v29.2: Chat's first write verb. Widened deliberately (addendum
        // §F): the gesture people actually have is a sentence, and a verb
        // reachable only from a drawer they must first open is one they must
        // first remember.
        //
        // v30.1 adds UndoMove — which is not a second capability so much as
        // the first one's inverse. §B.1 promised no undo button on the
        // grounds that every verb has an inverse the assistant can also
        // call; MoveBlock's had no caller for two versions, so this pair is
        // what makes that sentence true rather than aspirational.
        return { Verb::MoveBlock, Verb::UndoMove };
    case Role::Nudge:
    case Role::CheckIn:
        return {}; // observe and phrase — FOREVER. A toast that can
                   // rearrange your afternoon is the thing this file
                   // exists to make structurally impossible, and a
                   // prompt-injection landing here has nothing to reach for.
    }
    return {};
}

namespace
{
// The two namespaces share ONE implementation on purpose: the fail-safe
// properties (§B.2) must hold identically for blocks and tasks, and two
// hand-written copies are two chances for them to drift apart. The prefix
// and the vector are the only things that vary.
QString registerHandle(QVector<QString>& ids, const QString& id, QChar prefix)
{
    // Dedup first: a thing printed in two briefing sections keeps ONE
    // handle, or the model would see [T2] and [T9] naming the same task
    // and reasonably treat them as two.
    const int existing = ids.indexOf(id);
    if (existing >= 0)
        return QStringLiteral("%1%2").arg(prefix).arg(existing + 1);
    ids.append(id);
    return QStringLiteral("%1%2").arg(prefix).arg(ids.size());
}

QString resolveHandle(const QVector<QString>& ids, const QString& handle,
                      QChar prefix)
{
    // Accept exactly what we print — the prefix + a 1-based index — and
    // nothing else. Anything malformed, out of range, inventive, or from
    // the OTHER namespace resolves to "" and dies in validate() with a
    // readable reason. Fail safe, not fuzzy: guessing "t 3 " probably means
    // T3 would reintroduce the exact class of wrong-target bug handles
    // exist to prevent.
    if (handle.size() < 2 || !handle.startsWith(prefix))
        return QString();
    bool numeric = false;
    const int n = handle.mid(1).toInt(&numeric);
    if (!numeric || n < 1 || n > ids.size())
        return QString();
    return ids.at(n - 1);
}
} // namespace

QString HandleMap::addTask(const QString& id)
{
    return registerHandle(taskIds, id, QLatin1Char('T'));
}

QString HandleMap::addBlock(const QString& id)
{
    return registerHandle(blockIds, id, QLatin1Char('B'));
}

QString HandleMap::taskIdFor(const QString& handle) const
{
    return resolveHandle(taskIds, handle, QLatin1Char('T'));
}

QString HandleMap::blockIdFor(const QString& handle) const
{
    return resolveHandle(blockIds, handle, QLatin1Char('B'));
}

namespace
{
// "09:30". Local rather than brief::clockLabel: the verb layer formatting a
// clock is not a reason to depend on the briefing layer, and the duplication
// is four lines against a new edge in the include graph.
QString clockLabel(int minutesAfterMidnight)
{
    return QStringLiteral("%1:%2")
        .arg(minutesAfterMidnight / 60, 2, 10, QLatin1Char('0'))
        .arg(minutesAfterMidnight % 60, 2, 10, QLatin1Char('0'));
}

// Does this placement appear among the options the search would offer right
// now? THE MoveBlock check, factored because validate() and apply() must ask
// it identically — apply() re-asks at the tap, and a second hand-written
// copy is where the two would drift.
//
// Split is excluded here and not merely unmatched: a multi-piece option has
// no single placement to compare against, and the verb is fenced to
// single-replacement kinds because those are the ones undoReschedule can
// invert (addendum §H.2, §I).
bool placementIsOffered(const AppData& data, const Event& block,
                        const Proposal& p, const World& world)
{
    const missed::Verdict v = missed::judge(block, world.missedRule, world.now);

    // The deadline is a property of THIS block, not of the caller's world, so
    // it is derived here rather than trusted from the World. The caller
    // supplies policy (the agenda window, the horizon — preferences, which
    // are not domain knowledge); the domain supplies the fact. A caller that
    // passed the wrong deadline would silently shift which options exist,
    // and every proposal would be judged against a different search than the
    // one that produced it.
    reschedule::Context ctx = world.reschedule;
    ctx.now = world.now;
    if (const Task* t = data.taskById(block.taskId))
        ctx.deadline = coverage::deadlineOf(*t, world.now.date());

    const QVector<reschedule::Option> options =
        reschedule::propose(block, v, data.events(), ctx);

    for (const reschedule::Option& o : options) {
        if (o.kind == reschedule::Kind::Split || o.kind == reschedule::Kind::Bump)
            continue; // out of scope by design, not by omission
        if (o.pieces.size() != 1)
            continue;
        const reschedule::Piece& piece = o.pieces.first();
        if (piece.date == p.newDate && piece.startMinutes == p.newStartMinutes
            && piece.endMinutes == p.newEndMinutes)
            return true;
    }
    return false;
}
} // namespace

QString Proposal::summary(const AppData& data, const HandleMap& handles,
                          const World& world) const
{
    if (verb == Verb::UndoMove) {
        // Named from the WORLD, because that is where this verb's target
        // lives. The card must say which block goes back and where to — an
        // undo card that said only "undo the move" would be asking for a
        // blank cheque, and the whole point of the card is that what you
        // approve is what will run.
        const Event* e = data.eventById(world.undoableMoveId);
        if (!e)
            return QObject::tr("Take back the last move");
        return QObject::tr("Put '%1' back on %2, %3–%4")
            .arg(data.eventLabel(*e),
                 e->date.toString(QStringLiteral("ddd d MMM")),
                 clockLabel(e->plannedStartMinutes),
                 clockLabel(e->plannedEndMinutes));
    }

    if (verb == Verb::MoveBlock) {
        const Event* e = data.eventById(handles.blockIdFor(targetHandle));
        const QString label =
            e ? data.eventLabel(*e)
              : QObject::tr("(unknown block %1)").arg(targetHandle);
        return QObject::tr("Move '%1' → %2, %3–%4")
            .arg(label,
                 newDate.toString(QStringLiteral("ddd d MMM")),
                 clockLabel(newStartMinutes),
                 clockLabel(newEndMinutes));
    }

    // Composed from the STRUCTURED fields — the card never displays the
    // proposer's own prose as the description of what will happen.
    const Task* t = data.taskById(handles.taskIdFor(targetHandle));
    const QString title =
        t ? t->title : QObject::tr("(unknown task %1)").arg(targetHandle);

    QStringList parts;
    if (estimateMinutes > 0) {
        const int h = estimateMinutes / 60;
        const int m = estimateMinutes % 60;
        QString span;
        if (h > 0 && m > 0)
            span = QObject::tr("%1h %2m").arg(h).arg(m);
        else if (h > 0)
            span = QObject::tr("%1h").arg(h);
        else
            span = QObject::tr("%1m").arg(m);
        parts << QObject::tr("estimate %1").arg(span);
    }
    if (dueDate.isValid())
        parts << QObject::tr("due %1").arg(dueDate.toString(Qt::ISODate));

    return QObject::tr("Set '%1' — %2")
        .arg(title, parts.join(QObject::tr(", ")));
}

namespace
{
// MoveBlock's own gate. Split out rather than threaded through the task
// checks with if-verb branches: the two verbs share only the role check,
// and interleaving them is how a check meant for one silently starts
// guarding the other.
Verdict validateMove(const AppData& data, const HandleMap& handles,
                     const Proposal& p, const World& world)
{
    // The handle resolves — in the BLOCK namespace, chosen by the verb.
    const QString id = handles.blockIdFor(p.targetHandle);
    if (id.isEmpty())
        return { false, QObject::tr("'%1' doesn't refer to any block in "
                                    "this conversation.")
                            .arg(p.targetHandle) };

    const Event* block = data.eventById(id);
    if (!block)
        return { false, QObject::tr("That block no longer exists.") };

    // Already dealt with. Re-moving a block that was moved would chain
    // replacements and leave the first link describing a world two steps
    // back; Done and Dropped are decisions the owner already made.
    if (block->outcome != BlockOutcome::Unset)
        return { false, QObject::tr("That block already has a decision.") };

    // THE fence (addendum §B). A block you might still do is a plan you are
    // living inside, and an assistant that may move it is a calendar editor.
    // Only a block the domain itself judges missed is in scope.
    const missed::Verdict v = missed::judge(*block, world.missedRule, world.now);
    if (v.reason == missed::Reason::None)
        return { false, QObject::tr("That block hasn't been missed — it's "
                                    "still yours to do.") };

    if (!p.newDate.isValid() || p.newEndMinutes <= p.newStartMinutes)
        return { false, QObject::tr("That isn't a usable time.") };

    // THE check (addendum §C): the model selects, it never invents. If the
    // search wouldn't offer this placement right now, it is refused —
    // which is also how a stale card fails, safely, at the tap.
    if (!placementIsOffered(data, *block, p, world))
        return { false, QObject::tr("That slot isn't one of the options for "
                                    "this block any more.") };

    return { true, QString() };
}

// UndoMove's gate. Its own function for the reason validateMove is: the
// checks share only the role test, and interleaving them is how a check
// meant for one silently starts guarding the other.
//
// Note what it does NOT read: the Proposal. This verb has no target, no
// placement and no fields — everything it needs is in the World, which the
// caller built. There is nothing here a reply could steer.
Verdict validateUndo(const AppData& data, const World& world)
{
    if (world.undoableMoveId.isEmpty())
        return { false, QObject::tr("There's no move of mine to take back in "
                                    "this conversation.") };

    const Event* block = data.eventById(world.undoableMoveId);
    if (!block)
        return { false, QObject::tr("That block no longer exists.") };

    // Already undone, or re-decided by hand since. Either way there is
    // nothing to reverse, and saying so beats acting on a stale belief.
    if (block->outcome != BlockOutcome::Moved)
        return { false, QObject::tr("That move has already been taken back.") };

    // The refusal that protects a FACT rather than a pointer, checked here
    // so the card can say so before the tap rather than failing at it.
    // undoReschedule refuses the same case; this is the first of the two
    // verdicts, not a substitute for the second.
    //
    // Every replacement, not just the first — a split's later piece holding
    // real time is exactly the case the pre-v29.3 single link could not even
    // ask about.
    for (const QString& replacementId : block->movedToIds) {
        const Event* replacement = data.eventById(replacementId);
        if (replacement && !replacement->segments.isEmpty())
            return { false, QObject::tr("You've already tracked time against "
                                        "the new block, so putting it back "
                                        "would throw that away.") };
    }

    return { true, QString() };
}
} // namespace

Verdict validate(const AppData& data, const HandleMap& handles, Role role,
                 const Proposal& p, const World& world)
{
    // 1. The role may use this verb at all. First check on purpose: a
    //    forbidden role must not learn, via later error texts, which
    //    handles exist or what state tasks are in.
    if (!verbsFor(role).contains(p.verb))
        return { false, QObject::tr("This assistant role is not allowed to "
                                    "make changes.") };

    if (p.verb == Verb::UndoMove)
        return validateUndo(data, world);

    if (p.verb == Verb::MoveBlock)
        return validateMove(data, handles, p, world);

    // 2. The handle resolves — §B.2's fail-safe firing as a sentence.
    const QString id = handles.taskIdFor(p.targetHandle);
    if (id.isEmpty())
        return { false, QObject::tr("'%1' doesn't refer to anything in "
                                    "this conversation.")
                            .arg(p.targetHandle) };

    const Task* t = data.taskById(id);
    if (!t)
        return { false, QObject::tr("That task no longer exists.") };

    // 3. Open targets only. A done or archived task's details are a
    //    historical record, not a blank to fill.
    if (t->done || t->archived)
        return { false, QObject::tr("'%1' is closed — its details are "
                                    "history now.")
                            .arg(t->title) };

    // 4. Something must actually be proposed.
    if (p.estimateMinutes <= 0 && !p.dueDate.isValid())
        return { false, QObject::tr("Nothing is being proposed.") };

    // 5. The additive rule (§K.5): only ABSENT fields may be filled.
    //    Overwrite is a different verb with an inverse story of its own —
    //    it does not sneak in as a special case of this one.
    if (p.estimateMinutes > 0 && t->estimateMinutes > 0)
        return { false, QObject::tr("'%1' already has an estimate — this "
                                    "verb only fills blanks.")
                            .arg(t->title) };
    if (p.dueDate.isValid() && t->dueDate.isValid())
        return { false, QObject::tr("'%1' already has a due date — this "
                                    "verb only fills blanks.")
                            .arg(t->title) };

    // 6. Sanity. The doors clamp for themselves (setTaskSize floors at 0);
    //    validation exists to refuse with a REASON before the tap, not to
    //    silently repair after it.
    if (p.dueDate.isValid() && p.dueDate < QDate(2000, 1, 1))
        return { false, QObject::tr("That due date is in the distant past "
                                    "— it looks like a mistake.") };

    return { true, QString() };
}

Verdict apply(AppData& data, const HandleMap& handles, Role role,
              const Proposal& p, const World& world)
{
    // Re-validate at the tap, not just at the render: the owner may have
    // filled the estimate by hand while the card sat there, and a stale
    // Apply must refuse politely (the additive check above catches
    // exactly this) rather than trust a verdict from an older world.
    //
    // For MoveBlock the same re-run does more work than it looks: the option
    // search happens again against live data, so a slot taken by something
    // else since the card rendered is simply no longer offered, and the tap
    // refuses instead of colliding.
    const Verdict v = validate(data, handles, role, p, world);
    if (!v.ok)
        return v;

    if (p.verb == Verb::UndoMove) {
        // The door built in v29.2 and generalized in v29.3 — which until
        // this line had no caller anywhere in the app. It removes every
        // replacement and returns the original to unresolved as ONE change,
        // and refuses rather than forcing, so the race between the verdict
        // above and this call still ends in a refusal.
        if (!data.undoReschedule(world.undoableMoveId))
            return { false, QObject::tr("That move couldn't be taken back.") };
        return { true, QString() };
    }

    if (p.verb == Verb::MoveBlock) {
        const QString blockId = handles.blockIdFor(p.targetHandle);
        // The existing door, which appends a replacement and annotates the
        // original as Moved in one change. This file grants reach, never
        // capability — and it declines rather than forces, so even the
        // race between validate and here ends in a refusal, not a mess.
        const QString newId = data.rescheduleBlock(blockId, p.newDate,
                                                   p.newStartMinutes,
                                                   p.newEndMinutes);
        if (newId.isEmpty())
            return { false, QObject::tr("That slot was taken before the "
                                        "change could be made.") };
        return { true, QString() };
    }

    const QString id = handles.taskIdFor(p.targetHandle);
    const Task*   t  = data.taskById(id);

    // Existing doors only — this file grants reach, never capability.
    if (p.estimateMinutes > 0)
        data.setTaskSize(id, p.estimateMinutes,
                         t->chunkable); // preserve, don't decide
    if (p.dueDate.isValid())
        data.setTaskDueDate(id, p.dueDate,
                            QTime()); // date without time is the honest
                                      // grain of what was proposed

    return { true, QString() };
}

} // namespace verbs
