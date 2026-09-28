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

QHash<QString, QString> artistMergePromptVars(const QList<ArtistMergeGroup> &groups)
{
    QHash<QString, QString> vars;
    QStringList groupBlocks;

    for (const auto &group : groups) {
        QStringList memberLines;
        for (const auto &m : group.members) {
            const QString albumsStr = m.albums.isEmpty() ? QStringLiteral("(none)")
                                                         : m.albums.join(QStringLiteral(", "));
            const QString akaStr
                = m.aka.isEmpty() ? QStringLiteral("(none)") : m.aka.join(QStringLiteral(", "));
            memberLines.append(
                QStringLiteral("  - Artist ID %1: \"%2\" (tracks: %3, albums: [%4], aka: [%5])")
                    .arg(QString::number(m.entry.artistId))
                    .arg(m.entry.name)
                    .arg(QString::number(m.entry.trackCount))
                    .arg(albumsStr)
                    .arg(akaStr));
        }

        groupBlocks.append(QStringLiteral("Group ID %1:\n%2")
                .arg(QString::number(group.id))
                .arg(memberLines.join(QLatin1Char('\n'))));
    }

    vars.insert(QStringLiteral("groups"),
        groupBlocks.isEmpty() ? QStringLiteral("(none)")
                              : groupBlocks.join(QStringLiteral("\n\n")));
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

bool validateGroupSubsets(const QJsonArray &subsetsArr,
    const QHash<qint64, ArtistEntry> &validMembers, QSet<qint64> &seenArtistIds)
{
    for (const auto &subVal : subsetsArr) {
        if (!subVal.isObject()) {
            return false;
        }
        const QJsonObject subObj = subVal.toObject();
        if (!subObj.value(QStringLiteral("members")).isArray()) {
            return false;
        }
        const QJsonArray membersArr = subObj.value(QStringLiteral("members")).toArray();
        for (const auto &mVal : membersArr) {
            const qint64 artistId = mVal.toInteger();
            if (!validMembers.contains(artistId) || seenArtistIds.contains(artistId)) {
                return false;
            }
            seenArtistIds.insert(artistId);
        }
    }
    return true;
}

void processValidSubsets(const QJsonArray &subsetsArr,
    const QHash<qint64, ArtistEntry> &validMembers, QList<library::ArtistAliasProposal> &proposals)
{
    for (const auto &subVal : subsetsArr) {
        const QJsonObject subObj = subVal.toObject();
        const QJsonArray membersArr = subObj.value(QStringLiteral("members")).toArray();
        if (membersArr.size() < 2) {
            continue;
        }

        const double rawConf = subObj.value(QStringLiteral("confidence")).toDouble(0.0);
        const double confidence = std::clamp(rawConf, 0.0, 1.0);
        if (confidence < 0.5) {
            continue;
        }

        const QString reason = subObj.value(QStringLiteral("reason")).toString();

        QList<ArtistEntry> subsetEntries;
        subsetEntries.reserve(membersArr.size());
        for (const auto &mVal : membersArr) {
            subsetEntries.append(validMembers.value(mVal.toInteger()));
        }

        const qint64 canonicalId = pickCanonical(subsetEntries);
        if (canonicalId <= 0) {
            continue;
        }

        for (const auto &entry : subsetEntries) {
            if (entry.artistId == canonicalId) {
                continue;
            }
            proposals.append(library::ArtistAliasProposal {
                .canonicalArtistId = canonicalId,
                .alias = entry.name,
                .locale = std::nullopt,
                .source = library::CorrectionSource::Llm,
                .confidence = confidence,
                .reason = reason,
            });
        }
    }
}

void processGroupElement(const QJsonValue &elem,
    const QHash<int, const ArtistMergeGroup *> &groupById, QSet<int> &seenGroupIds,
    QList<library::ArtistAliasProposal> &proposals)
{
    if (!elem.isObject()) {
        return;
    }
    const QJsonObject groupObj = elem.toObject();
    if (!groupObj.contains(QStringLiteral("id"))
        || !groupObj.value(QStringLiteral("subsets")).isArray()) {
        return;
    }

    const int groupId = groupObj.value(QStringLiteral("id")).toInt();
    if (!groupById.contains(groupId) || seenGroupIds.contains(groupId)) {
        return;
    }
    seenGroupIds.insert(groupId);

    const auto *group = groupById.value(groupId);
    QHash<qint64, ArtistEntry> validMembers;
    for (const auto &m : group->members) {
        validMembers.insert(m.entry.artistId, m.entry);
    }

    const QJsonArray subsetsArr = groupObj.value(QStringLiteral("subsets")).toArray();
    QSet<qint64> seenArtistIds;
    if (!validateGroupSubsets(subsetsArr, validMembers, seenArtistIds)) {
        return;
    }

    processValidSubsets(subsetsArr, validMembers, proposals);
}

} // namespace

core::Result<QList<library::ArtistAliasProposal>> parseArtistMergeResult(
    const QJsonValue &value, const QList<ArtistMergeGroup> &groups)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Expected JSON object for artist merge result"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    const QJsonValue groupsVal = obj.value(QStringLiteral("groups"));
    if (!groupsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = QStringLiteral("Missing 'groups' array in result"),
            .detail = QString(),
        };
    }

    QHash<int, const ArtistMergeGroup *> groupById;
    for (const auto &g : groups) {
        groupById.insert(g.id, &g);
    }

    QSet<int> seenGroupIds;
    QList<library::ArtistAliasProposal> proposals;

    const QJsonArray groupsArr = groupsVal.toArray();
    for (const auto &elem : groupsArr) {
        processGroupElement(elem, groupById, seenGroupIds, proposals);
    }

    return proposals;
}

} // namespace linernotes::butler
