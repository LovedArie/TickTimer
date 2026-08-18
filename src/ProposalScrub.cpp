#include "ProposalScrub.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace scrub
{
namespace
{

// "09:00" / "9:00" -> 540. -1 for anything else.
//
// Deliberately NOT tolerant beyond the leading zero: the briefing prints
// HH:MM, and the placement is matched against the offered set by exact
// value, so a clock this function had to guess at would produce a proposal
// that fails validation anyway — with a confusing reason instead of an
// honest "no proposal here".
int parseClock(const QString& text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 2)
        return -1;
    bool okH = false, okM = false;
    const int h = parts.at(0).toInt(&okH);
    const int m = parts.at(1).toInt(&okM);
    if (!okH || !okM || h < 0 || h > 24 || m < 0 || m > 59)
        return -1;
    const int total = h * 60 + m;
    return total > 24 * 60 ? -1 : total;
}

// The end of the JSON object starting at `open`, or -1 if it never closes.
// String-aware, because a brace inside a title ("{ study }") is not
// structure — counting braces naively is the bug this exists to avoid.
int matchingBrace(const QString& s, int open)
{
    int  depth    = 0;
    bool inString = false;
    bool escaped  = false;
    for (int i = open; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == QLatin1Char('\\'))
                escaped = true;
            else if (c == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (c == QLatin1Char('"'))
            inString = true;
        else if (c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char('}') && --depth == 0)
            return i;
    }
    return -1;
}

// Fences left behind once the object between them is gone, plus the blank
// lines that held it. Cosmetic, but a bubble ending in a bare ``` reads as
// a bug to the person looking at it.
QString tidy(QString text)
{
    text.replace(QStringLiteral("```json"), QString());
    text.replace(QStringLiteral("```"), QString());
    while (text.contains(QStringLiteral("\n\n\n")))
        text.replace(QStringLiteral("\n\n\n"), QStringLiteral("\n\n"));
    return text.trimmed();
}

} // namespace

MoveReply moveFromReply(const QString& reply)
{
    MoveReply out;
    out.prose = reply.trimmed();

    // Walk every '{' and keep the LAST one that parses as a move object.
    // Scanning rather than regexing: the payload is nested JSON, and a
    // regex that matches balanced braces is a regex that will one day be
    // wrong about a title containing one.
    int found = -1, foundEnd = -1;
    QJsonObject move;

    for (int i = reply.indexOf(QLatin1Char('{')); i >= 0;
         i = reply.indexOf(QLatin1Char('{'), i + 1)) {
        const int end = matchingBrace(reply, i);
        if (end < 0)
            break; // nothing after this can close either

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(
            reply.mid(i, end - i + 1).toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonValue v = doc.object().value(QStringLiteral("move"));
        if (!v.isObject())
            continue; // valid JSON about something else — not ours

        move     = v.toObject();
        found    = i;
        foundEnd = end;
    }

    if (found < 0)
        return out; // just conversation

    // Strip it whether or not the fields turn out to be usable: the object
    // was addressed to the machine, and showing the person a half-parsed
    // payload is worse than showing them nothing.
    QString prose = reply;
    prose.remove(found, foundEnd - found + 1);
    out.prose   = tidy(prose);
    out.hasMove = true;

    out.blockHandle = move.value(QStringLiteral("block")).toString().trimmed();
    out.date = QDate::fromString(move.value(QStringLiteral("date")).toString(),
                                 Qt::ISODate);

    const int start = parseClock(move.value(QStringLiteral("start")).toString());
    const int end   = parseClock(move.value(QStringLiteral("end")).toString());
    if (start >= 0 && end >= 0) {
        out.startMinutes = start;
        out.endMinutes   = end;
    }
    return out;
}

} // namespace scrub
