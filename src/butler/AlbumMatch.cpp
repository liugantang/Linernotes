// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QChar>
#include <QList>
#include <QString>
#include <QStringView>

#include <butler/AlbumMatch.h>
#include <butler/ArtistName.h>
#include <butler/MusicBrainz.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace linernotes::butler {

namespace {

QString stripBrackets(QStringView str)
{
    QString result;
    result.reserve(str.size());
    int depth = 0;
    for (const QChar ch : str) {
        if (ch == u'(' || ch == u'（' || ch == u'[' || ch == u'［' || ch == u'【') {
            ++depth;
        } else if (ch == u')' || ch == u'）' || ch == u']' || ch == u'］' || ch == u'】') {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0) {
            result.append(ch);
        }
    }
    return result;
}

int levenshteinDistance(QStringView s1, QStringView s2)
{
    if (s1.size() > s2.size()) {
        std::swap(s1, s2);
    }
    if (s1.isEmpty()) {
        return static_cast<int>(s2.size());
    }
    const auto m = s2.size();
    std::vector<int> dp(static_cast<size_t>(m + 1));
    for (int j = 0; j <= m; ++j) {
        dp.at(static_cast<size_t>(j)) = j;
    }
    for (qsizetype i = 1; i <= s1.size(); ++i) {
        int prevDiag = dp.at(0);
        dp.at(0) = static_cast<int>(i);
        const QChar c1 = s1.at(i - 1);
        for (qsizetype j = 1; j <= m; ++j) {
            const int temp = dp.at(static_cast<size_t>(j));
            const int cost = (c1 == s2.at(j - 1)) ? 0 : 1;
            const int insertCost = dp.at(static_cast<size_t>(j - 1)) + 1;
            const int deleteCost = dp.at(static_cast<size_t>(j)) + 1;
            const int replaceCost = prevDiag + cost;
            dp.at(static_cast<size_t>(j)) = std::min({ insertCost, deleteCost, replaceCost });
            prevDiag = temp;
        }
    }
    return dp.at(static_cast<size_t>(m));
}

double keySimilarity(const QString &k1, const QString &k2)
{
    if (k1.isEmpty() && k2.isEmpty()) {
        return 0.0;
    }
    const qsizetype maxLen = std::max(k1.size(), k2.size());
    if (maxLen == 0) {
        return 0.0;
    }
    const int dist = levenshteinDistance(k1, k2);
    return std::max(0.0, 1.0 - (static_cast<double>(dist) / static_cast<double>(maxLen)));
}

bool isEquivalentCompletion(const AlbumMatch &matchA, const MbRelease &releaseA,
    const AlbumMatch &matchB, const MbRelease &releaseB)
{
    if (yearFromDate(releaseA.date) != yearFromDate(releaseB.date)) {
        return false;
    }
    if (releaseA.tracks.size() != releaseB.tracks.size()) {
        return false;
    }
    if (matchA.mapping.size() != matchB.mapping.size()) {
        return false;
    }
    for (qsizetype k = 0; k < matchA.mapping.size(); ++k) {
        const auto &ma = matchA.mapping.at(k);
        const auto &mb = matchB.mapping.at(k);
        if (ma.trackId != mb.trackId) {
            return false;
        }
        if (ma.mbIndex < 0 || ma.mbIndex >= releaseA.tracks.size()) {
            return false;
        }
        if (mb.mbIndex < 0 || mb.mbIndex >= releaseB.tracks.size()) {
            return false;
        }
        const auto &trackA = releaseA.tracks.at(ma.mbIndex);
        const auto &trackB = releaseB.tracks.at(mb.mbIndex);
        if (trackA.disc != trackB.disc || trackA.position != trackB.position) {
            return false;
        }
    }
    return true;
}

} // namespace

double titleSimilarity(const QString &a, const QString &b)
{
    const QString aRaw = exactKey(a);
    const QString aStripped = exactKey(stripBrackets(a));
    const QString bRaw = exactKey(b);
    const QString bStripped = exactKey(stripBrackets(b));

    const double sim1 = keySimilarity(aRaw, bRaw);
    const double sim2 = keySimilarity(aRaw, bStripped);
    const double sim3 = keySimilarity(aStripped, bRaw);
    const double sim4 = keySimilarity(aStripped, bStripped);

    return std::max({ sim1, sim2, sim3, sim4 });
}

double durationScore(qint64 aMs, qint64 bMs)
{
    if (aMs <= 0 || bMs <= 0) {
        return 0.5;
    }
    const qint64 diff = std::abs(aMs - bMs);
    constexpr qint64 kMinDiffMs = 2000;
    constexpr qint64 kMaxDiffMs = 15000;
    if (diff <= kMinDiffMs) {
        return 1.0;
    }
    if (diff >= kMaxDiffMs) {
        return 0.0;
    }
    return 1.0
        - (static_cast<double>(diff - kMinDiffMs) / static_cast<double>(kMaxDiffMs - kMinDiffMs));
}

double pairScore(const LocalTrack &local, const MbTrack &mb)
{
    const double titleSim = titleSimilarity(local.title, mb.title);
    const double durScore = durationScore(local.durationMs, mb.lengthMs);
    double score = (0.6 * titleSim) + (0.4 * durScore);

    const bool numberMatch = local.trackNumber.has_value() && (*local.trackNumber == mb.position)
        && (local.discNumber.value_or(1) == mb.disc);

    if (numberMatch) {
        score += 0.1;
    }
    return std::min(1.0, score);
}

AlbumMatch scoreRelease(const LocalAlbum &local, const MbRelease &release)
{
    if (local.tracks.isEmpty() || release.tracks.isEmpty()) {
        return AlbumMatch {
            .releaseId = release.id,
            .score = 0.0,
            .mapping = { },
        };
    }

    struct CandidatePair {
        double score = 0.0;
        int localIndex = -1;
        int mbIndex = -1;
    };

    QList<CandidatePair> candidates;
    candidates.reserve(local.tracks.size() * release.tracks.size());

    for (int i = 0; i < local.tracks.size(); ++i) {
        const auto &localTrack = local.tracks.at(i);
        for (int j = 0; j < release.tracks.size(); ++j) {
            const auto &mbTrack = release.tracks.at(j);
            const double pair = pairScore(localTrack, mbTrack);
            if (pair >= kMinPairScore) {
                candidates.append(CandidatePair {
                    .score = pair,
                    .localIndex = i,
                    .mbIndex = j,
                });
            }
        }
    }

    std::ranges::sort(candidates, [](const CandidatePair &a, const CandidatePair &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.localIndex != b.localIndex) {
            return a.localIndex < b.localIndex;
        }
        return a.mbIndex < b.mbIndex;
    });

    std::vector<bool> localUsed(static_cast<size_t>(local.tracks.size()), false);
    std::vector<bool> mbUsed(static_cast<size_t>(release.tracks.size()), false);
    std::vector<TrackMapping> matchedByLocal(static_cast<size_t>(local.tracks.size()));
    std::vector<bool> localMatched(static_cast<size_t>(local.tracks.size()), false);

    double sumScore = 0.0;
    for (const auto &cand : std::as_const(candidates)) {
        const auto li = static_cast<size_t>(cand.localIndex);
        const auto mi = static_cast<size_t>(cand.mbIndex);
        if (!localUsed.at(li) && !mbUsed.at(mi)) {
            localUsed.at(li) = true;
            mbUsed.at(mi) = true;
            localMatched.at(li) = true;
            matchedByLocal.at(li) = TrackMapping {
                .trackId = local.tracks.at(cand.localIndex).trackId,
                .mbIndex = cand.mbIndex,
                .score = cand.score,
            };
            sumScore += cand.score;
        }
    }

    QList<TrackMapping> mapping;
    for (size_t i = 0; i < localMatched.size(); ++i) {
        if (localMatched.at(i)) {
            mapping.append(matchedByLocal.at(i));
        }
    }

    const auto localCount = static_cast<double>(local.tracks.size());
    const auto mbCount = static_cast<double>(release.tracks.size());
    const double extraMb = std::max(0.0, mbCount - localCount);
    const double sizePenalty = 1.0 - (0.3 * (extraMb / mbCount));
    const double score = (sumScore / localCount) * sizePenalty;

    return AlbumMatch {
        .releaseId = release.id,
        .score = score,
        .mapping = mapping,
    };
}

MatchDecision decide(const LocalAlbum &local, const QList<MbRelease> &releases)
{
    if (releases.isEmpty()) {
        return MatchDecision {
            .best = std::nullopt,
            .ambiguous = false,
        };
    }

    struct ScoredRelease {
        AlbumMatch match;
        int releaseIndex = -1;
    };

    QList<ScoredRelease> scored;
    scored.reserve(releases.size());
    for (int i = 0; i < releases.size(); ++i) {
        const auto &rel = releases.at(i);
        scored.append(ScoredRelease {
            .match = scoreRelease(local, rel),
            .releaseIndex = i,
        });
    }

    std::ranges::sort(scored, [](const ScoredRelease &a, const ScoredRelease &b) {
        if (a.match.score != b.match.score) {
            return a.match.score > b.match.score;
        }
        return a.releaseIndex < b.releaseIndex;
    });

    const auto &top = scored.at(0);
    if (top.match.score < kMinAlbumScore) {
        return MatchDecision {
            .best = std::nullopt,
            .ambiguous = false,
        };
    }

    const auto &bestMatch = top.match;
    const auto &bestRelease = releases.at(top.releaseIndex);

    bool isAmbiguous = false;
    for (qsizetype i = 1; i < scored.size(); ++i) {
        const auto &cand = scored.at(i);
        if (cand.match.score < kMinAlbumScore) {
            break;
        }
        if ((bestMatch.score - cand.match.score) < kAmbiguityMargin) {
            const auto &candRelease = releases.at(cand.releaseIndex);
            if (!isEquivalentCompletion(bestMatch, bestRelease, cand.match, candRelease)) {
                isAmbiguous = true;
                break;
            }
        }
    }

    return MatchDecision {
        .best = bestMatch,
        .ambiguous = isAmbiguous,
    };
}

} // namespace linernotes::butler
