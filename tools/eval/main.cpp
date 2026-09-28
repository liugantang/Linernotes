// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistMergeEval.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QTimer>

#include <iostream>
#include <optional>

#ifndef DEFAULT_ARTIST_MERGE_CORPUS_PATH
#define DEFAULT_ARTIST_MERGE_CORPUS_PATH ""
#endif

namespace {

struct CliOptions {
    QCommandLineOption baseUrlOption { QStringLiteral("base-url"),
        QStringLiteral("Base URL of the LLM service (required)."), QStringLiteral("url") };
    QCommandLineOption modelOption { QStringLiteral("model"),
        QStringLiteral("Model name to use (required)."), QStringLiteral("name") };
    QCommandLineOption corpusOption { QStringLiteral("corpus"),
        QStringLiteral("Path to corpus JSON file (default: embedded/source default path)."),
        QStringLiteral("path"), QStringLiteral(DEFAULT_ARTIST_MERGE_CORPUS_PATH) };
    QCommandLineOption mbOption { QStringLiteral("mb"),
        QStringLiteral("Enable MusicBrainz lookup (default: false).") };
    QCommandLineOption keepDbOption { QStringLiteral("keep-db"),
        QStringLiteral(
            "Path to persist SQLite database file for inspection (default: temporary db)."),
        QStringLiteral("path") };
    QCommandLineOption timeoutMsOption { QStringLiteral("timeout-ms"),
        QStringLiteral("Service timeout in milliseconds (default: 60000)."), QStringLiteral("n"),
        QStringLiteral("60000") };
    QCommandLineOption rpmOption { QStringLiteral("rpm"),
        QStringLiteral("Service requests per minute limit (default: 0)."), QStringLiteral("n"),
        QStringLiteral("0") };
    QCommandLineOption verboseOption { QStringLiteral("verbose"),
        QStringLiteral("Enable verbose debug logging.") };
};

void setupParser(QCommandLineParser &parser, const CliOptions &opts)
{
    parser.setApplicationDescription(QStringLiteral("Linernotes evaluation tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("subcommand"),
        QStringLiteral("Evaluation subcommand to run (e.g. artist-merge)."),
        QStringLiteral("<artist-merge>"));
    parser.addOption(opts.baseUrlOption);
    parser.addOption(opts.modelOption);
    parser.addOption(opts.corpusOption);
    parser.addOption(opts.mbOption);
    parser.addOption(opts.keepDbOption);
    parser.addOption(opts.timeoutMsOption);
    parser.addOption(opts.rpmOption);
    parser.addOption(opts.verboseOption);
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

std::optional<linernotes::eval::EvalConfig> parseAndValidateArgs(
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
    if (positionalArgs.isEmpty() || positionalArgs.at(0) != QStringLiteral("artist-merge")) {
        std::cerr
            << "Error: Unknown or missing subcommand. Available subcommands: artist-merge\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
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

    const auto timeoutVal = parsePositiveInt(parser.value(opts.timeoutMsOption), "--timeout-ms");
    if (!timeoutVal.has_value()) {
        exitCode = 2;
        return std::nullopt;
    }

    bool ok = false;
    const int rpm = parser.value(opts.rpmOption).toInt(&ok);
    if (!ok || rpm < 0) {
        std::cerr << "Invalid --rpm specified: " << qPrintable(parser.value(opts.rpmOption))
                  << " (must be >= 0)\n";
        exitCode = 2;
        return std::nullopt;
    }

    const QString corpusPath = parser.value(opts.corpusOption);
    if (corpusPath.isEmpty()) {
        std::cerr << "Error: --corpus path is not specified and default path is not set.\n";
        exitCode = 2;
        return std::nullopt;
    }

    QString keepDbPath;
    if (parser.isSet(opts.keepDbOption)) {
        keepDbPath = parser.value(opts.keepDbOption);
    }

    return linernotes::eval::EvalConfig {
        .baseUrl = baseUrl,
        .model = model,
        .corpusPath = corpusPath,
        .keepDbPath = keepDbPath,
        .useMusicBrainz = parser.isSet(opts.mbOption),
        .timeoutMs = *timeoutVal,
        .requestsPerMinute = rpm,
        .verbose = parser.isSet(opts.verboseOption),
    };
}

} // namespace

int main(int argc, char *argv[])
{
    try {
        const QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("linernotes-eval"));
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

        linernotes::eval::ArtistMergeEval eval(*configOpt);
        if (!eval.init()) {
            return 2;
        }

        QTimer::singleShot(0, &eval, &linernotes::eval::ArtistMergeEval::start);

        return QCoreApplication::exec();
    } catch (const std::exception &e) {
        std::cerr << "Fatal exception: " << e.what() << "\n";
        return 2;
    }
}
