// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "QmlTypes.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>
#include <QWindow>

#include <core/CoreSettings.h>
#include <core/Logging.h>
#include <core/Paths.h>
#include <core/Settings.h>
#include <core/SingleInstance.h>
#include <core/UnixSignals.h>
#include <core/Version.h>
#include <ui/AppContext.h>
#include <ui/CoverImageProvider.h>
#include <ui/LibraryActions.h>
#include <ui/Translations.h>

#include <memory>

#ifdef Q_OS_LINUX
#include <ui/DBusNotificationSink.h>
#include <ui/TrackNotifier.h>
#include <ui/mpris/Mpris.h>
#endif

namespace {
void activateMainWindow(const QQmlApplicationEngine &engine)
{
    const auto rootObjects = engine.rootObjects();
    if (rootObjects.isEmpty()) {
        return;
    }
    auto *window = qobject_cast<QWindow *>(rootObjects.constFirst());
    if (window != nullptr) {
        window->show();
        window->raise();
        window->requestActivate();
    }
}

QtMsgType logLevelFromSettings(const linernotes::core::Settings &settings)
{
    const QString logLevelStr = settings.value(linernotes::core::kLogLevel).trimmed().toLower();
    if (logLevelStr == u"debug") {
        return QtDebugMsg;
    }
    if (logLevelStr == u"warning" || logLevelStr == u"warn") {
        return QtWarningMsg;
    }
    if (logLevelStr == u"critical") {
        return QtCriticalMsg;
    }
    return QtInfoMsg;
}

bool handOffToPrimary(linernotes::core::SingleInstance &singleInstance, const QStringList &files)
{
    QStringList absoluteFilePaths;
    absoluteFilePaths.reserve(files.size());
    for (const auto &file : files) {
        absoluteFilePaths.append(QFileInfo(file).absoluteFilePath());
    }
    const auto sendRes = singleInstance.sendMessage(absoluteFilePaths);
    if (sendRes.ok()) {
        return true;
    }
    qCWarning(linernotes::core::lcCore,
        "Failed to send message to primary instance: %s; starting as standalone",
        qPrintable(sendRes.error().toString()));
    return false;
}
} // namespace

// Qt 启动/分配失败时的异常无法恢复，交给默认终止
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[])
{
    QGuiApplication::setOrganizationName(QString());
    QGuiApplication::setApplicationName(linernotes::core::applicationName());
    QGuiApplication::setApplicationVersion(linernotes::core::versionString());
    QGuiApplication::setDesktopFileName(QStringLiteral("linernotes"));

    QApplication app(argc, argv);

#ifdef Q_OS_UNIX
    const linernotes::core::UnixSignalQuitter signalQuitter;
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AI music player"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files to play."),
        QStringLiteral("[files...]"));

    const QCommandLineOption printPathsOption(
        QStringLiteral("print-paths"), QStringLiteral("Print application directories and exit."));
    parser.addOption(printPathsOption);

    QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"), QStringLiteral("Run smoke test and exit immediately."));
    smokeTestOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(smokeTestOption);

    parser.process(app);

    const auto paths = linernotes::core::Paths::fromEnvironment();

    if (parser.isSet(printPathsOption)) {
        QTextStream out(stdout);
        out << QStringLiteral("Config: ") << paths.configDir() << Qt::endl;
        out << QStringLiteral("Data:   ") << paths.dataDir() << Qt::endl;
        out << QStringLiteral("Cache:  ") << paths.cacheDir() << Qt::endl;
        out << QStringLiteral("Logs:   ") << paths.logDir() << Qt::endl;
        return 0;
    }

    paths.ensureCreated();

    const QStringList positionalFiles = parser.positionalArguments();
    const bool isSmokeTest = parser.isSet(smokeTestOption);

    std::unique_ptr<linernotes::core::SingleInstance> singleInstance;
    if (!isSmokeTest) {
        singleInstance = std::make_unique<linernotes::core::SingleInstance>(paths.dataDir());
        const auto startRes = singleInstance->start();
        if (!startRes.ok()) {
            qCWarning(linernotes::core::lcCore,
                "SingleInstance start error: %s; starting as standalone",
                qPrintable(startRes.error().toString()));
        } else if (startRes.value() == linernotes::core::SingleInstance::Role::Secondary
            && handOffToPrimary(*singleInstance, positionalFiles)) {
            return 0;
        }
    }

    const QString iniFilePath = QDir(paths.configDir()).filePath(QStringLiteral("settings.ini"));
    linernotes::core::Settings settings(iniFilePath);

    linernotes::core::LogConfig logConfig;
    logConfig.directory = paths.logDir();
    logConfig.minimumLevel = logLevelFromSettings(settings);
    linernotes::core::installLogging(logConfig);

    qCInfo(linernotes::core::lcCore, "%s %s starting (config: %s, data: %s, cache: %s, logs: %s)",
        qPrintable(linernotes::core::applicationName()),
        qPrintable(linernotes::core::versionString()), qPrintable(paths.configDir()),
        qPrintable(paths.dataDir()), qPrintable(paths.cacheDir()), qPrintable(paths.logDir()));

    linernotes::ui::AppContext::Options appOptions {
        .databasePath = QDir(paths.dataDir()).filePath(QStringLiteral("library.db")),
        .coverCacheDir = QDir(paths.cacheDir()).filePath(QStringLiteral("covers")),
        .playerOptions = { },
        .uiStatePath = QDir(paths.configDir()).filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = QDir(paths.dataDir()).filePath(QStringLiteral("playback-state.json")),
        .backupDir = QDir(paths.dataDir()).filePath(QStringLiteral("backups")),
    };
    if (isSmokeTest) {
        appOptions.playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } };
    }

    linernotes::ui::AppContext appContext(settings, appOptions);
    AppContextForeign::setInstance(&appContext);

    QObject::connect(
        &app, &QCoreApplication::aboutToQuit, &appContext, &linernotes::ui::AppContext::saveState);

    const auto startRes = appContext.start();
    if (!startRes.ok()) {
        qCWarning(linernotes::core::lcCore, "AppContext start error: %s",
            qPrintable(startRes.error().toString()));
    }

    if (!positionalFiles.isEmpty()) {
        appContext.actions()->openFiles(positionalFiles);
    }

    linernotes::ui::Translations translations(app, *appContext.settings());
    translations.apply(appContext.settings()->language());

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    int exitCode = 0;
    {
        QQmlApplicationEngine engine;

        if (singleInstance) {
            QObject::connect(singleInstance.get(),
                &linernotes::core::SingleInstance::messageReceived, &app,
                [&engine, actions = appContext.actions()](const QStringList &args) {
                    activateMainWindow(engine);
                    if (!args.isEmpty() && actions != nullptr) {
                        actions->openFiles(args);
                    }
                });
        }

        QObject::connect(&translations, &linernotes::ui::Translations::retranslateRequested,
            &engine, &QQmlApplicationEngine::retranslate);

        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
            [](const QUrl &url) {
                qCCritical(linernotes::core::lcCore, "Failed to create QML root object: %s",
                    qPrintable(url.toString()));
                QCoreApplication::exit(EXIT_FAILURE);
            },
            Qt::QueuedConnection);

        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, &app,
            [isSmokeTest](QObject *object, const QUrl &url) {
                if (!object) {
                    qCCritical(linernotes::core::lcCore, "Failed to load QML root object from: %s",
                        qPrintable(url.toString()));
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                if (isSmokeTest) {
                    qCInfo(linernotes::core::lcCore,
                        "Smoke test: QML root object created successfully from %s",
                        qPrintable(url.toString()));
                    QCoreApplication::exit(0);
                }
            },
            Qt::QueuedConnection);

        engine.addImageProvider(QStringLiteral("cover"),
            new linernotes::ui::CoverImageProvider(appContext.coverStore()));

        engine.loadFromModule(QStringLiteral("Linernotes"), QStringLiteral("Main"));

#ifdef Q_OS_LINUX
        std::unique_ptr<linernotes::ui::Mpris> mpris;
        std::unique_ptr<linernotes::ui::DBusNotificationSink> notificationSink;
        std::unique_ptr<linernotes::ui::TrackNotifier> trackNotifier;
        if (!isSmokeTest) {
            mpris = std::make_unique<linernotes::ui::Mpris>(
                *appContext.player(), *appContext.nowPlaying(), *appContext.coverStore());
            QObject::connect(mpris.get(), &linernotes::ui::Mpris::raiseRequested, &app,
                [&engine]() { activateMainWindow(engine); });
            const auto mprisRes = mpris->registerOnBus();
            if (!mprisRes.ok()) {
                qCWarning(linernotes::core::lcCore, "Failed to register MPRIS on D-Bus: %s",
                    qPrintable(mprisRes.error().toString()));
            }

            notificationSink = std::make_unique<linernotes::ui::DBusNotificationSink>();
            trackNotifier = std::make_unique<linernotes::ui::TrackNotifier>(
                *appContext.nowPlaying(), *appContext.player(), *appContext.coverStore(),
                *appContext.settings(), *notificationSink);
        }
#endif

        exitCode = QGuiApplication::exec();
    }

    AppContextForeign::setInstance(nullptr);
    linernotes::core::uninstallLogging();
    return exitCode;
}
