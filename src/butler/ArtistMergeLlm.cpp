// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistMergeLlm.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QSet>

#include <butler/ArtistMerge.h>
#include <butler/Errors.h>

#include <algorithm>

namespace linernotes::butler {

QHash<QString, QString> artistMergePromptVars(const QList<ArtistMergeCandidatePair> &pairs)
{
    QHash<QString, QString> vars;
    QStringList pairLines;

    for (const auto &pair : pairs) {
        const QString albumsAStr = pair.albumsA.isEmpty() ? QStringLiteral("(none)")
                                                          : pair.albumsA.join(QStringLiteral(", "));
        const QString albumsBStr = pair.albumsB.isEmpty() ? QStringLiteral("(none)")
                                                          : pair.albumsB.join(QStringLiteral(", "));

        pairLines.append(QStringLiteral("ID %1:\n"
                                        "  - Artist A: \"%2\" (tracks: %3, albums: [%4])\n"
                                        "  - Artist B: \"%5\" (tracks: %6, albums: [%7])")
                .arg(QString::number(pair.id))
                .arg(pair.artistA.name)
                .arg(QString::number(pair.artistA.trackCount))
                .arg(albumsAStr)
                .arg(pair.artistB.name)
                .arg(QString::number(pair.artistB.trackCount))
                .arg(albumsBStr));
    }

    vars.insert(QStringLiteral("pairs"),
        pairLines.isEmpty() ? QStringLiteral("(none)") : pairLines.join(QLatin1Char('\n')));
    return vars;
}

QJsonObject artistMergeSchema()
{
    QFile file(QStringLiteral(":/schemas/cleanup/artist_merge.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

namespace {

core::Result<void> processMergeItem(const QJsonValue &elem,
    const QHash<int, const ArtistMergeCandidatePair *> &pairMap,
    const QHash<qint64, ArtistEntry> &entriesById, QSet<int> &seenIds,
    QList<library::ArtistAliasProposal> &proposals)
{
    if (!elem.isObject()) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Pair item must be an object"),
            .detail = QString(),
        };
    }

    const QJsonObject itemObj = elem.toObject();
    if (!itemObj.contains(QStringLiteral("id"))) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Pair item missing 'id'"),
            .detail = QString(),
        };
    }

    const int id = itemObj.value(QStringLiteral("id")).toInt();
    if (!pairMap.contains(id)) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Unknown pair id in result"),
            .detail = QString::number(id),
        };
    }

    if (seenIds.contains(id)) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Duplicate pair id in result"),
            .detail = QString::number(id),
        };
    }
    seenIds.insert(id);

    const bool same = itemObj.value(QStringLiteral("same")).toBool(false);
    if (!same) {
        return { };
    }

    const double rawConf = itemObj.value(QStringLiteral("confidence")).toDouble(0.0);
    const double confidence = std::clamp(rawConf, 0.0, 1.0);
    if (confidence < 0.5) {
        return { };
    }

    const auto *cand = pairMap.value(id);
    const qint64 idA = cand->artistA.artistId;
    const qint64 idB = cand->artistB.artistId;

    const ArtistEntry entryA = entriesById.value(idA, cand->artistA);
    const ArtistEntry entryB = entriesById.value(idB, cand->artistB);

    const qint64 canonicalId = pickCanonical({ entryA, entryB });
    const QString alias = (canonicalId == entryA.artistId) ? entryB.name : entryA.name;
    const QString reason = itemObj.value(QStringLiteral("reason")).toString();

    proposals.append(library::ArtistAliasProposal {
        .canonicalArtistId = canonicalId,
        .alias = alias,
        .locale = std::nullopt,
        .source = library::CorrectionSource::Llm,
        .confidence = confidence,
        .reason = reason,
    });

    return { };
}

} // namespace

core::Result<QList<library::ArtistAliasProposal>> parseArtistMergeResult(const QJsonValue &value,
    const QList<ArtistMergeCandidatePair> &pairs, const QHash<qint64, ArtistEntry> &entriesById)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Expected JSON object for artist merge result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue pairsVal = obj.value(QStringLiteral("pairs"));
    if (!pairsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Missing 'pairs' array in result"),
            .detail = QString(),
        };
    }

    QHash<int, const ArtistMergeCandidatePair *> pairMap;
    for (const auto &p : pairs) {
        pairMap.insert(p.id, &p);
    }

    QSet<int> seenIds;
    QList<library::ArtistAliasProposal> proposals;

    const QJsonArray arr = pairsVal.toArray();
    for (const auto &elem : arr) {
        auto res = processMergeItem(elem, pairMap, entriesById, seenIds, proposals);
        if (!res.ok()) {
            return res.error();
        }
    }

    return proposals;
}

} // namespace linernotes::butler
