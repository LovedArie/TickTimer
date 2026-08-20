#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// DeviceStore — the server's registry of REMEMBERED DEVICES (v30.2).
//
// WHY THIS EXISTS, and why it does not contradict the comment it appears to.
// AuthServer's session tokens are in-memory on purpose: "tokens are session
// state, not records… persisting tokens would be persisting open doors." That
// reasoning is still right, and session tokens are untouched — they still die
// with the process and still cost nothing to forget.
//
// What it assumed is the part that stopped being true. It justified itself
// with "logging in again mints fresh ones — which the app already does on
// every launch", and that was a fair description of a desktop app on your own
// LAN. A phone is a different animal: it launches constantly, often with no
// server in reach, and asking someone to type a password every single time is
// how an app stops being used.
//
// So this is a SECOND, DIFFERENT credential rather than a persisted session
// token — the standard split between a short-lived access token and a
// long-lived refresh one:
//
//     session token   in memory, dies on restart, proves a live session
//     device token    persisted HERE, survives restarts, proves only
//                     "this device already logged in once" and can be
//                     exchanged for a fresh session token
//
// The door that persists is therefore not an open one: it opens exactly one
// thing (a new session token for one account), it is revocable per device
// without touching anyone else, and revoking it is a single line in a file.
//
// STORED HASHED, NEVER RAW. A stolen devices.json must not be a stack of
// working credentials — the same instinct that keeps passwordHash out of
// reach in AccountStore. But the hash is a plain SHA-256, NOT the PBKDF2 that
// passwords get, and the difference is the point: stretching exists to make
// GUESSING expensive, and nobody guesses a 128-bit value drawn from the
// system CSPRNG. Paying 200,000 iterations per resume would buy no security
// and hand anyone a cheap way to make the server work hard.
//
// Value semantics, tolerant load, additive schema, save-after-mutation — the
// same shape as AccountStore, for the same reasons.
// ---------------------------------------------------------------------------

struct Device
{
    QString   tokenHash;  // sha256 hex — the raw token is never written down
    QString   username;   // canonical (lower-cased), as everywhere else
    QString   label;      // "Arie's phone" — for a human revoking one later
    QDateTime createdAt;
    QDateTime lastSeenAt; // updated on each resume; what a revoke UI shows
};

class DeviceStore
{
public:
    explicit DeviceStore(const QString& filePath);

    // Mint a device token for an account.
    //
    // Returns the RAW token, which exists in this process exactly once and is
    // never stored: the caller must hand it to the client immediately or lose
    // it forever. That is the property that makes the file safe to leak.
    QString remember(const QString& username, const QString& label);

    // Raw token -> canonical username, or "" for anything unknown, revoked or
    // malformed. Fail-safe like every other lookup in this project: an
    // unrecognised credential resolves to nobody rather than to somebody.
    //
    // NOT const: a successful resolve touches lastSeenAt, because "when did
    // this device last appear" is the only thing that makes a revoke list
    // readable to a human.
    QString userFor(const QString& rawToken);

    // Revoke one device. Idempotent — forgetting an unknown token is a
    // success, because the caller's goal (that token does not work) is
    // already true. Returns whether anything was actually removed.
    bool forget(const QString& rawToken);

    // Revoke every device for an account. The "I lost my phone" door, and
    // what a future password change should call.
    int forgetAllFor(const QString& username);

    int count() const { return m_devices.size(); }

private:
    void load();
    void save() const;

    QString         m_filePath;
    QVector<Device> m_devices;
};
