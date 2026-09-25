// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayOrder.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace linernotes::player {

namespace {

int remapIndex(int val, int from, int to)
{
    if (val == from) {
        return to;
    }
    if (from < to) {
        if (val > from && val <= to) {
            return val - 1;
        }
    } else if (from > to) {
        if (val >= to && val < from) {
            return val + 1;
        }
    }
    return val;
}

} // namespace

PlayOrder::PlayOrder(quint64 seed)
    : m_rng(seed)
{
}

void PlayOrder::setMode(PlayMode mode)
{
    if (m_mode == mode) {
        return;
    }

    const PlayMode oldMode = m_mode;
    m_mode = mode;

    if (m_mode == PlayMode::Shuffle) {
        if (m_current != -1) {
            buildShuffleForCurrent();
        } else {
            buildShuffleInitial();
        }
    } else if (oldMode == PlayMode::Shuffle) {
        m_currentRound.clear();
        m_nextRound.clear();
        m_shuffleIndex = -1;
        m_pendingNext = -1;
    }
}

PlayMode PlayOrder::mode() const
{
    return m_mode;
}

void PlayOrder::reset(int count, int current)
{
    m_count = std::max(0, count);
    if (current < 0 || current >= m_count) {
        m_current = -1;
    } else {
        m_current = current;
    }
    m_pendingNext = -1;

    if (m_mode == PlayMode::Shuffle) {
        if (m_current != -1) {
            buildShuffleForCurrent();
        } else {
            buildShuffleInitial();
        }
    } else {
        m_currentRound.clear();
        m_nextRound.clear();
        m_shuffleIndex = -1;
    }
}

int PlayOrder::count() const
{
    return m_count;
}

int PlayOrder::current() const
{
    return m_current;
}

/*
 * 洗牌防短期重复算法说明（Anti-Repeat Algorithm）：
 *
 * 目标：
 * 设 n = count，K = min(⌊n / 2⌋, 20)。
 * 在 Shuffle 模式下无限次 advance(Auto) 得到的播放序列中，同一曲目两次出现之间至少隔着 K
 * 首其它曲目。 即：若同一曲目 X 在连续播放流中的下标分别为 i 和 j (j > i)，则两者之间间隔的曲目数 j
 * - i - 1 >= K (即 j - i >= K + 1)。
 *
 * 算法原理与为何能严格保证间隔：
 * 1. 单轮内部无重复：
 *    每一轮都是集合 {0, 1, ..., n - 1} 的一个全排列，每个元素在单轮中恰好出现 1 次。
 * 2. 跨轮次保证最小间隔 K：
 *    - 设上一轮（m_currentRound）最后播放的 K 首曲目构成的集合为 recent（即 m_currentRound 末尾的 K
 * 个元素）。
 *    - 新一轮（m_nextRound）首先通过 Fisher-Yates 随机打乱生成一个全排列。
 *    - 检查 m_nextRound 的前 K 个位置（下标 0 .. K - 1）。若其中存在属于 recent 的元素（设数量为
 * m）， 则将它们与位于 [K, n - 1] 且不在 recent 中的元素进行随机等概率交换。
 *    - 槽位充分性证明：
 *      因为 K <= ⌊n / 2⌋ <= n / 2，所以区间 [K, n - 1] 包含 n - K 个槽位。
 *      在整个排列中，属于 recent 的元素恰好有 K 个。假设落在前 K 位中的有 m 个，则落在 [K, n - 1]
 * 中的有 K - m 个。 因此区间 [K, n - 1] 中不属于 recent 的可用候选槽位数 = (n - K) - (K - m) = n -
 * 2K + m。 由于 n >= 2K，所以 n - 2K + m >= m。这保证了候选槽位数恒大于等于违规元素数
 * m，交换必定可以成功。
 * 3. 间隔数学证明：
 *    - 对于属于 recent 的曲目（在上一轮中的位置 p1 满足 n - K <= p1 <= n - 1）：
 *      经上述调整后，在新一轮中的位置 p2 必然满足 K <= p2 <= n - 1。
 *      两轮之间的流间距 = (n + p2) - p1 >= n + K - (n - 1) = K + 1。
 *      因此中间间隔的曲目数 = 间距 - 1 >= K。
 *    - 对于不属于 recent 的曲目（在上一轮中的位置 p1 满足 0 <= p1 <= n - K - 1）：
 *      在新一轮中其位置 p2 满足 p2 >= 0。
 *      两轮之间的流间距 = (n + p2) - p1 >= n + 0 - (n - K - 1) = K + 1。
 *      因此中间间隔的曲目数 = 间距 - 1 >= K。
 * 综上所述，所有曲目在任意相邻两轮之间的重现间隔均至少为 K 首。
 */
void PlayOrder::generateNextRound()
{
    if (m_count <= 0) {
        m_nextRound.clear();
        return;
    }

    m_nextRound.resize(m_count);
    std::iota(m_nextRound.begin(), m_nextRound.end(), 0);

    // Fisher-Yates shuffle
    for (int i = m_count - 1; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        const int j = dist(m_rng);
        std::swap(m_nextRound.at(static_cast<size_t>(i)), m_nextRound.at(static_cast<size_t>(j)));
    }

    const int k = std::min(m_count / 2, 20);
    if (k <= 0 || m_currentRound.size() < static_cast<size_t>(k)) {
        return;
    }

    const std::vector<int> recent(m_currentRound.end() - k, m_currentRound.end());
    auto isRecent = [&recent](int val) { return std::ranges::find(recent, val) != recent.end(); };

    std::vector<int> violatingIndices;
    violatingIndices.reserve(static_cast<size_t>(k));
    for (int i = 0; i < k; ++i) {
        if (isRecent(m_nextRound.at(static_cast<size_t>(i)))) {
            violatingIndices.push_back(i);
        }
    }

    if (violatingIndices.empty()) {
        return;
    }

    std::vector<int> eligibleTargets;
    eligibleTargets.reserve(static_cast<size_t>(m_count - k));
    for (int j = k; j < m_count; ++j) {
        if (!isRecent(m_nextRound.at(static_cast<size_t>(j)))) {
            eligibleTargets.push_back(j);
        }
    }

    for (const int violIdx : violatingIndices) {
        std::uniform_int_distribution<size_t> dist(0, eligibleTargets.size() - 1);
        const size_t targetPick = dist(m_rng);
        const int targetIdx = eligibleTargets.at(targetPick);

        std::swap(m_nextRound.at(static_cast<size_t>(violIdx)),
            m_nextRound.at(static_cast<size_t>(targetIdx)));

        eligibleTargets.at(targetPick) = eligibleTargets.back();
        eligibleTargets.pop_back();
    }
}

void PlayOrder::buildShuffleForCurrent()
{
    if (m_count <= 0) {
        m_currentRound.clear();
        m_nextRound.clear();
        m_shuffleIndex = -1;
        m_current = -1;
        return;
    }

    m_currentRound.resize(m_count);
    m_currentRound.at(0) = m_current;

    size_t idx = 1;
    for (int i = 0; i < m_count; ++i) {
        if (i != m_current) {
            m_currentRound.at(idx++) = i;
        }
    }

    for (int i = m_count - 1; i > 1; --i) {
        std::uniform_int_distribution<int> dist(1, i);
        const int j = dist(m_rng);
        std::swap(
            m_currentRound.at(static_cast<size_t>(i)), m_currentRound.at(static_cast<size_t>(j)));
    }

    m_shuffleIndex = 0;
    generateNextRound();
}

void PlayOrder::buildShuffleInitial()
{
    if (m_count <= 0) {
        m_currentRound.clear();
        m_nextRound.clear();
        m_shuffleIndex = -1;
        m_current = -1;
        return;
    }

    m_currentRound.resize(m_count);
    std::iota(m_currentRound.begin(), m_currentRound.end(), 0);

    for (int i = m_count - 1; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        const int j = dist(m_rng);
        std::swap(
            m_currentRound.at(static_cast<size_t>(i)), m_currentRound.at(static_cast<size_t>(j)));
    }

    m_shuffleIndex = -1;
    m_current = -1;
    generateNextRound();
}

std::optional<int> PlayOrder::peekNextSequential() const
{
    if (m_current == -1) {
        if (m_pendingNext != -1) {
            return (m_pendingNext < m_count) ? std::optional<int>(m_pendingNext) : std::nullopt;
        }
        return 0;
    }
    const int next = m_current + 1;
    return (next < m_count) ? std::optional<int>(next) : std::nullopt;
}

std::optional<int> PlayOrder::peekNextRepeatAll() const
{
    if (m_current == -1) {
        return (m_pendingNext != -1) ? (m_pendingNext % m_count) : 0;
    }
    return (m_current + 1) % m_count;
}

std::optional<int> PlayOrder::peekNextRepeatOne(Advance advance) const
{
    if (advance == Advance::Auto) {
        if (m_current == -1) {
            return (m_pendingNext != -1) ? (m_pendingNext % m_count) : 0;
        }
        return m_current;
    }
    return peekNextRepeatAll();
}

std::optional<int> PlayOrder::peekNextShuffle() const
{
    if (m_currentRound.empty()) {
        return std::nullopt;
    }
    if (m_shuffleIndex == -1) {
        return m_currentRound.front();
    }
    if (m_current == -1) {
        if (std::cmp_less(m_shuffleIndex, m_currentRound.size())) {
            return m_currentRound.at(static_cast<size_t>(m_shuffleIndex));
        }
        return m_nextRound.empty() ? std::nullopt : std::optional<int>(m_nextRound.front());
    }
    if (std::cmp_less(m_shuffleIndex + 1, m_currentRound.size())) {
        return m_currentRound.at(static_cast<size_t>(m_shuffleIndex) + 1);
    }
    return m_nextRound.empty() ? std::nullopt : std::optional<int>(m_nextRound.front());
}

std::optional<int> PlayOrder::peekNext(Advance advance) const
{
    if (m_count <= 0) {
        return std::nullopt;
    }

    switch (m_mode) {
    case PlayMode::Sequential:
        return peekNextSequential();
    case PlayMode::RepeatAll:
        return peekNextRepeatAll();
    case PlayMode::RepeatOne:
        return peekNextRepeatOne(advance);
    case PlayMode::Shuffle:
        return peekNextShuffle();
    }

    return std::nullopt;
}

std::optional<int> PlayOrder::advanceSequential()
{
    if (m_current == -1) {
        if (m_pendingNext != -1) {
            if (m_pendingNext < m_count) {
                m_current = m_pendingNext;
                m_pendingNext = -1;
                return m_current;
            }
            m_pendingNext = -1;
            m_current = -1;
            return std::nullopt;
        }
        m_current = 0;
        return m_current;
    }
    const int next = m_current + 1;
    if (next < m_count) {
        m_current = next;
        return m_current;
    }
    m_current = -1;
    return std::nullopt;
}

std::optional<int> PlayOrder::advanceRepeatAll()
{
    if (m_current == -1) {
        if (m_pendingNext != -1) {
            m_current = m_pendingNext % m_count;
            m_pendingNext = -1;
            return m_current;
        }
        m_current = 0;
        return m_current;
    }
    m_current = (m_current + 1) % m_count;
    return m_current;
}

std::optional<int> PlayOrder::advanceRepeatOne(Advance advance)
{
    if (advance == Advance::Auto) {
        if (m_current == -1) {
            if (m_pendingNext != -1) {
                m_current = m_pendingNext % m_count;
                m_pendingNext = -1;
                return m_current;
            }
            m_current = 0;
            return m_current;
        }
        return m_current;
    }
    return advanceRepeatAll();
}

std::optional<int> PlayOrder::advanceShuffle()
{
    if (m_currentRound.empty()) {
        m_current = -1;
        return std::nullopt;
    }
    if (m_shuffleIndex == -1) {
        m_shuffleIndex = 0;
        m_current = m_currentRound.front();
        return m_current;
    }
    if (m_current == -1) {
        if (std::cmp_less(m_shuffleIndex, m_currentRound.size())) {
            m_current = m_currentRound.at(static_cast<size_t>(m_shuffleIndex));
            return m_current;
        }
        m_currentRound = std::move(m_nextRound);
        m_shuffleIndex = 0;
        m_current = m_currentRound.front();
        generateNextRound();
        return m_current;
    }
    if (std::cmp_less(m_shuffleIndex + 1, m_currentRound.size())) {
        m_shuffleIndex++;
        m_current = m_currentRound.at(static_cast<size_t>(m_shuffleIndex));
        return m_current;
    }
    m_currentRound = std::move(m_nextRound);
    m_shuffleIndex = 0;
    m_current = m_currentRound.front();
    generateNextRound();
    return m_current;
}

std::optional<int> PlayOrder::advance(Advance advance)
{
    if (m_count <= 0) {
        m_current = -1;
        m_pendingNext = -1;
        return std::nullopt;
    }

    switch (m_mode) {
    case PlayMode::Sequential:
        return advanceSequential();
    case PlayMode::RepeatAll:
        return advanceRepeatAll();
    case PlayMode::RepeatOne:
        return advanceRepeatOne(advance);
    case PlayMode::Shuffle:
        return advanceShuffle();
    }

    return std::nullopt;
}

std::optional<int> PlayOrder::previous()
{
    if (m_count <= 0 || m_current == -1) {
        return std::nullopt;
    }

    switch (m_mode) {
    case PlayMode::Sequential: {
        if (m_current > 0) {
            m_current--;
            return m_current;
        }
        return std::nullopt;
    }
    case PlayMode::RepeatAll:
    case PlayMode::RepeatOne: {
        m_current = (m_current > 0) ? (m_current - 1) : (m_count - 1);
        return m_current;
    }
    case PlayMode::Shuffle: {
        if (m_shuffleIndex > 0) {
            m_shuffleIndex--;
            m_current = m_currentRound.at(static_cast<size_t>(m_shuffleIndex));
            return m_current;
        }
        return std::nullopt;
    }
    }

    return std::nullopt;
}

void PlayOrder::jumpTo(int index)
{
    if (index < 0 || index >= m_count) {
        return;
    }

    m_current = index;
    m_pendingNext = -1;

    if (m_mode == PlayMode::Shuffle) {
        buildShuffleForCurrent();
    }
}

void PlayOrder::onInserted(int row, int count)
{
    if (count <= 0 || row < 0 || row > m_count) {
        return;
    }

    m_count += count;

    if (m_current != -1 && m_current >= row) {
        m_current += count;
    }

    if (m_pendingNext != -1 && m_pendingNext >= row) {
        m_pendingNext += count;
    }

    if (m_mode == PlayMode::Shuffle) {
        for (int &val : m_currentRound) {
            if (val >= row) {
                val += count;
            }
        }

        int unplayedStart = 0;
        if (m_current == -1) {
            unplayedStart = (m_shuffleIndex >= 0) ? m_shuffleIndex : 0;
        } else {
            unplayedStart = m_shuffleIndex + 1;
        }

        for (int i = 0; i < count; ++i) {
            const int newVal = row + i;
            std::uniform_int_distribution<int> dist(
                unplayedStart, static_cast<int>(m_currentRound.size()));
            const int insertPos = dist(m_rng);
            m_currentRound.insert(m_currentRound.begin() + insertPos, newVal);
        }

        generateNextRound();
    }
}

void PlayOrder::handleShuffleRemoved(int row, int removeEnd, int actualCount)
{
    std::vector<int> newRound;
    newRound.reserve(static_cast<size_t>(m_count));
    int newShuffleIndex = m_shuffleIndex;

    for (size_t i = 0; i < m_currentRound.size(); ++i) {
        const int val = m_currentRound.at(i);
        if (val >= row && val < removeEnd) {
            if (std::cmp_less(i, m_shuffleIndex)) {
                newShuffleIndex--;
            }
        } else {
            const int shiftedVal = (val >= removeEnd) ? (val - actualCount) : val;
            newRound.push_back(shiftedVal);
        }
    }

    m_currentRound = std::move(newRound);
    m_shuffleIndex = newShuffleIndex;
    generateNextRound();
}

void PlayOrder::onRemoved(int row, int count)
{
    if (count <= 0 || row < 0 || row >= m_count) {
        return;
    }

    const int actualCount = std::min(count, m_count - row);
    const int removeEnd = row + actualCount;
    const bool currentRemoved = (m_current >= row && m_current < removeEnd);

    m_count -= actualCount;

    if (m_count == 0) {
        m_current = -1;
        m_pendingNext = -1;
        m_currentRound.clear();
        m_nextRound.clear();
        m_shuffleIndex = -1;
        return;
    }

    if (currentRemoved) {
        m_current = -1;
        m_pendingNext = row;
    } else if (m_current >= removeEnd) {
        m_current -= actualCount;
    }

    if (m_pendingNext >= removeEnd) {
        m_pendingNext -= actualCount;
    } else if (m_pendingNext >= row && m_pendingNext < removeEnd) {
        m_pendingNext = row;
    }

    if (m_mode == PlayMode::Shuffle) {
        handleShuffleRemoved(row, removeEnd, actualCount);
    }
}

void PlayOrder::onMoved(int from, int to)
{
    if (from < 0 || from >= m_count || to < 0 || to >= m_count || from == to) {
        return;
    }

    if (m_current != -1) {
        m_current = remapIndex(m_current, from, to);
    }

    if (m_pendingNext != -1) {
        m_pendingNext = remapIndex(m_pendingNext, from, to);
    }

    if (m_mode == PlayMode::Shuffle) {
        for (int &val : m_currentRound) {
            val = remapIndex(val, from, to);
        }
        for (int &val : m_nextRound) {
            val = remapIndex(val, from, to);
        }
    }
}

} // namespace linernotes::player
