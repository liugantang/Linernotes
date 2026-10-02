// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Recommender.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QtGlobal>

#include <audio/EmbeddingIndex.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <rec/CandidatePool.h>
#include <rec/SoundIndex.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace linernotes::rec {

namespace {

constexpr double kAudioWeight = 1.0;
constexpr double kSameArtistWeight = 0.8;
constexpr double kSameAlbumWeight = 0.3;
constexpr double kYearWeight = 0.3;
constexpr double kFavoriteWeight = 0.5;
constexpr double kSkipWeight = 0.8;

struct PosSeedInfo {
    qint64 trackId = 0;
    double weight = 0.0;
    std::optional<qint64> albumId;
    QList<qint64> artistIds;
    std::optional<int> year;
};

struct ScoredCandidate {
    const Candidate *candidate = nullptr;
    double score = 0.0;
};

bool buildQueryVector(
    const QList<Seed> &seeds, const audio::EmbeddingIndex *index, std::vector<float> &queryVec)
{
    if (index == nullptr || seeds.isEmpty()) {
        return false;
    }
    const int dim = index->dim();
    if (dim <= 0) {
        return false;
    }

    std::vector<double> accum(static_cast<size_t>(dim), 0.0);
    int validSeedCount = 0;

    for (const auto &seed : seeds) {
        const auto vec = index->vector(seed.trackId);
        if (std::cmp_equal(vec.size(), dim)) {
            ++validSeedCount;
            size_t d = 0;
            for (const float val : vec) {
                accum.at(d++) += seed.weight * static_cast<double>(val);
            }
        }
    }

    if (validSeedCount == 0) {
        return false;
    }

    double normSq = 0.0;
    for (const double val : accum) {
        normSq += val * val;
    }
    const double norm = std::sqrt(normSq);
    if (norm <= 1e-12) {
        return false;
    }

    queryVec.resize(static_cast<size_t>(dim));
    for (size_t d = 0; std::cmp_less(d, dim); ++d) {
        queryVec.at(d) = static_cast<float>(accum.at(d) / norm);
    }
    return true;
}

QHash<qint64, double> computeAudioScores(const QList<Candidate> &candidates,
    const audio::EmbeddingIndex *index, const std::vector<float> &queryVec)
{
    QHash<qint64, double> scores;
    if (index == nullptr || queryVec.empty()) {
        return scores;
    }

    const int dim = index->dim();
    if (dim <= 0 || !std::cmp_equal(queryVec.size(), dim)) {
        return scores;
    }

    std::vector<double> cosines;
    std::vector<qint64> trackIdsWithVec;
    cosines.reserve(candidates.size());
    trackIdsWithVec.reserve(candidates.size());

    for (const auto &c : candidates) {
        const auto vec = index->vector(c.trackId);
        if (std::cmp_equal(vec.size(), dim)) {
            const float dot
                = std::inner_product(queryVec.begin(), queryVec.end(), vec.begin(), 0.0F);
            cosines.push_back(static_cast<double>(dot));
            trackIdsWithVec.push_back(c.trackId);
        }
    }

    if (cosines.empty()) {
        return scores;
    }

    double sumCos = 0.0;
    for (const double cosVal : cosines) {
        sumCos += cosVal;
    }
    const auto mean = sumCos / static_cast<double>(cosines.size());

    double sumSqDiff = 0.0;
    for (const double cosVal : cosines) {
        const auto diff = cosVal - mean;
        sumSqDiff += diff * diff;
    }
    const auto variance = sumSqDiff / static_cast<double>(cosines.size());
    auto stdDev = std::sqrt(variance);
    if (stdDev < 1e-6) {
        stdDev = 1.0;
    }

    for (size_t i = 0; i < cosines.size(); ++i) {
        const auto z = (cosines.at(i) - mean) / stdDev;
        const auto clampedZ = std::clamp(z, -3.0, 3.0);
        scores.insert(trackIdsWithVec.at(i), clampedZ * kAudioWeight);
    }

    return scores;
}

quint64 splitmix64(quint64 x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

double computeRandomPerturbation(quint64 randomSeed, qint64 trackId, double randomness)
{
    if (randomness <= 0.0) {
        return 0.0;
    }
    const auto combined = randomSeed ^ (static_cast<quint64>(trackId) * 0x9e3779b97f4a7c15ULL);
    const auto h = splitmix64(combined);
    const auto u = static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
    return randomness * u;
}

double artistOverlapWeight(const Candidate &c, const QList<PosSeedInfo> &posSeeds)
{
    if (c.artistIds.isEmpty() || posSeeds.isEmpty()) {
        return 0.0;
    }
    const QSet<qint64> candArtistSet(c.artistIds.begin(), c.artistIds.end());
    double matched = 0.0;
    for (const auto &ps : posSeeds) {
        for (const qint64 artId : ps.artistIds) {
            if (candArtistSet.contains(artId)) {
                matched += ps.weight;
                break;
            }
        }
    }
    return matched;
}

double albumMatchWeight(const Candidate &c, const QList<PosSeedInfo> &posSeeds)
{
    if (!c.albumId.has_value() || posSeeds.isEmpty()) {
        return 0.0;
    }
    const qint64 albumId = c.albumId.value();
    double matched = 0.0;
    for (const auto &ps : posSeeds) {
        if (ps.albumId.has_value() && ps.albumId.value() == albumId) {
            matched += ps.weight;
        }
    }
    return matched;
}

double computeYearScore(std::optional<int> candYear, double avgSeedYear, bool hasAvgSeedYear)
{
    if (!hasAvgSeedYear || !candYear.has_value()) {
        return 0.0;
    }
    const auto diff = std::abs(static_cast<double>(candYear.value()) - avgSeedYear);
    if (diff <= 2.0) {
        return 1.0;
    }
    if (diff < 10.0) {
        return (10.0 - diff) / 8.0;
    }
    return 0.0;
}

double computeMetadataBonus(const Candidate &c, const QList<PosSeedInfo> &posSeeds,
    double totalPosWeight, double avgSeedYear, bool hasAvgSeedYear)
{
    if (posSeeds.isEmpty() || totalPosWeight <= 0.0) {
        return 0.0;
    }

    const auto artistBonus
        = (artistOverlapWeight(c, posSeeds) / totalPosWeight) * kSameArtistWeight;
    const auto albumBonus = (albumMatchWeight(c, posSeeds) / totalPosWeight) * kSameAlbumWeight;
    const auto yearBonus = computeYearScore(c.year, avgSeedYear, hasAvgSeedYear) * kYearWeight;

    return artistBonus + albumBonus + yearBonus;
}

double computeBehaviorScore(const Candidate &c, double freshnessWeight, qint64 nowMs)
{
    const double favBonus = c.favorite ? kFavoriteWeight : 0.0;

    const int totalPlays = c.playCount + c.skipCount;
    const double skipPenalty = (totalPlays > 0)
        ? (kSkipWeight * static_cast<double>(c.skipCount) / static_cast<double>(totalPlays))
        : 0.0;

    double freshnessScore = 0.0;
    if (!c.lastPlayedAtMs.has_value()) {
        freshnessScore = 1.0;
    } else {
        const auto diffMs = static_cast<double>(nowMs - c.lastPlayedAtMs.value());
        const auto days = std::max(0.0, diffMs / (24.0 * 3600.0 * 1000.0));
        freshnessScore = std::min(days / 180.0, 1.0);
    }
    const auto freshnessBonus = freshnessScore * freshnessWeight;

    return favBonus - skipPenalty + freshnessBonus;
}

bool isExcluded(const Candidate &c, const QSet<qint64> &seedTrackIds,
    const QSet<qint64> &seedWorkIds, const QSet<qint64> &exclude, qint64 recentExcludeMs,
    qint64 nowMs)
{
    if (seedTrackIds.contains(c.trackId)) {
        return true;
    }
    if (exclude.contains(c.trackId)) {
        return true;
    }
    if (c.workId.has_value() && seedWorkIds.contains(c.workId.value())) {
        return true;
    }
    if (recentExcludeMs > 0 && c.lastPlayedAtMs.has_value()) {
        const qint64 elapsed = nowMs - c.lastPlayedAtMs.value();
        if (elapsed >= 0 && elapsed < recentExcludeMs) {
            return true;
        }
    }
    return false;
}

bool passesDiversity(const Candidate &c, const QSet<qint64> &selectedWorkIds,
    const QHash<qint64, int> &albumCounts, const QHash<qint64, int> &artistCounts, int maxPerArtist,
    int maxPerAlbum)
{
    if (c.workId.has_value() && selectedWorkIds.contains(c.workId.value())) {
        return false;
    }
    if (maxPerAlbum > 0 && c.albumId.has_value()) {
        if (albumCounts.value(c.albumId.value(), 0) >= maxPerAlbum) {
            return false;
        }
    }
    if (maxPerArtist > 0) {
        for (const qint64 artistId : c.artistIds) {
            if (artistCounts.value(artistId, 0) >= maxPerArtist) {
                return false;
            }
        }
    }
    return true;
}

void recordSelection(const Candidate &c, QList<qint64> &selected, QSet<qint64> &selectedWorkIds,
    QHash<qint64, int> &albumCounts, QHash<qint64, int> &artistCounts)
{
    selected.append(c.trackId);
    if (c.workId.has_value()) {
        selectedWorkIds.insert(c.workId.value());
    }
    if (c.albumId.has_value()) {
        albumCounts.insert(c.albumId.value(), albumCounts.value(c.albumId.value(), 0) + 1);
    }
    for (const qint64 artistId : c.artistIds) {
        artistCounts.insert(artistId, artistCounts.value(artistId, 0) + 1);
    }
}

QList<qint64> selectGreedy(
    std::vector<ScoredCandidate> &scored, int count, int maxPerArtist, int maxPerAlbum)
{
    if (count <= 0 || scored.empty()) {
        return { };
    }

    std::ranges::stable_sort(scored, [](const ScoredCandidate &a, const ScoredCandidate &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.candidate->trackId < b.candidate->trackId;
    });

    QList<qint64> selected;
    selected.reserve(std::min(count, static_cast<int>(scored.size())));
    QHash<qint64, int> artistCounts;
    QHash<qint64, int> albumCounts;
    QSet<qint64> selectedWorkIds;

    for (const auto &sc : scored) {
        if (std::cmp_greater_equal(selected.size(), count)) {
            break;
        }
        const auto &c = *sc.candidate;
        if (!passesDiversity(
                c, selectedWorkIds, albumCounts, artistCounts, maxPerArtist, maxPerAlbum)) {
            continue;
        }
        recordSelection(c, selected, selectedWorkIds, albumCounts, artistCounts);
    }

    return selected;
}

} // namespace

QList<qint64> rank(const QList<Candidate> &candidates, const audio::EmbeddingIndex *index,
    const RecRequest &request, qint64 nowMs)
{
    if (candidates.isEmpty() || request.count <= 0) {
        return { };
    }

    QHash<qint64, const Candidate *> candidateMap;
    candidateMap.reserve(candidates.size());
    for (const auto &c : candidates) {
        candidateMap.insert(c.trackId, &c);
    }

    QSet<qint64> seedTrackIds;
    QSet<qint64> seedWorkIds;
    QList<PosSeedInfo> posSeeds;
    double totalPosWeight = 0.0;
    double seedYearSum = 0.0;
    double seedYearWeightSum = 0.0;

    for (const auto &seed : request.seeds) {
        seedTrackIds.insert(seed.trackId);
        const auto it = candidateMap.constFind(seed.trackId);
        if (it != candidateMap.constEnd()) {
            const auto *c = it.value();
            if (c->workId.has_value()) {
                seedWorkIds.insert(c->workId.value());
            }
            if (seed.weight > 0.0) {
                PosSeedInfo psi;
                psi.trackId = seed.trackId;
                psi.weight = seed.weight;
                psi.albumId = c->albumId;
                psi.artistIds = c->artistIds;
                psi.year = c->year;
                posSeeds.append(psi);
                totalPosWeight += seed.weight;
                if (c->year.has_value()) {
                    seedYearSum += seed.weight * static_cast<double>(c->year.value());
                    seedYearWeightSum += seed.weight;
                }
            }
        }
    }

    const bool hasAvgSeedYear = (seedYearWeightSum > 0.0);
    const auto avgSeedYear = hasAvgSeedYear ? (seedYearSum / seedYearWeightSum) : 0.0;

    std::vector<float> queryVec;
    buildQueryVector(request.seeds, index, queryVec);

    const auto audioScores = computeAudioScores(candidates, index, queryVec);

    std::vector<ScoredCandidate> scoredCandidates;
    scoredCandidates.reserve(candidates.size());

    for (const auto &c : candidates) {
        if (isExcluded(
                c, seedTrackIds, seedWorkIds, request.exclude, request.recentExcludeMs, nowMs)) {
            continue;
        }

        const auto audioScore = audioScores.value(c.trackId, 0.0);
        const auto metadataScore
            = computeMetadataBonus(c, posSeeds, totalPosWeight, avgSeedYear, hasAvgSeedYear);
        const auto behaviorScore = computeBehaviorScore(c, request.freshnessWeight, nowMs);
        const auto randomScore
            = computeRandomPerturbation(request.randomSeed, c.trackId, request.randomness);

        const auto totalScore = audioScore + metadataScore + behaviorScore + randomScore;
        scoredCandidates.push_back(ScoredCandidate {
            .candidate = &c,
            .score = totalScore,
        });
    }

    return selectGreedy(scoredCandidates, request.count, request.maxPerArtist, request.maxPerAlbum);
}

Recommender::Recommender(library::Database &db, SoundIndex &soundIndex, const core::Clock &clock)
    : m_db(db)
    , m_soundIndex(soundIndex)
    , m_clock(clock)
{
}

core::Result<QList<qint64>> Recommender::recommend(const RecRequest &request)
{
    const auto candRes = loadCandidates(m_db);
    if (!candRes.ok()) {
        return candRes.error();
    }

    const auto indexRes = m_soundIndex.index();
    if (!indexRes.ok()) {
        return indexRes.error();
    }

    return rank(candRes.value(), indexRes.value(), request, m_clock.nowMs());
}

} // namespace linernotes::rec
