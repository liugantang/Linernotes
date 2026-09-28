// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MojibakeLlm.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <butler/Errors.h>
#include <library/EnumNames.h>

#include <algorithm>

namespace linernotes::butler {

using library::CorrectionProposal;
using library::CorrectionSource;
using library::tagFieldToColumn;

QHash<QString, QString> mojibakePromptVars(
    const MojibakeGroup &group, const QList<AmbiguousItem> &items)
{
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("directory"), group.directory);

    QStringList contextLines;
    for (const auto &track : group.tracks) {
        for (const auto &f : track.readable) {
            if (contextLines.size() >= 20) {
                break;
            }
            contextLines.append(QStringLiteral("%1: %2").arg(tagFieldToColumn(f.field), f.value));
        }
        if (contextLines.size() >= 20) {
            break;
        }
    }
    vars.insert(QStringLiteral("context"),
        contextLines.isEmpty() ? QStringLiteral("(none)") : contextLines.join(QLatin1Char('\n')));

    QStringList itemLines;
    for (const auto &item : items) {
        QStringList candStrs;
        for (const auto &c : item.candidates) {
            candStrs.append(QStringLiteral("%1: \"%2\" (score: %3)")
                    .arg(encodingName(c.encoding))
                    .arg(c.text)
                    .arg(QString::number(c.score, 'f', 2)));
        }
        itemLines.append(QStringLiteral("ID %1 | Field: %2 | Original: \"%3\" | Candidates: [%4]")
                .arg(QString::number(item.id))
                .arg(tagFieldToColumn(item.field))
                .arg(item.original)
                .arg(candStrs.join(QStringLiteral(", "))));
    }
    vars.insert(QStringLiteral("items"), itemLines.join(QLatin1Char('\n')));

    return vars;
}

QJsonObject mojibakeSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/mojibake.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<QList<CorrectionProposal>> parseMojibakeResult(
    const QJsonValue &value, const QList<AmbiguousItem> &items)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidResult),
            .message = QStringLiteral("Expected JSON object for mojibake result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kMojibakeInvalidResult),
            .message = QStringLiteral("Missing 'items' array in result"),
            .detail = QString(),
        };
    }

    QHash<int, const AmbiguousItem *> itemMap;
    for (const auto &item : items) {
        itemMap.insert(item.id, &item);
    }

    QSet<int> seenIds;
    QList<CorrectionProposal> proposals;

    const QJsonArray arr = itemsVal.toArray();
    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            return core::Error {
                .code = QString(errc::kMojibakeInvalidResult),
                .message = QStringLiteral("Item must be an object"),
                .detail = QString(),
            };
        }
        const QJsonObject itemObj = elem.toObject();
        if (!itemObj.contains(QStringLiteral("id"))) {
            return core::Error {
                .code = QString(errc::kMojibakeInvalidResult),
                .message = QStringLiteral("Item missing 'id'"),
                .detail = QString(),
            };
        }
        const int id = itemObj.value(QStringLiteral("id")).toInt();
        if (!itemMap.contains(id)) {
            return core::Error {
                .code = QString(errc::kMojibakeInvalidResult),
                .message = QStringLiteral("Unknown item id in result"),
                .detail = QString::number(id),
            };
        }
        if (seenIds.contains(id)) {
            return core::Error {
                .code = QString(errc::kMojibakeInvalidResult),
                .message = QStringLiteral("Duplicate item id in result"),
                .detail = QString::number(id),
            };
        }
        seenIds.insert(id);

        const QJsonValue textVal = itemObj.value(QStringLiteral("text"));
        if (textVal.isNull()) {
            continue;
        }

        const QString text = textVal.toString();
        const auto *ambItem = itemMap.value(id);
        if (text == ambItem->original) {
            continue;
        }

        const double rawConf = itemObj.value(QStringLiteral("confidence")).toDouble(0.8);
        const double confidence = std::clamp(rawConf, 0.0, 1.0);
        const QString reason = itemObj.value(QStringLiteral("reason")).toString();

        proposals.append(CorrectionProposal {
            .trackId = ambItem->trackId,
            .field = ambItem->field,
            .oldValue = ambItem->original,
            .newValue = text,
            .source = CorrectionSource::Llm,
            .confidence = confidence,
            .reason = reason,
        });
    }

    return proposals;
}

QList<CorrectionProposal> fallbackProposals(const QList<AmbiguousItem> &items)
{
    QList<CorrectionProposal> proposals;
    for (const auto &item : items) {
        if (item.candidates.isEmpty()) {
            continue;
        }
        const auto &best = item.candidates.first();
        if (best.text == item.original) {
            continue;
        }
        const double conf = std::min(best.score, 0.5);
        proposals.append(CorrectionProposal {
            .trackId = item.trackId,
            .field = item.field,
            .oldValue = item.original,
            .newValue = best.text,
            .source = CorrectionSource::Rule,
            .confidence = conf,
            .reason = QCoreApplication::translate("butler", "未经 AI 确认"),
        });
    }
    return proposals;
}

} // namespace linernotes::butler
