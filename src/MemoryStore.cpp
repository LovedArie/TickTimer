#include "MemoryStore.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

MemoryStore::MemoryStore(QString filePath)
    : m_filePath(std::move(filePath))
{
}

QString MemoryStore::defaultFilePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/memory.md");
}

QString MemoryStore::pathForUser(const QString& username)
{
    if (username.trimmed().isEmpty())
        return defaultFilePath();

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/memory-")
         + username.trimmed().toLower() + QStringLiteral(".md");
}

memory::File MemoryStore::load() const
{
    m_error.clear();

    QFile file(m_filePath);
    if (!file.exists())
        return {}; // first run — nothing written yet, not a failure

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QStringLiteral("Could not read %1: %2")
                      .arg(m_filePath, file.errorString());
        return {};
    }

    QTextStream in(&file);
    // Explicit UTF-8 both ways. The file is meant to be opened in whatever
    // editor the owner already has, and a name with an accent in it must
    // survive that round trip — the locale default would not guarantee it.
    in.setEncoding(QStringConverter::Utf8);
    return memory::parse(in.readAll());
}

bool MemoryStore::save(const memory::File& f)
{
    m_error.clear();

    // Same reliability rule as the planner: QSaveFile writes to a temporary
    // and only commit() renames it over the real file, so a crash mid-save
    // leaves the previous memory intact rather than half a file.
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = QStringLiteral("Could not write %1: %2")
                      .arg(m_filePath, file.errorString());
        return false;
    }

    file.write(memory::render(f).toUtf8());

    if (!file.commit()) {
        m_error = QStringLiteral("Could not commit save to %1: %2")
                      .arg(m_filePath, file.errorString());
        return false;
    }
    return true;
}
