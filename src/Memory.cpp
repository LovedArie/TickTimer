#include "Memory.h"

namespace memory
{
namespace
{

// One bullet, as written and as read. Kept in one place so render() and
// parse() cannot drift apart — the round-trip property depends on them
// agreeing about exactly these two characters.
const QString kBullet = QStringLiteral("- ");

// Markdown heading depth. Also in one place, same reason.
const QString kHeadingPrefix = QStringLiteral("## ");

} // namespace

QVector<Section> allSections()
{
    return {Section::Routines, Section::Preferences, Section::Situation,
            Section::People};
}

QString sectionHeading(Section s)
{
    switch (s) {
    case Section::Routines:    return QStringLiteral("Routines");
    case Section::Preferences: return QStringLiteral("Preferences");
    case Section::Situation:   return QStringLiteral("Current situation");
    case Section::People:      return QStringLiteral("People");
    }
    return {}; // unreachable; keeps every compiler quiet
}

QString preservedHeading()
{
    return QStringLiteral("Kept as written");
}

// ---- File ------------------------------------------------------------------

const QStringList& File::entriesFor(Section s) const
{
    switch (s) {
    case Section::Routines:    return routines;
    case Section::Preferences: return preferences;
    case Section::Situation:   return situation;
    case Section::People:      return people;
    }
    return routines; // unreachable
}

QStringList& File::entriesFor(Section s)
{
    // Delegate to the const version and cast the const away. The alternative
    // is a second copy of the switch, and two switches over the same enum are
    // one edit away from disagreeing — which is the bug this avoids, not the
    // keystrokes.
    return const_cast<QStringList&>(
        static_cast<const File*>(this)->entriesFor(s));
}

bool File::isEmpty() const
{
    return entryCount() == 0;
}

int File::entryCount() const
{
    int n = 0;
    for (Section s : allSections())
        n += entriesFor(s).size();
    return n;
}

bool operator==(const File& a, const File& b)
{
    return a.routines == b.routines && a.preferences == b.preferences
           && a.situation == b.situation && a.people == b.people
           && a.preserved == b.preserved;
}

// ---- parse -----------------------------------------------------------------

File parse(const QString& markdown)
{
    File f;

    // `current` is the section lines are currently landing in; no section
    // means "not inside recognised structure", and those lines are preserved.
    const Section* current = nullptr;
    Section        currentValue = Section::Routines;

    QStringList preservedLines;
    bool        inPreservedSink = false;

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();

        // Everything after the sink heading is verbatim, headings included —
        // that is what makes the round trip stable. Without it, a preserved
        // line beginning "- " would be re-read as an entry of whichever
        // section happened to be last, and the file would mutate on every
        // save.
        if (inPreservedSink) {
            preservedLines.append(raw);
            continue;
        }

        if (line.startsWith(kHeadingPrefix)) {
            const QString heading = line.mid(kHeadingPrefix.size()).trimmed();

            if (heading.compare(preservedHeading(), Qt::CaseInsensitive) == 0) {
                inPreservedSink = true;
                current = nullptr;
                continue;
            }

            bool matched = false;
            for (Section s : allSections()) {
                if (heading.compare(sectionHeading(s), Qt::CaseInsensitive) == 0) {
                    currentValue = s;
                    current      = &currentValue;
                    matched      = true;
                    break;
                }
            }
            if (matched)
                continue;

            // An unrecognised heading — a typo, or a section a later version
            // added and this one doesn't know. Keep it and everything under
            // it; never guess which section was meant.
            current = nullptr;
            preservedLines.append(raw);
            continue;
        }

        if (line.isEmpty()) {
            // Blank lines are structure, not content. Inside a section they
            // are dropped (render puts its own back); outside one they are
            // only worth keeping if something else already is, so a file of
            // blank lines doesn't become a growing preserved block.
            if (!current && !preservedLines.isEmpty())
                preservedLines.append(raw);
            continue;
        }

        if (current && line.startsWith(kBullet)) {
            const QString entry = line.mid(kBullet.size()).trimmed();
            if (!entry.isEmpty())
                f.entriesFor(*current).append(entry);
            continue;
        }

        // Prose inside a section, or anything at all outside one. Preserved
        // rather than adopted: adopting it would silently promote a stray
        // sentence into something the model is told every turn.
        preservedLines.append(raw);
    }

    // Trailing blank lines carry no information and would otherwise grow by
    // one on every save.
    while (!preservedLines.isEmpty() && preservedLines.last().trimmed().isEmpty())
        preservedLines.removeLast();

    f.preserved = preservedLines.join(QLatin1Char('\n'));
    return f;
}

// ---- render ----------------------------------------------------------------

QString render(const File& f)
{
    QStringList out;

    for (Section s : allSections()) {
        const QStringList& entries = f.entriesFor(s);
        if (entries.isEmpty())
            continue; // no empty headings in a file a human is meant to read
        out.append(kHeadingPrefix + sectionHeading(s));
        for (const QString& e : entries)
            out.append(kBullet + e);
        out.append(QString());
    }

    if (!f.preserved.isEmpty()) {
        out.append(kHeadingPrefix + preservedHeading());
        out.append(f.preserved);
        out.append(QString());
    }

    return out.join(QLatin1Char('\n'));
}

// ---- promptBand ------------------------------------------------------------

QString promptBand(const File& f, int budgetChars)
{
    if (f.isEmpty() || budgetChars <= 0)
        return {};

    QString out;

    for (Section s : allSections()) {
        const QStringList& entries = f.entriesFor(s);
        if (entries.isEmpty())
            continue;

        // The heading is only paid for if at least one entry under it fits,
        // so a section trimmed to nothing leaves no orphan header behind.
        QString block;
        for (const QString& e : entries) {
            const QString headingCost =
                block.isEmpty() ? (out.isEmpty() ? QString() : QStringLiteral("\n\n"))
                                      + sectionHeading(s)
                                : QString();
            const QString candidate = headingCost + QLatin1Char('\n') + kBullet + e;

            // Dropped WHOLE, never truncated — an entry cut in half reads as
            // a fact with its qualifier removed.
            if (out.size() + block.size() + candidate.size() > budgetChars)
                continue;

            block += candidate;
        }
        out += block;
    }

    return out.trimmed();
}

} // namespace memory
