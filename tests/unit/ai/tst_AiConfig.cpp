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
    void defaultValuesForLegacyConfigWithoutNewFields();
    void purposeNameRoundtrip_data();
    void purposeNameRoundtrip();
};

void TstAiConfig::saveAndReloadServices()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    Capabilities expectedCapsStruct;
    expectedCapsStruct.jsonSchema = true;
    expectedCapsStruct.tools = true;
    expectedCapsStruct.jsonObject = true;
    const std::optional<Capabilities> expectedCaps = expectedCapsStruct;

    {
        Settings settings(iniPath);
        AiConfig config(settings);

        ServiceProfile s1;
        s1.id = QString();
        s1.name = QStringLiteral("DeepSeek");
        s1.baseUrl = QUrl(QStringLiteral("https://api.deepseek.com"));
        s1.defaultModel = QStringLiteral("deepseek-chat");
        s1.timeoutMs = 30000;
        s1.capabilities = expectedCaps;
        s1.maxConcurrent = 5;
        s1.requestsPerMinute = 60;

        ServiceProfile s2;
        s2.id = QString();
        s2.name = QStringLiteral("Ollama");
        s2.baseUrl = QUrl(QStringLiteral("http://localhost:11434"));
        s2.defaultModel = QStringLiteral("llama3");
        s2.timeoutMs = 60000;
        s2.capabilities = std::nullopt;
        s2.maxConcurrent = 2;
        s2.requestsPerMinute = 0;

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
        QCOMPARE(list.at(0).maxConcurrent, 5);
        QCOMPARE(list.at(0).requestsPerMinute, 60);

        QCOMPARE(list.at(1).id, id2);
        QCOMPARE(list.at(1).name, QStringLiteral("Ollama"));
        QCOMPARE(list.at(1).capabilities, std::nullopt);
        QCOMPARE(list.at(1).maxConcurrent, 2);
        QCOMPARE(list.at(1).requestsPerMinute, 0);
    }

    // Reload in a new Settings / AiConfig instance
    {
        Settings settings(iniPath);
        AiConfig config(settings);

        const auto list = config.services();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).name, QStringLiteral("DeepSeek"));
        QCOMPARE(list.at(0).capabilities, expectedCaps);
        QCOMPARE(list.at(0).maxConcurrent, 5);
        QCOMPARE(list.at(0).requestsPerMinute, 60);

        QCOMPARE(list.at(1).name, QStringLiteral("Ollama"));
        QCOMPARE(list.at(1).capabilities, std::nullopt);
        QCOMPARE(list.at(1).maxConcurrent, 2);
        QCOMPARE(list.at(1).requestsPerMinute, 0);

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
    ServiceProfile s1;
    s1.id = QStringLiteral("srv-1");
    s1.name = QStringLiteral("Main LLM");
    s1.baseUrl = QUrl(QStringLiteral("https://api.main.com"));
    s1.defaultModel = QStringLiteral("main-model");
    s1.timeoutMs = 60000;
    s1.capabilities = std::nullopt;
    s1.maxConcurrent = 2;
    s1.requestsPerMinute = 0;

    ServiceProfile s2;
    s2.id = QStringLiteral("srv-2");
    s2.name = QStringLiteral("Fast LLM");
    s2.baseUrl = QUrl(QStringLiteral("https://api.fast.com"));
    s2.defaultModel = QStringLiteral("fast-model");
    s2.timeoutMs = 15000;
    s2.capabilities = std::nullopt;
    s2.maxConcurrent = 4;
    s2.requestsPerMinute = 120;

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
    PurposeRoute r1;
    r1.serviceId = QStringLiteral("srv-2");
    r1.model = QStringLiteral("custom-fast");
    config.setRoute(Purpose::Cleanup, r1);

    const auto resCleanup = config.resolve(Purpose::Cleanup);
    QVERIFY(resCleanup.has_value());
    if (!resCleanup.has_value()) {
        return;
    }
    QCOMPARE(resCleanup->profile.id, QStringLiteral("srv-2"));
    QCOMPARE(resCleanup->model, QStringLiteral("custom-fast"));

    // 4. Route specifies non-existent service -> fallback to default
    PurposeRoute r2;
    r2.serviceId = QStringLiteral("deleted-srv");
    r2.model = QStringLiteral("dj-model");
    config.setRoute(Purpose::Dj, r2);

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

    ServiceProfile s1;
    s1.id = QStringLiteral("s1");
    s1.name = QStringLiteral("S1");
    s1.baseUrl = QUrl(QStringLiteral("https://s1.example.com"));
    s1.defaultModel = QStringLiteral("m1");
    s1.timeoutMs = 60000;
    s1.capabilities = std::nullopt;
    s1.maxConcurrent = 2;
    s1.requestsPerMinute = 0;

    ServiceProfile s2;
    s2.id = QStringLiteral("s2");
    s2.name = QStringLiteral("S2");
    s2.baseUrl = QUrl(QStringLiteral("https://s2.example.com"));
    s2.defaultModel = QStringLiteral("m2");
    s2.timeoutMs = 60000;
    s2.capabilities = std::nullopt;
    s2.maxConcurrent = 2;
    s2.requestsPerMinute = 0;

    config.saveService(s1);
    config.saveService(s2);

    PurposeRoute r;
    r.serviceId = QStringLiteral("s1");
    r.model = QStringLiteral("m1-custom");
    config.setRoute(Purpose::Narrative, r);

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

void TstAiConfig::defaultValuesForLegacyConfigWithoutNewFields()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString iniPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    {
        Settings settings(iniPath);
        const linernotes::core::SettingKey<QString> rawServicesKey { u"ai/services", QString() };
        // JSON without maxConcurrent and requestsPerMinute
        const char *const legacyRaw
            = R"json([{"id":"legacy1","name":"Legacy Service","baseUrl":"https://legacy.example.com","defaultModel":"m1"}])json";
        const QString legacyJson = QString::fromUtf8(legacyRaw);
        settings.setValue(rawServicesKey, legacyJson);
        settings.sync();
    }

    {
        Settings settings(iniPath);
        AiConfig config(settings);

        const auto list = config.services();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).id, QStringLiteral("legacy1"));
        QCOMPARE(list.at(0).maxConcurrent, 2);
        QCOMPARE(list.at(0).requestsPerMinute, 0);
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
