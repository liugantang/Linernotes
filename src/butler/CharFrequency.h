// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <cstdint>

namespace linernotes::butler::internal {

/// 检查字符是否在简体中文高频字表中（前约 800 字）
bool isSimplifiedChineseHighFreq(char32_t cp);

/// 检查字符是否在繁体中文高频字表中（前约 800 字）
bool isTraditionalChineseHighFreq(char32_t cp);

/// 检查字符是否在韩文高频音节表中（前约 400 音节）
bool isKoreanHighFreq(char32_t cp);

} // namespace linernotes::butler::internal
