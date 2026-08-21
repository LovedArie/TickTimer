#pragma once

// ---------------------------------------------------------------------------
// Version.h — the ONE place TickTimer's version number lives.
//
// Until now "v18" was folklore: it lived in the session notes, in two .rc
// files, in an installer script — four copies, guaranteed to drift. Auto-
// update makes the version load-bearing (the app compares itself against the
// server's answer), and a load-bearing value gets a single source of truth.
//
// The #defines up top are deliberately plain preprocessor — no Qt, no C++ —
// because the Windows RESOURCE compiler (windres) also includes this file to
// stamp the exe's version metadata. RC_INVOKED is the standard symbol that
// compiler defines about itself; everything below the guard is invisible to
// it. One file, three consumers: C++ code, the .rc resources, and (by hand,
// noted in a comment there) the installer script.
// ---------------------------------------------------------------------------

// ---- v26.8: the third act of this file's own drift story -------------------
// Found by the v26.8 documentation audit, and it is worth the paragraph
// because it is a DIFFERENT failure from the two below.
//
// This file sat at 26.0.0 while the tree contained the catch-up feature
// (v26.2), the chip states (v26.7) and `lookBackDays = 3` (v26.8 — the code
// is right there in MissedBlocks.h). Four drops shipped without touching it.
// The README knew: it said "v26.8" in one line and "v27.0" in another.
//
// The guard below did not fire, and could not have. It proves the three
// MACROS and the STRING agree with each other — internal consistency, and it
// does that job perfectly. Nothing in a compiler knows what version the WORK
// is at. That is not a checkable fact, it is an external one, and external
// facts need a step in a human process, not a static_assert.
//
// So: **bumping this file is now item one of the release checklist**
// (docs/SESSION_NOTES.md, "shipping a drop"). The lesson generalises past
// this repo — when you catch a class of bug with a compile-time check, write
// down what the check still cannot see, or you will trust it for that too.
#define TICKTIMER_VERSION_MAJOR  30
#define TICKTIMER_VERSION_MINOR  4
#define TICKTIMER_VERSION_PATCH  4
#define TICKTIMER_VERSION_STRING "30.4.4"

#ifndef RC_INVOKED

#include <QString>
#include <QStringList>

namespace version
{

// ---- the guard this file forgot to give itself (v24) ----------------------
// Two representations of one fact live above: three numbers for the resource
// compiler, one string for everything else. v23.0 shipped with the numbers at
// 22.0.0 and the notes at 23.0.0 — the anti-drift file drifting internally,
// which the question bank duly recorded as the session's most ironic bug.
//
// The fix isn't discipline, it's the compiler. This constexpr parser reads
// the STRING at compile time and compares it to the MACROS; disagree, and the
// build stops with a message naming both. A single source of truth that isn't
// enforced is just the first of several copies.
//
// (Why not build the string from the numbers with the ## / # stringify trick?
// Because the .rc files consume TICKTIMER_VERSION_STRING as a single token in
// a VALUE statement, and resource compilers are unreliable about adjacent
// string-literal concatenation there. Checking is portable; generating isn't.)
namespace guard
{
constexpr int digitsFrom(const char* s, int& pos)
{
    int  value = 0;
    bool any   = false;
    while (s[pos] >= '0' && s[pos] <= '9') {
        value = value * 10 + (s[pos] - '0');
        ++pos;
        any = true;
    }
    return any ? value : -1;
}

constexpr bool stringMatchesMacros(const char* s)
{
    int       pos   = 0;
    const int major = digitsFrom(s, pos);
    if (s[pos] != '.')
        return false;
    ++pos;
    const int minor = digitsFrom(s, pos);
    if (s[pos] != '.')
        return false;
    ++pos;
    const int patch = digitsFrom(s, pos);
    return s[pos] == '\0' && major == TICKTIMER_VERSION_MAJOR
           && minor == TICKTIMER_VERSION_MINOR
           && patch == TICKTIMER_VERSION_PATCH;
}
} // namespace guard

static_assert(guard::stringMatchesMacros(TICKTIMER_VERSION_STRING),
              "Version.h: TICKTIMER_VERSION_STRING disagrees with the "
              "MAJOR/MINOR/PATCH macros. Update BOTH — they are one fact.");

// The app's own version, as code (not string) — comparisons never touch
// string ordering.
inline QString current()
{
    return QStringLiteral(TICKTIMER_VERSION_STRING);
}

// ---- semantic versioning, the pure brain ---------------------------------
// "major.minor.patch". The comparison is field-by-field NUMERIC, never
// string comparison — as strings, "18.10.0" < "18.9.0" because '1' < '9',
// which is precisely the bug that ships in a hurry and bites a year later.

struct Semver
{
    int  major = 0;
    int  minor = 0;
    int  patch = 0;
    bool valid = false;
};

inline Semver parse(const QString& text)
{
    Semver v;
    const QStringList parts = text.trimmed().split(QLatin1Char('.'));
    if (parts.size() != 3)
        return v; // invalid stays invalid — garbage in, .valid == false out
    bool okA = false, okB = false, okC = false;
    v.major = parts[0].toInt(&okA);
    v.minor = parts[1].toInt(&okB);
    v.patch = parts[2].toInt(&okC);
    v.valid = okA && okB && okC
              && v.major >= 0 && v.minor >= 0 && v.patch >= 0;
    return v;
}

// Classic three-way compare: negative if a < b, 0 if equal, positive if
// a > b. Field by field, most significant first.
inline int compare(const Semver& a, const Semver& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

// "Is `candidate` strictly newer than `baseline`?" — with the fail-CLOSED
// posture an updater needs: if either string doesn't parse, the answer is
// no. A malformed version.json must produce silence, never a nag banner
// built on garbage.
inline bool isNewer(const QString& candidate, const QString& baseline)
{
    const Semver c = parse(candidate);
    const Semver b = parse(baseline);
    if (!c.valid || !b.valid)
        return false;
    return compare(c, b) > 0;
}

// ---- the banner decision, as a pure function -----------------------------
// Same pattern as sync::decide and compare::focusVerdict: the feature's one
// real judgement extracted where a table of tiny tests can pin it. The
// non-nag rule lives HERE, not scattered through widget code:
//
//   show the banner iff the advertised version is strictly newer than us
//   AND it isn't the exact version the person already dismissed.
//
// Dismissing 19.0.0 silences 19.0.0 forever — but 19.0.1 speaks up again,
// because a dismissal is "stop telling me about THIS one", not "never talk
// to me again".

enum class Banner { Show, Silent };

inline Banner decideBanner(const QString& currentVersion,
                           const QString& latestVersion,
                           const QString& lastDismissed)
{
    if (!isNewer(latestVersion, currentVersion))
        return Banner::Silent; // up to date (or garbage) — say nothing
    if (parse(lastDismissed).valid
        && compare(parse(latestVersion), parse(lastDismissed)) == 0)
        return Banner::Silent; // they said "not this one" — respect it
    return Banner::Show;
}

} // namespace version

#endif // RC_INVOKED
