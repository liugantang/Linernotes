// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <ai/AiConfig.h>
#include <ai/AiEnums.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/PrivacyGuard.h>
#include <ai/PromptLibrary.h>
#include <ai/SecretStore.h>
#include <ai/UsageStore.h>
#include <core/Clock.h>
#include <ui/ServiceListModel.h>
#include <ui/UsageSummaryModel.h>

namespace linernotes::ui {

class AiSettingsController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AiSettingsController)

    Q_PROPERTY(linernotes::ui::ServiceListModel *services READ services CONSTANT)
    Q_PROPERTY(QString testingServiceId READ testingServiceId NOTIFY testingServiceIdChanged)
    Q_PROPERTY(bool playHistoryAllowed READ playHistoryAllowed WRITE setPlayHistoryAllowed NOTIFY
            privacyChanged)
    Q_PROPERTY(
        bool momentsAllowed READ momentsAllowed WRITE setMomentsAllowed NOTIFY privacyChanged)
    Q_PROPERTY(
        bool locationAllowed READ locationAllowed WRITE setLocationAllowed NOTIFY privacyChanged)
    Q_PROPERTY(linernotes::ui::UsageSummaryModel *usage READ usage CONSTANT)

public:
    explicit AiSettingsController(ai::AiConfig &config, ai::SecretStore &secrets,
        ai::LlmClient &client, ai::PrivacyGuard &privacy, ai::UsageStore &usage,
        ai::LlmCache &cache, ai::PromptLibrary &prompts, const core::Clock &clock,
        QObject *parent = nullptr);
    ~AiSettingsController() override = default;

    // Services
    [[nodiscard]] ServiceListModel *services();
    Q_INVOKABLE QString saveService(const QString &id, const QString &name, const QString &baseUrl,
        const QString &defaultModel, int timeoutMs, int maxConcurrent, int requestsPerMinute);
    Q_INVOKABLE void removeService(const QString &id);
    Q_INVOKABLE void setDefaultService(const QString &id);

    // API Key
    Q_INVOKABLE void setApiKey(const QString &serviceId, const QString &key);
    Q_INVOKABLE void checkApiKey(const QString &serviceId);

    // Test Connection
    [[nodiscard]] QString testingServiceId() const;
    Q_INVOKABLE void testService(const QString &serviceId);

    // Routes
    [[nodiscard]] Q_INVOKABLE QString routeServiceId(linernotes::ai::Purpose purpose) const;
    [[nodiscard]] Q_INVOKABLE QString routeModel(linernotes::ai::Purpose purpose) const;
    Q_INVOKABLE void setRoute(
        linernotes::ai::Purpose purpose, const QString &serviceId, const QString &model);

    // Privacy
    [[nodiscard]] bool playHistoryAllowed() const;
    void setPlayHistoryAllowed(bool allowed);
    [[nodiscard]] bool momentsAllowed() const;
    void setMomentsAllowed(bool allowed);
    [[nodiscard]] bool locationAllowed() const;
    void setLocationAllowed(bool allowed);

    // Usage & Cache
    [[nodiscard]] UsageSummaryModel *usage();
    Q_INVOKABLE void refreshUsage(int days);
    Q_INVOKABLE void clearCache();

signals:
    void errorOccurred(const QString &message);
    void apiKeySaved(const QString &serviceId, bool ok, const QString &message);
    void apiKeyChecked(const QString &serviceId, bool present);
    void serviceTested(const QString &serviceId, bool ok, const QString &message, int latencyMs);
    void routesChanged();
    void testingServiceIdChanged();
    void privacyChanged();
    void cacheCleared(bool ok);

private:
    void handleTestPingReply(const QString &serviceId, const ai::ServiceConfig &svcConfig,
        const ai::LlmReply &reply, int latencyMs);
    void finishTest(const QString &serviceId, bool ok, const QString &message, int latencyMs);
    static QString friendlyErrorMessage(const core::Error &error);

    ai::AiConfig &m_config;
    ai::SecretStore &m_secrets;
    ai::LlmClient &m_client;
    ai::PrivacyGuard &m_privacy;
    ai::UsageStore &m_usageStore;
    ai::LlmCache &m_cache;
    ai::PromptLibrary &m_prompts;
    const core::Clock &m_clock;

    ServiceListModel m_servicesModel;
    UsageSummaryModel m_usageModel;
    QString m_testingServiceId;
};

} // namespace linernotes::ui
