// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

#include <core/CoreSettings.h>
#include <core/Logging.h>
#include <core/Paths.h>
#include <core/Settings.h>
#include <core/Version.h>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QString());
    QCoreApplication::setApplicationName(aimusic::core::applicationName());
    QCoreApplication::setApplicationVersion(aimusic::core::versionString());

    const QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AI music player"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption printPathsOption(
        QStringLiteral("print-paths"), QStringLiteral("Print application directories and exit."));
    parser.addOption(printPathsOption);

    parser.process(app);

    const auto paths = aimusic::core::Paths::fromEnvironment();

    if (parser.isSet(printPathsOption)) {
        QTextStream out(stdout);
        out << QStringLiteral("Config: ") << paths.configDir() << Qt::endl;
        out << QStringLiteral("Data:   ") << paths.dataDir() << Qt::endl;
        out << QStringLiteral("Cache:  ") << paths.cacheDir() << Qt::endl;
        out << QStringLiteral("Logs:   ") << paths.logDir() << Qt::endl;
        return 0;
    }

    paths.ensureCreated();

    const QString iniFilePath = QDir(paths.configDir()).filePath(QStringLiteral("settings.ini"));
    const aimusic::core::Settings settings(iniFilePath);

    const QString logLevelStr = settings.value(aimusic::core::kLogLevel).trimmed().toLower();
    QtMsgType minimumLevel = QtInfoMsg;
    if (logLevelStr == u"debug") {
        minimumLevel = QtDebugMsg;
    } else if (logLevelStr == u"warning" || logLevelStr == u"warn") {
        minimumLevel = QtWarningMsg;
    } else if (logLevelStr == u"critical") {
        minimumLevel = QtCriticalMsg;
    } else {
        minimumLevel = QtInfoMsg;
    }

    aimusic::core::LogConfig logConfig;
    logConfig.directory = paths.logDir();
    logConfig.minimumLevel = minimumLevel;
    aimusic::core::installLogging(logConfig);

    qCInfo(aimusic::core::lcCore, "%s %s starting (config: %s, data: %s, cache: %s, logs: %s)",
        qPrintable(aimusic::core::applicationName()), qPrintable(aimusic::core::versionString()),
        qPrintable(paths.configDir()), qPrintable(paths.dataDir()), qPrintable(paths.cacheDir()),
        qPrintable(paths.logDir()));

    QTextStream out(stdout);
    out << aimusic::core::applicationName() << u' ' << aimusic::core::versionString() << Qt::endl;

    aimusic::core::uninstallLogging();
    return 0;
}
