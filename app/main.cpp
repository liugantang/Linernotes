// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>

#include <core/CoreSettings.h>
#include <core/Logging.h>
#include <core/Paths.h>
#include <core/Settings.h>
#include <core/Version.h>

int main(int argc, char *argv[])
{
    QGuiApplication::setOrganizationName(QString());
    QGuiApplication::setApplicationName(aimusic::core::applicationName());
    QGuiApplication::setApplicationVersion(aimusic::core::versionString());

    const QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AI music player"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption printPathsOption(
        QStringLiteral("print-paths"), QStringLiteral("Print application directories and exit."));
    parser.addOption(printPathsOption);

    QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"), QStringLiteral("Run smoke test and exit immediately."));
    smokeTestOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(smokeTestOption);

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

    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QQmlApplicationEngine engine;

    const bool isSmokeTest = parser.isSet(smokeTestOption);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [](const QUrl &url) {
            qCCritical(aimusic::core::lcCore, "Failed to create QML root object: %s",
                qPrintable(url.toString()));
            QCoreApplication::exit(EXIT_FAILURE);
        },
        Qt::QueuedConnection);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [isSmokeTest](QObject *object, const QUrl &url) {
            if (!object) {
                qCCritical(aimusic::core::lcCore, "Failed to load QML root object from: %s",
                    qPrintable(url.toString()));
                QCoreApplication::exit(EXIT_FAILURE);
                return;
            }
            if (isSmokeTest) {
                qCInfo(aimusic::core::lcCore,
                    "Smoke test: QML root object created successfully from %s",
                    qPrintable(url.toString()));
                QCoreApplication::exit(0);
            }
        },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("AiMusic"), QStringLiteral("Main"));

    const int exitCode = QGuiApplication::exec();

    aimusic::core::uninstallLogging();
    return exitCode;
}
