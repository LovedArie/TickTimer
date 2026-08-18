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
    case Role::Nudge:
    case Role::CheckIn:
        return {}; // observe and phrase — forever, for Nudge and CheckIn
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

QString Proposal::summary(const AppData& data, const HandleMap& handles) const
{
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

Verdict validate(const AppData& data, const HandleMap& handles, Role role,
                 const Proposal& p)
{
    // 1. The role may use this verb at all. First check on purpose: a
    //    forbidden role must not learn, via later error texts, which
    //    handles exist or what state tasks are in.
    if (!verbsFor(role).contains(p.verb))
        return { false, QObject::tr("This assistant role is not allowed to "
                                    "make changes.") };

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
              const Proposal& p)
{
    // Re-validate at the tap, not just at the render: the owner may have
    // filled the estimate by hand while the card sat there, and a stale
    // Apply must refuse politely (the additive check above catches
    // exactly this) rather than trust a verdict from an older world.
    const Verdict v = validate(data, handles, role, p);
    if (!v.ok)
        return v;

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
