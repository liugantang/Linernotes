// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <core/Result.h>

namespace linernotes::audio {

struct DecodeOptions {
    int sampleRate = 0; // 0 = 保持源采样率
    int channels = 2; // 1 或 2；源声道数更少时取源声道数
    qint64 maxDurationMs = 0; // 0 = 解码全部；否则最多解码这么长
};

struct PcmBuffer {
    int sampleRate = 0;
    int channels = 0;
    QList<qint16> samples; // 交错排列
    [[nodiscard]] qint64 durationMs() const;
    bool operator==(const PcmBuffer &) const = default;
};

class AudioDecoder {
public:
    /// 同步解码，可在任意线程调用（不使用 Qt 对象）。FFmpeg 资源用 RAII（unique_ptr + 自定义
    /// deleter）管理。
    static core::Result<PcmBuffer> decode(const QString &path, const DecodeOptions &options);
};

} // namespace linernotes::audio
