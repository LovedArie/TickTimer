#pragma once
// ---------------------------------------------------------------------------
// QuickAddParser — natural-language quick-add, as a PURE FUNCTION.
//
//   nlp::parseQuickAdd("lab 4 friday urgent weekly #school", today)
//     -> { title:"lab 4", dueDate:<soonest Friday>, priority:Urgent,
//          repeat:Weekly, categoryHint:"school" }
//
// This is the app's first AI-flavoured feature, and it deliberately starts as
// DETERMINISTIC parsing, not a model call. The shape matters more than the
// smarts: text in, struct out, no AppData, no widgets, no clock of its own
// (`today` is a parameter, so tests never depend on the real date — the same
// trick stats::summarizeDay and version::decideBanner use). A later LLM
// fallback can slot BEHIND this same ParsedTask struct without the UI changing.
//
// THE RULES (each one is a decision, stated so it can be tested):
//  * Tokens are matched case-insensitively; the TITLE keeps original casing
//    and order — it is simply every token nothing else claimed.
//  * FIRST MATCH WINS, per facet. One date, one priority, one repeat, one
//    #category; later expressions of the same facet stay in the title, where
//    the live preview makes the surprise visible instead of silent.
//  * A bare weekday means the SOONEST such day that is today-or-later (so
//    "friday" typed on a Friday means today). "next friday" adds a week.
//  * "aug 8" (or "8 aug") with no year means the soonest such date: this
//    year, or next year if it already passed. An impossible date ("feb 30")
//    is NOT a date — the tokens stay in the title.
//  * '#school' is a HINT, not a category id. The parser knows no categories;
//    the UI resolves the hint (or falls back). Keeping the lookup out keeps
//    the function pure.
//  * Numeric slash dates ("8/8") are deliberately unsupported: Aug 8 or
//    8 Aug depends on locale, and a quick-add that guesses wrong is worse
//    than one that leaves the text alone.
//
// TIMES (v22) follow the same instincts as dates, one facet, first match wins:
//  * "5pm", "5:30pm", "17:00", "17h", "17h30", "noon", "midnight" — and the
//    two-token "5 pm". A leading "at" or "@" is swallowed with the time.
//  * A BARE hour ("5") is not a time, exactly as a bare number is not a date:
//    half a student's tasks are "lab 4" and "chapter 7". Writing "at 5"
//    licenses it — the word "at" is the user saying "this one is a clock",
//    the same role the "th" in "28th" plays for dates.
//  * "midnight" resolves to 23:59, not 00:00. This is a DEADLINE parser: "due
//    midnight Friday" universally means the end of Friday, and 00:00 would
//    make the task twenty-four hours late the moment it was typed.
//  * A time with no date implies TODAY. (The parser only receives a QDate, so
//    it cannot know whether 17:00 has already passed — it does not guess
//    tomorrow; the live preview shows the date it chose before you commit.)
// ---------------------------------------------------------------------------

#include <QDate>
#include <QMetaType>
#include <QString>
#include <QTime>

#include "Task.h" // for the Priority / Repeat enums (Core-only header)

namespace nlp
{

struct ParsedTask
{
    QString        title;                              // leftovers, verbatim
    QDate          dueDate;                            // invalid = "date TBD"
    QTime          dueTime;                            // invalid = "all day"
    Task::Priority priority = Task::Priority::Medium;  // default: ordinary
    Task::Repeat   repeat   = Task::Repeat::None;
    QString        categoryHint;                       // "#school" -> "school"
};

// Parse one quick-add line. `today` anchors every relative date; pass
// QDate::currentDate() in production and a fixed date in tests.
ParsedTask parseQuickAdd(const QString& text, const QDate& today);

} // namespace nlp

// Registered as a metatype so ParsedTask can travel through queued signal
// connections and QMetaObject::invokeMethod (the tests' network-free seam
// into the overlay's AI path).
Q_DECLARE_METATYPE(nlp::ParsedTask)
