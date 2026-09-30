// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MbMatchPlanner.h"

#include <QHash>
#include <QLatin1StringView>
#include <QPair>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

namespace {

bool isPseudoRelease(const MbReleaseSummary &summary)
{
    return summary.status == QLatin1StringView("Pseudo-Release");
}

bool isBetterDuplicate(const MbReleaseSummary &candidate, const MbReleaseSummary &existing)
{
    if (candidate.score != existing.score) {
        return candidate.score > existing.score;
    }
    return !isPseudoRelease(candidate) && isPseudoRelease(existing);
}

int trackCountTier(int trackCount, int localTrackCount)
{
    if (trackCount == localTrackCount) {
        return 0;
    }
    if (trackCount > localTrackCount) {
        return 1;
    }
    return 2;
}

bool compareCandidates(const MbReleaseSummary &a, const MbReleaseSummary &b, int localTrackCount)
{
    const int aTier = trackCountTier(a.trackCount, localTrackCount);
    const int bTier = trackCountTier(b.trackCount, localTrackCount);
    if (aTier != bTier) {
        return aTier < bTier;
    }

    if (a.score != b.score) {
        return a.score > b.score;
    }

    const bool aPseudo = isPseudoRelease(a);
    const bool bPseudo = isPseudoRelease(b);
    if (aPseudo != bPseudo) {
        return !aPseudo;
    }

    return false;
}

QList<MbReleaseSummary> deduplicateSummaries(const QList<MbReleaseSummary> &summaries)
{
    QList<MbReleaseSummary> deduplicated;
    QHash<QPair<QString, int>, int> seenMap;

    for (const auto &s : summaries) {
        if (s.score < kMinCandidateScore) {
            continue;
        }

        if (s.releaseGroupId.isEmpty()) {
            deduplicated.append(s);
        } else {
            const auto key = qMakePair(s.releaseGroupId, s.trackCount);
            auto it = seenMap.find(key);
            if (it == seenMap.end()) {
                seenMap.insert(key, static_cast<int>(deduplicated.size()));
                deduplicated.append(s);
            } else {
                const int existingIdx = it.value();
                if (isBetterDuplicate(s, deduplicated.at(existingIdx))) {
                    deduplicated.replace(existingIdx, s);
                }
            }
        }
    }

    return deduplicated;
}

QHash<int, int> countDiscTracks(const MbRelease &release)
{
    QHash<int, int> counts;
    for (const auto &t : release.tracks) {
        counts.insert(t.disc, counts.value(t.disc, 0) + 1);
    }
    return counts;
}

std::optional<int> determineReleaseYear(const MbRelease &release)
{
    const auto y = yearFromDate(release.originalDate);
    return y.has_value() ? y : yearFromDate(release.date);
}

void appendTitleProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbTrack &mbTrack, double confidence,
    const QString &reason)
{
    if (localTrack.titleNeedsOnline && !mbTrack.title.trimmed().isEmpty()) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::Title,
            .oldValue = std::nullopt,
            .newValue = mbTrack.title.trimmed(),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendArtistProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbTrack &mbTrack, double confidence,
    const QString &reason)
{
    if (localTrack.artist.trimmed().isEmpty() && !mbTrack.artist.trimmed().isEmpty()) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::Artist,
            .oldValue = std::nullopt,
            .newValue = mbTrack.artist.trimmed(),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendAlbumArtistProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbRelease &release, double confidence,
    const QString &reason)
{
    if (localTrack.albumArtist.trimmed().isEmpty() && !release.artist.trimmed().isEmpty()) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::AlbumArtist,
            .oldValue = std::nullopt,
            .newValue = release.artist.trimmed(),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendYearProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, std::optional<int> releaseYear, double confidence,
    const QString &reason)
{
    if ((!localTrack.year.has_value() || localTrack.year.value() <= 0) && releaseYear.has_value()
        && releaseYear.value() > 0) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::Year,
            .oldValue = std::nullopt,
            .newValue = QString::number(releaseYear.value()),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendTrackNumberProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbTrack &mbTrack, double confidence,
    const QString &reason)
{
    if ((!localTrack.local.trackNumber.has_value() || localTrack.local.trackNumber.value() <= 0)
        && mbTrack.position > 0) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::TrackNumber,
            .oldValue = std::nullopt,
            .newValue = QString::number(mbTrack.position),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendDiscNumberProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbTrack &mbTrack, bool hasMultipleDiscs,
    double confidence, const QString &reason)
{
    if (hasMultipleDiscs
        && (!localTrack.local.discNumber.has_value() || localTrack.local.discNumber.value() <= 0)
        && mbTrack.disc > 0) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::DiscNumber,
            .oldValue = std::nullopt,
            .newValue = QString::number(mbTrack.disc),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendTrackTotalProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbTrack &mbTrack, const QHash<int, int> &discTrackCounts,
    double confidence, const QString &reason)
{
    const int discTotalTracks = discTrackCounts.value(mbTrack.disc, 0);
    if ((!localTrack.trackTotal.has_value() || localTrack.trackTotal.value() <= 0)
        && discTotalTracks > 0) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::TrackTotal,
            .oldValue = std::nullopt,
            .newValue = QString::number(discTotalTracks),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendDiscTotalProposal(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbRelease &release, bool hasMultipleDiscs,
    double confidence, const QString &reason)
{
    if (hasMultipleDiscs && (!localTrack.discTotal.has_value() || localTrack.discTotal.value() <= 0)
        && release.discCount > 0) {
        proposals.append(library::CorrectionProposal {
            .trackId = localTrack.local.trackId,
            .field = library::TagField::DiscTotal,
            .oldValue = std::nullopt,
            .newValue = QString::number(release.discCount),
            .source = library::CorrectionSource::MusicBrainz,
            .confidence = confidence,
            .reason = reason,
        });
    }
}

void appendTrackProposals(QList<library::CorrectionProposal> &proposals,
    const MbAlbumTrack &localTrack, const MbRelease &release, const MbTrack &mbTrack,
    bool hasMultipleDiscs, std::optional<int> releaseYear, const QHash<int, int> &discTrackCounts,
    double confidence, const QString &reason)
{
    appendTitleProposal(proposals, localTrack, mbTrack, confidence, reason);
    appendArtistProposal(proposals, localTrack, mbTrack, confidence, reason);
    appendAlbumArtistProposal(proposals, localTrack, release, confidence, reason);
    appendYearProposal(proposals, localTrack, releaseYear, confidence, reason);
    appendTrackNumberProposal(proposals, localTrack, mbTrack, confidence, reason);
    appendDiscNumberProposal(proposals, localTrack, mbTrack, hasMultipleDiscs, confidence, reason);
    appendTrackTotalProposal(proposals, localTrack, mbTrack, discTrackCounts, confidence, reason);
    appendDiscTotalProposal(proposals, localTrack, release, hasMultipleDiscs, confidence, reason);
}

} // namespace

QStringList selectCandidates(const QList<MbReleaseSummary> &summaries, int localTrackCount)
{
    auto deduplicated = deduplicateSummaries(summaries);

    std::ranges::stable_sort(
        deduplicated, [localTrackCount](const MbReleaseSummary &a, const MbReleaseSummary &b) {
            return compareCandidates(a, b, localTrackCount);
        });

    QStringList result;
    const int count = std::min(static_cast<int>(deduplicated.size()), kMaxDetailFetches);
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.append(deduplicated.at(i).id);
    }
    return result;
}

QList<library::CorrectionProposal> buildProposals(
    const MbAlbumInput &input, const MbRelease &release, const AlbumMatch &match)
{
    const QHash<int, int> discTrackCounts = countDiscTracks(release);
    const bool hasMultipleDiscs
        = (release.discCount > 1) || std::ranges::any_of(input.tracks, [](const MbAlbumTrack &t) {
              return t.local.discNumber.has_value() && t.local.discNumber.value() > 0;
          });
    const std::optional<int> releaseYear = determineReleaseYear(release);

    QHash<qint64, int> trackToMbIndex;
    for (const auto &m : match.mapping) {
        trackToMbIndex.insert(m.trackId, m.mbIndex);
    }

    QList<library::CorrectionProposal> proposals;
    const QString reason = QStringLiteral("MusicBrainz release %1").arg(release.id);
    const double confidence = match.score;

    for (const auto &localTrack : input.tracks) {
        auto mbIdxIt = trackToMbIndex.find(localTrack.local.trackId);
        if (mbIdxIt == trackToMbIndex.end()) {
            continue;
        }
        const int mbIdx = mbIdxIt.value();
        if (mbIdx < 0 || mbIdx >= release.tracks.size()) {
            continue;
        }
        const MbTrack &mbTrack = release.tracks.at(mbIdx);
        appendTrackProposals(proposals, localTrack, release, mbTrack, hasMultipleDiscs, releaseYear,
            discTrackCounts, confidence, reason);
    }

    return proposals;
}

} // namespace linernotes::butler
