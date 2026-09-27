// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AiCli.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QTimer>

#include <iostream>
#include <optional>

namespace {

struct CliOptions {
    QCommandLineOption baseUrlOption { QStringLiteral("base-url"),
        QStringLiteral("Base URL of the LLM service (required)."), QStringLiteral("url") };
    QCommandLineOption modelOption { QStringLiteral("model"),
        QStringLiteral("Model name to use (required)."), QStringLiteral("name") };
    QCommandLineOption dbOption { QStringLiteral("db"),
        QStringLiteral("Path to SQLite database file (default: temporary db, removed on exit)."),
        QStringLiteral("path") };
    QCommandLineOption promptOption { QStringLiteral("prompt"),
        QStringLiteral("User message prompt (default: \"Reply with exactly one word: hello\")."),
        QStringLiteral("text"), QStringLiteral("Reply with exactly one word: hello") };
    QCommandLineOption systemOption { QStringLiteral("system"),
        QStringLiteral("Optional system message."), QStringLiteral("text") };
    QCommandLineOption streamOption { QStringLiteral("stream"),
        QStringLiteral("Stream response deltas in real-time to stdout.") };
    QCommandLineOption structuredOption { QStringLiteral("structured"),
        QStringLiteral("Perform structured output call with test schema.") };
    QCommandLineOption repeatOption { QStringLiteral("repeat"),
        QStringLiteral("Repeat the call sequentially N times (default: 1)."), QStringLiteral("n") };
    QCommandLineOption cacheOption { QStringLiteral("cache"),
        QStringLiteral("Cache policy: use, refresh, bypass (default: use)."),
        QStringLiteral("policy"), QStringLiteral("use") };
    QCommandLineOption timeoutMsOption { QStringLiteral("timeout-ms"),
        QStringLiteral("ServiceProfile timeout in milliseconds (default: 60000)."),
        QStringLiteral("n"), QStringLiteral("60000") };
    QCommandLineOption rpmOption { QStringLiteral("rpm"),
        QStringLiteral("ServiceProfile requests per minute (default: 0)."), QStringLiteral("n"),
        QStringLiteral("0") };
    QCommandLineOption concurrentOption { QStringLiteral("concurrent"),
        QStringLiteral("Launch N calls concurrently (mutually exclusive with --repeat)."),
        QStringLiteral("n") };
    QCommandLineOption verboseOption { QStringLiteral("verbose"),
        QStringLiteral("Enable verbose debug logging.") };
};

void setupParser(QCommandLineParser &parser, const CliOptions &opts)
{
    parser.setApplicationDescription(QStringLiteral("Linernotes LLM CLI verification tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(opts.baseUrlOption);
    parser.addOption(opts.modelOption);
    parser.addOption(opts.dbOption);
    parser.addOption(opts.promptOption);
    parser.addOption(opts.systemOption);
    parser.addOption(opts.streamOption);
    parser.addOption(opts.structuredOption);
    parser.addOption(opts.repeatOption);
    parser.addOption(opts.cacheOption);
    parser.addOption(opts.timeoutMsOption);
    parser.addOption(opts.rpmOption);
    parser.addOption(opts.concurrentOption);
    parser.addOption(opts.verboseOption);
}

std::optional<linernotes::ai::CachePolicy> parseCachePolicy(const QString &str)
{
    const QString lower = str.toLower();
    if (lower == QStringLiteral("use")) {
        return linernotes::ai::CachePolicy::Use;
    }
    if (lower == QStringLiteral("refresh")) {
        return linernotes::ai::CachePolicy::Refresh;
    }
    if (lower == QStringLiteral("bypass")) {
        return linernotes::ai::CachePolicy::Bypass;
    }
    return std::nullopt;
}

std::optional<int> parsePositiveInt(const QString &str, const char *name)
{
    bool ok = false;
    const int val = str.toInt(&ok);
    if (!ok || val <= 0) {
        std::cerr << "Invalid " << name << " specified: " << qPrintable(str) << " (must be > 0)\n";
        return std::nullopt;
    }
    return val;
}

std::optional<linernotes::aicli::AiCliConfig> parseAndValidateArgs(
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

    if (!parser.isSet(opts.baseUrlOption) || !parser.isSet(opts.modelOption)) {
        std::cerr << "Error: --base-url and --model are required options.\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }

    const QString baseUrlStr = parser.value(opts.baseUrlOption).trimmed();
    const QUrl baseUrl(baseUrlStr);
    const QString scheme = baseUrl.scheme().toLower();
    if (!baseUrl.isValid()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        std::cerr << "Invalid --base-url specified: " << qPrintable(baseUrlStr)
                  << " (must be http:// or https://)\n";
        exitCode = 2;
        return std::nullopt;
    }

    const QString model = parser.value(opts.modelOption).trimmed();
    if (model.isEmpty()) {
        std::cerr << "Error: --model cannot be empty.\n";
        exitCode = 2;
        return std::nullopt;
    }

    const bool hasRepeat = parser.isSet(opts.repeatOption);
    const bool hasConcurrent = parser.isSet(opts.concurrentOption);
    if (hasRepeat && hasConcurrent) {
        std::cerr << "Error: --repeat and --concurrent are mutually exclusive.\n";
        exitCode = 2;
        return std::nullopt;
    }

    int repeatCount = 1;
    if (hasRepeat) {
        const auto val = parsePositiveInt(parser.value(opts.repeatOption), "--repeat");
        if (!val.has_value()) {
            exitCode = 2;
            return std::nullopt;
        }
        repeatCount = *val;
    }

    int concurrentCount = 0;
    if (hasConcurrent) {
        const auto val = parsePositiveInt(parser.value(opts.concurrentOption), "--concurrent");
        if (!val.has_value()) {
            exitCode = 2;
            return std::nullopt;
        }
        concurrentCount = *val;
    }

    const QString cacheStr = parser.value(opts.cacheOption);
    const auto cachePolicyOpt = parseCachePolicy(cacheStr);
    if (!cachePolicyOpt.has_value()) {
        std::cerr << "Invalid --cache specified: " << qPrintable(cacheStr)
                  << " (must be use, refresh, or bypass)\n";
        exitCode = 2;
        return std::nullopt;
    }

    const auto timeoutVal = parsePositiveInt(parser.value(opts.timeoutMsOption), "--timeout-ms");
    if (!timeoutVal.has_value()) {
        exitCode = 2;
        return std::nullopt;
    }
    const int timeoutMs = *timeoutVal;

    bool ok = false;
    const int rpm = parser.value(opts.rpmOption).toInt(&ok);
    if (!ok || rpm < 0) {
        std::cerr << "Invalid --rpm specified: " << qPrintable(parser.value(opts.rpmOption))
                  << " (must be >= 0)\n";
        exitCode = 2;
        return std::nullopt;
    }

    std::optional<QString> systemPrompt;
    if (parser.isSet(opts.systemOption)) {
        systemPrompt = parser.value(opts.systemOption);
    }

    QString dbPath;
    if (parser.isSet(opts.dbOption)) {
        dbPath = parser.value(opts.dbOption);
    }

    linernotes::aicli::AiCliConfig cfg;
    cfg.baseUrl = baseUrl;
    cfg.model = model;
    cfg.dbPath = dbPath;
    cfg.prompt = parser.value(opts.promptOption);
    cfg.systemPrompt = systemPrompt;
    cfg.stream = parser.isSet(opts.streamOption);
    cfg.structured = parser.isSet(opts.structuredOption);
    cfg.repeatCount = repeatCount;
    cfg.concurrentCount = concurrentCount;
    cfg.cachePolicy = *cachePolicyOpt;
    cfg.timeoutMs = timeoutMs;
    cfg.requestsPerMinute = rpm;
    cfg.verbose = parser.isSet(opts.verboseOption);

    return cfg;
}

} // namespace

int main(int argc, char *argv[])
{
    try {
        const QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("linernotes-aicli"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

        const CliOptions opts;
        QCommandLineParser parser;
        setupParser(parser, opts);

        int exitCode = 0;
        const auto configOpt = parseAndValidateArgs(parser, opts, exitCode);
        if (!configOpt.has_value()) {
            return exitCode;
        }

        if (!configOpt->verbose) {
            QLoggingCategory::setFilterRules(QStringLiteral("linernotes.*.debug=false"));
        }

        linernotes::aicli::AiCli cli(*configOpt);
        if (!cli.init()) {
            return 1;
        }

        QTimer::singleShot(0, &cli, &linernotes::aicli::AiCli::start);

        return QCoreApplication::exec();
    } catch (const std::exception &e) {
        std::cerr << "Fatal exception: " << e.what() << "\n";
        return 1;
    }
}
