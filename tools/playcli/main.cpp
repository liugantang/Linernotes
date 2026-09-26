// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayCli.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>

#include <iostream>
#include <optional>

namespace {

struct CliOptions {
    QCommandLineOption modeOption { QStringLiteral("mode"),
        QStringLiteral(
            "Playback mode: sequential, repeat-all, repeat-one, shuffle (default: sequential)."),
        QStringLiteral("mode"), QStringLiteral("sequential") };
    QCommandLineOption volumeOption { QStringLiteral("volume"),
        QStringLiteral("Playback volume (0-100)."), QStringLiteral("volume") };
    QCommandLineOption aoOption { QStringLiteral("ao"),
        QStringLiteral("Audio output driver passed to mpv (e.g. null)."), QStringLiteral("name") };
    QCommandLineOption stateFileOption { QStringLiteral("state-file"),
        QStringLiteral("Path to state file to restore from / save to."), QStringLiteral("path") };
    QCommandLineOption reportMemoryOption { QStringLiteral("report-memory"),
        QStringLiteral("Interval in seconds to report memory usage to stdout."),
        QStringLiteral("seconds") };
    QCommandLineOption verboseOption { QStringLiteral("verbose"),
        QStringLiteral("Show debug logs of the player.") };
};

void setupCommandLineParser(QCommandLineParser &parser, const CliOptions &opts)
{
    parser.setApplicationDescription(QStringLiteral("Linernotes command line music player"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(opts.modeOption);
    parser.addOption(opts.volumeOption);
    parser.addOption(opts.aoOption);
    parser.addOption(opts.stateFileOption);
    parser.addOption(opts.reportMemoryOption);
    parser.addOption(opts.verboseOption);
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files to play."),
        QStringLiteral("[files...]"));
}

std::optional<linernotes::player::PlayMode> parsePlayMode(
    const QCommandLineParser &parser, const QCommandLineOption &modeOption, bool &error)
{
    if (!parser.isSet(modeOption)) {
        return std::nullopt;
    }
    const QString modeStr = parser.value(modeOption).toLower();
    if (modeStr == QStringLiteral("sequential")) {
        return linernotes::player::PlayMode::Sequential;
    }
    if (modeStr == QStringLiteral("repeat-all")) {
        return linernotes::player::PlayMode::RepeatAll;
    }
    if (modeStr == QStringLiteral("repeat-one")) {
        return linernotes::player::PlayMode::RepeatOne;
    }
    if (modeStr == QStringLiteral("shuffle")) {
        return linernotes::player::PlayMode::Shuffle;
    }
    std::cerr << "Invalid --mode specified: " << qPrintable(modeStr) << "\n";
    error = true;
    return std::nullopt;
}

std::optional<int> parseVolume(
    const QCommandLineParser &parser, const QCommandLineOption &volumeOption, bool &error)
{
    if (!parser.isSet(volumeOption)) {
        return std::nullopt;
    }
    bool ok = false;
    const int vol = parser.value(volumeOption).toInt(&ok);
    if (!ok || vol < 0 || vol > 100) {
        std::cerr << "Invalid --volume specified: " << qPrintable(parser.value(volumeOption))
                  << " (must be 0-100)\n";
        error = true;
        return std::nullopt;
    }
    return vol;
}

int parseReportMemorySec(
    const QCommandLineParser &parser, const QCommandLineOption &reportMemoryOption, bool &error)
{
    if (!parser.isSet(reportMemoryOption)) {
        return 0;
    }
    bool ok = false;
    const int reportMemorySec = parser.value(reportMemoryOption).toInt(&ok);
    if (!ok || reportMemorySec <= 0) {
        std::cerr << "Invalid --report-memory interval: "
                  << qPrintable(parser.value(reportMemoryOption)) << "\n";
        error = true;
        return 0;
    }
    return reportMemorySec;
}

} // namespace

int main(int argc, char *argv[])
{
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("linernotes-playcli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const CliOptions opts;
    QCommandLineParser parser;
    setupCommandLineParser(parser, opts);

    if (!parser.parse(QCoreApplication::arguments())) {
        std::cerr << qPrintable(parser.errorText()) << "\n";
        return 2;
    }

    // 调试日志默认会刷屏，淹没事件输出和命令回显
    if (!parser.isSet(opts.verboseOption)) {
        QLoggingCategory::setFilterRules(QStringLiteral("linernotes.*.debug=false"));
    }

    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp(0);
    }
    if (parser.isSet(QStringLiteral("version"))) {
        parser.showVersion();
    }

    const QStringList files = parser.positionalArguments();
    const bool hasStateFile = parser.isSet(opts.stateFileOption);

    if (files.isEmpty() && !hasStateFile) {
        std::cerr << qPrintable(parser.helpText());
        return 2;
    }

    bool parseError = false;
    const auto explicitMode = parsePlayMode(parser, opts.modeOption, parseError);
    if (parseError) {
        return 2;
    }
    const linernotes::player::PlayMode mode
        = explicitMode.value_or(linernotes::player::PlayMode::Sequential);

    const auto volume = parseVolume(parser, opts.volumeOption, parseError);
    if (parseError) {
        return 2;
    }

    const int reportMemorySec = parseReportMemorySec(parser, opts.reportMemoryOption, parseError);
    if (parseError) {
        return 2;
    }

    const QString ao = parser.value(opts.aoOption);
    linernotes::playcli::PlayCli cli(ao);
    if (!cli.init()) {
        return 1;
    }

    if (hasStateFile) {
        cli.setStateFile(parser.value(opts.stateFileOption));
    }

    if (reportMemorySec > 0) {
        cli.setReportMemoryInterval(reportMemorySec);
    }

    if (!files.isEmpty()) {
        if (!cli.playFiles(files, mode, volume)) {
            return 1;
        }
    } else {
        if (!cli.restoreState(explicitMode, volume)) {
            return 2;
        }
    }

    return QCoreApplication::exec();
}
