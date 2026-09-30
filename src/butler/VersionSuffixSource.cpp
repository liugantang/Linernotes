// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <butler/Errors.h>
#include <butler/TitleVersion.h>
#include <butler/VersionSuffixSource.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>

namespace linernotes::butler {

core::Result<QList<SuffixSample>> parseSuffixItemKey(const QString &itemKey)
{
    QJsonParseError parseErr { };
    const auto doc = QJsonDocument::fromJson(itemKey.toUtf8(), &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kVersionSuffixInvalidKey),
            .message = QStringLiteral("Failed to parse item key as JSON object"),
            .detail = itemKey,
        };
    }

    const QJsonObject obj = doc.object();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kVersionSuffixInvalidKey),
            .message = QStringLiteral("Missing or invalid 'items' array in item key"),
            .detail = itemKey,
        };
    }

    QList<SuffixSample> samples;
    const QJsonArray arr = itemsVal.toArray();
    samples.reserve(arr.size());

    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            return core::Error {
                .code = QString(errc::kVersionSuffixInvalidKey),
                .message = QStringLiteral("Item in 'items' array is not an object"),
                .detail = itemKey,
            };
        }
        const QJsonObject itemObj = elem.toObject();
        samples.append(SuffixSample {
            .key = itemObj.value(QStringLiteral("key")).toString(),
            .suffix = itemObj.value(QStringLiteral("suffix")).toString(),
            .title = itemObj.value(QStringLiteral("title")).toString(),
        });
    }

    return samples;
}

VersionSuffixSource::VersionSuffixSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QList<SuffixSample>> VersionSuffixSource::collectPendingSamples(
    int promptVersion) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery qKnown(conn);
    qKnown.prepare(
        QStringLiteral("SELECT suffix_key FROM version_suffixes WHERE prompt_version = ?"));
    qKnown.addBindValue(promptVersion);
    if (!qKnown.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qKnown.lastError().text(),
            .detail = QString(),
        };
    }

    QSet<QString> knownKeys;
    while (qKnown.next()) {
        knownKeys.insert(qKnown.value(0).toString());
    }

    QSqlQuery qTitles(conn);
    if (!qTitles.exec(QStringLiteral(
            "SELECT DISTINCT title FROM effective_metadata WHERE title IS NOT NULL AND title != "
            "''"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qTitles.lastError().text(),
            .detail = QString(),
        };
    }

    QHash<QString, SuffixSample> pendingMap;
    while (qTitles.next()) {
        const QString title = qTitles.value(0).toString();
        const auto suffixes = splitSuffixes(title);
        for (const auto &suffix : suffixes) {
            const auto cls = classifySuffix(suffix.text);
            if (cls.role == SuffixRole::Unknown) {
                const QString key = suffixKey(suffix.text);
                if (!key.isEmpty() && !knownKeys.contains(key) && !pendingMap.contains(key)) {
                    pendingMap.insert(key,
                        SuffixSample {
                            .key = key,
                            .suffix = suffix.text,
                            .title = title,
                        });
                }
            }
        }
    }

    QList<SuffixSample> samples = pendingMap.values();
    std::ranges::sort(
        samples, [](const SuffixSample &a, const SuffixSample &b) { return a.key < b.key; });

    return samples;
}

core::Result<int> VersionSuffixSource::countPending(int promptVersion) const
{
    auto samplesRes = collectPendingSamples(promptVersion);
    if (!samplesRes.ok()) {
        return samplesRes.error();
    }
    return static_cast<int>(samplesRes.value().size());
}

core::Result<QStringList> VersionSuffixSource::findItems(int promptVersion) const
{
    auto samplesRes = collectPendingSamples(promptVersion);
    if (!samplesRes.ok()) {
        return samplesRes.error();
    }

    const auto &samples = samplesRes.value();
    if (samples.isEmpty()) {
        return QStringList();
    }

    QStringList groupKeys;
    for (qsizetype i = 0; i < samples.size(); i += kBatchSize) {
        const qsizetype end = std::min(i + kBatchSize, samples.size());
        QJsonArray arr;
        for (qsizetype j = i; j < end; ++j) {
            const auto &s = samples.at(j);
            QJsonObject itemObj;
            itemObj.insert(QStringLiteral("key"), s.key);
            itemObj.insert(QStringLiteral("suffix"), s.suffix);
            itemObj.insert(QStringLiteral("title"), s.title);
            arr.append(itemObj);
        }
        QJsonObject rootObj;
        rootObj.insert(QStringLiteral("items"), arr);
        groupKeys.append(QString::fromUtf8(QJsonDocument(rootObj).toJson(QJsonDocument::Compact)));
    }

    return groupKeys;
}

} // namespace linernotes::butler
