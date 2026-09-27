// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#include <cstdint>

namespace linernotes::ai {

Q_NAMESPACE
Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")

/// 这是 ai 命名空间唯一的 Q_NAMESPACE，需要反射/暴露给 QML 的 ai 枚举都放在这里。

enum class Purpose : std::uint8_t { Cleanup, Query, Dj, Guide, Narrative };
Q_ENUM_NS(Purpose)

enum class DataCategory : std::uint8_t {
    PlayHistory, // 播放记录、播放次数、跳过等行为数据
    Moments, // 用户写的“瞬间”内容
    Location, // 位置
};
Q_ENUM_NS(DataCategory)

} // namespace linernotes::ai
