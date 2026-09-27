// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <ai/AiConfig.h>
#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/PrivacyGuard.h>
#include <ai/PromptLibrary.h>
#include <ai/SecretStore.h>
#include <ai/StructuredOutput.h>
#include <ai/UsageStore.h>
#include <common/FakeLlmServer.h>
#include <common/ManualClock.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <ui/AiSettingsController.h>
#include <ui/ServiceListModel.h>
#include <ui/UsageSummaryModel.h>

using linernotes::ai::AiConfig;
using linernotes::ai::LlmCache;
using linernotes::ai::LlmClient;
using linernotes::ai::MemorySecretStore;
using linernotes::ai::PrivacyGuard;
using linernotes::ai::PromptLibrary;
using linernotes::ai::UsageStore;
using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::FakeLlmServer;
using linernotes::test::ManualClock;
using linernotes::ui::AiSettingsController;
using linernotes::ui::ServiceListModel;
namespace ai = linernotes::ai;

namespace {

FakeLlmServer::Response makeJsonResponse(const QString &content, int status = 200)
{
    QJsonObject msgObj;
    msgObj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    msgObj.insert(QStringLiteral("content"), content);

    QJsonObject choice0;
    choice0.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));
    choice0.insert(QStringLiteral("message"), msgObj);

    QJsonObject respObj;
    respObj.insert(QStringLiteral("choices"), QJsonArray { choice0 });
    return FakeLlmServer::json(respObj, status);
}

FakeLlmServer::Response makeToolCallResponse(const QString &fnName, const QString &arguments)
{
    QJsonObject fnObj;
    fnObj.insert(QStringLiteral("name"), fnName);
    fnObj.insert(QStringLiteral("arguments"), arguments);

    QJsonObject tcObj;
    tcObj.insert(QStringLiteral("id"), QStringLiteral("call_1"));
    tcObj.insert(QStringLiteral("type"), QStringLiteral("function"));
    tcObj.insert(QStringLiteral("function"), fnObj);

    QJsonObject msgObj;
    msgObj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    msgObj.insert(QStringLiteral("tool_calls"), QJsonArray { tcObj });

    QJsonObject choice0;
    choice0.insert(QStringLiteral("finish_reason"), QStringLiteral("tool_calls"));
    choice0.insert(QStringLiteral("message"), msgObj);

    QJsonObject respObj;
    respObj.insert(QStringLiteral("choices"), QJsonArray { choice0 });
    return FakeLlmServer::json(respObj, 200);
}

class TstAiSettingsController : public QObject {
    Q_OBJECT

private slots:
    void saveEditRemoveService();
    void testServiceSuccess();
    void testServiceAuthFailure();
    void privacyAndRoutes();
    void usageSummary();
};

void TstAiSettingsController::saveEditRemoveService()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    ManualClock clock(1000);
    MemorySecretStore secrets;
    QNetworkAccessManager nam;
    LlmClient client(nam);
    AiConfig config(settings);
    PrivacyGuard privacy(settings);
    UsageStore usage(db);
    LlmCache cache(db, clock);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });

    AiSettingsController ctrl(config, secrets, client, privacy, usage, cache, prompts, clock);
    QCOMPARE(ctrl.services()->rowCount(), 0);

    QSignalSpy errorSpy(&ctrl, &AiSettingsController::errorOccurred);

    // 1. Invalid baseUrl (not http/https) -> returns empty, emits errorOccurred
    const QString emptyId = ctrl.saveService(QString(), QStringLiteral("OpenAI"),
        QStringLiteral("ftp://api.openai.com"), QStringLiteral("gpt-4o"), 60000, 2, 0);
    QVERIFY(emptyId.isEmpty());
    QCOMPARE(errorSpy.count(), 1);

    // 2. Empty service name -> returns empty, emits errorOccurred
    const QString emptyId2 = ctrl.saveService(QString(), QStringLiteral("   "),
        QStringLiteral("https://api.openai.com/v1"), QStringLiteral("gpt-4o"), 60000, 2, 0);
    QVERIFY(emptyId2.isEmpty());
    QCOMPARE(errorSpy.count(), 2);

    // 3. Valid new service
    const QString id = ctrl.saveService(QString(), QStringLiteral("OpenAI"),
        QStringLiteral("https://api.openai.com/v1"), QStringLiteral("gpt-4o"), 60000, 2, 0);
    QVERIFY(!id.isEmpty());
    QCOMPARE(ctrl.services()->rowCount(), 1);

    const auto idx0 = ctrl.services()->index(0, 0);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::ServiceIdRole).toString(), id);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::NameRole).toString(),
        QStringLiteral("OpenAI"));
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::BaseUrlRole).toString(),
        QStringLiteral("https://api.openai.com/v1"));
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::DefaultModelRole).toString(),
        QStringLiteral("gpt-4o"));
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::IsDefaultRole).toBool(), true);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::IsLocalRole).toBool(), false);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::CapabilitiesKnownRole).toBool(), false);

    // 4. Set capabilities -> capabilitiesKnown is true
    config.setCapabilities(
        id, ai::Capabilities { .jsonSchema = true, .tools = true, .jsonObject = true });
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::CapabilitiesKnownRole).toBool(), true);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::SupportsJsonSchemaRole).toBool(), true);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::SupportsToolsRole).toBool(), true);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::SupportsJsonObjectRole).toBool(), true);

    // 5. Edit baseUrl -> capabilities reset to unknown
    const QString idUpdated = ctrl.saveService(id, QStringLiteral("OpenAI V2"),
        QStringLiteral("https://api.openai.com/v2"), QStringLiteral("gpt-4o"), 60000, 2, 0);
    QCOMPARE(idUpdated, id);
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::NameRole).toString(),
        QStringLiteral("OpenAI V2"));
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::BaseUrlRole).toString(),
        QStringLiteral("https://api.openai.com/v2"));
    QCOMPARE(ctrl.services()->data(idx0, ServiceListModel::CapabilitiesKnownRole).toBool(), false);

    // 6. Set and check API key
    QSignalSpy keySavedSpy(&ctrl, &AiSettingsController::apiKeySaved);
    ctrl.setApiKey(id, QStringLiteral("sk-test-key-12345"));
    QTRY_COMPARE(keySavedSpy.count(), 1);
    QCOMPARE(keySavedSpy.at(0).at(0).toString(), id);
    QCOMPARE(keySavedSpy.at(0).at(1).toBool(), true);

    QSignalSpy keyCheckedSpy(&ctrl, &AiSettingsController::apiKeyChecked);
    ctrl.checkApiKey(id);
    QTRY_COMPARE(keyCheckedSpy.count(), 1);
    QCOMPARE(keyCheckedSpy.at(0).at(0).toString(), id);
    QCOMPARE(keyCheckedSpy.at(0).at(1).toBool(), true);

    // 7. Remove service -> service removed and key deleted
    ctrl.removeService(id);
    QCOMPARE(ctrl.services()->rowCount(), 0);

    keyCheckedSpy.clear();
    ctrl.checkApiKey(id);
    QTRY_COMPARE(keyCheckedSpy.count(), 1);
    QCOMPARE(keyCheckedSpy.at(0).at(0).toString(), id);
    QCOMPARE(keyCheckedSpy.at(0).at(1).toBool(), false);
}

void TstAiSettingsController::testServiceSuccess()
{
    FakeLlmServer server;
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    ManualClock clock(1000);
    MemorySecretStore secrets;
    QNetworkAccessManager nam;
    LlmClient client(nam);
    AiConfig config(settings);
    PrivacyGuard privacy(settings);
    UsageStore usage(db);
    LlmCache cache(db, clock);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });

    AiSettingsController ctrl(config, secrets, client, privacy, usage, cache, prompts, clock);

    // Save service pointing to fake server
    const QString serviceId = ctrl.saveService(QString(), QStringLiteral("Test Server"),
        server.baseUrl().toString(), QStringLiteral("gpt-4o"), 5000, 2, 0);
    QVERIFY(!serviceId.isEmpty());

    QSignalSpy keySavedSpy(&ctrl, &AiSettingsController::apiKeySaved);
    ctrl.setApiKey(serviceId, QStringLiteral("test-key"));
    QTRY_COMPARE(keySavedSpy.count(), 1);

    // Enqueue: 1. Ping response, 2. Probe json_schema, 3. Probe tools, 4. Probe json_object
    server.enqueue(makeJsonResponse(QStringLiteral("OK")));
    server.enqueue(makeJsonResponse(QStringLiteral("{\"ok\": true}")));
    server.enqueue(
        makeToolCallResponse(QStringLiteral("report"), QStringLiteral("{\"ok\": true}")));
    server.enqueue(makeJsonResponse(QStringLiteral("{\"ok\": true}")));

    QSignalSpy testedSpy(&ctrl, &AiSettingsController::serviceTested);

    ctrl.testService(serviceId);
    QCOMPARE(ctrl.testingServiceId(), serviceId);

    // Second call while testing should be ignored
    ctrl.testService(serviceId);

    QTRY_COMPARE_WITH_TIMEOUT(testedSpy.count(), 1, 5000);
    QCOMPARE(testedSpy.at(0).at(0).toString(), serviceId);
    QCOMPARE(testedSpy.at(0).at(1).toBool(), true);
    const QString msg = testedSpy.at(0).at(2).toString();
    QVERIFY(msg.contains(QStringLiteral("OK")));
    QVERIFY(testedSpy.at(0).at(3).toInt() >= 0);

    QCOMPARE(ctrl.testingServiceId(), QString());

    // Capabilities should be written back to AiConfig
    const auto svc = config.service(serviceId);
    QVERIFY(svc.has_value());
    if (!svc.has_value()) {
        return;
    }
    QVERIFY(svc->capabilities.has_value());
    if (!svc->capabilities.has_value()) {
        return;
    }
    QCOMPARE(svc->capabilities->jsonSchema, true);
    QCOMPARE(svc->capabilities->tools, true);
    QCOMPARE(svc->capabilities->jsonObject, true);
}

void TstAiSettingsController::testServiceAuthFailure()
{
    FakeLlmServer server;
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    ManualClock clock(1000);
    MemorySecretStore secrets;
    QNetworkAccessManager nam;
    LlmClient client(nam);
    AiConfig config(settings);
    PrivacyGuard privacy(settings);
    UsageStore usage(db);
    LlmCache cache(db, clock);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });

    AiSettingsController ctrl(config, secrets, client, privacy, usage, cache, prompts, clock);

    const QString serviceId = ctrl.saveService(QString(), QStringLiteral("Auth Server"),
        server.baseUrl().toString(), QStringLiteral("gpt-4o"), 5000, 2, 0);
    QVERIFY(!serviceId.isEmpty());

    const QString secretKey = QStringLiteral("super-secret-key-987654321");
    QSignalSpy keySavedSpy(&ctrl, &AiSettingsController::apiKeySaved);
    ctrl.setApiKey(serviceId, secretKey);
    QTRY_COMPARE(keySavedSpy.count(), 1);

    // Enqueue 401 response
    QJsonObject errObj;
    errObj.insert(QStringLiteral("error"),
        QJsonObject { { QStringLiteral("message"), QStringLiteral("Invalid API key") } });
    server.enqueue(FakeLlmServer::json(errObj, 401));

    QSignalSpy testedSpy(&ctrl, &AiSettingsController::serviceTested);
    ctrl.testService(serviceId);

    QTRY_COMPARE_WITH_TIMEOUT(testedSpy.count(), 1, 5000);
    QCOMPARE(testedSpy.at(0).at(0).toString(), serviceId);
    QCOMPARE(testedSpy.at(0).at(1).toBool(), false);
    const QString msg = testedSpy.at(0).at(2).toString();
    QVERIFY(!msg.isEmpty());
    QVERIFY(!msg.contains(secretKey));
    QCOMPARE(ctrl.testingServiceId(), QString());
}

void TstAiSettingsController::privacyAndRoutes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    ManualClock clock(1000);
    MemorySecretStore secrets;
    QNetworkAccessManager nam;
    LlmClient client(nam);
    AiConfig config(settings);
    PrivacyGuard privacy(settings);
    UsageStore usage(db);
    LlmCache cache(db, clock);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });

    AiSettingsController ctrl(config, secrets, client, privacy, usage, cache, prompts, clock);

    // 1. Privacy switches
    QCOMPARE(ctrl.sendPlayHistory(), true);
    QCOMPARE(ctrl.sendMoments(), false);
    QCOMPARE(ctrl.sendLocation(), false);

    QSignalSpy privacySpy(&ctrl, &AiSettingsController::privacyChanged);

    ctrl.setSendPlayHistory(false);
    QCOMPARE(ctrl.sendPlayHistory(), false);
    QCOMPARE(privacy.isAllowed(linernotes::ai::DataCategory::PlayHistory), false);
    QCOMPARE(privacySpy.count(), 1);

    ctrl.setSendMoments(true);
    QCOMPARE(ctrl.sendMoments(), true);
    QCOMPARE(privacy.isAllowed(linernotes::ai::DataCategory::Moments), true);
    QCOMPARE(privacySpy.count(), 2);

    ctrl.setSendLocation(true);
    QCOMPARE(ctrl.sendLocation(), true);
    QCOMPARE(privacy.isAllowed(linernotes::ai::DataCategory::Location), true);
    QCOMPARE(privacySpy.count(), 3);

    // 2. Routes
    QSignalSpy routesSpy(&ctrl, &AiSettingsController::routesChanged);
    ctrl.setRoute(linernotes::ai::Purpose::Cleanup, QStringLiteral("custom-svc"),
        QStringLiteral("custom-model"));
    QCOMPARE(routesSpy.count(), 1);
    QCOMPARE(ctrl.routeServiceId(linernotes::ai::Purpose::Cleanup), QStringLiteral("custom-svc"));
    QCOMPARE(ctrl.routeModel(linernotes::ai::Purpose::Cleanup), QStringLiteral("custom-model"));
    QCOMPARE(ctrl.routeServiceId(linernotes::ai::Purpose::Query), QString());
    QCOMPARE(ctrl.routeModel(linernotes::ai::Purpose::Query), QString());
}

void TstAiSettingsController::usageSummary()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    Database db(tempDir.filePath(QStringLiteral("library.db")));
    QVERIFY(db.open(Migrator()).ok());
    ManualClock clock(1000000);
    MemorySecretStore secrets;
    QNetworkAccessManager nam;
    LlmClient client(nam);
    AiConfig config(settings);
    PrivacyGuard privacy(settings);
    UsageStore usage(db);
    LlmCache cache(db, clock);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });

    AiSettingsController ctrl(config, secrets, client, privacy, usage, cache, prompts, clock);

    // Insert 2 usage records
    ai::UsageRecord rec1;
    rec1.createdAtMs = clock.nowMs() - 1000;
    rec1.purpose = linernotes::ai::Purpose::Query;
    rec1.serviceId = QStringLiteral("svc1");
    rec1.model = QStringLiteral("gpt-4o");
    rec1.usage = ai::TokenUsage { .promptTokens = 100, .completionTokens = 50 };
    rec1.cached = false;
    rec1.ok = true;
    rec1.elapsedMs = 200;
    QVERIFY(usage.record(rec1).ok());

    ai::UsageRecord rec2;
    rec2.createdAtMs = clock.nowMs() - 2000;
    rec2.purpose = linernotes::ai::Purpose::Cleanup;
    rec2.serviceId = QStringLiteral("svc1");
    rec2.model = QStringLiteral("gpt-4o-mini");
    rec2.usage = ai::TokenUsage { .promptTokens = 200, .completionTokens = 80 };
    rec2.cached = false;
    rec2.ok = true;
    rec2.elapsedMs = 150;
    QVERIFY(usage.record(rec2).ok());

    ctrl.refreshUsage(30);

    QCOMPARE(ctrl.usage()->rowCount(), 2);
    QCOMPARE(ctrl.usage()->totalPromptTokens(), 300);
    QCOMPARE(ctrl.usage()->totalCompletionTokens(), 130);
    QCOMPARE(ctrl.usage()->totalRequests(), 2);

    // Clear cache
    QSignalSpy cacheSpy(&ctrl, &AiSettingsController::cacheCleared);
    ctrl.clearCache();
    QCOMPARE(cacheSpy.count(), 1);
    QCOMPARE(cacheSpy.at(0).at(0).toBool(), true);
}

} // namespace

QTEST_MAIN(TstAiSettingsController)
#include "tst_AiSettingsController.moc"
