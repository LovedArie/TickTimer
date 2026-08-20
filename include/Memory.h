#pragma once
// ---------------------------------------------------------------------------
// memory — the residue file (assistant addendum §L, v30.0).
//
// WHAT THIS IS FOR, in one line: the model does not learn (§L.1). Nothing is
// trained and nothing persists inside it; "memory" is text re-sent with every
// request. Everything below follows from that one fact.
//
// §L.2 — IF IT CAN BE DERIVED, IT DOES NOT GO IN MEMORY. Preferences,
// routines, the current situation, people. Never blocks, tasks, deadlines or
// anything the briefing computes. The briefing is regenerated every turn
// precisely so it cannot go stale; a memory file that shadowed it would undo
// that, and become the contradictory second copy §L names as the number-one
// failure mode of memory systems.
//
// §L.3 — MEMORY IS BILLED EVERY TURN, so entries are REPLACED, not appended.
// "Breakfast at 08:00" and "moved to 07:30" cannot both live here: the model
// would see a contradiction and pick one at random. An entry is one line and
// the editor edits lines; there is no append path that could grow unbounded.
//
// PURE — a string in, values out. No file I/O, no clock, no AppData, no
// network. MemoryStore does the disk half, and this half is asserted offline
// in microseconds. (Same shape as chat::, intake:: and scrub:: before it.)
// ---------------------------------------------------------------------------

#include <QString>
#include <QStringList>
#include <QVector>

namespace memory
{

// The §L.5 sections, fixed rather than freeform. Sections are TRIMMABLE —
// prose drifts into a diary, and a diary cannot be budgeted.
enum class Section
{
    Routines,   // "Breakfast at 07:30", "gym Tuesday evenings"
    Preferences,// "nothing before 09:00"
    Situation,  // "exam period until Dec 15" — the section that expires
    People,     // "Marc — group project, unreliable, budget extra"
};

// Fixed order, used by the file, the editor and the prompt band alike, so all
// three agree without anyone re-deciding.
QVector<Section> allSections();

// The heading this section is written under, and the reverse. Unknown
// headings resolve to no section (see `File::preserved`).
QString sectionHeading(Section s);

// The heading that ends structured content. Everything after it is kept
// verbatim and never sent to a model — see `File::preserved` for why this
// sink exists at all.
QString preservedHeading();

// How much of the band a turn may spend. The briefing is ~1000 chars and the
// history budget is 6000 (chat::kDefaultBudgetChars), so this is deliberately
// the smallest of the three: memory is the part that is paid for on every
// single turn forever, whether or not it was relevant.
inline constexpr int kDefaultBudgetChars = 1200;

// The file, as values.
struct File
{
    QStringList routines;
    QStringList preferences;
    QStringList situation;
    QStringList people;

    // Anything the parser did not recognise, kept VERBATIM.
    //
    // The sidecar was chosen over a slot in data.json because "a file you can
    // read and fix yourself" is a trust feature (§L.5). A parser that silently
    // ate a line because a heading was misspelled would retire exactly that
    // trust, so unrecognised text is never dropped — it is preserved here,
    // rewritten under preservedHeading(), and excluded from the prompt band.
    // Preserved, not obeyed: the model never sees it.
    QString preserved;

    // Accessors by section, so loops over allSections() don't need a switch at
    // every call site. Two overloads for the usual const/non-const reasons.
    const QStringList& entriesFor(Section s) const;
    QStringList&       entriesFor(Section s);

    // Empty means "nothing to say to the model" — preserved text does not
    // count, because it never reaches one.
    bool isEmpty() const;

    // Total entries across the four sections.
    int entryCount() const;
};

// Value equality, so the round-trip property below can be asserted directly
// rather than field by field at every call site.
bool operator==(const File& a, const File& b);
inline bool operator!=(const File& a, const File& b) { return !(a == b); }

// Read the file's text. Tolerant by construction: unknown headings, stray
// prose and malformed bullets all land in `preserved` rather than failing.
// There is no error return, because there is no such thing as an invalid
// memory file — only text this parser did not recognise.
File parse(const QString& markdown);

// Write the file's text. `parse(render(f)) == f` for every f, which is the
// property that makes preserved text safe to round-trip.
QString render(const File& f);

// The prompt band: what the model is actually given.
//
// TRIMMING IS A PROMPT CONCERN, NEVER A DATA CONCERN. The file keeps
// everything its owner wrote; this is what respects the budget. An entry that
// does not fit is dropped WHOLE — never truncated, because half a sentence
// about a person reads as a fact with its qualifier removed ("Marc is
// unreliable" from "Marc is unreliable about deadlines but great in a room").
// A section whose entries all get dropped emits no heading at all.
//
// Returns "" for an empty file, so the caller can omit the band entirely
// rather than emit a header with no body — a header with no body reads to a
// model like an instruction it failed to receive (chat::systemPrompt made the
// same call for STYLE).
QString promptBand(const File& f, int budgetChars = kDefaultBudgetChars);

} // namespace memory
