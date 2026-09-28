// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistCreditLlm.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <butler/ArtistCredit.h>
#include <butler/Errors.h>

namespace linernotes::butler {

QHash<QString, QString> artistCreditPromptVars(const QStringList &values)
{
    QHash<QString, QString> vars;
    QStringList itemLines;
    itemLines.reserve(values.size());

    for (qsizetype i = 0; i < values.size(); ++i) {
        itemLines.append(QStringLiteral("ID %1: %2").arg(QString::number(i + 1), values.at(i)));
    }

    vars.insert(QStringLiteral("items"),
        itemLines.isEmpty() ? QStringLiteral("(none)") : itemLines.join(QLatin1Char('\n')));
    return vars;
}

QJsonObject artistCreditSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/artist_credit.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<QHash<QString, ArtistCredit>> parseArtistCreditResult(
    const QJsonValue &value, const QStringList &values)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kArtistCreditInvalidResult),
            .message = QStringLiteral("Expected JSON object for artist credit result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kArtistCreditInvalidResult),
            .message = QStringLiteral("Missing 'items' array in result"),
            .detail = QString(),
        };
    }

    const QJsonArray arr = itemsVal.toArray();
    QSet<int> seenIds;
    QHash<QString, ArtistCredit> results;

    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            continue;
        }
        const QJsonObject itemObj = elem.toObject();
        if (!itemObj.contains(QStringLiteral("id"))) {
            continue;
        }
        const int id = itemObj.value(QStringLiteral("id")).toInt(-1);
        if (id < 1 || id > values.size()) {
            continue;
        }
        if (seenIds.contains(id)) {
            continue;
        }
        seenIds.insert(id);

        const auto creditOpt = artistCreditFromJson(itemObj);
        if (!creditOpt.has_value()) {
            continue;
        }

        const QString &originalValue = values.at(id - 1);
        results.insert(originalValue, *creditOpt);
    }

    return results;
}

} // namespace linernotes::butler
