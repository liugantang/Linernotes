// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QtGlobal>

#include <span>
#include <vector>

namespace linernotes::audio {

struct Neighbor {
    qint64 id = 0;
    float score = 0.0F;
    bool operator==(const Neighbor &) const = default;
};

class EmbeddingIndex {
public:
    explicit EmbeddingIndex(int dim);
    void add(qint64 id, std::span<const float> vector); // 假定已归一化；维度不符则忽略并记日志
    [[nodiscard]] int size() const;
    [[nodiscard]] bool contains(qint64 id) const;
    [[nodiscard]] std::span<const float> vector(qint64 id) const;
    [[nodiscard]] int dim() const;
    // 与 id 对应向量余弦（点积）最高的 k 个，不含自身，按分数降序；id 不存在返回空
    [[nodiscard]] QList<Neighbor> nearest(qint64 id, int k) const;
    // 与任意查询向量最近的 k 个（阶段 10 推荐会用：种子向量的平均）
    [[nodiscard]] QList<Neighbor> nearest(std::span<const float> query, int k) const;

private:
    int m_dim = 0;
    std::vector<qint64> m_ids;
    std::vector<float> m_vectors;
    QHash<qint64, int> m_idToIndex;
};

} // namespace linernotes::audio
