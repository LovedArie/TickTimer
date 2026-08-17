#include "QuickAddParser.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace nlp
{
namespace
{

// Weekday word -> Qt day number (Monday=1 … Sunday=7), or 0 if not a weekday.
int weekdayOf(const QString& w)
{
    if (w == "mon" || w == "monday") return 1;
    if (w == "tue" || w == "tues" || w == "tuesday") return 2;
    if (w == "wed" || w == "wednesday") return 3;
    if (w == "thu" || w == "thur" || w == "thurs" || w == "thursday") return 4;
    if (w == "fri" || w == "friday") return 5;
    if (w == "sat" || w == "saturday") return 6;
    if (w == "sun" || w == "sunday") return 7;
    return 0;
}

// Month word -> 1..12, or 0.
int monthOf(const QString& w)
{
    static const struct { const char* name; int m; } table[] = {
        {"jan", 1}, {"january", 1},   {"feb", 2}, {"february", 2},
        {"mar", 3}, {"march", 3},     {"apr", 4}, {"april", 4},
        {"may", 5},                   {"jun", 6}, {"june", 6},
        {"jul", 7}, {"july", 7},      {"aug", 8}, {"august", 8},
        {"sep", 9}, {"sept", 9},      {"september", 9},
        {"oct", 10}, {"october", 10}, {"nov", 11}, {"november", 11},
        {"dec", 12}, {"december", 12},
    };
    for (const auto& e : table)
        if (w == QLatin1String(e.name))
            return e.m;
    return 0;
}

// The soonest weekday `dow` that is today-or-later. delta 0 == today itself:
// "friday" typed ON a Friday means today, not next week — the intuitive read.
QDate soonestWeekday(const QDate& today, int dow)
{
    const int delta = (dow - today.dayOfWeek() + 7) % 7;
    return today.addDays(delta);
}

// "aug 8" with no year = the soonest such date. Try this year; if that's
// already behind us, next year. Returns invalid for impossible dates (feb 30)
// so the caller can leave those tokens in the title.
QDate soonestMonthDay(const QDate& today, int month, int day)
{
    QDate d(today.year(), month, day);
    if (d.isValid() && d < today)
        d = QDate(today.year() + 1, month, day); // may go invalid (feb 29) —
    return d;                                    // caller checks isValid()
}

int asInt(const QString& s, bool* ok) { return s.toInt(ok); }

// A day number, with or without an ordinal suffix: "28", "28th", "1st",
// "2nd", "3rd". The suffix is stripped LOOSELY ("22th" still reads as 22) —
// quick capture forgives typos; policing grammar would just lose the date.
// `hadSuffix` reports whether one was present, because the suffix carries
// MEANING on its own: "28th" alone is a date, bare "28" alone is not (half a
// student's tasks contain plain numbers — "lab 4" must never grow a date).
int dayNumber(const QString& s, bool* ok, bool* hadSuffix = nullptr)
{
    QString t = s;
    bool suffixed = false;
    for (const char* suf : {"st", "nd", "rd", "th"}) {
        if (t.size() > 2 && t.endsWith(QLatin1String(suf))) {
            t.chop(2);
            suffixed = true;
            break;
        }
    }
    if (hadSuffix)
        *hadSuffix = suffixed;
    return t.toInt(ok);
}

// "28th" with no month = the soonest 28th that is today-or-later: this month
// if still ahead, else the next month that HAS that day (a "31st" skips the
// short months). Same soonest-future spirit as weekdays and month-day dates.
QDate soonestDayOfMonth(const QDate& today, int day)
{
    int y = today.year(), m = today.month();
    for (int i = 0; i < 24; ++i) { // 24 months covers every day 1..31
        const QDate d(y, m, day);
        if (d.isValid() && d >= today)
            return d;
        if (++m > 12) {
            m = 1;
            ++y;
        }
    }
    return {};
}

// ---- times (v22) -----------------------------------------------------------
// One token in, a QTime out (invalid = "not a time"). `allowBareHour` is the
// LICENCE flag: it is false everywhere except directly after "at"/"@", so a
// stray "4" in "lab 4" can never become 04:00. Same shape as dayNumber()'s
// `hadSuffix` — the parser only guesses when the user signalled intent.
QTime timeToken(const QString& tok, bool allowBareHour)
{
    QString t = tok;
    if (t.startsWith(QLatin1Char('@')))
        t = t.mid(1);
    if (t.isEmpty())
        return {};

    // Words first — they are unambiguous and cheap to rule out.
    if (t == QLatin1String("noon") || t == QLatin1String("midi"))
        return QTime(12, 0);
    // A DEADLINE's midnight is the end of the day named, not its start.
    if (t == QLatin1String("midnight") || t == QLatin1String("minuit"))
        return QTime(23, 59);

    // "5pm", "5:30pm", "530pm" — 12-hour with an explicit meridiem.
    static const QRegularExpression ampm(
        QStringLiteral("^(\\d{1,2})(?::?(\\d{2}))?(am|pm)$"));
    if (const auto m = ampm.match(t); m.hasMatch()) {
        int h = m.captured(1).toInt();
        const int min = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        if (h < 1 || h > 12 || min > 59)
            return {};
        if (m.captured(3) == QLatin1String("pm") && h != 12) h += 12;
        if (m.captured(3) == QLatin1String("am") && h == 12) h = 0;
        return QTime(h, min);
    }

    // "17:00" and the French "17h" / "17h30" — 24-hour, no meridiem needed.
    static const QRegularExpression h24(
        QStringLiteral("^(\\d{1,2})(?::|h)([0-5]\\d)?$"));
    if (const auto m = h24.match(t); m.hasMatch()) {
        const int h = m.captured(1).toInt();
        const int min = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        if (h > 23)
            return {};
        return QTime(h, min);
    }

    // A bare hour, ONLY when licensed by a preceding "at".
    if (allowBareHour) {
        bool ok = false;
        const int h = t.toInt(&ok);
        if (ok && h >= 0 && h <= 23)
            return QTime(h, 0);
    }
    return {};
}

} // namespace

ParsedTask parseQuickAdd(const QString& text, const QDate& today)
{
    ParsedTask out;

    // Tokenize on whitespace. `raw` keeps the user's exact words for the
    // title; `low` is the lowercase, edge-punctuation-stripped copy we MATCH
    // on ("friday," still reads as friday — but the comma vanishes only if the
    // token is consumed).
    const QStringList raw =
        text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList low;
    low.reserve(raw.size());
    for (const QString& t : raw) {
        QString l = t.toLower();
        while (!l.isEmpty() && QStringLiteral(",.;:").contains(l.back()))
            l.chop(1);
        low << l;
    }

    QVector<bool> used(raw.size(), false);
    bool haveDate = false, havePrio = false, haveRepeat = false, haveCat = false;
    bool haveTime = false;

    // A single left-to-right pass. Multi-token expressions are tried FIRST at
    // each position (so "next friday" wins over the bare "friday" inside it,
    // and "every week" over a stray "week"); the order of checks below is the
    // grammar. First match per facet wins — see the header for why.
    for (int i = 0; i < raw.size(); ++i) {
        if (used[i])
            continue;
        const QString& t = low[i];
        const bool hasNext  = i + 1 < raw.size() && !used[i + 1];
        const bool hasNext2 = i + 2 < raw.size() && !used[i + 2];

        // ---- dates -------------------------------------------------------
        if (!haveDate) {
            // "next <weekday>"
            if (t == "next" && hasNext && weekdayOf(low[i + 1]) != 0) {
                out.dueDate =
                    soonestWeekday(today, weekdayOf(low[i + 1])).addDays(7);
                used[i] = used[i + 1] = true;
                haveDate = true;
                continue;
            }
            // "in N days" / "in N weeks"
            if (t == "in" && hasNext && hasNext2) {
                bool ok = false;
                const int n = asInt(low[i + 1], &ok);
                const QString& unit = low[i + 2];
                if (ok && n >= 0
                    && (unit == "day" || unit == "days" || unit == "week"
                        || unit == "weeks")) {
                    const int days = unit.startsWith("week") ? n * 7 : n;
                    out.dueDate = today.addDays(days);
                    used[i] = used[i + 1] = used[i + 2] = true;
                    haveDate = true;
                    continue;
                }
            }
            // "<month> <day>"  or  "<day> <month>", with an optional 4-digit
            // year after the month-first form ("aug 8 2027"). dayNumber
            // accepts ordinals, so "28th july" and "july 28th" both read.
            if (hasNext) {
                int month = monthOf(t), day = 0;
                bool dayOk = false;
                int  monthPos = i, lastPos = i + 1;
                if (month != 0) {
                    day = dayNumber(low[i + 1], &dayOk);
                } else if ((month = monthOf(low[i + 1])) != 0) {
                    day = dayNumber(t, &dayOk);
                    monthPos = i + 1;
                }
                if (month != 0 && dayOk && day >= 1 && day <= 31) {
                    QDate d;
                    int yearPos = -1;
                    if (monthPos == i && hasNext2) { // maybe "aug 8 2027"
                        bool yOk = false;
                        const int y = asInt(low[i + 2], &yOk);
                        if (yOk && y >= 1900 && y <= 2999) {
                            d = QDate(y, month, day);
                            yearPos = i + 2;
                        }
                    }
                    if (yearPos < 0)
                        d = soonestMonthDay(today, month, day);
                    if (d.isValid()) { // feb 30 falls through to the title
                        out.dueDate = d;
                        used[i] = used[lastPos] = true;
                        if (yearPos >= 0)
                            used[yearPos] = true;
                        haveDate = true;
                        continue;
                    }
                }
            }
            // single tokens: today / tomorrow / weekday / ISO
            if (t == "today") {
                out.dueDate = today;
                used[i] = haveDate = true;
                continue;
            }
            if (t == "tomorrow" || t == "tmr" || t == "tmrw") {
                out.dueDate = today.addDays(1);
                used[i] = haveDate = true;
                continue;
            }
            if (const int dow = weekdayOf(t)) {
                out.dueDate = soonestWeekday(today, dow);
                used[i] = haveDate = true;
                continue;
            }
            if (const QDate iso = QDate::fromString(t, Qt::ISODate);
                iso.isValid()) {
                out.dueDate = iso;
                used[i] = haveDate = true;
                continue;
            }
            // A bare ordinal ("28th") is a date; a bare number ("28") is not.
            // The suffix is the user SAYING "this is a day" — that intent is
            // what licenses the guess. Soonest such day-of-month wins.
            {
                bool dayOk = false, suffixed = false;
                const int day = dayNumber(t, &dayOk, &suffixed);
                if (dayOk && suffixed && day >= 1 && day <= 31) {
                    const QDate d = soonestDayOfMonth(today, day);
                    if (d.isValid()) {
                        out.dueDate = d;
                        used[i] = haveDate = true;
                        continue;
                    }
                }
            }
        }

        // ---- time of day (v22) -------------------------------------------
        // AFTER dates on purpose: "aug 8" and "in 3 days" must claim their
        // tokens first, or a stray number could be read as an hour. Facets
        // are independent, so "friday 17:00" fills both in one pass.
        if (!haveTime) {
            // "at <something>" / "@ <something>" — the word licenses a bare
            // hour AND is consumed with it, so it never lands in the title.
            if ((t == QLatin1String("at") || t == QLatin1String("@"))
                && hasNext) {
                if (const QTime tm = timeToken(low[i + 1], true);
                    tm.isValid()) {
                    out.dueTime = tm;
                    used[i] = used[i + 1] = true;
                    haveTime = true;
                    continue;
                }
            }
            // "5 pm" written as two tokens.
            if (hasNext
                && (low[i + 1] == QLatin1String("am")
                    || low[i + 1] == QLatin1String("pm"))) {
                if (const QTime tm = timeToken(t + low[i + 1], false);
                    tm.isValid()) {
                    out.dueTime = tm;
                    used[i] = used[i + 1] = true;
                    haveTime = true;
                    continue;
                }
            }
            // The single-token forms.
            if (const QTime tm = timeToken(t, false); tm.isValid()) {
                out.dueTime = tm;
                used[i] = haveTime = true;
                continue;
            }
        }

        // ---- priority ----------------------------------------------------
        if (!havePrio) {
            static const QRegularExpression bangs("^!+$");
            if (bangs.match(t).hasMatch() || t == "urgent" || t == "high") {
                out.priority = Task::Priority::Urgent;
                used[i] = havePrio = true;
                continue;
            }
            if (t == "low") {
                out.priority = Task::Priority::Low;
                used[i] = havePrio = true;
                continue;
            }
        }

        // ---- repeat ------------------------------------------------------
        if (!haveRepeat) {
            if (t == "every" && hasNext) { // "every week" == "weekly"
                const QString& u = low[i + 1];
                Task::Repeat r = Task::Repeat::None;
                if (u == "day") r = Task::Repeat::Daily;
                else if (u == "week") r = Task::Repeat::Weekly;
                else if (u == "month") r = Task::Repeat::Monthly;
                else if (u == "year") r = Task::Repeat::Yearly;
                if (r != Task::Repeat::None) {
                    out.repeat = r;
                    used[i] = used[i + 1] = true;
                    haveRepeat = true;
                    continue;
                }
            }
            if (t == "daily" || t == "weekly" || t == "monthly"
                || t == "yearly") {
                out.repeat = repeatFromString(t); // Task.h's own converter
                used[i] = haveRepeat = true;
                continue;
            }
        }

        // ---- category hint ----------------------------------------------
        if (!haveCat && t.size() > 1 && t.startsWith('#')) {
            out.categoryHint = t.mid(1); // "#school" -> "school", lowercased
            used[i] = haveCat = true;
            continue;
        }
    }

    // A clock with no calendar is not a deadline the domain will store, so
    // the parser resolves it here rather than handing the UI an orphan:
    // "call the clinic at 9am" means today. (AppData would drop the time
    // otherwise — better to make the sensible choice where the user can SEE
    // it in the preview than to have it vanish silently at commit.)
    if (out.dueTime.isValid() && !out.dueDate.isValid())
        out.dueDate = today;

    // The title is everything nobody claimed — original casing, original
    // order. (An all-facets line like "friday urgent" yields an empty title;
    // the UI refuses to add a task with no title, and the preview shows why.)
    QStringList kept;
    for (int i = 0; i < raw.size(); ++i)
        if (!used[i])
            kept << raw[i];
    out.title = kept.join(' ').trimmed();
    return out;
}

} // namespace nlp
