#pragma once
// ---------------------------------------------------------------------------
// Mood — the one fact the app cannot derive (v28.2, assistant roadmap §G.2).
//
// Everything else the check-in talks about is computed from data that
// already exists: the plan, the tracked time, the deadlines. How the owner
// FEELS must be asked — and therefore stored, which makes it the first new
// stored fact since dismissals, and the reason for format v12.
//
// Deliberately coarse: three levels, not a 1–10 scale. §G.2's argument is
// that pattern work ("Wednesday mornings are consistently rough") needs a
// comparable value, and a ten-point scale invites false precision that a
// 07:40 tap cannot honestly deliver. The optional note is for the human;
// only the coarse level ever enters an AI briefing — the note is the
// owner's words about their own state, and it stays theirs (the privacy
// test in test_domain pins this, it is not a comment).
//
// One mood PER DAY, upsert semantics: a check-in answers "how is today",
// not "append to a feelings log". Re-answering replaces.
// ---------------------------------------------------------------------------

#include <QDate>
#include <QString>

struct Mood
{
    enum class Level { Rough, Okay, Good };

    QDate   date;
    Level   level = Level::Okay;
    QString note; // optional, owner-only; NEVER serialised into a briefing
};

// Settings-token style serialisation, same shape as returnModeToString:
// unknown input repairs to Okay rather than failing a whole file load.
inline QString moodLevelToString(Mood::Level level)
{
    switch (level) {
    case Mood::Level::Rough: return QStringLiteral("rough");
    case Mood::Level::Okay:  return QStringLiteral("okay");
    case Mood::Level::Good:  return QStringLiteral("good");
    }
    return QStringLiteral("okay");
}

inline Mood::Level moodLevelFromString(const QString& s)
{
    if (s == QLatin1String("rough")) return Mood::Level::Rough;
    if (s == QLatin1String("good"))  return Mood::Level::Good;
    return Mood::Level::Okay;
}
