// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistCreditEval.h"
#include "ArtistMergeEval.h"
#include "EvalHarness.h"
#include "NlqAskEval.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QTimer>

#include <iostream>
#include <optional>

#ifndef DEFAULT_ARTIST_MERGE_CORPUS_PATH
#define DEFAULT_ARTIST_MERGE_CORPUS_PATH ""
#endif

#ifndef DEFAULT_ARTIST_CREDIT_CORPUS_PATH
#define DEFAULT_ARTIST_CREDIT_CORPUS_PATH ""
#endif

namespace {

struct CliOptions {
    QCommandLineOption baseUrlOption { QStringLiteral("base-url"),
        QStringLiteral("Base URL of the LLM service (required)."), QStringLiteral("url") };
    QCommandLineOption modelOption { QStringLiteral("model"),
        QStringLiteral("Model name to use (required)."), QStringLiteral("name") };
    QCommandLineOption corpusOption { QStringLiteral("corpus"),
        QStringLiteral("Path to corpus JSON file (default: default corpus for subcommand)."),
        QStringLiteral("path") };
    QCommandLineOption libraryOption { QStringLiteral("library"),
        QStringLiteral(
            "Path to a library.db file for real library evaluation (artist-credit or nlq-ask)."),
        QStringLiteral("path") };
    QCommandLineOption questionOption { QStringLiteral("question"),
        QStringLiteral("Natural language question to ask (nlq-ask only)."),
        QStringLiteral("question") };
    QCommandLineOption previousOption { QStringLiteral("previous"),
        QStringLiteral("Previous query JSON string for multi-turn inquiry (nlq-ask only)."),
        QStringLiteral("previous") };
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

struct ParsedArgs {
    QString subcommand { };
    linernotes::eval::EvalConfig config;
};

void setupParser(QCommandLineParser &parser, const CliOptions &opts)
{
    parser.setApplicationDescription(QStringLiteral("Linernotes evaluation tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("subcommand"),
        QStringLiteral("Evaluation subcommand to run (artist-merge, artist-credit, nlq-ask)."),
        QStringLiteral("<subcommand>"));
    parser.addOption(opts.baseUrlOption);
    parser.addOption(opts.modelOption);
    parser.addOption(opts.corpusOption);
    parser.addOption(opts.libraryOption);
    parser.addOption(opts.questionOption);
    parser.addOption(opts.previousOption);
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

std::optional<QString> extractSubcommand(const QCommandLineParser &parser, int &exitCode)
{
    const QStringList positionalArgs = parser.positionalArguments();
    if (positionalArgs.isEmpty()) {
        std::cerr << "Error: Missing subcommand. Available subcommands: artist-merge, "
                     "artist-credit, nlq-ask\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }

    const QString &subcommand = positionalArgs.at(0);
    if (subcommand != QStringLiteral("artist-merge")
        && subcommand != QStringLiteral("artist-credit")
        && subcommand != QStringLiteral("nlq-ask")) {
        std::cerr << "Error: Unknown subcommand: " << qPrintable(subcommand)
                  << ". Available subcommands: artist-merge, artist-credit, nlq-ask\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }
    return subcommand;
}

std::optional<QUrl> parseBaseUrl(
    const QCommandLineParser &parser, const QCommandLineOption &opt, int &exitCode)
{
    const QString baseUrlStr = parser.value(opt).trimmed();
    QUrl baseUrl(baseUrlStr);
    const QString scheme = baseUrl.scheme().toLower();
    if (!baseUrl.isValid()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        std::cerr << "Invalid --base-url specified: " << qPrintable(baseUrlStr)
                  << " (must be http:// or https://)\n";
        exitCode = 2;
        return std::nullopt;
    }
    return baseUrl;
}

std::optional<QString> parseModel(
    const QCommandLineParser &parser, const QCommandLineOption &opt, int &exitCode)
{
    QString model = parser.value(opt).trimmed();
    if (model.isEmpty()) {
        std::cerr << "Error: --model cannot be empty.\n";
        exitCode = 2;
        return std::nullopt;
    }
    return model;
}

std::optional<int> parseRpm(
    const QCommandLineParser &parser, const QCommandLineOption &opt, int &exitCode)
{
    bool ok = false;
    const int rpm = parser.value(opt).toInt(&ok);
    if (!ok || rpm < 0) {
        std::cerr << "Invalid --rpm specified: " << qPrintable(parser.value(opt))
                  << " (must be >= 0)\n";
        exitCode = 2;
        return std::nullopt;
    }
    return rpm;
}

bool resolveArtistMergeCorpus(
    const QCommandLineParser &parser, const CliOptions &opts, QString &corpusPath, int &exitCode)
{
    if (parser.isSet(opts.libraryOption)) {
        std::cerr << "Error: --library option is not supported for artist-merge subcommand.\n";
        exitCode = 2;
        return false;
    }
    corpusPath = parser.isSet(opts.corpusOption) ? parser.value(opts.corpusOption)
                                                 : QStringLiteral(DEFAULT_ARTIST_MERGE_CORPUS_PATH);
    if (corpusPath.isEmpty()) {
        std::cerr << "Error: --corpus path is not specified and default path is not set.\n";
        exitCode = 2;
        return false;
    }
    return true;
}

bool resolveArtistCreditPaths(const QCommandLineParser &parser, const CliOptions &opts,
    QString &corpusPath, QString &libraryPath, int &exitCode)
{
    if (parser.isSet(opts.corpusOption) && parser.isSet(opts.libraryOption)) {
        std::cerr << "Error: --corpus and --library options are mutually exclusive.\n";
        exitCode = 2;
        return false;
    }
    if (parser.isSet(opts.libraryOption)) {
        libraryPath = parser.value(opts.libraryOption);
        if (!QFile::exists(libraryPath)) {
            std::cerr << "Error: Specified library database file does not exist: "
                      << qPrintable(libraryPath) << "\n";
            exitCode = 2;
            return false;
        }
        return true;
    }

    corpusPath = parser.isSet(opts.corpusOption)
        ? parser.value(opts.corpusOption)
        : QStringLiteral(DEFAULT_ARTIST_CREDIT_CORPUS_PATH);
    if (corpusPath.isEmpty()) {
        std::cerr << "Error: --corpus path is not specified and default path is not set.\n";
        exitCode = 2;
        return false;
    }
    return true;
}

bool resolveNlqAskPaths(const QCommandLineParser &parser, const CliOptions &opts,
    QString &libraryPath, QString &question, QString &previous, int &exitCode)
{
    if (parser.isSet(opts.corpusOption)) {
        std::cerr << "Error: --corpus option is not supported for nlq-ask subcommand.\n";
        exitCode = 2;
        return false;
    }
    if (!parser.isSet(opts.libraryOption)) {
        std::cerr << "Error: --library option is required for nlq-ask subcommand.\n";
        exitCode = 2;
        return false;
    }
    libraryPath = parser.value(opts.libraryOption);
    if (!QFile::exists(libraryPath)) {
        std::cerr << "Error: Specified library database file does not exist: "
                  << qPrintable(libraryPath) << "\n";
        exitCode = 2;
        return false;
    }
    if (!parser.isSet(opts.questionOption)
        || parser.value(opts.questionOption).trimmed().isEmpty()) {
        std::cerr << "Error: --question option is required for nlq-ask subcommand.\n";
        exitCode = 2;
        return false;
    }
    question = parser.value(opts.questionOption).trimmed();
    if (parser.isSet(opts.previousOption)) {
        previous = parser.value(opts.previousOption).trimmed();
    }
    return true;
}

bool resolveSubcommandPaths(const QString &subcommand, const QCommandLineParser &parser,
    const CliOptions &opts, QString &corpusPath, QString &libraryPath, QString &question,
    QString &previous, int &exitCode)
{
    if (subcommand == QStringLiteral("artist-merge")) {
        if (parser.isSet(opts.questionOption) || parser.isSet(opts.previousOption)) {
            std::cerr
                << "Error: --question and --previous options are only supported for nlq-ask.\n";
            exitCode = 2;
            return false;
        }
        return resolveArtistMergeCorpus(parser, opts, corpusPath, exitCode);
    }
    if (subcommand == QStringLiteral("artist-credit")) {
        if (parser.isSet(opts.questionOption) || parser.isSet(opts.previousOption)) {
            std::cerr
                << "Error: --question and --previous options are only supported for nlq-ask.\n";
            exitCode = 2;
            return false;
        }
        return resolveArtistCreditPaths(parser, opts, corpusPath, libraryPath, exitCode);
    }
    if (subcommand == QStringLiteral("nlq-ask")) {
        return resolveNlqAskPaths(parser, opts, libraryPath, question, previous, exitCode);
    }
    return false;
}

std::optional<ParsedArgs> parseAndValidateArgs(
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

    const auto subcommandOpt = extractSubcommand(parser, exitCode);
    if (!subcommandOpt.has_value()) {
        return std::nullopt;
    }
    const QString &subcommand = *subcommandOpt;

    if (!parser.isSet(opts.baseUrlOption) || !parser.isSet(opts.modelOption)) {
        std::cerr << "Error: --base-url and --model are required options.\n\n";
        std::cerr << qPrintable(parser.helpText());
        exitCode = 2;
        return std::nullopt;
    }

    const auto baseUrlOpt = parseBaseUrl(parser, opts.baseUrlOption, exitCode);
    if (!baseUrlOpt.has_value()) {
        return std::nullopt;
    }

    const auto modelOpt = parseModel(parser, opts.modelOption, exitCode);
    if (!modelOpt.has_value()) {
        return std::nullopt;
    }

    const auto timeoutVal = parsePositiveInt(parser.value(opts.timeoutMsOption), "--timeout-ms");
    if (!timeoutVal.has_value()) {
        exitCode = 2;
        return std::nullopt;
    }

    const auto rpmOpt = parseRpm(parser, opts.rpmOption, exitCode);
    if (!rpmOpt.has_value()) {
        return std::nullopt;
    }

    QString corpusPath;
    QString libraryPath;
    QString question;
    QString previous;
    if (!resolveSubcommandPaths(
            subcommand, parser, opts, corpusPath, libraryPath, question, previous, exitCode)) {
        return std::nullopt;
    }

    QString keepDbPath;
    if (parser.isSet(opts.keepDbOption)) {
        keepDbPath = parser.value(opts.keepDbOption);
    }

    return ParsedArgs {
        .subcommand = subcommand,
        .config = linernotes::eval::EvalConfig {
            .baseUrl = *baseUrlOpt,
            .model = *modelOpt,
            .corpusPath = corpusPath,
            .libraryPath = libraryPath,
            .keepDbPath = keepDbPath,
            .question = question,
            .previous = previous,
            .timeoutMs = *timeoutVal,
            .requestsPerMinute = *rpmOpt,
            .verbose = parser.isSet(opts.verboseOption),
        },
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
        const auto parsedArgsOpt = parseAndValidateArgs(parser, opts, exitCode);
        if (!parsedArgsOpt.has_value()) {
            return exitCode;
        }

        const auto &parsedArgs = *parsedArgsOpt;
        if (!parsedArgs.config.verbose) {
            QLoggingCategory::setFilterRules(QStringLiteral("linernotes.*.debug=false"));
        }

        if (parsedArgs.subcommand == QStringLiteral("artist-merge")) {
            linernotes::eval::ArtistMergeEval eval(parsedArgs.config);
            if (!eval.init()) {
                return 2;
            }
            QTimer::singleShot(0, &eval, &linernotes::eval::ArtistMergeEval::start);
            return QCoreApplication::exec();
        }

        if (parsedArgs.subcommand == QStringLiteral("artist-credit")) {
            linernotes::eval::ArtistCreditEval eval(parsedArgs.config);
            if (!eval.init()) {
                return 2;
            }
            QTimer::singleShot(0, &eval, &linernotes::eval::ArtistCreditEval::start);
            return QCoreApplication::exec();
        }

        if (parsedArgs.subcommand == QStringLiteral("nlq-ask")) {
            linernotes::eval::NlqAskEval eval(parsedArgs.config);
            if (!eval.init()) {
                return 2;
            }
            QTimer::singleShot(0, &eval, &linernotes::eval::NlqAskEval::start);
            return QCoreApplication::exec();
        }

        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Fatal exception: " << e.what() << "\n";
        return 2;
    }
}
