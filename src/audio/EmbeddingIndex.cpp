// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EmbeddingIndex.h"

#include <audio/AudioLogging.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <utility>

namespace linernotes::audio {

EmbeddingIndex::EmbeddingIndex(int dim)
    : m_dim(std::max(0, dim))
{
}

void EmbeddingIndex::add(qint64 id, std::span<const float> vector)
{
    if (m_dim <= 0 || !std::cmp_equal(vector.size(), m_dim)) {
        qCWarning(lcAudio) << "EmbeddingIndex::add ignored vector with size" << vector.size()
                           << "expected" << m_dim;
        return;
    }

    const auto it = m_idToIndex.constFind(id);
    if (it != m_idToIndex.constEnd()) {
        const auto idx = static_cast<size_t>(it.value());
        const auto dim = static_cast<size_t>(m_dim);
        auto destSpan = std::span(m_vectors).subspan(idx * dim, dim);
        std::ranges::copy(vector, destSpan.begin());
    } else {
        const int idx = static_cast<int>(m_ids.size());
        m_ids.push_back(id);
        m_vectors.insert(m_vectors.end(), vector.begin(), vector.end());
        m_idToIndex.insert(id, idx);
    }
}

int EmbeddingIndex::size() const
{
    return static_cast<int>(m_ids.size());
}

bool EmbeddingIndex::contains(qint64 id) const
{
    return m_idToIndex.contains(id);
}

QList<Neighbor> EmbeddingIndex::nearest(qint64 id, int k) const
{
    if (k <= 0 || m_dim <= 0 || !m_idToIndex.contains(id)) {
        return { };
    }

    const auto selfIndex = static_cast<size_t>(m_idToIndex.value(id));
    const auto dim = static_cast<size_t>(m_dim);
    const auto query = std::span(m_vectors).subspan(selfIndex * dim, dim);

    std::vector<Neighbor> candidates;
    if (m_ids.size() > 1) {
        candidates.reserve(m_ids.size() - 1);
    }

    const size_t total = m_ids.size();
    for (size_t i = 0; i < total; ++i) {
        if (i == selfIndex) {
            continue;
        }
        const auto target = std::span(m_vectors).subspan(i * dim, dim);
        const float dot = std::inner_product(query.begin(), query.end(), target.begin(), 0.0F);
        candidates.push_back(Neighbor { .id = m_ids.at(i), .score = dot });
    }

    const auto cmp = [](const Neighbor &a, const Neighbor &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.id < b.id;
    };

    const auto kSize = static_cast<size_t>(k);
    if (kSize < candidates.size()) {
        std::ranges::partial_sort(
            candidates, candidates.begin() + static_cast<ptrdiff_t>(kSize), cmp);
        candidates.resize(kSize);
    } else {
        std::ranges::sort(candidates, cmp);
    }

    QList<Neighbor> result;
    result.reserve(static_cast<qsizetype>(candidates.size()));
    for (const auto &c : candidates) {
        result.append(c);
    }
    return result;
}

QList<Neighbor> EmbeddingIndex::nearest(std::span<const float> query, int k) const
{
    if (k <= 0 || m_dim <= 0 || !std::cmp_equal(query.size(), m_dim) || m_ids.empty()) {
        return { };
    }

    double queryNormSq = 0.0;
    for (const float val : query) {
        queryNormSq += static_cast<double>(val) * static_cast<double>(val);
    }
    const double queryNorm = std::sqrt(queryNormSq);

    std::vector<Neighbor> candidates;
    candidates.reserve(m_ids.size());

    const auto dim = static_cast<size_t>(m_dim);
    const size_t total = m_ids.size();
    for (size_t i = 0; i < total; ++i) {
        const auto target = std::span(m_vectors).subspan(i * dim, dim);
        const float dot = std::inner_product(query.begin(), query.end(), target.begin(), 0.0F);
        const float score
            = (queryNorm > 1e-12) ? static_cast<float>(static_cast<double>(dot) / queryNorm) : 0.0F;
        candidates.push_back(Neighbor { .id = m_ids.at(i), .score = score });
    }

    const auto cmp = [](const Neighbor &a, const Neighbor &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.id < b.id;
    };

    const auto kSize = static_cast<size_t>(k);
    if (kSize < candidates.size()) {
        std::ranges::partial_sort(
            candidates, candidates.begin() + static_cast<ptrdiff_t>(kSize), cmp);
        candidates.resize(kSize);
    } else {
        std::ranges::sort(candidates, cmp);
    }

    QList<Neighbor> result;
    result.reserve(static_cast<qsizetype>(candidates.size()));
    for (const auto &c : candidates) {
        result.append(c);
    }
    return result;
}

} // namespace linernotes::audio
