#include "Intake.h"
// ---------------------------------------------------------------------------
// IntakeExtract — intake::llm's DEFINITIONS, in their own translation unit
// (v29.1). One header, two .cpps, split by DEPENDENCY GROUP: the pure
// interview brain (Intake.cpp) compiles into the domain test targets,
// which do not and should not link the provider layer; the extraction
// half below calls ai::extractText and therefore lives with the NLP
// sources, next to LlmQuickAdd.cpp whose split it mirrors. The linker
// taught this lesson the direct way: an undefined ai::extractText in
// test_domain is the compiler saying "this function is not domain".
// ---------------------------------------------------------------------------

#include "Task.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QObject>

namespace intake
{

namespace llm
{

QString systemPrompt(const Task& task, const QString& areaName,
                     const Guess& guess, QDate today)
{
    const QString& area = areaName;

    QString p = QStringLiteral(
        "You extract task-sizing facts from one casual sentence.\n"
        "Task: \"%1\"%2\n"
        "Today: %3\n")
                    .arg(task.title,
                         area.isEmpty()
                             ? QString()
                             : QStringLiteral(" (area: %1)").arg(area),
                         today.toString(Qt::ISODate));
    if (task.dueDate.isValid())
        p += QStringLiteral("It is already due %1 — do not restate that.\n")
                 .arg(task.dueDate.toString(Qt::ISODate));
    if (guess.exists())
        // The nod case (§K.3): "sounds right" must extract to the number
        // the question offered — the model needs to know what was offered.
        p += QStringLiteral(
                 "The user was offered a guess of %1 minutes; plain "
                 "agreement (\"sounds right\", \"yeah\") means that "
                 "number.\n")
                 .arg(guess.minutes);
    p += QStringLiteral(
        "The user answers what the task is and roughly how long it will "
        "take. If they name conditions or risks, size the REALISTIC "
        "total, not the optimistic one.\n"
        "Reply with ONLY a JSON object:\n"
        "{\"estimate_minutes\": integer or null, "
        "\"due_date\": \"YYYY-MM-DD\" or null}\n"
        "null for anything the answer does not actually indicate. "
        "Resolve relative dates (\"friday\", \"end of month\") "
        "against today. No prose, no markdown fences.");
    return p;
}

Extraction parseReply(const QByteArray& body, ai::Dialect dialect)
{
    // Envelope first (the provider layer's knowledge), then fences, then
    // JSON, then defensive field mapping — quick-add's exact ladder,
    // because every rung has already caught a real model doing the thing
    // the rung exists for.
    const ai::TextResult text = ai::extractText(dialect, body);
    if (!text.ok)
        return { false, 0, QDate(),
                 QObject::tr("couldn't read the AI reply") };

    QString t = text.text.trimmed();
    if (t.startsWith(QStringLiteral("```"))) {
        const int firstNl = t.indexOf(QLatin1Char('\n'));
        const int lastFence = t.lastIndexOf(QStringLiteral("```"));
        if (firstNl >= 0 && lastFence > firstNl)
            t = t.mid(firstNl + 1, lastFence - firstNl - 1).trimmed();
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return { false, 0, QDate(),
                 QObject::tr("the AI didn't answer in the agreed shape") };

    const QJsonObject obj = doc.object();
    Extraction out;
    out.ok = true;

    // Defensive on every rung: a double is truncated, a string number is
    // read, garbage becomes "not indicated" — never a crash, never a lie.
    const QJsonValue est = obj.value(QStringLiteral("estimate_minutes"));
    if (est.isDouble())
        out.estimateMinutes = qMax(0, int(est.toDouble()));
    else if (est.isString())
        out.estimateMinutes = qMax(0, est.toString().toInt());

    const QJsonValue due = obj.value(QStringLiteral("due_date"));
    if (due.isString())
        out.dueDate = QDate::fromString(due.toString(), Qt::ISODate);

    if (out.estimateMinutes == 0 && !out.dueDate.isValid()) {
        out.ok = false;
        out.error = QObject::tr(
            "couldn't find a duration in that — try something like "
            "\"2h\" or \"90 min\"");
    }
    return out;
}

} // namespace llm

} // namespace intake
