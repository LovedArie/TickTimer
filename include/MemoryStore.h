#pragma once
// ---------------------------------------------------------------------------
// MemoryStore — the disk half of the residue file (§L, v30.0).
//
// Deliberately thin. Everything that decides ANYTHING about memory lives in
// Memory.h as pure functions; this class knows one thing the pure half must
// never know, which is where the bytes are. Same division JsonStore keeps for
// the planner, and it is what lets the whole parse/render/budget contract be
// asserted offline in microseconds.
//
// A SIDECAR, NOT A SLOT IN data.json (§L.5, decided in v30.0). The file is
// plain Markdown you can open in any editor and correct by hand, which the
// addendum calls a trust feature and this class is built around: a save must
// never destroy text the parser did not understand.
//
// IT DOES NOT SYNC — and that is not the same claim as "it never leaves the
// machine". It never travels to the TickTimer server, so it does not follow
// you to another device. It IS sent to your AI provider inside the system
// prompt on every chat turn, exactly like the briefing. docs/AI.md states
// both halves; do not let a comment here imply only the flattering one.
// ---------------------------------------------------------------------------

#include "Memory.h"

#include <QString>

class MemoryStore
{
public:
    explicit MemoryStore(QString filePath);

    // The OS-blessed app-data folder, same one JsonStore uses, so a person
    // looking for their data finds the planner and the memory side by side.
    static QString defaultFilePath();

    // Per-account: memory-<username>.md beside data-<username>.json.
    //
    // Mirrors JsonStore::filePathForUser() exactly — same canonical
    // lowercasing ("Alice" and "alice" are one account, so one file), same
    // fallback to a global file when the username is empty (tests, tools, a
    // build with login disabled). Mirrored on purpose: if these two ever
    // disagreed about what a username maps to, logging in would pair one
    // person's planner with another person's memory.
    static QString pathForUser(const QString& username);

    const QString& filePath() const { return m_filePath; }

    // A missing file is not an error — it is a person who has not written
    // anything yet, which is the normal first-run state. Returns an empty
    // File for both "no file" and "empty file", and there is no invalid
    // memory file: parse() is tolerant by construction.
    memory::File load() const;

    // Atomic write-then-replace via QSaveFile, the same reliability rule the
    // planner gets: a crash mid-save must leave the previous file intact.
    bool save(const memory::File& f);

    // Empty when the last operation succeeded.
    const QString& errorMessage() const { return m_error; }

private:
    QString         m_filePath;
    mutable QString m_error;
};
