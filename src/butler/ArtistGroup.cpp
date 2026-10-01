// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistGroup.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <butler/ArtistName.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace linernotes::butler {

namespace {

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

QSet<QString> collectIdentityKeys(
    const ArtistEntry &entry, const QHash<QString, QStringList> &altNames)
{
    QSet<QString> keys;
    const QString selfKey = exactKey(entry.name);
    if (!selfKey.isEmpty()) {
        keys.insert(selfKey);
        auto it = altNames.find(selfKey);
        if (it != altNames.end()) {
            for (const auto &aka : it.value()) {
                const QString akaKey = exactKey(aka);
                if (!akaKey.isEmpty()) {
                    keys.insert(akaKey);
                }
            }
        }
    }
    const QString rKey = romanKey(entry.name);
    if (!rKey.isEmpty()) {
        keys.insert(QStringLiteral("r:") + rKey);
    }
    return keys;
}

bool checkExactOnly(const QList<ArtistEntry> &members)
{
    if (members.isEmpty()) {
        return false;
    }
    const QString firstKey = exactKey(members.first().name);
    if (firstKey.length() <= 3) {
        return false;
    }
    for (qsizetype i = 1; i < members.size(); ++i) {
        if (exactKey(members.at(i).name) != firstKey) {
            return false;
        }
    }
    return true;
}

bool compareArtists(const ArtistEntry &a, const ArtistEntry &b)
{
    if (a.trackCount != b.trackCount) {
        return a.trackCount > b.trackCount;
    }
    const int cmp = a.name.compare(b.name);
    if (cmp != 0) {
        return cmp < 0;
    }
    return a.artistId < b.artistId;
}

bool compareMemberLists(const QList<ArtistEntry> &a, const QList<ArtistEntry> &b)
{
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    return compareArtists(a.first(), b.first());
}

} // namespace

ArtistGrouping groupArtists(const QList<ArtistEntry> &entries,
    const QHash<QString, QStringList> &altNames, int maxGroupSize)
{
    const qsizetype n = entries.size();
    if (n < 2) {
        return { };
    }

    QHash<QString, QList<int>> keyBuckets;
    for (int i = 0; i < static_cast<int>(n); ++i) {
        const auto keys = collectIdentityKeys(entries.at(i), altNames);
        for (const auto &k : keys) {
            auto it = keyBuckets.find(k);
            if (it == keyBuckets.end()) {
                it = keyBuckets.insert(k, { });
            }
            it->append(i);
        }
    }

    Dsu dsu(static_cast<int>(n));
    for (auto it = keyBuckets.cbegin(); it != keyBuckets.cend(); ++it) {
        const auto &bucket = it.value();
        if (bucket.size() >= 2) {
            const int firstIdx = bucket.at(0);
            for (qsizetype j = 1; j < bucket.size(); ++j) {
                dsu.unite(firstIdx, bucket.at(j));
            }
        }
    }

    QHash<int, QList<int>> rootGroups;
    for (int i = 0; i < static_cast<int>(n); ++i) {
        const int root = dsu.find(i);
        auto it = rootGroups.find(root);
        if (it == rootGroups.end()) {
            it = rootGroups.insert(root, { });
        }
        it->append(i);
    }

    ArtistGrouping result;
    for (auto it = rootGroups.cbegin(); it != rootGroups.cend(); ++it) {
        const auto &indices = it.value();
        if (indices.size() < 2) {
            continue;
        }

        QList<ArtistEntry> members;
        members.reserve(indices.size());
        for (const int idx : indices) {
            members.append(entries.at(idx));
        }
        std::ranges::sort(members, compareArtists);

        if (members.size() > maxGroupSize) {
            result.oversized.append(std::move(members));
        } else {
            const bool exactOnly = checkExactOnly(members);
            result.groups.append(ArtistGroup {
                .members = std::move(members),
                .exactOnly = exactOnly,
            });
        }
    }

    std::ranges::sort(result.groups, [](const ArtistGroup &a, const ArtistGroup &b) {
        return compareMemberLists(a.members, b.members);
    });
    std::ranges::sort(result.oversized, compareMemberLists);

    return result;
}

} // namespace linernotes::butler
