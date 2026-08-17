#include "Intake.h"

#include "AppData.h"
#include "Category.h"
#include "Event.h"
#include "Task.h"

#include <QHash>
#include <QObject>
#include <QRegularExpression>

#include <algorithm>

namespace intake
{

Guess historyGuess(const AppData& data, const Task& task)
{
    if (task.categoryId.isEmpty())
        return {};

    // Tracked focus per task — the affordability rate's scan, reused
    // shape-for-shape (reverse questions are scans; derive, don't store).
    QHash<QString, int> trackedByTask;
    for (const Event& e : data.events())
        if (!e.taskId.isEmpty())
            trackedByTask[e.taskId] += int(e.focusSeconds() / 60);

    QVector<int> actuals;
    for (const Task& t : data.tasks()) {
        if (!t.done || t.categoryId != task.categoryId)
            continue;
        const int actual = trackedByTask.value(t.id, 0);
        if (actual > 0)
            actuals.append(actual);
    }

    if (actuals.size() < 2)
        return {}; // one data point is a story, not a pattern

    std::sort(actuals.begin(), actuals.end());
    const int median = actuals.at(actuals.size() / 2);
    if (median <= 0)
        return {};

    QString area;
    if (const Category* c = data.categoryById(task.categoryId))
        area = c->name;

    const int h = median / 60;
    const int m = median % 60;
    QString span;
    if (h > 0 && m > 0)
        span = QObject::tr("%1h %2m").arg(h).arg(m);
    else if (h > 0)
        span = QObject::tr("%1h").arg(h);
    else
        span = QObject::tr("%1m").arg(m);

    return { median,
             QObject::tr("%1 finished %2 tasks ran ~%3 each")
                 .arg(actuals.size())
                 .arg(area.isEmpty() ? QObject::tr("similar") : area)
                 .arg(span) };
}

bool worthInterviewing(const AppData& data, const Task& task,
                       const QDateTime& now)
{
    if (task.done || task.archived || task.hasEstimate())
        return false; // nothing to ask, or already answered

    // Ask once (§K.6): a live dismissal is the owner's Skip still in
    // force. Compared against `now` directly — same rule as the
    // needs-a-block gate, so a lapsed timestamp never hides a task even
    // if housekeeping hasn't run.
    if (task.dismissedUntil.isValid() && task.dismissedUntil > now)
        return false;

    // Substance signals, §K.6's list verbatim: a real deadline…
    if (task.dueDate.isValid())
        return true;
    // …urgent priority…
    if (task.priority == Task::Priority::Urgent)
        return true;
    // …or a category that historically absorbs hours. The guess doubles
    // as the evidence: if the median finished task there ran an hour or
    // more, an unsized task there is a real hole in the plan.
    if (historyGuess(data, task).minutes >= 60)
        return true;

    return false; // "buy milk" — the queue keeps it, the interview skips it
}

QString questionFor(const AppData& data, const Task& task)
{
    const Guess g = historyGuess(data, task);
    if (g.exists())
        // §K.3: guess-and-confirm — a nod or a correction, never a blank
        // page. The basis rides along: evidence, not adjectives (§G.3).
        return QObject::tr("%1 — what is it, and roughly how long? "
                           "(%2 — sound right?)")
            .arg(task.title, g.basis);
    return QObject::tr("%1 — what is it, and roughly how long do you "
                       "think?")
        .arg(task.title);
}

int parseDurationAnswer(const QString& text)
{
    const QString t = text.trimmed().toLower();
    if (t.isEmpty())
        return 0;

    // Anchored end-to-end on purpose: "2h" parses, "probably 2h if Marc
    // shows up" does NOT — a sentence with opinions in it belongs to the
    // model, and a cheap parser that plucks numbers out of prose would
    // silently pre-empt the reader that understands the prose. One
    // pattern covers "2h", "90m", "1h 30m", "1h30", "90 min" and bare
    // "90" (minutes); the minutes SUFFIX is optional so "1h30" works,
    // and \\d{1,3} caps the bare form so "2024" stays a year, not a
    // duration.
    static const QRegularExpression full(QStringLiteral(
        "^(?:(\\d{1,2})\\s*h(?:ours?)?)?\\s*"
        "(?:(\\d{1,3})\\s*(?:m(?:in(?:ute)?s?)?)?)?$"));
    const auto m = full.match(t);
    if (m.hasMatch()
        && (!m.captured(1).isEmpty() || !m.captured(2).isEmpty())) {
        const int minutes =
            m.captured(1).toInt() * 60 + m.captured(2).toInt();
        return minutes > 0 ? minutes : 0;
    }
    return 0;
}


} // namespace intake
