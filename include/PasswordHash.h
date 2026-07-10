#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QString>

// ---------------------------------------------------------------------------
// PasswordHash — secure credential storage, done with only what Qt ships.
//
// THE ONE RULE OF PASSWORDS: never store the password. Store a value derived
// from it that (a) can't be reversed, and (b) is deliberately SLOW to
// compute, so an attacker who steals the file can't brute-force millions of
// guesses per second. Three ingredients make that true:
//
//   1. HASH — a one-way function. Given the hash you cannot get the password
//      back; you can only check a guess by hashing it and comparing.
//   2. SALT — a random value stored alongside each hash. Without it, two
//      users with the same password get the same hash (and "rainbow tables"
//      of precomputed hashes crack them instantly). The salt makes every
//      hash unique even for identical passwords.
//   3. STRETCHING — hashing thousands of times in a row. One SHA-256 is
//      microseconds; 200k of them is ~100ms. Unnoticeable when you log in
//      once; ruinous for an attacker trying billions of guesses.
//
// This is PBKDF2 in spirit (salt + iterated hashing). A PRODUCTION app should
// use a purpose-built password hash — Argon2 or bcrypt — which also resist
// GPU/ASIC attacks in ways a plain SHA loop does not. We use QCryptographicHash
// because it's in every Qt install and it teaches the SHAPE correctly; the
// swap to Argon2 is one function body, and the stored format below already
// carries the algorithm tag to make that migration clean.
//
// Stored format (one string, self-describing so verify() needs no config):
//     pbkdf2$<iterations>$<salt-hex>$<hash-hex>
// The leading tag is why a future Argon2 upgrade is painless: old hashes say
// "pbkdf2", new ones say "argon2", verify() dispatches on the tag, and nobody
// has to reset their password.
// ---------------------------------------------------------------------------

namespace auth
{

inline constexpr int kIterations = 200000; // ~100ms on a modern CPU
inline constexpr int kSaltBytes  = 16;

// Derive the raw hash bytes from a password + salt by iterated SHA-256.
// Each round feeds the previous digest back in — that chaining is the
// "stretching" that makes the whole thing slow on purpose.
inline QByteArray pbkdf2(const QString& password, const QByteArray& salt,
                         int iterations)
{
    QByteArray data = salt + password.toUtf8();
    QByteArray digest =
        QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    for (int i = 1; i < iterations; ++i)
        digest = QCryptographicHash::hash(salt + digest,
                                          QCryptographicHash::Sha256);
    return digest;
}

// Hash a fresh password for storage. Generates a new random salt every time —
// so registering the same password twice yields two different stored strings,
// exactly as it should.
inline QString hashPassword(const QString& password)
{
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32*>(salt.data()), kSaltBytes / 4);

    const QByteArray digest = pbkdf2(password, salt, kIterations);
    return QStringLiteral("pbkdf2$%1$%2$%3")
        .arg(kIterations)
        .arg(QString::fromLatin1(salt.toHex()),
             QString::fromLatin1(digest.toHex()));
}

// Check a login attempt against a stored hash. Re-derives using the SALT and
// ITERATION COUNT read from the stored string — never from the constants
// above — so hashes made with older parameters still verify. Returns false on
// any malformed input rather than throwing: a corrupt record is a failed
// login, not a crash.
inline bool verifyPassword(const QString& password, const QString& stored)
{
    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() != 4 || parts[0] != QLatin1String("pbkdf2"))
        return false;

    bool ok = false;
    const int iterations = parts[1].toInt(&ok);
    if (!ok || iterations < 1)
        return false;

    const QByteArray salt   = QByteArray::fromHex(parts[2].toLatin1());
    const QByteArray expect = QByteArray::fromHex(parts[3].toLatin1());
    const QByteArray actual = pbkdf2(password, salt, iterations);

    // Length-independent equality would be even better (constant-time compare
    // resists timing attacks); QByteArray's == is fine for a home server and
    // keeps the teaching focused. Flagged, not gold-plated.
    return actual == expect;
}

} // namespace auth
