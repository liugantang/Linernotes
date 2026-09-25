// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QtGlobal>

namespace linernotes::player {

/// 增益渐变曲线。纯函数式，时间由调用方传入（便于测试）。
///
/// 曲线算法选择 smoothstep（t * t * (3 - 2 * t)）：
/// 1. 在 t = 0 与 t = 1 处的导数均为 0（即两端斜率为 0），保证渐变开始与结束时无斜率跳变，
///    避免音频增益突变产生爆音（Click / Pop）。
/// 2. 相较于余弦插值（0.5 * (1 - cos(pi * t))），smoothstep 仅需基础代数乘加运算，
///    无三角函数开销，在毫秒级高频定时器或音频帧计算中具有更优的性能与稳定性。
class GainRamp {
public:
    GainRamp() = default; // 静止在 1.0

    /// 从 from 在 durationMs 内渐变到 to。durationMs <= 0 表示立即到位。
    GainRamp(double from, double to, int durationMs);

    [[nodiscard]] double valueAt(qint64 elapsedMs) const; // elapsed < 0 视为 0；>= duration 返回 to
    [[nodiscard]] bool isFinishedAt(qint64 elapsedMs) const;
    [[nodiscard]] double target() const;

private:
    double m_from = 1.0;
    double m_to = 1.0;
    int m_durationMs = 0;
};

} // namespace linernotes::player
