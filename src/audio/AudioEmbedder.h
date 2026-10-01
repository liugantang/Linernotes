// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <core/Result.h>

#include <memory>
#include <span>

namespace linernotes::audio {

struct EmbedderOptions {
    int intraOpThreads = 4; // ONNX Runtime 推理线程数
    bool preferGpu = true; // 有 CUDA 执行器就用，失败回退 CPU
};

class AudioEmbedder {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kWindowSamples = 7 * kSampleRate; // 308700
    static constexpr int kDim = 1024;

    // 加载模型；失败返回错误（文件不存在、模型输入/输出名或形状不符）
    static core::Result<std::unique_ptr<AudioEmbedder>> create(
        const QString &modelPath, const EmbedderOptions &options = { });

    // 输入单声道 44.1 kHz float PCM（[-1, 1]）。长度不等于 kWindowSamples 时：
    // 长了取中间 kWindowSamples，短了末尾补零。返回 L2 归一化后的 kDim 维向量。
    // 可在任意线程调用，但同一实例不要并发调用。
    core::Result<QList<float>> embed(std::span<const float> pcm);

    [[nodiscard]] bool usingGpu() const;
    ~AudioEmbedder();

    Q_DISABLE_COPY_MOVE(AudioEmbedder)

private:
    struct Impl;
    explicit AudioEmbedder(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace linernotes::audio
