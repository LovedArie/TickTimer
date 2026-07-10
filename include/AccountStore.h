#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// AccountStore — the server's registry of who exists. The SERVER side's
// answer to AppData: one owner of the data, mutations through named doors,
// tolerant JSON load, additive schema. Deliberately NOT coupled to AppData or
// any planner data — an account is "a name + a way to prove it's you", and
// nothing about time tracking belongs here. Keeping identity and planner data
// in separate stores is the same settings-vs-domain separation the desktop
// app already lives by, one level up.
//
// This is a plain value-semantics class (no QObject, no signals): the server
// is request/response, not an event loop of live listeners. It loads on
// startup, mutates on register, saves after each change. Simpler than AppData
// on purpose — a server has different needs than a live UI.
// ---------------------------------------------------------------------------

struct Account
{
    QString   username;      // the login name; unique, case-insensitive
    QString   passwordHash;  // pbkdf2$... — NEVER the password itself
    QDateTime createdAt;
};

class AccountStore
{
public:
    // The store reads/writes ONE json file. The server picks the path (its own
    // data dir), exactly as AppData lets the app pick where data.json lives —
    // storage location is the caller's policy, not the store's business.
    explicit AccountStore(const QString& filePath);

    // Result of a register/login attempt. An enum, not a bool, because the UI
    // needs to tell the reasons apart — "name taken" and "wrong password" are
    // different messages to a human, and making them different VALUES (not
    // just different log strings) is what lets the dialog respond correctly.
    enum class Result {
        Ok,
        UsernameTaken,      // register: someone already has this name
        UsernameNotFound,   // login: no such account
        WrongPassword,      // login: account exists, password mismatch
        InvalidInput        // empty username/password, etc.
    };

    // Create an account. Hashes the password (the plaintext never leaves this
    // call), refuses duplicates case-insensitively, persists on success.
    Result registerAccount(const QString& username, const QString& password);

    // Check a login. Same guard shape as the domain doors: resolve, compare,
    // report — no side effects on failure.
    Result login(const QString& username, const QString& password) const;

    bool   hasUser(const QString& username) const;
    int    count() const { return m_accounts.size(); }

private:
    const Account* findByName(const QString& username) const;
    void load();
    void save() const;

    QString          m_filePath;
    QVector<Account> m_accounts;
};
