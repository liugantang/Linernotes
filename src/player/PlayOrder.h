// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QtGlobal>

#include <player/PlayMode.h>

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace linernotes::player {

class PlayOrder {
public:
    enum class Advance : std::uint8_t {
        Auto,
        User
    }; // Auto：当前曲目自然播完；User：用户按“下一首”

    explicit PlayOrder(quint64 seed); // 随机数用 std::mt19937_64(seed)

    void setMode(PlayMode mode);
    [[nodiscard]] PlayMode mode() const;

    /// 队列内容整体替换：count 首，当前为 current（-1 表示无当前）
    void reset(int count, int current = -1);
    [[nodiscard]] int count() const;
    /// 当前曲目的队列下标；无当前（未开始、已被删除、已播完）时为 -1
    [[nodiscard]] int current() const;

    /// 预判下一首（用于向 mpv 预加载），不改变状态。没有下一首返回 std::nullopt。
    /// 约定：只要中间没有其它修改，peekNext(a) 与紧随其后的 advance(a) 结果必须相同。
    [[nodiscard]] std::optional<int> peekNext(Advance advance) const;
    /// 前进到下一首并返回其下标；没有下一首时返回 nullopt，并且 current() 变为 -1。
    std::optional<int> advance(Advance advance);
    /// 回到上一首并返回其下标；没有上一首返回 nullopt 且状态不变。
    std::optional<int> previous();
    /// 用户直接点选某首
    void jumpTo(int index);

    /// 队列变化通知（下标语义与 QAbstractItemModel 的 rowsInserted/rowsRemoved/rowsMoved 一致）
    void onInserted(int row, int count);
    void onRemoved(int row, int count);
    void onMoved(int from, int to); // 单个元素从 from 移到 to（移动后的下标）

private:
    [[nodiscard]] std::optional<int> peekNextSequential() const;
    [[nodiscard]] std::optional<int> peekNextRepeatAll() const;
    [[nodiscard]] std::optional<int> peekNextRepeatOne(Advance advance) const;
    [[nodiscard]] std::optional<int> peekNextShuffle() const;

    std::optional<int> advanceSequential();
    std::optional<int> advanceRepeatAll();
    std::optional<int> advanceRepeatOne(Advance advance);
    std::optional<int> advanceShuffle();

    void generateNextRound();
    void buildShuffleForCurrent();
    void buildShuffleInitial();
    void handleShuffleRemoved(int row, int removeEnd, int actualCount);

    PlayMode m_mode = PlayMode::Sequential;
    int m_count = 0;
    int m_current = -1;
    int m_pendingNext = -1; // 删除当前曲目后的下一个下标（非 Shuffle 模式）

    std::mt19937_64 m_rng;
    std::vector<int> m_currentRound;
    std::vector<int> m_nextRound;
    int m_shuffleIndex = -1;
};

} // namespace linernotes::player
