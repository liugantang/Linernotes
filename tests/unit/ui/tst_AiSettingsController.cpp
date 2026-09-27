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
using linernotes::test::ManualClock;
using linernotes::ui::AiSettingsController;
using linernotes::ui::ServiceListModel;
namespace ai = linernotes::ai;

namespace {

class TstAiSettingsController : public QObject {
    Q_OBJECT

private slots:
    void saveEditRemoveService();
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
    QCOMPARE(ctrl.playHistoryAllowed(), true);
    QCOMPARE(ctrl.momentsAllowed(), false);
    QCOMPARE(ctrl.locationAllowed(), false);

    QSignalSpy privacySpy(&ctrl, &AiSettingsController::privacyChanged);

    ctrl.setPlayHistoryAllowed(false);
    QCOMPARE(ctrl.playHistoryAllowed(), false);
    QCOMPARE(privacy.isAllowed(linernotes::ai::DataCategory::PlayHistory), false);
    QCOMPARE(privacySpy.count(), 1);

    ctrl.setMomentsAllowed(true);
    QCOMPARE(ctrl.momentsAllowed(), true);
    QCOMPARE(privacy.isAllowed(linernotes::ai::DataCategory::Moments), true);
    QCOMPARE(privacySpy.count(), 2);

    ctrl.setLocationAllowed(true);
    QCOMPARE(ctrl.locationAllowed(), true);
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
