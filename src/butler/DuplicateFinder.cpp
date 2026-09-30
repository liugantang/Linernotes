// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DuplicateFinder.h"

#include "ButlerLogging.h"

#include <QHashFunctions>
#include <QSet>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

namespace {

bool isLosslessCodec(const QString &codec)
{
    const QString c = codec.trimmed().toLower();
    if (c.isEmpty()) {
        return false;
    }
    return c == QStringLiteral("flac") || c == QStringLiteral("alac") || c == QStringLiteral("wav")
        || c == QStringLiteral("wave") || c == QStringLiteral("pcm")
        || c.startsWith(QStringLiteral("pcm_")) || c == QStringLiteral("aiff")
        || c == QStringLiteral("aif") || c == QStringLiteral("ape")
        || c == QStringLiteral("wavpack") || c == QStringLiteral("wv") || c == QStringLiteral("tta")
        || c == QStringLiteral("dsd") || c == QStringLiteral("dsf") || c == QStringLiteral("dff")
        || c.startsWith(QStringLiteral("dsd"));
}

struct WorkVersionKey {
    qint64 workId = 0;
    QString versionType { };
    bool operator==(const WorkVersionKey &) const = default;
};

inline size_t qHash(const WorkVersionKey &key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.workId, key.versionType);
}

class DisjointSet {
public:
    void add(qint64 id)
    {
        if (!m_parent.contains(id)) {
            m_parent.insert(id, id);
        }
    }

    qint64 find(qint64 id)
    {
        qint64 root = id;
        while (m_parent.value(root, root) != root) {
            root = m_parent.value(root, root);
        }
        qint64 curr = id;
        while (curr != root) {
            const qint64 next = m_parent.value(curr, curr);
            m_parent.insert(curr, root);
            curr = next;
        }
        return root;
    }

    void unite(qint64 a, qint64 b)
    {
        add(a);
        add(b);
        const qint64 rootA = find(a);
        const qint64 rootB = find(b);
        if (rootA != rootB) {
            m_parent.insert(rootA, rootB);
        }
    }

    QHash<qint64, QList<qint64>> components()
    {
        QHash<qint64, QList<qint64>> comps;
        for (auto it = m_parent.cbegin(); it != m_parent.cend(); ++it) {
            const qint64 id = it.key();
            const qint64 root = find(id);
            auto compIt = comps.find(root);
            if (compIt == comps.end()) {
                comps.insert(root, QList<qint64> { id });
            } else {
                compIt.value().append(id);
            }
        }
        return comps;
    }

private:
    QHash<qint64, qint64> m_parent;
};

void connectExactContentHashes(DisjointSet &dsu, const QList<DupTrack> &tracks)
{
    QHash<QString, QList<qint64>> hashToTracks;
    for (const auto &track : tracks) {
        if (!track.contentHash.isEmpty()) {
            auto it = hashToTracks.find(track.contentHash);
            if (it == hashToTracks.end()) {
                hashToTracks.insert(track.contentHash, QList<qint64> { track.trackId });
            } else {
                it.value().append(track.trackId);
            }
        }
    }
    for (auto it = hashToTracks.cbegin(); it != hashToTracks.cend(); ++it) {
        const auto &ids = it.value();
        if (ids.size() >= 2) {
            const qint64 firstId = ids.first();
            for (qsizetype i = 1; i < ids.size(); ++i) {
                dsu.unite(firstId, ids.at(i));
            }
        }
    }
}

const audio::RawFingerprint *fingerprintOf(const QHash<qint64, DupTrack> &trackMap, qint64 id)
{
    const auto it = trackMap.constFind(id);
    if (it == trackMap.constEnd()) {
        return nullptr;
    }
    const std::optional<audio::RawFingerprint> &fp = it->fingerprint;
    return fp.has_value() ? &fp.value() : nullptr;
}

void compareClusterFingerprints(
    DisjointSet &dsu, const QList<QList<qint64>> &clusters, const QHash<qint64, DupTrack> &trackMap)
{
    for (const auto &cluster : clusters) {
        qsizetype limit = cluster.size();
        if (limit > 200) {
            qCWarning(lcButler) << "Cluster size" << limit
                                << "exceeds 200, truncating pairwise comparison to first 200";
            limit = 200;
        }

        for (qsizetype i = 0; i < limit; ++i) {
            const qint64 idA = cluster.at(i);
            const audio::RawFingerprint *fpA = fingerprintOf(trackMap, idA);
            if (fpA == nullptr) {
                continue;
            }
            for (qsizetype j = i + 1; j < limit; ++j) {
                const qint64 idB = cluster.at(j);
                const audio::RawFingerprint *fpB = fingerprintOf(trackMap, idB);
                if (fpB == nullptr) {
                    continue;
                }
                const double sim = audio::fingerprintSimilarity(*fpA, *fpB);
                if (sim >= kSameRecordingThreshold) {
                    dsu.unite(idA, idB);
                }
            }
        }
    }
}

bool areAllContentHashesSame(const QList<qint64> &members, const QHash<qint64, DupTrack> &trackMap)
{
    if (members.isEmpty()) {
        return false;
    }
    const auto firstIt = trackMap.constFind(members.first());
    if (firstIt == trackMap.constEnd() || firstIt->contentHash.isEmpty()) {
        return false;
    }
    const QString &firstHash = firstIt->contentHash;
    for (qsizetype i = 1; i < members.size(); ++i) {
        const auto it = trackMap.constFind(members.at(i));
        if (it == trackMap.constEnd() || it->contentHash != firstHash) {
            return false;
        }
    }
    return true;
}

void formSuspectGroups(QList<DupGroup> &groups, QSet<qint64> &assignedTrackIds,
    const QList<QList<qint64>> &clusters, const QHash<qint64, DupTrack> &trackMap)
{
    for (const auto &cluster : clusters) {
        QList<qint64> unassigned;
        bool hasMissingFingerprint = false;
        for (const qint64 id : cluster) {
            if (!assignedTrackIds.contains(id)) {
                unassigned.append(id);
                const auto it = trackMap.constFind(id);
                if (it == trackMap.constEnd() || !it->fingerprint.has_value()) {
                    hasMissingFingerprint = true;
                }
            }
        }
        if (unassigned.size() >= 2 && hasMissingFingerprint) {
            DupGroup suspectGroup;
            suspectGroup.kind = DuplicateKind::Suspect;
            suspectGroup.trackIds = unassigned;
            std::ranges::sort(suspectGroup.trackIds);
            for (const qint64 id : suspectGroup.trackIds) {
                assignedTrackIds.insert(id);
            }
            groups.append(suspectGroup);
        }
    }
}

void assignRecommendationsAndSort(QList<DupGroup> &groups, const QHash<qint64, DupTrack> &trackMap)
{
    for (auto &group : groups) {
        qint64 bestTrackId = 0;
        double bestScore = -1.0;
        for (const qint64 id : std::as_const(group.trackIds)) {
            const auto it = trackMap.constFind(id);
            const double score = (it != trackMap.constEnd()) ? it->keepScore : 0.0;
            if (bestTrackId == 0 || score > bestScore || (score == bestScore && id < bestTrackId)) {
                bestScore = score;
                bestTrackId = id;
            }
        }
        group.recommendedTrackId = bestTrackId;
    }

    std::ranges::sort(groups, [](const DupGroup &a, const DupGroup &b) {
        if (a.trackIds.isEmpty() || b.trackIds.isEmpty()) {
            return false;
        }
        return a.trackIds.first() < b.trackIds.first();
    });
}

} // namespace

QString duplicateKindToString(DuplicateKind kind)
{
    switch (kind) {
    case DuplicateKind::Exact:
        return QStringLiteral("exact");
    case DuplicateKind::SameRecording:
        return QStringLiteral("same_recording");
    case DuplicateKind::Suspect:
        return QStringLiteral("suspect");
    }
    return { };
}

std::optional<DuplicateKind> duplicateKindFromString(const QString &str)
{
    if (str == QStringLiteral("exact")) {
        return DuplicateKind::Exact;
    }
    if (str == QStringLiteral("same_recording")) {
        return DuplicateKind::SameRecording;
    }
    if (str == QStringLiteral("suspect")) {
        return DuplicateKind::Suspect;
    }
    return std::nullopt;
}

double keepScore(const KeepFactors &f)
{
    double score = 0.0;
    if (isLosslessCodec(f.codec)) {
        score += 1000.0;
        score += static_cast<double>(f.bitDepth) * 10.0;
        score += static_cast<double>(f.sampleRate) / 1000.0;
    } else {
        score += static_cast<double>(f.bitrate) / 10.0;
    }
    score += static_cast<double>(f.filledTagFields) * 5.0;
    if (f.hasCover) {
        score += 5.0;
    }
    return score;
}

namespace {

/// groupTracks 已按时长排序；相邻时长差 ≤ kDurationToleranceMs 的连成一簇，只返回 ≥ 2 首的簇。
QList<QList<qint64>> splitByDuration(const QList<DupTrack> &groupTracks)
{
    QList<QList<qint64>> clusters;
    QList<qint64> currentCluster { groupTracks.first().trackId };
    for (qsizetype i = 1; i < groupTracks.size(); ++i) {
        const auto &prev = groupTracks.at(i - 1);
        const auto &curr = groupTracks.at(i);
        if (curr.durationMs - prev.durationMs > kDurationToleranceMs) {
            if (currentCluster.size() >= 2) {
                clusters.append(currentCluster);
            }
            currentCluster.clear();
        }
        currentCluster.append(curr.trackId);
    }
    if (currentCluster.size() >= 2) {
        clusters.append(currentCluster);
    }
    return clusters;
}

} // namespace

QList<QList<qint64>> candidateClusters(const QList<DupTrack> &tracks)
{
    QHash<WorkVersionKey, QList<DupTrack>> grouped;
    for (const auto &track : tracks) {
        if (track.workId <= 0) {
            continue;
        }
        const WorkVersionKey key {
            .workId = track.workId,
            .versionType = track.versionType,
        };
        auto it = grouped.find(key);
        if (it == grouped.end()) {
            grouped.insert(key, QList<DupTrack> { track });
        } else {
            it.value().append(track);
        }
    }

    auto keys = grouped.keys();
    std::ranges::sort(keys, [](const WorkVersionKey &a, const WorkVersionKey &b) {
        if (a.workId != b.workId) {
            return a.workId < b.workId;
        }
        return a.versionType < b.versionType;
    });

    QList<QList<qint64>> allClusters;
    for (const auto &key : std::as_const(keys)) {
        auto groupTracks = grouped.value(key);
        if (groupTracks.size() < 2) {
            continue;
        }

        std::ranges::sort(groupTracks, [](const DupTrack &a, const DupTrack &b) {
            if (a.durationMs != b.durationMs) {
                return a.durationMs < b.durationMs;
            }
            return a.trackId < b.trackId;
        });

        allClusters.append(splitByDuration(groupTracks));
    }

    return allClusters;
}

QList<DupGroup> findDuplicates(const QList<DupTrack> &tracks)
{
    QHash<qint64, DupTrack> trackMap;
    DisjointSet dsu;
    for (const auto &track : tracks) {
        dsu.add(track.trackId);
        trackMap.insert(track.trackId, track);
    }

    // 1. Union exact content hashes
    connectExactContentHashes(dsu, tracks);

    // 2. Candidate clusters & acoustic fingerprint comparison
    const auto clusters = candidateClusters(tracks);
    compareClusterFingerprints(dsu, clusters, trackMap);

    // 3. Form Exact and SameRecording groups from connected components >= 2
    QList<DupGroup> groups;
    QSet<qint64> assignedTrackIds;
    const auto comps = dsu.components();
    for (auto it = comps.cbegin(); it != comps.cend(); ++it) {
        const auto &members = it.value();
        if (members.size() < 2) {
            continue;
        }

        DupGroup group;
        group.kind = areAllContentHashesSame(members, trackMap) ? DuplicateKind::Exact
                                                                : DuplicateKind::SameRecording;
        group.trackIds = members;
        std::ranges::sort(group.trackIds);

        for (const qint64 id : group.trackIds) {
            assignedTrackIds.insert(id);
        }
        groups.append(group);
    }

    // 4. Form Suspect groups for unassigned tracks with missing fingerprint in clusters
    formSuspectGroups(groups, assignedTrackIds, clusters, trackMap);

    // 5. Assign recommendedTrackId and sort groups by smallest trackId
    assignRecommendationsAndSort(groups, trackMap);

    return groups;
}

} // namespace linernotes::butler
