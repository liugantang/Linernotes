// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistMerge.h"

#include <algorithm>

namespace linernotes::butler {

namespace {

bool isBetterCanonical(const ArtistEntry &current, const ArtistEntry &best)
{
    if (current.trackCount != best.trackCount) {
        return current.trackCount > best.trackCount;
    }
    const int cmp = current.name.compare(best.name);
    if (cmp != 0) {
        return cmp < 0;
    }
    return current.artistId < best.artistId;
}

} // namespace

qint64 pickCanonical(const QList<ArtistEntry> &members)
{
    if (members.isEmpty()) {
        return 0;
    }

    const ArtistEntry *best = &members.at(0);
    for (qsizetype i = 1; i < members.size(); ++i) {
        const auto &current = members.at(i);
        if (isBetterCanonical(current, *best)) {
            best = &current;
        }
    }
    return best->artistId;
}

QList<library::ArtistAliasProposal> groupProposals(const ArtistGroup &group)
{
    if (group.members.size() < 2) {
        return { };
    }

    const qint64 canonicalId = pickCanonical(group.members);
    if (canonicalId <= 0) {
        return { };
    }

    const double confidence = 0.95;
    const QString reason
        = QString::fromUtf8(QT_TRANSLATE_NOOP("butler", "Same name with different spelling"));

    QList<library::ArtistAliasProposal> proposals;
    proposals.reserve(group.members.size() - 1);

    for (const auto &member : group.members) {
        if (member.artistId == canonicalId) {
            continue;
        }
        proposals.append(library::ArtistAliasProposal {
            .canonicalArtistId = canonicalId,
            .alias = member.name,
            .locale = std::nullopt,
            .source = library::CorrectionSource::Rule,
            .confidence = confidence,
            .reason = reason,
        });
    }

    return proposals;
}

} // namespace linernotes::butler
