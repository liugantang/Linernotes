// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QtGlobal>

#include <cstdint>
#include <optional>

namespace linernotes::player {

enum class ListenEnd : std::uint8_t { Switched, Stopped }; // 换到另一首 / 停止或退出

struct ListenResult {
    qint64 startedAtMs = 0; // 第一次真正进入播放状态的时刻
    qint64 endedAtMs = 0;
    qint64 playedMs = 0; // 实际听到的时长
    qint64 pausedMs = 0; // 开始之后暂停的总时长
    qint64 durationMs = 0; // 曲目时长，未知为 0
    bool completed = false;
    bool skipped = false;
    std::optional<qint64> skipPositionMs;

    bool operator==(const ListenResult &other) const = default;
};

/// 负责“实际听到多少”的纯逻辑计算类。
///
/// 规则：
/// - 只有处于播放状态时，位置的前进量才计入 playedMs；且前进量不能超过距上一次位置更新的墙钟时间 +
/// 1000 ms 容差，超过视为 seek，不计入。后退一律视为 seek。
/// - 位置 0 不作为有效位置（Player 在空闲/换曲时用 0 表示“没有位置”），忽略，不更新“上一次位置”。
/// -
/// pausedMs：开始之后，从暂停到恢复（或结束）的墙钟时长累加；开始之前的暂停（如恢复上次会话后停在暂停状态）不计。
/// - completed：最后位置 >= durationMs - 3000（durationMs > 0 时）。
/// - skipped：end == Switched && !completed，skipPositionMs = 最后位置。Stopped 时 skipped =
/// false。
/// - 从未开始播放的会话 hasStarted() == false，调用方不写库。
class ListenSession {
public:
    explicit ListenSession(qint64 durationMs);

    void setDuration(qint64 durationMs);
    void onPlaying(bool playing, qint64 nowMs); // 播放/暂停切换
    void onPosition(qint64 positionMs, qint64 nowMs); // 播放位置更新
    [[nodiscard]] bool hasStarted() const;
    [[nodiscard]] ListenResult result(ListenEnd end, qint64 nowMs) const; // 可多次调用（检查点用）

private:
    qint64 m_durationMs = 0;
    bool m_isPlaying = false;
    qint64 m_startedAtMs = 0;
    qint64 m_playedMs = 0;
    qint64 m_accumulatedPausedMs = 0;
    std::optional<qint64> m_pausedSinceMs;

    std::optional<qint64> m_lastPositionMs;
    std::optional<qint64> m_lastPositionWallMs;
    std::optional<qint64> m_latestPositionMs;
};

} // namespace linernotes::player
