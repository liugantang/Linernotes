// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QHash>
#include <QLatin1Char>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <butler/ArtistCluster.h>
#include <butler/ArtistName.h>
#include <butler/ArtistSplit.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace linernotes::butler {

namespace {

int levenshteinDistance(QStringView s1, QStringView s2)
{
    const qsizetype n = s1.length();
    const qsizetype m = s2.length();
    if (std::abs(n - m) > 1) {
        return 2;
    }
    if (n == m) {
        int diff = 0;
        for (qsizetype i = 0; i < n; ++i) {
            if (s1.at(i) != s2.at(i)) {
                ++diff;
                if (diff > 1) {
                    return 2;
                }
            }
        }
        return diff;
    }
    const QStringView longer = (n > m) ? s1 : s2;
    const QStringView shorter = (n > m) ? s2 : s1;
    qsizetype i = 0;
    qsizetype j = 0;
    int diff = 0;
    while (i < longer.length() && j < shorter.length()) {
        if (longer.at(i) == shorter.at(j)) {
            ++i;
            ++j;
        } else {
            ++diff;
            if (diff > 1) {
                return 2;
            }
            ++i;
        }
    }
    return 1;
}

struct Dsu {
    std::vector<int> parent;
    explicit Dsu(int n)
        : parent(static_cast<size_t>(n))
    {
        for (int i = 0; i < n; ++i) {
            parent.at(static_cast<size_t>(i)) = i;
        }
    }

    int find(int i)
    {
        int root = i;
        while (root != parent.at(static_cast<size_t>(root))) {
            root = parent.at(static_cast<size_t>(root));
        }
        int curr = i;
        while (curr != root) {
            const int nxt = parent.at(static_cast<size_t>(curr));
            parent.at(static_cast<size_t>(curr)) = root;
            curr = nxt;
        }
        return root;
    }

    void unite(int i, int j)
    {
        const int rootI = find(i);
        const int rootJ = find(j);
        if (rootI != rootJ) {
            parent.at(static_cast<size_t>(rootI)) = rootJ;
        }
    }
};

struct EntryInfo {
    qint64 artistId = 0;
    QString name { };
    int trackCount = 0;
    QString exactKey { };
    QStringList tokens;
    QString joinedTokens { };
    bool hasSep = false;
    bool isShort = false;
};

void addLink(QHash<uint64_t, ArtistLinkKind> &linksMap, int u, int v, ArtistLinkKind kind)
{
    if (u == v) {
        return;
    }
    const int idxA = std::min(u, v);
    const int idxB = std::max(u, v);
    const uint64_t key = (static_cast<uint64_t>(idxA) << 32) | static_cast<uint32_t>(idxB);
    auto it = linksMap.find(key);
    if (it == linksMap.end()) {
        linksMap.insert(key, kind);
    } else if (static_cast<uint8_t>(kind) < static_cast<uint8_t>(it.value())) {
        it.value() = kind;
    }
}

template <typename K> void appendToBucket(QHash<K, QList<int>> &buckets, const K &key, int val)
{
    auto it = buckets.find(key);
    if (it == buckets.end()) {
        it = buckets.insert(key, { });
    }
    it.value().append(val);
}

void collectExactLinks(const QList<EntryInfo> &infoList, QHash<uint64_t, ArtistLinkKind> &linksMap)
{
    QHash<QString, QList<int>> buckets;
    for (int i = 0; i < infoList.size(); ++i) {
        const auto &k = infoList.at(i).exactKey;
        if (!k.isEmpty()) {
            appendToBucket(buckets, k, i);
        }
    }
    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
        const auto &b = it.value();
        if (b.size() < 2) {
            continue;
        }
        for (qsizetype i = 0; i < b.size(); ++i) {
            for (qsizetype j = i + 1; j < b.size(); ++j) {
                const int u = b.at(i);
                const int v = b.at(j);
                const auto kind = (infoList.at(u).isShort || infoList.at(v).isShort)
                    ? ArtistLinkKind::Fuzzy
                    : ArtistLinkKind::Exact;
                addLink(linksMap, u, v, kind);
            }
        }
    }
}

void collectRomanizedLinks(
    const QList<EntryInfo> &infoList, QHash<uint64_t, ArtistLinkKind> &linksMap)
{
    QHash<QString, QList<int>> buckets;
    for (int i = 0; i < infoList.size(); ++i) {
        const auto &info = infoList.at(i);
        if (!info.hasSep && !info.tokens.isEmpty()) {
            const QString rKey = info.tokens.join(QLatin1Char(' '));
            appendToBucket(buckets, rKey, i);
        }
    }
    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
        const auto &b = it.value();
        if (b.size() < 2) {
            continue;
        }
        for (qsizetype i = 0; i < b.size(); ++i) {
            for (qsizetype j = i + 1; j < b.size(); ++j) {
                const int u = b.at(i);
                const int v = b.at(j);
                const auto kind = (infoList.at(u).isShort || infoList.at(v).isShort)
                    ? ArtistLinkKind::Fuzzy
                    : ArtistLinkKind::Romanized;
                addLink(linksMap, u, v, kind);
            }
        }
    }
}

void collectFuzzyLinksForStrings(const QList<int> &indices, const QList<EntryInfo> &infoList,
    const std::function<const QString &(const EntryInfo &)> &getString,
    QHash<uint64_t, ArtistLinkKind> &linksMap)
{
    QHash<QString, QList<int>> delBuckets;
    for (const int idx : indices) {
        const QString &s = getString(infoList.at(idx));
        appendToBucket(delBuckets, s, idx);
        for (qsizetype pos = 0; pos < s.length(); ++pos) {
            appendToBucket(delBuckets, QString(s.left(pos) + s.mid(pos + 1)), idx);
        }
    }
    QSet<uint64_t> checked;
    for (auto dIt = delBuckets.cbegin(); dIt != delBuckets.cend(); ++dIt) {
        const auto &b = dIt.value();
        if (b.size() < 2) {
            continue;
        }
        for (qsizetype i = 0; i < b.size(); ++i) {
            for (qsizetype j = i + 1; j < b.size(); ++j) {
                const int u = std::min(b.at(i), b.at(j));
                const int v = std::max(b.at(i), b.at(j));
                if (u == v) {
                    continue;
                }
                const uint64_t key = (static_cast<uint64_t>(u) << 32) | static_cast<uint32_t>(v);
                if (checked.contains(key)) {
                    continue;
                }
                checked.insert(key);
                if (levenshteinDistance(getString(infoList.at(u)), getString(infoList.at(v)))
                    == 1) {
                    addLink(linksMap, u, v, ArtistLinkKind::Fuzzy);
                }
            }
        }
    }
}

void collectFuzzyRomanLinks(
    const QList<EntryInfo> &infoList, QHash<uint64_t, ArtistLinkKind> &linksMap)
{
    QHash<qsizetype, QList<int>> tokenGroups;
    for (int i = 0; i < infoList.size(); ++i) {
        const auto &info = infoList.at(i);
        if (!info.hasSep && info.tokens.size() >= 2 && info.joinedTokens.length() >= 8) {
            appendToBucket(tokenGroups, info.tokens.size(), i);
        }
    }

    for (auto gIt = tokenGroups.cbegin(); gIt != tokenGroups.cend(); ++gIt) {
        collectFuzzyLinksForStrings(
            gIt.value(), infoList,
            [](const EntryInfo &info) -> const QString & { return info.joinedTokens; }, linksMap);
    }
}

void collectFuzzyExactLinks(
    const QList<EntryInfo> &infoList, QHash<uint64_t, ArtistLinkKind> &linksMap)
{
    QList<int> candidates;
    for (int i = 0; i < infoList.size(); ++i) {
        const auto &info = infoList.at(i);
        if (!info.hasSep && info.exactKey.length() >= 6) {
            candidates.append(i);
        }
    }
    collectFuzzyLinksForStrings(
        candidates, infoList,
        [](const EntryInfo &info) -> const QString & { return info.exactKey; }, linksMap);
}

ArtistCluster buildSingleCluster(const QList<int> &memberIndices, const QList<ArtistEntry> &entries,
    const QHash<uint64_t, ArtistLinkKind> &linksMap)
{
    ArtistCluster cluster;
    cluster.members.reserve(memberIndices.size());
    for (const int idx : memberIndices) {
        cluster.members.append(entries.at(idx));
    }
    std::ranges::sort(cluster.members, [](const ArtistEntry &a, const ArtistEntry &b) {
        if (a.trackCount != b.trackCount) {
            return a.trackCount > b.trackCount;
        }
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.artistId < b.artistId;
    });

    for (qsizetype i = 0; i < memberIndices.size(); ++i) {
        for (qsizetype j = i + 1; j < memberIndices.size(); ++j) {
            const int u = std::min(memberIndices.at(i), memberIndices.at(j));
            const int v = std::max(memberIndices.at(i), memberIndices.at(j));
            const uint64_t key = (static_cast<uint64_t>(u) << 32) | static_cast<uint32_t>(v);
            const auto it = linksMap.find(key);
            if (it != linksMap.end()) {
                const auto kind = it.value();
                if (kind == ArtistLinkKind::Exact || kind == ArtistLinkKind::Romanized) {
                    const qint64 idA = entries.at(u).artistId;
                    const qint64 idB = entries.at(v).artistId;
                    cluster.links.append(ArtistLink {
                        .a = std::min(idA, idB),
                        .b = std::max(idA, idB),
                        .kind = kind,
                    });
                }
            }
        }
    }

    std::ranges::sort(cluster.links, [](const ArtistLink &a, const ArtistLink &b) {
        if (a.a != b.a) {
            return a.a < b.a;
        }
        if (a.b != b.b) {
            return a.b < b.b;
        }
        return static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind);
    });

    return cluster;
}

QList<ArtistLink> buildFuzzyCandidates(
    const QHash<uint64_t, ArtistLinkKind> &linksMap, const QList<ArtistEntry> &entries, Dsu &dsu)
{
    QList<ArtistLink> fuzzyCandidates;
    QSet<QPair<qint64, qint64>> seen;

    for (auto it = linksMap.cbegin(); it != linksMap.cend(); ++it) {
        if (it.value() != ArtistLinkKind::Fuzzy) {
            continue;
        }
        const uint64_t key = it.key();
        const int u = static_cast<int>(key >> 32);
        const int v = static_cast<int>(key & 0xFFFFFFFF);
        if (dsu.find(u) == dsu.find(v)) {
            continue;
        }
        const qint64 idA = std::min(entries.at(u).artistId, entries.at(v).artistId);
        const qint64 idB = std::max(entries.at(u).artistId, entries.at(v).artistId);
        const auto pair = qMakePair(idA, idB);
        if (seen.contains(pair)) {
            continue;
        }
        seen.insert(pair);
        fuzzyCandidates.append(ArtistLink {
            .a = idA,
            .b = idB,
            .kind = ArtistLinkKind::Fuzzy,
        });
    }

    std::ranges::sort(fuzzyCandidates, [](const ArtistLink &a, const ArtistLink &b) {
        if (a.a != b.a) {
            return a.a < b.a;
        }
        return a.b < b.b;
    });

    return fuzzyCandidates;
}

} // namespace

ArtistClustering clusterArtists(const QList<ArtistEntry> &entries)
{
    const qsizetype n = entries.size();
    if (n < 2) {
        return { };
    }

    QList<EntryInfo> infoList;
    infoList.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        const auto &e = entries.at(i);
        EntryInfo info {
            .artistId = e.artistId,
            .name = e.name,
            .trackCount = e.trackCount,
            .exactKey = exactKey(e.name),
            .tokens = { },
            .joinedTokens = { },
            .hasSep = hasSeparators(e.name),
            .isShort = false,
        };
        info.isShort = (info.exactKey.length() <= 3);
        if (!info.hasSep) {
            info.tokens = romanTokens(e.name);
            info.joinedTokens = info.tokens.join(QString());
        }
        infoList.append(std::move(info));
    }

    QHash<uint64_t, ArtistLinkKind> linksMap;
    collectExactLinks(infoList, linksMap);
    collectRomanizedLinks(infoList, linksMap);
    collectFuzzyRomanLinks(infoList, linksMap);
    collectFuzzyExactLinks(infoList, linksMap);

    Dsu dsu(static_cast<int>(n));
    for (auto it = linksMap.cbegin(); it != linksMap.cend(); ++it) {
        if (it.value() == ArtistLinkKind::Exact || it.value() == ArtistLinkKind::Romanized) {
            const uint64_t key = it.key();
            const int u = static_cast<int>(key >> 32);
            const int v = static_cast<int>(key & 0xFFFFFFFF);
            dsu.unite(u, v);
        }
    }

    QHash<int, QList<int>> clusterGroups;
    for (int i = 0; i < n; ++i) {
        appendToBucket(clusterGroups, dsu.find(i), i);
    }

    ArtistClustering result;
    for (auto it = clusterGroups.cbegin(); it != clusterGroups.cend(); ++it) {
        const auto &memberIndices = it.value();
        if (memberIndices.size() >= 2) {
            result.clusters.append(buildSingleCluster(memberIndices, entries, linksMap));
        }
    }

    std::ranges::sort(result.clusters, [](const ArtistCluster &a, const ArtistCluster &b) {
        if (a.members.isEmpty() || b.members.isEmpty()) {
            return false;
        }
        const auto &ma = a.members.first();
        const auto &mb = b.members.first();
        if (ma.trackCount != mb.trackCount) {
            return ma.trackCount > mb.trackCount;
        }
        if (ma.name != mb.name) {
            return ma.name < mb.name;
        }
        return ma.artistId < mb.artistId;
    });

    result.fuzzyCandidates = buildFuzzyCandidates(linksMap, entries, dsu);

    return result;
}

} // namespace linernotes::butler
