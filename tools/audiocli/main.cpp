// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStringLiteral>

#include <audio/AudioDecoder.h>
#include <audio/AudioEmbedder.h>
#include <core/Result.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <span>
#include <vector>

namespace core = linernotes::core;

namespace {

struct CliConfig {
    QString command;
    QString modelPath;
    bool preferGpu = true;
    int threads = 4;
    std::optional<QString> refPath;
    QStringList files;
    bool verbose = false;
};

struct CliOptions {
    QCommandLineOption modelOption { QStringLiteral("model"),
        QStringLiteral("Path to MS-CLAP ONNX model file (required for embed)."),
        QStringLiteral("path") };
    QCommandLineOption cpuOption { QStringLiteral("cpu"),
        QStringLiteral("Force CPU inference instead of GPU.") };
    QCommandLineOption threadsOption { QStringLiteral("threads"),
        QStringLiteral("Number of intra-op threads for inference (default: 4)."),
        QStringLiteral("N"), QStringLiteral("4") };
    QCommandLineOption refOption { QStringLiteral("ref"),
        QStringLiteral("Path to reference embeddings JSON file for similarity comparison."),
        QStringLiteral("path") };
    QCommandLineOption verboseOption { QStringLiteral("verbose"),
        QStringLiteral("Enable verbose debug logging.") };
};

void setupParser(QCommandLineParser &parser, const CliOptions &opts)
{
    parser.setApplicationDescription(
        QStringLiteral("Linernotes audio feature extraction & embedding tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(opts.modelOption);
    parser.addOption(opts.cpuOption);
    parser.addOption(opts.threadsOption);
    parser.addOption(opts.refOption);
    parser.addOption(opts.verboseOption);
    parser.addPositionalArgument(QStringLiteral("command"),
        QStringLiteral("Subcommand to run (e.g. 'embed')."), QStringLiteral("<command>"));
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files to process."),
        QStringLiteral("[files...]"));
}

std::optional<CliConfig> parseArgs(
    QCommandLineParser &parser, const CliOptions &opts, int &exitCode)
{
    if (!parser.parse(QCoreApplication::arguments())) {
        std::cerr << qPrintable(parser.errorText()) << "\n";
        exitCode = 2;
        return std::nullopt;
    }

    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp(0);
    }
    if (parser.isSet(QStringLiteral("version"))) {
        parser.showVersion();
    }

    const QStringList positionalArgs = parser.positionalArguments();
    if (positionalArgs.isEmpty()) {
        std::cerr << "Error: No command specified. Available commands: embed\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }

    const QString &command = positionalArgs.at(0);
    if (command != QStringLiteral("embed")) {
        std::cerr << "Error: Unknown command '" << qPrintable(command)
                  << "'. Available commands: embed\n\n";
        exitCode = 2;
        return std::nullopt;
    }

    if (!parser.isSet(opts.modelOption)) {
        std::cerr << "Error: --model <path> is required for 'embed'.\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }

    int threads = 4;
    if (parser.isSet(opts.threadsOption)) {
        bool ok = false;
        threads = parser.value(opts.threadsOption).toInt(&ok);
        if (!ok || threads <= 0) {
            std::cerr << "Error: Invalid --threads value: "
                      << qPrintable(parser.value(opts.threadsOption)) << " (must be > 0)\n";
            exitCode = 2;
            return std::nullopt;
        }
    }

    const QStringList files = positionalArgs.mid(1);
    if (files.isEmpty()) {
        std::cerr << "Error: No audio files specified for 'embed'.\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }

    std::optional<QString> refPath;
    if (parser.isSet(opts.refOption)) {
        refPath = parser.value(opts.refOption);
    }

    return CliConfig {
        .command = command,
        .modelPath = parser.value(opts.modelOption),
        .preferGpu = !parser.isSet(opts.cpuOption),
        .threads = threads,
        .refPath = refPath,
        .files = files,
        .verbose = parser.isSet(opts.verboseOption),
    };
}

core::Result<QHash<QString, QList<float>>> loadReferenceJson(const QString &refPath)
{
    QFile file(refPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return core::Error { .code = QStringLiteral("audio.ref_open_failed"),
            .message = QStringLiteral("Failed to open reference JSON file"),
            .detail = refPath };
    }

    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error { .code = QStringLiteral("audio.ref_parse_failed"),
            .message = parseError.errorString(),
            .detail = refPath };
    }

    QHash<QString, QList<float>> refMap;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!it.value().isArray()) {
            continue;
        }
        const QJsonArray arr = it.value().toArray();
        QList<float> vec;
        vec.reserve(arr.size());
        for (const auto &val : arr) {
            vec.append(static_cast<float>(val.toDouble()));
        }
        refMap.insert(it.key(), vec);
    }

    return refMap;
}

double computeCosineSimilarity(const QList<float> &a, const QList<float> &b)
{
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    const qsizetype dim = std::min(a.size(), b.size());
    for (qsizetype i = 0; i < dim; ++i) {
        const double valA = a.at(i);
        const double valB = b.at(i);
        dot += valA * valB;
        normA += valA * valA;
        normB += valB * valB;
    }
    const double normProd = std::sqrt(normA) * std::sqrt(normB);
    return (normProd > 1e-12) ? (dot / normProd) : 0.0;
}

QString formatFirst4(const QList<float> &embedding)
{
    QString first4Str = QStringLiteral("[");
    const qsizetype previewCount = std::min<qsizetype>(4, embedding.size());
    for (qsizetype i = 0; i < previewCount; ++i) {
        if (i > 0) {
            first4Str += QStringLiteral(", ");
        }
        first4Str += QString::asprintf("%.6f", embedding.at(i));
    }
    first4Str += QStringLiteral("]");
    return first4Str;
}

struct EmbedStats {
    qint64 totalInferTimeMs = 0;
    int successInferCount = 0;
    bool anyError = false;
};

void processSingleFile(const QString &filePath, linernotes::audio::AudioEmbedder &embedder,
    const QHash<QString, QList<float>> &refMap, bool hasRef, EmbedStats &stats)
{
    // 1. Decode audio (44.1 kHz, mono)
    QElapsedTimer decodeTimer;
    decodeTimer.start();

    linernotes::audio::DecodeOptions decodeOpts;
    decodeOpts.sampleRate = linernotes::audio::AudioEmbedder::kSampleRate;
    decodeOpts.channels = 1;
    decodeOpts.maxDurationMs = 0;

    const auto decodeRes = linernotes::audio::AudioDecoder::decode(filePath, decodeOpts);
    if (!decodeRes.ok()) {
        std::cerr << "Decode error for " << qPrintable(filePath) << ": "
                  << qPrintable(decodeRes.error().toString()) << "\n";
        stats.anyError = true;
        return;
    }
    const qint64 decodeMs = decodeTimer.elapsed();

    // 2. Convert qint16 to float [-1.0, 1.0]
    const auto &samples = decodeRes.value().samples;
    std::vector<float> floatPcm;
    floatPcm.reserve(samples.size());
    for (const qint16 s : samples) {
        floatPcm.push_back(static_cast<float>(s) / 32768.0F);
    }

    // 3. Extract 7-second chunk centered at 50%
    constexpr int kWindowSamples = linernotes::audio::AudioEmbedder::kWindowSamples;
    const std::span<const float> fullPcm(floatPcm);
    std::span<const float> slice;
    if (fullPcm.size() <= static_cast<size_t>(kWindowSamples)) {
        slice = fullPcm;
    } else {
        const size_t start = (fullPcm.size() - static_cast<size_t>(kWindowSamples)) / 2;
        slice = fullPcm.subspan(start, static_cast<size_t>(kWindowSamples));
    }

    // 4. Run inference
    QElapsedTimer inferTimer;
    inferTimer.start();

    const auto embedRes = embedder.embed(slice);
    if (!embedRes.ok()) {
        std::cerr << "Embed error for " << qPrintable(filePath) << ": "
                  << qPrintable(embedRes.error().toString()) << "\n";
        stats.anyError = true;
        return;
    }
    const qint64 inferMs = inferTimer.elapsed();
    stats.totalInferTimeMs += inferMs;
    stats.successInferCount++;

    const QList<float> &embedding = embedRes.value();
    const QString first4Str = formatFirst4(embedding);
    const char *deviceStr = embedder.usingGpu() ? "gpu" : "cpu";

    // Output line: <路径>\t<gpu|cpu>\t<解码 ms>\t<推理 ms>\t<向量前 4 个数>
    QString line = QStringLiteral("%1\t%2\t%3\t%4\t%5")
                       .arg(filePath)
                       .arg(QLatin1StringView(deviceStr))
                       .arg(decodeMs)
                       .arg(inferMs)
                       .arg(first4Str);

    if (hasRef) {
        const QString absPath = QFileInfo(filePath).absoluteFilePath();
        auto it = refMap.constFind(absPath);
        if (it == refMap.constEnd()) {
            it = refMap.constFind(filePath);
        }
        if (it != refMap.constEnd()) {
            const double cosSim = computeCosineSimilarity(embedding, it.value());
            line += QString::asprintf("\t%.6f", cosSim);
        } else {
            line += QStringLiteral("\tN/A");
        }
    }

    std::cout << qPrintable(line) << "\n";
}

void printSummary(qint64 modelLoadTimeMs, const EmbedStats &stats)
{
    const double avgInferMs = (stats.successInferCount > 0)
        ? static_cast<double>(stats.totalInferTimeMs) / stats.successInferCount
        : 0.0;
    std::cout << qPrintable(QStringLiteral("Model load: %1 ms, avg inference: %2 ms")
            .arg(modelLoadTimeMs)
            .arg(QString::asprintf("%.2f", avgInferMs)))
              << "\n";
}

int executeEmbed(const CliConfig &cfg)
{
    QHash<QString, QList<float>> refMap;
    const bool hasRef = cfg.refPath.has_value();
    if (hasRef) {
        const auto refRes = loadReferenceJson(cfg.refPath.value());
        if (!refRes.ok()) {
            std::cerr << "Error loading reference JSON: " << qPrintable(refRes.error().toString())
                      << "\n";
            return 2;
        }
        refMap = refRes.value();
    }

    // Load model
    QElapsedTimer modelTimer;
    modelTimer.start();

    linernotes::audio::EmbedderOptions embedderOptions;
    embedderOptions.intraOpThreads = cfg.threads;
    embedderOptions.preferGpu = cfg.preferGpu;

    auto embedderRes = linernotes::audio::AudioEmbedder::create(cfg.modelPath, embedderOptions);
    if (!embedderRes.ok()) {
        std::cerr << "Error loading model: " << qPrintable(embedderRes.error().toString()) << "\n";
        return 1;
    }
    const qint64 modelLoadTimeMs = modelTimer.elapsed();
    const auto embedder = std::move(embedderRes.value());

    EmbedStats stats;
    for (const QString &filePath : cfg.files) {
        processSingleFile(filePath, *embedder, refMap, hasRef, stats);
    }

    printSummary(modelLoadTimeMs, stats);

    if (stats.anyError || stats.successInferCount == 0) {
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    try {
        const QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("linernotes-audio"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

        const CliOptions opts;
        QCommandLineParser parser;
        setupParser(parser, opts);

        int exitCode = 0;
        const auto cfg = parseArgs(parser, opts, exitCode);
        if (!cfg.has_value()) {
            return exitCode;
        }

        if (!cfg->verbose) {
            QLoggingCategory::setFilterRules(QStringLiteral("linernotes.*.debug=false"));
        }

        return executeEmbed(*cfg);
    } catch (const std::exception &e) {
        std::cerr << "Fatal exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown exception\n";
        return 1;
    }
}
