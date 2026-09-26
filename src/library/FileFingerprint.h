// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <core/Result.h>

namespace linernotes::library {

/// 部分内容指纹，用于移动检测与完全重复识别。
/// 算法 v1：Blake2b-160 over [文件大小(8 字节小端) + 前 64 KiB + 后 64 KiB]；文件 ≤ 128 KiB 时为
/// [大小 + 全部内容]。 结果形如 "v1:<40 位十六进制>"。前缀便于以后升级算法。
/// 局限：只改动中间部分（且大小不变）的文件指纹相同 —— 对音频文件可接受，改标签通常改变头部或大小。
/// 只读、线程安全。打不开/读失败 → Error{errc::kFileRead, ..., detail=路径}。
class FileFingerprint {
public:
    static core::Result<QString> compute(const QString &path);
};

} // namespace linernotes::library
