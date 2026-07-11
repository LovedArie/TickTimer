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

#define TICKTIMER_VERSION_MAJOR  19
#define TICKTIMER_VERSION_MINOR  1
#define TICKTIMER_VERSION_PATCH  0
#define TICKTIMER_VERSION_STRING "19.1.0"

#ifndef RC_INVOKED

#include <QString>
#include <QStringList>

namespace version
{

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
