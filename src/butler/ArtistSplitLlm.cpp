// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistSplitLlm.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <butler/Errors.h>

#include <algorithm>

namespace linernotes::butler {

using library::CorrectionProposal;
using library::CorrectionSource;

QHash<QString, QString> artistSplitPromptVars(const ArtistSplitGroup &group)
{
    QHash<QString, QString> vars;
    QStringList itemLines;

    for (const auto &cand : group.candidates) {
        if (cand.decision.verdict == SplitVerdict::Ambiguous) {
            const QString ctxStr = cand.contextAlbums.isEmpty()
                ? QStringLiteral("(none)")
                : cand.contextAlbums.join(QStringLiteral(", "));
            const QString rulePartsStr = cand.decision.parts.join(QStringLiteral(", "));
            itemLines.append(
                QStringLiteral("ID %1 | Original: \"%2\" | Context: [%3] | Rule suggestion: [%4]")
                    .arg(QString::number(cand.id))
                    .arg(cand.original)
                    .arg(ctxStr)
                    .arg(rulePartsStr));
        }
    }

    vars.insert(QStringLiteral("items"),
        itemLines.isEmpty() ? QStringLiteral("(none)") : itemLines.join(QLatin1Char('\n')));
    return vars;
}

QJsonObject artistSplitSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/artist_split.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

namespace {

core::Result<void> parseCandidateParts(
    const QJsonObject &itemObj, const ArtistSplitCandidate &cand, QStringList &outParts)
{
    const QJsonValue partsVal = itemObj.value(QStringLiteral("parts"));
    if (!partsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Item missing 'parts' array"),
            .detail = QString::number(cand.id),
        };
    }

    const QJsonArray partsArr = partsVal.toArray();
    if (partsArr.isEmpty()) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Item 'parts' array is empty"),
            .detail = QString::number(cand.id),
        };
    }

    for (const auto &pElem : partsArr) {
        const QString partStr = pElem.toString().trimmed();
        if (partStr.isEmpty()) {
            continue;
        }
        if (!cand.original.contains(partStr, Qt::CaseInsensitive)) {
            return core::Error {
                .code = QString(errc::kArtistSplitInvalidResult),
                .message = QStringLiteral("Part is not a substring of original value"),
                .detail = partStr,
            };
        }
        if (!outParts.contains(partStr)) {
            outParts.append(partStr);
        }
    }
    return { };
}

core::Result<void> processSplitItem(const QJsonValue &elem,
    const QHash<int, const ArtistSplitCandidate *> &candMap, QSet<int> &seenIds,
    QList<CorrectionProposal> &proposals)
{
    if (!elem.isObject()) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Item must be an object"),
            .detail = QString(),
        };
    }
    const QJsonObject itemObj = elem.toObject();
    if (!itemObj.contains(QStringLiteral("id"))) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Item missing 'id'"),
            .detail = QString(),
        };
    }
    const int id = itemObj.value(QStringLiteral("id")).toInt();
    if (!candMap.contains(id)) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Unknown item id in result"),
            .detail = QString::number(id),
        };
    }
    if (seenIds.contains(id)) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Duplicate item id in result"),
            .detail = QString::number(id),
        };
    }
    seenIds.insert(id);

    const auto *cand = candMap.value(id);
    QStringList partsList;
    auto partsRes = parseCandidateParts(itemObj, *cand, partsList);
    if (!partsRes.ok()) {
        return partsRes;
    }

    if (partsList.size() <= 1) {
        return { };
    }

    const QString newValue = partsList.join(QStringLiteral(" / "));
    const double rawConf = itemObj.value(QStringLiteral("confidence")).toDouble(0.8);
    const double confidence = std::clamp(rawConf, 0.0, 1.0);
    const QString reason = itemObj.value(QStringLiteral("reason")).toString();

    for (const auto &target : cand->targets) {
        proposals.append(CorrectionProposal {
            .trackId = target.trackId,
            .field = target.field,
            .oldValue = cand->original,
            .newValue = newValue,
            .source = CorrectionSource::Llm,
            .confidence = confidence,
            .reason = reason,
        });
    }
    return { };
}

} // namespace

core::Result<QList<CorrectionProposal>> parseArtistSplitResult(
    const QJsonValue &value, const ArtistSplitGroup &group)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Expected JSON object for artist split result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = QStringLiteral("Missing 'items' array in result"),
            .detail = QString(),
        };
    }

    QHash<int, const ArtistSplitCandidate *> candMap;
    for (const auto &cand : group.candidates) {
        candMap.insert(cand.id, &cand);
    }

    QSet<int> seenIds;
    QList<CorrectionProposal> proposals;

    const QJsonArray arr = itemsVal.toArray();
    for (const auto &elem : arr) {
        auto itemRes = processSplitItem(elem, candMap, seenIds, proposals);
        if (!itemRes.ok()) {
            return itemRes.error();
        }
    }

    return proposals;
}

} // namespace linernotes::butler
