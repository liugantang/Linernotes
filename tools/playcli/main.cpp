// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayCli.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

#include <iostream>
#include <optional>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("linernotes-playcli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Linernotes command line music player"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption modeOption(QStringLiteral("mode"),
        QStringLiteral(
            "Playback mode: sequential, repeat-all, repeat-one, shuffle (default: sequential)."),
        QStringLiteral("mode"), QStringLiteral("sequential"));
    parser.addOption(modeOption);

    QCommandLineOption volumeOption(QStringLiteral("volume"),
        QStringLiteral("Playback volume (0-100)."), QStringLiteral("volume"));
    parser.addOption(volumeOption);

    QCommandLineOption aoOption(QStringLiteral("ao"),
        QStringLiteral("Audio output driver passed to mpv (e.g. null)."), QStringLiteral("name"));
    parser.addOption(aoOption);

    QCommandLineOption stateFileOption(QStringLiteral("state-file"),
        QStringLiteral("Path to state file to restore from / save to."), QStringLiteral("path"));
    parser.addOption(stateFileOption);

    QCommandLineOption reportMemoryOption(QStringLiteral("report-memory"),
        QStringLiteral("Interval in seconds to report memory usage to stdout."),
        QStringLiteral("seconds"));
    parser.addOption(reportMemoryOption);

    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files to play."),
        QStringLiteral("[files...]"));

    if (!parser.parse(app.arguments())) {
        std::cerr << qPrintable(parser.errorText()) << "\n";
        return 2;
    }

    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp(0);
    }
    if (parser.isSet(QStringLiteral("version"))) {
        parser.showVersion();
    }

    const QStringList files = parser.positionalArguments();
    const bool hasStateFile = parser.isSet(stateFileOption);

    if (files.isEmpty() && !hasStateFile) {
        std::cerr << qPrintable(parser.helpText());
        return 2;
    }

    linernotes::player::PlayMode mode = linernotes::player::PlayMode::Sequential;
    std::optional<linernotes::player::PlayMode> explicitMode;
    if (parser.isSet(modeOption)) {
        const QString modeStr = parser.value(modeOption).toLower();
        if (modeStr == QStringLiteral("sequential")) {
            mode = linernotes::player::PlayMode::Sequential;
        } else if (modeStr == QStringLiteral("repeat-all")) {
            mode = linernotes::player::PlayMode::RepeatAll;
        } else if (modeStr == QStringLiteral("repeat-one")) {
            mode = linernotes::player::PlayMode::RepeatOne;
        } else if (modeStr == QStringLiteral("shuffle")) {
            mode = linernotes::player::PlayMode::Shuffle;
        } else {
            std::cerr << "Invalid --mode specified: " << qPrintable(modeStr) << "\n";
            return 2;
        }
        explicitMode = mode;
    }

    std::optional<int> volume;
    if (parser.isSet(volumeOption)) {
        bool ok = false;
        const int vol = parser.value(volumeOption).toInt(&ok);
        if (!ok || vol < 0 || vol > 100) {
            std::cerr << "Invalid --volume specified: " << qPrintable(parser.value(volumeOption))
                      << " (must be 0-100)\n";
            return 2;
        }
        volume = vol;
    }

    int reportMemorySec = 0;
    if (parser.isSet(reportMemoryOption)) {
        bool ok = false;
        reportMemorySec = parser.value(reportMemoryOption).toInt(&ok);
        if (!ok || reportMemorySec <= 0) {
            std::cerr << "Invalid --report-memory interval: "
                      << qPrintable(parser.value(reportMemoryOption)) << "\n";
            return 2;
        }
    }

    const QString ao = parser.value(aoOption);
    linernotes::playcli::PlayCli cli(ao);
    if (!cli.init()) {
        return 1;
    }

    if (hasStateFile) {
        cli.setStateFile(parser.value(stateFileOption));
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

    return app.exec();
}
