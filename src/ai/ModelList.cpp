// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ModelList.h"

#include "Errors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace linernotes::ai {

core::Result<QStringList> parseModelList(const QByteArray &json)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kBadResponse),
            .message = QStringLiteral("Invalid JSON response"),
            .detail = parseErr.errorString(),
        };
    }

    const QJsonObject rootObj = doc.object();
    const auto dataVal = rootObj.value(QStringLiteral("data"));
    if (!dataVal.isArray()) {
        return core::Error {
            .code = QString(errc::kBadResponse),
            .message = QStringLiteral("Missing data array in model list response"),
            .detail = QString(),
        };
    }

    const QJsonArray dataArr = dataVal.toArray();
    QSet<QString> seen;
    QStringList result;
    result.reserve(dataArr.size());

    for (const auto &itemVal : dataArr) {
        if (!itemVal.isObject()) {
            continue;
        }
        const QString id = itemVal.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty() || seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        result.append(id);
    }

    std::ranges::sort(result, [](const QString &a, const QString &b) {
        const int cmp = a.compare(b, Qt::CaseInsensitive);
        if (cmp != 0) {
            return cmp < 0;
        }
        return a < b;
    });

    return result;
}

} // namespace linernotes::ai
