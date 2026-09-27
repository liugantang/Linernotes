// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <ai/AiConfig.h>
#include <ai/AiEnumNames.h>
#include <ai/AiEnums.h>
#include <core/Settings.h>

#include <optional>

using linernotes::ai::AiConfig;
using linernotes::ai::Capabilities;
using linernotes::ai::Purpose;
using linernotes::ai::purposeFromName;
using linernotes::ai::purposeName;
using linernotes::ai::PurposeRoute;
using linernotes::ai::ServiceProfile;
using linernotes::core::Settings;

namespace {

class TstAiConfig : public QObject {
    Q_OBJECT

private slots:
    void saveAndReloadServices();
    void resolveRoutes();
    void removeDefaultService();
    void corruptedJsonDoesNotCrash();
    void purposeNameRoundtrip_data();
    void purposeNameRoundtrip();
};

void TstAiConfig::saveAndReloadServices()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    const std::optional<Capabilities> expectedCaps = Capabilities {
        .jsonSchema = true,
        .tools = true,
        .jsonObject = true,
    };

    {
        Settings settings(iniPath);
        AiConfig config(settings);

        ServiceProfile s1 {
            .id = QString(),
            .name = QStringLiteral("DeepSeek"),
            .baseUrl = QUrl(QStringLiteral("https://api.deepseek.com")),
            .defaultModel = QStringLiteral("deepseek-chat"),
            .timeoutMs = 30000,
            .capabilities = Capabilities {
                .jsonSchema = true,
                .tools = true,
                .jsonObject = true,
            },
        };

        ServiceProfile s2 {
            .id = QString(),
            .name = QStringLiteral("Ollama"),
            .baseUrl = QUrl(QStringLiteral("http://localhost:11434")),
            .defaultModel = QStringLiteral("llama3"),
            .timeoutMs = 60000,
            .capabilities = std::nullopt,
        };

        const QString id1 = config.saveService(s1);
        QVERIFY(!id1.isEmpty());
        // First service automatically becomes default
        QCOMPARE(config.defaultServiceId(), id1);

        const QString id2 = config.saveService(s2);
        QVERIFY(!id2.isEmpty());
        QVERIFY(id1 != id2);
        // Default remains first service
        QCOMPARE(config.defaultServiceId(), id1);

        const auto list = config.services();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).id, id1);
        QCOMPARE(list.at(0).name, QStringLiteral("DeepSeek"));
        QCOMPARE(list.at(0).capabilities, expectedCaps);

        QCOMPARE(list.at(1).id, id2);
        QCOMPARE(list.at(1).name, QStringLiteral("Ollama"));
        QCOMPARE(list.at(1).capabilities, std::nullopt);
    }

    // Reload in a new Settings / AiConfig instance
    {
        Settings settings(iniPath);
        AiConfig config(settings);

        const auto list = config.services();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).name, QStringLiteral("DeepSeek"));
        QCOMPARE(list.at(0).capabilities, expectedCaps);

        QCOMPARE(list.at(1).name, QStringLiteral("Ollama"));
        QCOMPARE(list.at(1).capabilities, std::nullopt);

        QCOMPARE(config.defaultServiceId(), list.at(0).id);
    }
}

void TstAiConfig::resolveRoutes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    AiConfig config(settings);

    // 1. No services -> nullopt
    QCOMPARE(config.resolve(Purpose::Cleanup).has_value(), false);

    // Add service 1 (default) and service 2
    ServiceProfile s1 {
        .id = QStringLiteral("srv-1"),
        .name = QStringLiteral("Main LLM"),
        .baseUrl = QUrl(QStringLiteral("https://api.main.com")),
        .defaultModel = QStringLiteral("main-model"),
        .timeoutMs = 60000,
        .capabilities = std::nullopt,
    };
    ServiceProfile s2 {
        .id = QStringLiteral("srv-2"),
        .name = QStringLiteral("Fast LLM"),
        .baseUrl = QUrl(QStringLiteral("https://api.fast.com")),
        .defaultModel = QStringLiteral("fast-model"),
        .timeoutMs = 15000,
        .capabilities = std::nullopt,
    };
    config.saveService(s1);
    config.saveService(s2);

    // 2. Route is empty -> default service + defaultModel
    const auto resDefault = config.resolve(Purpose::Query);
    QVERIFY(resDefault.has_value());
    if (!resDefault.has_value()) {
        return;
    }
    QCOMPARE(resDefault->profile.id, QStringLiteral("srv-1"));
    QCOMPARE(resDefault->model, QStringLiteral("main-model"));

    // 3. Route specifies service and model
    config.setRoute(Purpose::Cleanup,
        PurposeRoute {
            .serviceId = QStringLiteral("srv-2"),
            .model = QStringLiteral("custom-fast"),
        });
    const auto resCleanup = config.resolve(Purpose::Cleanup);
    QVERIFY(resCleanup.has_value());
    if (!resCleanup.has_value()) {
        return;
    }
    QCOMPARE(resCleanup->profile.id, QStringLiteral("srv-2"));
    QCOMPARE(resCleanup->model, QStringLiteral("custom-fast"));

    // 4. Route specifies non-existent service -> fallback to default
    config.setRoute(Purpose::Dj,
        PurposeRoute {
            .serviceId = QStringLiteral("deleted-srv"),
            .model = QStringLiteral("dj-model"),
        });
    const auto resDj = config.resolve(Purpose::Dj);
    QVERIFY(resDj.has_value());
    if (!resDj.has_value()) {
        return;
    }
    QCOMPARE(resDj->profile.id, QStringLiteral("srv-1"));
    QCOMPARE(resDj->model, QStringLiteral("dj-model"));
}

void TstAiConfig::removeDefaultService()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    Settings settings(iniPath);
    AiConfig config(settings);

    ServiceProfile s1 {
        .id = QStringLiteral("s1"),
        .name = QStringLiteral("S1"),
        .baseUrl = QUrl(QStringLiteral("https://s1.example.com")),
        .defaultModel = QStringLiteral("m1"),
        .timeoutMs = 60000,
        .capabilities = std::nullopt,
    };
    ServiceProfile s2 {
        .id = QStringLiteral("s2"),
        .name = QStringLiteral("S2"),
        .baseUrl = QUrl(QStringLiteral("https://s2.example.com")),
        .defaultModel = QStringLiteral("m2"),
        .timeoutMs = 60000,
        .capabilities = std::nullopt,
    };
    config.saveService(s1);
    config.saveService(s2);
    config.setRoute(Purpose::Narrative,
        PurposeRoute {
            .serviceId = QStringLiteral("s1"),
            .model = QStringLiteral("m1-custom"),
        });

    QCOMPARE(config.defaultServiceId(), QStringLiteral("s1"));
    QCOMPARE(config.route(Purpose::Narrative).serviceId, QStringLiteral("s1"));

    // Remove default service s1
    config.removeService(QStringLiteral("s1"));

    // Default should become remaining service s2
    QCOMPARE(config.defaultServiceId(), QStringLiteral("s2"));

    // Route pointing to s1 should be cleared to default
    const PurposeRoute routeAfter = config.route(Purpose::Narrative);
    QCOMPARE(routeAfter.serviceId, QString());
    QCOMPARE(routeAfter.model, QString());
}

void TstAiConfig::corruptedJsonDoesNotCrash()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    {
        Settings settings(iniPath);
        // Write corrupted JSON directly to ini settings
        const linernotes::core::SettingKey<QString> rawServicesKey { u"ai/services", QString() };
        const linernotes::core::SettingKey<QString> rawRoutesKey { u"ai/routes", QString() };
        settings.setValue(rawServicesKey, QStringLiteral("{broken json [}"));
        settings.setValue(rawRoutesKey, QStringLiteral("not json"));
        settings.sync();
    }

    {
        Settings settings(iniPath);
        AiConfig config(settings);

        // Corrupted JSON should be treated as empty and not crash
        QCOMPARE(config.services().isEmpty(), true);
        QCOMPARE(config.route(Purpose::Cleanup), PurposeRoute());
        QCOMPARE(config.resolve(Purpose::Cleanup).has_value(), false);
    }
}

void TstAiConfig::purposeNameRoundtrip_data()
{
    QTest::addColumn<Purpose>("purpose");
    QTest::addColumn<QString>("expectedName");

    QTest::newRow("Cleanup") << Purpose::Cleanup << QStringLiteral("cleanup");
    QTest::newRow("Query") << Purpose::Query << QStringLiteral("query");
    QTest::newRow("Dj") << Purpose::Dj << QStringLiteral("dj");
    QTest::newRow("Guide") << Purpose::Guide << QStringLiteral("guide");
    QTest::newRow("Narrative") << Purpose::Narrative << QStringLiteral("narrative");
}

void TstAiConfig::purposeNameRoundtrip()
{
    QFETCH(Purpose, purpose);
    QFETCH(QString, expectedName);

    const QString name = purposeName(purpose);
    QCOMPARE(name, expectedName);

    const auto parsed = purposeFromName(name);
    QVERIFY(parsed.has_value());
    if (!parsed.has_value()) {
        return;
    }
    QCOMPARE(*parsed, purpose);
}

} // namespace

QTEST_GUILESS_MAIN(TstAiConfig)

#include "tst_AiConfig.moc"
