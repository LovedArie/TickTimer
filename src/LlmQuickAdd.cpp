#include "LlmQuickAdd.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace nlp::llm
{

QString systemPrompt(const QDate& today)
{
    // Everything the model needs, nothing it can misread: the schema, the
    // no-prose rule (stated twice — models love a preamble), and today's date
    // with its weekday so relative dates resolve to absolutes on their side.
    return QStringLiteral(
               "You convert ONE task-capture line into JSON. Reply with ONLY "
               "a JSON object — no prose, no markdown fences, no explanation. "
               "Schema: {\"title\": string, \"due_date\": \"YYYY-MM-DD\" or "
               "null, \"due_time\": \"HH:MM\" (24-hour) or null, "
               "\"priority\": \"urgent\"|\"medium\"|\"low\", \"repeat\": "
               "\"none\"|\"daily\"|\"weekly\"|\"monthly\"|\"yearly\", "
               "\"category\": string or null}. Today is %1 (%2). Resolve "
               "relative dates (\"next fri\", \"end of month\") to absolute "
               "dates. Keep the title concise: strip the date/priority/repeat "
               "words out of it. If no date is implied, due_date is null. "
               "Only set due_time when a clock time is actually stated; a bare "
               "day means null.")
        .arg(today.toString(Qt::ISODate), today.toString("dddd"));
}

Outcome fromJsonObject(const QJsonObject& obj)
{
    Outcome out;
    nlp::ParsedTask t;

    t.title = obj.value(QStringLiteral("title")).toString().trimmed();
    if (t.title.isEmpty()) {
        // A task with no name isn't a task; better to fail loudly (the hint
        // line shows this) than commit an unnamed row the user must hunt down.
        out.error = QStringLiteral("AI reply had no usable title");
        return out;
    }

    // Every field below DEGRADES rather than fails: a garbage priority means
    // Medium, a malformed date means TBD — exactly what the deterministic
    // parser would leave, so an imperfect AI answer is never worse than none.
    const QString due = obj.value(QStringLiteral("due_date")).toString();
    if (!due.isEmpty())
        t.dueDate = QDate::fromString(due, Qt::ISODate); // invalid -> TBD

    // v22: the same degrade-don't-fail rule as every field here. A malformed
    // or absent time is simply "all day", and a time with NO date is dropped
    // rather than stored as an orphan — the model does not get to invent a
    // state the domain refuses (AppData would strip it anyway; better to
    // agree at the edge than to rely on the door catching it).
    const QString dueAt = obj.value(QStringLiteral("due_time")).toString();
    if (!dueAt.isEmpty() && t.dueDate.isValid()) {
        QTime parsed = QTime::fromString(dueAt, QStringLiteral("HH:mm"));
        if (!parsed.isValid())
            parsed = QTime::fromString(dueAt, Qt::ISODate);
        t.dueTime = parsed; // still invalid on garbage -> all day
    }

    const QString prio =
        obj.value(QStringLiteral("priority")).toString().toLower();
    if (prio == QLatin1String("urgent") || prio == QLatin1String("high"))
        t.priority = Task::Priority::Urgent;
    else if (prio == QLatin1String("low"))
        t.priority = Task::Priority::Low;

    t.repeat = repeatFromString(
        obj.value(QStringLiteral("repeat")).toString().toLower());

    t.categoryHint =
        obj.value(QStringLiteral("category")).toString().trimmed().toLower();

    out.ok = true;
    out.task = t;
    return out;
}

Outcome parseApiReply(const QByteArray& body, ai::Dialect dialect)
{
    Outcome out;

    // Layer 1: the VENDOR envelope. Anthropic wraps the answer in
    // content[].text, OpenAI-compatible servers in choices[0].message.content
    // — the only vendor-shaped step in this file, and now the only one that
    // has to change when a third dialect appears.
    const ai::TextResult unwrapped = ai::extractText(dialect, body);
    if (!unwrapped.ok) {
        out.error = unwrapped.error;
        return out;
    }
    const QString text = unwrapped.text;

    // Layer 2: the model's own JSON — which, contract or not, sometimes
    // arrives wearing ```json fences. Strip them rather than fail: defending
    // against a model's habits is cheaper than arguing with them.
    QString cleaned = text.trimmed();
    if (cleaned.startsWith(QLatin1String("```"))) {
        const int firstNewline = cleaned.indexOf('\n');
        if (firstNewline >= 0)
            cleaned = cleaned.mid(firstNewline + 1);
        if (cleaned.endsWith(QLatin1String("```")))
            cleaned.chop(3);
        cleaned = cleaned.trimmed();
    }

    const QJsonDocument inner = QJsonDocument::fromJson(cleaned.toUtf8());
    if (!inner.isObject()) {
        out.error = QStringLiteral("AI reply was not the agreed JSON object");
        return out;
    }
    return fromJsonObject(inner.object());
}

} // namespace nlp::llm
