#pragma once
// ---------------------------------------------------------------------------
// session — who this machine remembers, and what it holds to prove it (v30.2).
//
// Two separate things live here, and keeping them separate is the design:
//
//   the NAME      who logged in last, plus which accounts have local data.
//                 Proves nothing. Enough to open data-<user>.json when the
//                 server cannot be reached.
//   the DEVICE    a durable credential the server minted, exchangeable for a
//   TOKEN         fresh session token without a password. Optional.
//
// They are separate because they answer different questions and carry
// different risk. Offline start needs only the NAME — and needs no credential
// at all, because the login gate never protected local data in the first
// place: data.json sits in the account's own folder, readable by anyone who
// can read the folder. Login proves who you are TO THE SERVER; it was never a
// lock on the file. An offline door that opens on a remembered name therefore
// gives away nothing that was not already given.
//
// The token is the part that carries real weight, so it is opt-in, per
// account, and revocable from either end.
//
// WHY QSettings. It is neither taste nor domain data, so neither existing
// home is obviously right — but it is machine-local, must never sync, and
// belongs beside sync/serverUrl which already lives there. data.json is
// exactly wrong: that file syncs, and a credential that replicated itself to
// every device would defeat the point of being per-device.
//
// WHAT THIS IS NOT: encryption. QSettings is plaintext (the registry on
// Windows, an ini or plist elsewhere). Anyone who can read it can also read
// data.json next door, so the token adds one thing to that exposure — reach
// to the SERVER copy from elsewhere — which is why revoking exists and why
// "remember me" is a choice rather than a default.
// ---------------------------------------------------------------------------

#include <QString>
#include <QStringList>

namespace session
{

// Who logged in last on this machine. Empty on a first run.
QString lastUser();
void    setLastUser(const QString& username);

// The durable credential for one account, or empty for "not remembered".
// Per account, so two people sharing a desktop cannot inherit each other's.
QString deviceToken(const QString& username);
void    setDeviceToken(const QString& username, const QString& token);
void    clearDeviceToken(const QString& username);

// Accounts with a planner file on this machine, canonical and sorted.
//
// Scanned from disk rather than remembered in settings, because the files are
// the truth: a planner copied in by hand should be openable, and an account
// whose file was deleted should stop being offered. Same glob JsonStore's own
// migration already walks.
QStringList localAccounts();

// A human-readable name for THIS machine, for the revoke list on the server.
// Cosmetic by design — nothing authenticates on it, so a wrong or duplicated
// label costs nothing but a confusing row in a list.
QString deviceLabel();

} // namespace session
