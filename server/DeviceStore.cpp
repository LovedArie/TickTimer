#include "DeviceStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>

namespace
{

// 128 bits from the system CSPRNG, hex-encoded — the same recipe (and the
// same entropy) as the session tokens AuthServer mints. Never QRandom-
// Generator::global(): that one is seeded for speed, not for secrecy.
QString newRawToken()
{
    QByteArray raw(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32*>(raw.data()), raw.size() / 4);
    return QString::fromLatin1(raw.toHex());
}

// SHA-256, deliberately NOT auth::hashPassword's PBKDF2. See the header: you
// stretch what a human chose, because humans choose guessable things. You do
// not stretch 128 bits of CSPRNG output — there is nothing to guess, and the
// 200,000 iterations would only make every resume slow and hand an attacker a
// cheap way to make the server work.
QString hashToken(const QString& rawToken)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(rawToken.toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}

QString canonical(const QString& username)
{
    return username.trimmed().toLower();
}

} // namespace

DeviceStore::DeviceStore(const QString& filePath)
    : m_filePath(filePath)
{
    load();
}

QString DeviceStore::remember(const QString& username, const QString& label)
{
    const QString name = canonical(username);
    if (name.isEmpty())
        return {}; // no account, no device — refuse rather than orphan a row

    const QString raw = newRawToken();

    Device d;
    d.tokenHash  = hashToken(raw);
    d.username   = name;
    d.label      = label.trimmed().left(64); // a label, not an essay
    d.createdAt  = QDateTime::currentDateTimeUtc();
    d.lastSeenAt = d.createdAt;
    m_devices.append(d);
    save();

    // The ONLY time the raw token exists. Nothing here keeps a copy.
    return raw;
}

QString DeviceStore::userFor(const QString& rawToken)
{
    if (rawToken.trimmed().isEmpty())
        return {};

    const QString wanted = hashToken(rawToken.trimmed());
    for (Device& d : m_devices) {
        if (d.tokenHash == wanted) {
            d.lastSeenAt = QDateTime::currentDateTimeUtc();
            save();
            return d.username;
        }
    }
    return {}; // unknown, revoked or malformed — all the same answer
}

bool DeviceStore::forget(const QString& rawToken)
{
    if (rawToken.trimmed().isEmpty())
        return false;

    const QString wanted = hashToken(rawToken.trimmed());
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).tokenHash == wanted) {
            m_devices.remove(i);
            save();
            return true;
        }
    }
    return false; // already gone: the caller's goal is true either way
}

int DeviceStore::forgetAllFor(const QString& username)
{
    const QString name = canonical(username);
    if (name.isEmpty())
        return 0;

    int removed = 0;
    for (int i = m_devices.size() - 1; i >= 0; --i) {
        if (m_devices.at(i).username == name) {
            m_devices.remove(i);
            ++removed;
        }
    }
    if (removed > 0)
        save();
    return removed;
}

void DeviceStore::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) // first run: nobody remembered yet
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr =
        doc.object().value(QStringLiteral("devices")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Device d;
        d.tokenHash  = o.value(QStringLiteral("tokenHash")).toString();
        d.username   = o.value(QStringLiteral("username")).toString();
        d.label      = o.value(QStringLiteral("label")).toString();
        d.createdAt  = QDateTime::fromString(
            o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        d.lastSeenAt = QDateTime::fromString(
            o.value(QStringLiteral("lastSeenAt")).toString(), Qt::ISODate);
        // A row missing either half of its identity cannot authenticate
        // anybody, so dropping it is the tolerant read, not a loss.
        if (!d.tokenHash.isEmpty() && !d.username.isEmpty())
            m_devices.append(d);
    }
}

void DeviceStore::save() const
{
    QJsonArray arr;
    for (const Device& d : m_devices) {
        QJsonObject o;
        o[QStringLiteral("tokenHash")]  = d.tokenHash;
        o[QStringLiteral("username")]   = d.username;
        o[QStringLiteral("label")]      = d.label;
        o[QStringLiteral("createdAt")]  = d.createdAt.toString(Qt::ISODate);
        o[QStringLiteral("lastSeenAt")] = d.lastSeenAt.toString(Qt::ISODate);
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("devices")] = arr;

    QFileInfo(m_filePath).absoluteDir().mkpath(QStringLiteral("."));

    // QSaveFile, for the reason spelled out at length in AccountStore::save:
    // a plain QFile truncates on open, so an interruption in that window
    // leaves a well-formed EMPTY file rather than the previous contents.
    //
    // Losing devices.json is milder than losing accounts.json — nobody is
    // locked out, because a forgotten device just means a password prompt.
    // It is still exactly the failure v30.2 exists to prevent: every phone
    // asking for a password on every launch is what makes a phone app
    // intolerable, and it would arrive looking like a regression in the
    // remembered-device feature rather than like a half-written file.
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("DeviceStore: cannot open %s for writing — devices NOT saved",
                 qUtf8Printable(m_filePath));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit())
        qWarning("DeviceStore: commit failed for %s — devices NOT saved",
                 qUtf8Printable(m_filePath));
}
