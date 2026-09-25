// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Paths.h"

#include <QDir>
#include <QStandardPaths>

#include <core/Logging.h>

#include <array>

namespace linernotes::core {

Paths Paths::standard()
{
    Paths p;
    p.m_configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    p.m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    p.m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    p.m_logDir = QDir(p.m_dataDir).filePath(QStringLiteral("logs"));
    return p;
}

Paths Paths::underRoot(const QString &root)
{
    const QString cleanRoot = QDir::cleanPath(root);
    Paths p;
    p.m_configDir = QDir(cleanRoot).filePath(QStringLiteral("config"));
    p.m_dataDir = QDir(cleanRoot).filePath(QStringLiteral("data"));
    p.m_cacheDir = QDir(cleanRoot).filePath(QStringLiteral("cache"));
    p.m_logDir = QDir(cleanRoot).filePath(QStringLiteral("logs"));
    return p;
}

Paths Paths::fromEnvironment()
{
    const QString home = qEnvironmentVariable("LINERNOTES_HOME");
    if (!home.isEmpty()) {
        return underRoot(home);
    }
    return standard();
}

QString Paths::configDir() const
{
    return m_configDir;
}

QString Paths::dataDir() const
{
    return m_dataDir;
}

QString Paths::cacheDir() const
{
    return m_cacheDir;
}

QString Paths::logDir() const
{
    return m_logDir;
}

bool Paths::ensureCreated() const
{
    const std::array<QString, 4> dirs = {
        m_configDir,
        m_dataDir,
        m_cacheDir,
        m_logDir,
    };

    bool allOk = true;
    for (const auto &dir : dirs) {
        if (dir.isEmpty()) {
            continue;
        }
        if (!QDir().mkpath(dir)) {
            qCWarning(lcCore) << "Failed to create directory:" << dir;
            allOk = false;
        }
    }
    return allOk;
}

} // namespace linernotes::core
