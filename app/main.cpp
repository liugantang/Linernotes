// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "QmlTypes.h"

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
#include <ui/AppContext.h>
#include <ui/CoverImageProvider.h>
#include <ui/Translations.h>

#include <memory>

#ifdef Q_OS_LINUX
#include <QWindow>

#include <ui/mpris/Mpris.h>

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
} // namespace
#endif

int main(int argc, char *argv[])
{
    QGuiApplication::setOrganizationName(QString());
    QGuiApplication::setApplicationName(linernotes::core::applicationName());
    QGuiApplication::setApplicationVersion(linernotes::core::versionString());

    QGuiApplication app(argc, argv);

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

    const QString iniFilePath = QDir(paths.configDir()).filePath(QStringLiteral("settings.ini"));
    linernotes::core::Settings settings(iniFilePath);

    const QString logLevelStr = settings.value(linernotes::core::kLogLevel).trimmed().toLower();
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

    linernotes::core::LogConfig logConfig;
    logConfig.directory = paths.logDir();
    logConfig.minimumLevel = minimumLevel;
    linernotes::core::installLogging(logConfig);

    qCInfo(linernotes::core::lcCore, "%s %s starting (config: %s, data: %s, cache: %s, logs: %s)",
        qPrintable(linernotes::core::applicationName()),
        qPrintable(linernotes::core::versionString()), qPrintable(paths.configDir()),
        qPrintable(paths.dataDir()), qPrintable(paths.cacheDir()), qPrintable(paths.logDir()));

    const bool isSmokeTest = parser.isSet(smokeTestOption);

    linernotes::ui::AppContext::Options appOptions {
        .databasePath = QDir(paths.dataDir()).filePath(QStringLiteral("library.db")),
        .coverCacheDir = QDir(paths.cacheDir()).filePath(QStringLiteral("covers")),
        .playerOptions = { },
        .uiStatePath = QDir(paths.configDir()).filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = QDir(paths.dataDir()).filePath(QStringLiteral("playback-state.json")),
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

    linernotes::ui::Translations translations(app, *appContext.settings());
    translations.apply(appContext.settings()->language());

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    int exitCode = 0;
    {
        QQmlApplicationEngine engine;

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
        }
#endif

        exitCode = QGuiApplication::exec();
    }

    AppContextForeign::setInstance(nullptr);
    linernotes::core::uninstallLogging();
    return exitCode;
}
