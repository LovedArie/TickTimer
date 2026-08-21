#include "SessionStore.h"

#include "JsonStore.h"

#include <QDir>
#include <QFileInfo>
#include <QHostInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>

namespace session
{
namespace
{

// The same canonical form the server and JsonStore::filePathForUser both use.
// If these three ever disagreed about what a username means, a device token
// could be filed under one spelling and looked up under another.
QString canonical(const QString& username)
{
    return username.trimmed().toLower();
}

QString tokenKey(const QString& username)
{
    return QStringLiteral("sync/device/") + canonical(username);
}

} // namespace

QString lastUser()
{
    return QSettings().value(QStringLiteral("sync/lastUser")).toString();
}

void setLastUser(const QString& username)
{
    QSettings().setValue(QStringLiteral("sync/lastUser"), canonical(username));
}

QString deviceToken(const QString& username)
{
    if (canonical(username).isEmpty())
        return {};
    return QSettings().value(tokenKey(username)).toString();
}

void setDeviceToken(const QString& username, const QString& token)
{
    if (canonical(username).isEmpty())
        return;
    if (token.isEmpty()) {
        clearDeviceToken(username);
        return;
    }
    QSettings().setValue(tokenKey(username), token);
}

void clearDeviceToken(const QString& username)
{
    if (canonical(username).isEmpty())
        return;
    // remove(), not setValue(""), so a revoked device leaves no key behind at
    // all — an empty string in a settings file reads like a credential
    // someone half-wrote.
    QSettings().remove(tokenKey(username));
}

QStringList localAccounts()
{
    // Derive the folder from the store itself rather than re-deriving the
    // QStandardPaths location here: one function owns where planners live,
    // and asking it keeps this list pointing at the same place even if that
    // ever moves.
    const QDir dir = QFileInfo(JsonStore::defaultFilePath()).absoluteDir();

    QStringList out;
    const QStringList files =
        dir.entryList({QStringLiteral("data-*.json")}, QDir::Files);
    for (const QString& f : files) {
        // "data-alice.json" -> "alice". The prefix and suffix are fixed by
        // filePathForUser, so this is a reverse of a known shape rather than
        // a guess about arbitrary filenames.
        QString name = f;
        name.remove(0, QStringLiteral("data-").size());
        name.chop(QStringLiteral(".json").size());
        if (!name.isEmpty())
            out.append(name);
    }
    out.sort();
    return out;
}

static QString pendingKey(const QString& username)
{
    return QStringLiteral("sync/offlinePending/") + canonical(username);
}


bool offlineEditsPending(const QString& username)
{
    if (canonical(username).isEmpty())
        return false;
    return QSettings().value(pendingKey(username), false).toBool();
}

void setOfflineEditsPending(const QString& username, bool pending)
{
    if (canonical(username).isEmpty())
        return;
    if (pending)
        QSettings().setValue(pendingKey(username), true);
    else
        QSettings().remove(pendingKey(username));
}

QString deviceLabel()
{
    // Machine name first — it is what a person recognises in a list of
    // devices. Falls back to the OS product name, and then to something
    // rather than nothing, because a blank row is worse than a vague one.
    const QString host = QHostInfo::localHostName().trimmed();
    if (!host.isEmpty())
        return host;
    const QString product = QSysInfo::prettyProductName().trimmed();
    if (!product.isEmpty())
        return product;
    return QStringLiteral("Unknown device");
}

} // namespace session
