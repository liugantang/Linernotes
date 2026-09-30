// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <butler/Errors.h>
#include <butler/VersionSuffixLlm.h>
#include <library/EnumNames.h>

#include <algorithm>

namespace linernotes::butler {

QHash<QString, QString> versionSuffixPromptVars(const QList<SuffixSample> &samples)
{
    QHash<QString, QString> vars;
    QStringList itemLines;
    itemLines.reserve(samples.size());

    for (qsizetype i = 0; i < samples.size(); ++i) {
        itemLines.append(QStringLiteral("ID %1: %2 ｜ 完整标题: %3")
                .arg(QString::number(i + 1), samples.at(i).suffix, samples.at(i).title));
    }

    vars.insert(QStringLiteral("items"),
        itemLines.isEmpty() ? QStringLiteral("(none)") : itemLines.join(QLatin1Char('\n')));
    return vars;
}

QJsonObject versionSuffixSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/version_suffix.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<QHash<QString, SuffixVerdict>> parseVersionSuffixResult(
    const QJsonValue &value, const QList<SuffixSample> &samples)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kVersionSuffixInvalidResult),
            .message = QStringLiteral("Expected JSON object for version suffix result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kVersionSuffixInvalidResult),
            .message = QStringLiteral("Missing 'items' array in result"),
            .detail = QString(),
        };
    }

    const QJsonArray arr = itemsVal.toArray();
    QSet<int> seenIds;
    QHash<QString, SuffixVerdict> results;

    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            continue;
        }
        const QJsonObject itemObj = elem.toObject();
        if (!itemObj.contains(QStringLiteral("id"))) {
            continue;
        }
        const int id = itemObj.value(QStringLiteral("id")).toInt(-1);
        if (id < 1 || id > samples.size()) {
            continue;
        }
        if (seenIds.contains(id)) {
            continue;
        }

        const QString roleStr = itemObj.value(QStringLiteral("role")).toString();
        SuffixRole role = SuffixRole::Unknown;
        if (roleStr == QLatin1StringView("version")) {
            role = SuffixRole::Version;
        } else if (roleStr == QLatin1StringView("annotation")) {
            role = SuffixRole::Annotation;
        } else if (roleStr == QLatin1StringView("title_part")) {
            role = SuffixRole::TitlePart;
        } else {
            continue;
        }

        library::VersionType vt = library::VersionType::Studio;
        if (role == SuffixRole::Version) {
            if (!itemObj.contains(QStringLiteral("type"))) {
                continue;
            }
            const QString typeStr = itemObj.value(QStringLiteral("type")).toString();
            const auto typeOpt = library::versionTypeFromString(typeStr);
            if (!typeOpt.has_value()) {
                continue;
            }
            vt = *typeOpt;
        }

        double confidence = itemObj.value(QStringLiteral("confidence")).toDouble(1.0);
        confidence = std::clamp(confidence, 0.0, 1.0);
        const QString reason = itemObj.value(QStringLiteral("reason")).toString();

        seenIds.insert(id);
        const auto &sample = samples.at(id - 1);
        results.insert(sample.key,
            SuffixVerdict {
                .cls = SuffixClass {
                    .role = role,
                    .type = vt,
                },
                .confidence = confidence,
                .reason = reason,
            });
    }

    return results;
}

} // namespace linernotes::butler
