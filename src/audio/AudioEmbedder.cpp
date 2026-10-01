// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AudioEmbedder.h"

#include "AudioLogging.h"
#include "Errors.h"

#include <QFile>
#include <QStringLiteral>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace linernotes::audio {

struct AudioEmbedder::Impl {
    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "linernotes_audio" };
    std::unique_ptr<Ort::Session> session;
    bool usingGpu = false;
};

namespace {

Ort::SessionOptions buildSessionOptions(const EmbedderOptions &options, bool enableGpu)
{
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(options.intraOpThreads);
    sessionOptions.SetInterOpNumThreads(1);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (enableGpu) {
        const OrtCUDAProviderOptions cudaOptions { };
        sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
    }
    return sessionOptions;
}

core::Result<void> validateModelMetadata(const Ort::Session &session, const QString &modelPath)
{
    // 1. Check input count
    if (session.GetInputCount() != 1) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Expected exactly 1 model input, found %1")
                .arg(session.GetInputCount()),
            .detail = modelPath };
    }

    // 2. Check input name
    const auto inputNames = session.GetInputNames();
    if (inputNames.empty() || inputNames.at(0) != "pcm") {
        const QString foundName = inputNames.empty() ? QStringLiteral("none")
                                                     : QString::fromStdString(inputNames.at(0));
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Expected input name 'pcm', found '%1'").arg(foundName),
            .detail = modelPath };
    }

    // 3. Check input tensor type and shape
    const auto inputTypeInfo = session.GetInputTypeInfo(0);
    if (inputTypeInfo.GetONNXType() != ONNX_TYPE_TENSOR) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Input is not a tensor"),
            .detail = modelPath };
    }
    const auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
    if (inputTensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Input tensor element type must be float"),
            .detail = modelPath };
    }
    const auto inputShape = inputTensorInfo.GetShape();
    if (inputShape.size() != 2
        || (inputShape.at(1) != AudioEmbedder::kWindowSamples && inputShape.at(1) != -1)) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Input tensor shape must be [?, %1]")
                .arg(AudioEmbedder::kWindowSamples),
            .detail = modelPath };
    }

    // 4. Check output count
    if (session.GetOutputCount() != 1) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Expected exactly 1 model output, found %1")
                .arg(session.GetOutputCount()),
            .detail = modelPath };
    }

    // 5. Check output name
    const auto outputNames = session.GetOutputNames();
    if (outputNames.empty() || outputNames.at(0) != "embedding") {
        const QString foundName = outputNames.empty() ? QStringLiteral("none")
                                                      : QString::fromStdString(outputNames.at(0));
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message
            = QStringLiteral("Expected output name 'embedding', found '%1'").arg(foundName),
            .detail = modelPath };
    }

    // 6. Check output tensor type and shape
    const auto outputTypeInfo = session.GetOutputTypeInfo(0);
    if (outputTypeInfo.GetONNXType() != ONNX_TYPE_TENSOR) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Output is not a tensor"),
            .detail = modelPath };
    }
    const auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
    if (outputTensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QStringLiteral("Output tensor element type must be float"),
            .detail = modelPath };
    }
    const auto outputShape = outputTensorInfo.GetShape();
    if (outputShape.size() != 2
        || (outputShape.at(1) != AudioEmbedder::kDim && outputShape.at(1) != -1)) {
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message
            = QStringLiteral("Output tensor shape must be [?, %1]").arg(AudioEmbedder::kDim),
            .detail = modelPath };
    }

    return { };
}

} // namespace

AudioEmbedder::AudioEmbedder(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

AudioEmbedder::~AudioEmbedder() = default;

core::Result<std::unique_ptr<AudioEmbedder>> AudioEmbedder::create(
    const QString &modelPath, const EmbedderOptions &options)
{
    if (!QFile::exists(modelPath)) {
        return core::Error { .code = QString(errc::kAudioModelNotFound),
            .message = QStringLiteral("Audio embedding model file not found"),
            .detail = modelPath };
    }

    auto impl = std::make_unique<Impl>();
    const std::string modelPathStd = modelPath.toStdString();
    bool usingGpu = false;

    if (options.preferGpu) {
        try {
            const Ort::SessionOptions sessionOptions = buildSessionOptions(options, true);
            impl->session
                = std::make_unique<Ort::Session>(impl->env, modelPathStd.c_str(), sessionOptions);
            usingGpu = true;
        } catch (const Ort::Exception &e) {
            qCWarning(lcAudio) << "Failed to initialize CUDA execution provider, falling back to "
                                  "CPU:"
                               << e.what();
            usingGpu = false;
        }
    }

    if (!impl->session) {
        try {
            const Ort::SessionOptions sessionOptions = buildSessionOptions(options, false);
            impl->session
                = std::make_unique<Ort::Session>(impl->env, modelPathStd.c_str(), sessionOptions);
            usingGpu = false;
        } catch (const Ort::Exception &e) {
            qCWarning(lcAudio) << "Failed to create ONNX session:" << e.what();
            return core::Error { .code = QString(errc::kAudioModelInvalid),
                .message = QString::fromUtf8(e.what()),
                .detail = modelPath };
        }
    }

    impl->usingGpu = usingGpu;

    try {
        const auto validateRes = validateModelMetadata(*impl->session, modelPath);
        if (!validateRes.ok()) {
            return validateRes.error();
        }
    } catch (const Ort::Exception &e) {
        qCWarning(lcAudio) << "Failed to query ONNX model metadata:" << e.what();
        return core::Error { .code = QString(errc::kAudioModelInvalid),
            .message = QString::fromUtf8(e.what()),
            .detail = modelPath };
    }

    std::unique_ptr<AudioEmbedder> embedder(new AudioEmbedder(std::move(impl)));
    return embedder;
}

core::Result<QList<float>> AudioEmbedder::embed(std::span<const float> pcm)
{
    if (!m_impl || !m_impl->session) {
        return core::Error { .code = QString(errc::kAudioEmbedFailed),
            .message = QStringLiteral("AudioEmbedder session is not initialized"),
            .detail = QString() };
    }

    try {
        const std::array<int64_t, 2> inputShape { 1, kWindowSamples };
        const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<float> buffer(static_cast<size_t>(kWindowSamples), 0.0F);

        if (pcm.size() <= static_cast<size_t>(kWindowSamples)) {
            std::ranges::copy(pcm, buffer.begin());
        } else {
            const size_t start = (pcm.size() - static_cast<size_t>(kWindowSamples)) / 2;
            const auto sub = pcm.subspan(start, static_cast<size_t>(kWindowSamples));
            std::ranges::copy(sub, buffer.begin());
        }

        const Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, buffer.data(), buffer.size(), inputShape.data(), inputShape.size());

        const std::array<const char *, 1> inputNames { "pcm" };
        const std::array<const char *, 1> outputNames { "embedding" };
        const Ort::RunOptions runOptions;

        auto outputTensors = m_impl->session->Run(
            runOptions, inputNames.data(), &inputTensor, 1, outputNames.data(), 1);

        if (outputTensors.empty() || !outputTensors.front().IsTensor()) {
            return core::Error { .code = QString(errc::kAudioEmbedFailed),
                .message = QStringLiteral("Inference did not return a valid tensor"),
                .detail = QString() };
        }

        const auto tensorInfo = outputTensors.front().GetTensorTypeAndShapeInfo();
        const size_t elementCount = tensorInfo.GetElementCount();
        if (elementCount < static_cast<size_t>(kDim)) {
            return core::Error { .code = QString(errc::kAudioEmbedFailed),
                .message = QStringLiteral(
                    "Output tensor element count (%1) is less than expected dimension (%2)")
                    .arg(elementCount)
                    .arg(kDim),
                .detail = QString() };
        }

        const auto embeddingSpan
            = std::span(outputTensors.front().GetTensorData<float>(), elementCount)
                  .subspan(0, static_cast<size_t>(kDim));

        // L2 normalization
        double sumSq = 0.0;
        for (const float val : embeddingSpan) {
            const auto dVal = static_cast<double>(val);
            sumSq += dVal * dVal;
        }
        const double norm = std::sqrt(sumSq);

        QList<float> result;
        result.reserve(kDim);
        if (norm > 1e-12) {
            for (const float val : embeddingSpan) {
                result.append(static_cast<float>(static_cast<double>(val) / norm));
            }
        } else {
            for (int i = 0; i < kDim; ++i) {
                result.append(0.0F);
            }
        }

        return result;
    } catch (const Ort::Exception &e) {
        qCWarning(lcAudio) << "Inference failed with Ort::Exception:" << e.what();
        return core::Error { .code = QString(errc::kAudioEmbedFailed),
            .message = QString::fromUtf8(e.what()),
            .detail = QString() };
    }
}

bool AudioEmbedder::usingGpu() const
{
    return m_impl ? m_impl->usingGpu : false;
}

} // namespace linernotes::audio
