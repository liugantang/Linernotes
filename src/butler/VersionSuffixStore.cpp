// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringView>
#include <QVariant>

#include <butler/VersionSuffixStore.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/Errors.h>

#include <optional>

namespace linernotes::butler {

namespace {

QString suffixRoleToString(SuffixRole role)
{
    switch (role) {
    case SuffixRole::Version:
        return QStringLiteral("version");
    case SuffixRole::Annotation:
        return QStringLiteral("annotation");
    case SuffixRole::TitlePart:
        return QStringLiteral("title_part");
    case SuffixRole::Unknown:
        return { };
    }
    return { };
}

std::optional<SuffixRole> suffixRoleFromString(QStringView str)
{
    if (str == QLatin1StringView("version")) {
        return SuffixRole::Version;
    }
    if (str == QLatin1StringView("annotation")) {
        return SuffixRole::Annotation;
    }
    if (str == QLatin1StringView("title_part")) {
        return SuffixRole::TitlePart;
    }
    return std::nullopt;
}

} // namespace

VersionSuffixStore::VersionSuffixStore(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<QHash<QString, SuffixVerdict>> VersionSuffixStore::loadAll(int promptVersion) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT suffix_key, role, version_type, confidence, reason "
                             "FROM version_suffixes WHERE prompt_version = ?"));
    q.addBindValue(promptVersion);

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QHash<QString, SuffixVerdict> results;
    while (q.next()) {
        const QString key = q.value(0).toString();
        const QString roleStr = q.value(1).toString();
        const auto roleOpt = suffixRoleFromString(roleStr);
        if (!roleOpt.has_value()) {
            continue;
        }

        library::VersionType vt = library::VersionType::Studio;
        if (*roleOpt == SuffixRole::Version) {
            const QString typeStr = q.value(2).toString();
            const auto typeOpt = library::versionTypeFromString(typeStr);
            if (!typeOpt.has_value()) {
                continue;
            }
            vt = *typeOpt;
        }

        const double confidence = q.value(3).toDouble();
        const QString reason = q.value(4).toString();

        results.insert(key,
            SuffixVerdict {
                .cls = SuffixClass {
                    .role = *roleOpt,
                    .type = vt,
                },
                .confidence = confidence,
                .reason = reason,
            });
    }

    return results;
}

core::Result<void> VersionSuffixStore::save(
    const QHash<QString, SuffixVerdict> &verdicts, const QString &model, int promptVersion) const
{
    if (verdicts.isEmpty()) {
        return { };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const qint64 now = m_clock.nowMs();

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for version suffix save"),
            .detail = QString(),
        };
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO version_suffixes "
                             "(suffix_key, role, version_type, confidence, reason, model, "
                             "prompt_version, classified_at) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));

    for (auto it = verdicts.cbegin(); it != verdicts.cend(); ++it) {
        const QString &key = it.key();
        const SuffixVerdict &verdict = it.value();

        const QString roleStr = suffixRoleToString(verdict.cls.role);
        if (roleStr.isEmpty()) {
            continue;
        }

        QVariant versionTypeVal;
        if (verdict.cls.role == SuffixRole::Version) {
            versionTypeVal = library::versionTypeToString(verdict.cls.type);
        } else {
            versionTypeVal = QVariant(QMetaType(QMetaType::QString));
        }

        q.bindValue(0, key);
        q.bindValue(1, roleStr);
        q.bindValue(2, versionTypeVal);
        q.bindValue(3, verdict.confidence);
        q.bindValue(4, verdict.reason);
        q.bindValue(5, model);
        q.bindValue(6, promptVersion);
        q.bindValue(7, now);

        if (!q.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = q.lastError().text(),
                .detail = key,
            };
        }
    }

    return tx.commit();
}

} // namespace linernotes::butler
