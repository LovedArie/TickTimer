#include "AccountStore.h"

#include "PasswordHash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

AccountStore::AccountStore(const QString& filePath)
    : m_filePath(filePath)
{
    load();
}

const Account* AccountStore::findByName(const QString& username) const
{
    // Case-INSENSITIVE: "Alice" and "alice" are the same person. Usernames
    // that differ only in case are a classic source of "I can't log in"
    // confusion and duplicate-account bugs, so we collapse the distinction at
    // the one place identity is resolved.
    for (const Account& a : m_accounts)
        if (a.username.compare(username, Qt::CaseInsensitive) == 0)
            return &a;
    return nullptr;
}

bool AccountStore::hasUser(const QString& username) const
{
    return findByName(username) != nullptr;
}

AccountStore::Result AccountStore::registerAccount(const QString& username,
                                                   const QString& password)
{
    const QString name = username.trimmed();
    // Minimal input gate. A real service would enforce a password policy here;
    // the point for now is that empty credentials never reach the hasher.
    if (name.isEmpty() || password.isEmpty())
        return Result::InvalidInput;
    // Usernames become FILENAMES on the server (planners/<name>.json), so
    // the charset is restricted at THIS door: letters, digits, _ and -.
    // Never let raw user input become a file path — "../../etc/passwd" is a
    // username somebody will eventually try, and refusing it here keeps
    // every layer below (PlannerStore included) simple and safe.
    static const QRegularExpression allowed(
        QStringLiteral("^[A-Za-z0-9_-]{1,32}$"));
    if (!allowed.match(name).hasMatch())
        return Result::InvalidInput;
    if (findByName(name))
        return Result::UsernameTaken;

    Account a;
    a.username     = name;
    a.passwordHash = auth::hashPassword(password); // plaintext dies here
    a.createdAt    = QDateTime::currentDateTimeUtc();
    m_accounts.append(a);
    save();
    return Result::Ok;
}

AccountStore::Result AccountStore::login(const QString& username,
                                         const QString& password) const
{
    const Account* a = findByName(username.trimmed());
    if (!a)
        return Result::UsernameNotFound;
    if (!auth::verifyPassword(password, a->passwordHash))
        return Result::WrongPassword;
    return Result::Ok;
}

// ---- persistence ----------------------------------------------------------
// Same shape as JsonStore: tolerant reads (missing file = empty store, not an
// error), additive object schema, one atomic-ish write. No migrations — new
// fields just default when absent.

void AccountStore::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) // first run: nobody registered yet
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object().value(QStringLiteral("accounts")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Account a;
        a.username     = o.value(QStringLiteral("username")).toString();
        a.passwordHash = o.value(QStringLiteral("passwordHash")).toString();
        a.createdAt    = QDateTime::fromString(
            o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        if (!a.username.isEmpty() && !a.passwordHash.isEmpty())
            m_accounts.append(a);
    }
}

void AccountStore::save() const
{
    QJsonArray arr;
    for (const Account& a : m_accounts) {
        QJsonObject o;
        o[QStringLiteral("username")]     = a.username;
        o[QStringLiteral("passwordHash")] = a.passwordHash;
        o[QStringLiteral("createdAt")]    = a.createdAt.toString(Qt::ISODate);
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("accounts")] = arr;

    QFileInfo(m_filePath).absoluteDir().mkpath(QStringLiteral("."));
    QFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
