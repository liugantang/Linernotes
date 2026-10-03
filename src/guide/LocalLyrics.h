// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <core/Result.h>
#include <library/Database.h>

#include <optional>

namespace linernotes::guide {

/// raw_tags 中该曲的 LYRICS / UNSYNCED LYRICS（取 priority 最小的一条，参照 raw_tags 表结构与
/// TagReader 的写入方式）
core::Result<std::optional<QString>> embeddedLyrics(library::Database &db, qint64 trackId);

/// 音频文件同目录、同 basename 的 .lrc（扩展名大小写不敏感：.lrc/.LRC）；cue 分轨（tracks.cue_index
/// 非空）不查。 编码：先按 UTF-8（去 BOM）解码，含替换字符时再按 GB18030 → Shift_JIS 依次尝试（用
/// QStringDecoder），都失败用 UTF-8 结果。
std::optional<QString> sidecarLyrics(const QString &audioPath);

/// 检查曲目是否为 cue 分轨（tracks.cue_index 非空）
core::Result<bool> isCueTrack(library::Database &db, qint64 trackId);

/// 非空行（trim 后）≥ 3
bool isUsableLyrics(const QString &text);

} // namespace linernotes::guide
