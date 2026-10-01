// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <butler/Errors.h>
#include <butler/TitleMatchLlm.h>

#include <algorithm>

namespace linernotes::butler {

QHash<QString, QString> titleMatchPromptVars(const QList<TitlePair> &pairs)
{
    QHash<QString, QString> vars;
    QStringList itemLines;
    itemLines.reserve(pairs.size());

    for (qsizetype i = 0; i < pairs.size(); ++i) {
        itemLines.append(QStringLiteral("ID %1: %2 ｜ %3 ｜ 艺人: %4")
                .arg(QString::number(i + 1), pairs.at(i).titleA, pairs.at(i).titleB,
                    pairs.at(i).artist));
    }

    vars.insert(QStringLiteral("items"),
        itemLines.isEmpty() ? QStringLiteral("(none)") : itemLines.join(QLatin1Char('\n')));
    return vars;
}

QJsonObject titleMatchSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/title_match.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<QHash<QPair<QString, QString>, TitleMatchVerdict>> parseTitleMatchResult(
    const QJsonValue &value, const QList<TitlePair> &pairs)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kTitleMatchInvalidResult),
            .message = QStringLiteral("Expected JSON object for title match result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kTitleMatchInvalidResult),
            .message = QStringLiteral("Missing 'items' array in result"),
            .detail = QString(),
        };
    }

    const QJsonArray arr = itemsVal.toArray();
    QSet<int> seenIds;
    QHash<QPair<QString, QString>, TitleMatchVerdict> results;

    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            continue;
        }
        const QJsonObject itemObj = elem.toObject();
        if (!itemObj.contains(QStringLiteral("id")) || !itemObj.contains(QStringLiteral("same"))
            || !itemObj.contains(QStringLiteral("confidence"))
            || !itemObj.contains(QStringLiteral("reason"))) {
            continue;
        }

        const int id = itemObj.value(QStringLiteral("id")).toInt(-1);
        if (id < 1 || id > pairs.size()) {
            continue;
        }
        if (seenIds.contains(id)) {
            continue;
        }

        const bool same = itemObj.value(QStringLiteral("same")).toBool();
        double confidence = itemObj.value(QStringLiteral("confidence")).toDouble(0.0);
        confidence = std::clamp(confidence, 0.0, 1.0);
        const QString reason = itemObj.value(QStringLiteral("reason")).toString();

        seenIds.insert(id);
        const auto &pair = pairs.at(id - 1);
        results.insert(qMakePair(pair.keyA, pair.keyB),
            TitleMatchVerdict {
                .same = same,
                .confidence = confidence,
                .reason = reason,
            });
    }

    return results;
}

} // namespace linernotes::butler
