// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AiSettingsController.h"

#include <QElapsedTimer>
#include <QUrl>

#include <ai/CapabilityProbe.h>
#include <ai/Errors.h>
#include <ai/LlmReply.h>

#include <memory>

namespace linernotes::ui {

AiSettingsController::AiSettingsController(ai::AiConfig &config, ai::SecretStore &secrets,
    ai::LlmClient &client, ai::PrivacyGuard &privacy, ai::UsageStore &usage, ai::LlmCache &cache,
    ai::PromptLibrary &prompts, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_secrets(secrets)
    , m_client(client)
    , m_privacy(privacy)
    , m_usageStore(usage)
    , m_cache(cache)
    , m_prompts(prompts)
    , m_clock(clock)
    , m_servicesModel(config, this)
    , m_usageModel(usage, clock, this)
{
    connect(&m_config, &ai::AiConfig::changed, this, &AiSettingsController::routesChanged);
    connect(&m_privacy, &ai::PrivacyGuard::changed, this, &AiSettingsController::privacyChanged);
}

ServiceListModel *AiSettingsController::services()
{
    return &m_servicesModel;
}

QString AiSettingsController::saveService(const QString &id, const QString &name,
    const QString &baseUrl, const QString &defaultModel, int timeoutMs, int maxConcurrent,
    int requestsPerMinute)
{
    const QString trimmedName = name.trimmed();
    const QString trimmedBaseUrl = baseUrl.trimmed();
    const QString trimmedModel = defaultModel.trimmed();

    if (trimmedName.isEmpty()) {
        emit errorOccurred(tr("Service name cannot be empty"));
        return { };
    }
    if (trimmedBaseUrl.isEmpty()) {
        emit errorOccurred(tr("Base URL cannot be empty"));
        return { };
    }
    if (trimmedModel.isEmpty()) {
        emit errorOccurred(tr("Default model cannot be empty"));
        return { };
    }

    const QUrl url(trimmedBaseUrl);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        emit errorOccurred(tr("Base URL must start with http:// or https://"));
        return { };
    }

    ai::ServiceProfile profile;
    profile.id = id.trimmed();
    profile.name = trimmedName;
    profile.baseUrl = url;
    profile.defaultModel = trimmedModel;
    profile.timeoutMs = timeoutMs > 0 ? timeoutMs : 60000;
    profile.maxConcurrent = maxConcurrent > 0 ? maxConcurrent : 2;
    profile.requestsPerMinute = requestsPerMinute >= 0 ? requestsPerMinute : 0;

    if (!profile.id.isEmpty()) {
        const auto existing = m_config.service(profile.id);
        if (existing.has_value() && existing->baseUrl == url) {
            profile.capabilities = existing->capabilities;
        } else {
            profile.capabilities = std::nullopt;
        }
    } else {
        profile.capabilities = std::nullopt;
    }

    return m_config.saveService(profile);
}

void AiSettingsController::removeService(const QString &id)
{
    m_config.removeService(id);
    m_secrets.remove(id, this, [](const core::Result<void> &) { });
}

void AiSettingsController::setDefaultService(const QString &id)
{
    m_config.setDefaultServiceId(id);
}

void AiSettingsController::setApiKey(const QString &serviceId, const QString &key)
{
    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty()) {
        m_secrets.remove(serviceId, this, [this, serviceId](const core::Result<void> &res) {
            if (res.ok()) {
                emit apiKeySaved(serviceId, true, tr("API key removed"));
            } else {
                emit apiKeySaved(
                    serviceId, false, tr("Failed to remove API key: %1").arg(res.error().message));
            }
        });
    } else {
        m_secrets.write(
            serviceId, trimmedKey, this, [this, serviceId](const core::Result<void> &res) {
                if (res.ok()) {
                    emit apiKeySaved(serviceId, true, tr("API key saved"));
                } else {
                    emit apiKeySaved(serviceId, false,
                        tr("Failed to save API key: %1").arg(res.error().message));
                }
            });
    }
}

void AiSettingsController::checkApiKey(const QString &serviceId)
{
    m_secrets.read(serviceId, this, [this, serviceId](const core::Result<QString> &res) {
        const bool present = res.ok() && !res.value().isEmpty();
        emit apiKeyChecked(serviceId, present);
    });
}

QString AiSettingsController::testingServiceId() const
{
    return m_testingServiceId;
}

void AiSettingsController::testService(const QString &serviceId)
{
    if (!m_testingServiceId.isEmpty()) {
        return;
    }

    const auto profileOpt = m_config.service(serviceId);
    if (!profileOpt.has_value()) {
        return;
    }
    const ai::ServiceProfile &profile = *profileOpt;

    m_testingServiceId = serviceId;
    emit testingServiceIdChanged();

    // Testing connection is a configuration operation; directly uses LlmClient without
    // going through LlmService (bypasses cache and usage tracking).
    m_secrets.read(serviceId, this, [this, profile](const core::Result<QString> &keyRes) {
        if (!keyRes.ok()) {
            finishTest(profile.id, false, tr("Failed to read API key"), 0);
            return;
        }

        const auto renderRes = m_prompts.render(QStringLiteral("common/ping"), { });
        if (!renderRes.ok()) {
            finishTest(profile.id, false, tr("Failed to load ping prompt template"), 0);
            return;
        }

        ai::ChatRequest req;
        req.messages = ai::toMessages(renderRes.value());
        req.temperature = 0.0;
        req.maxTokens = 50;

        const ai::ServiceConfig svcConfig {
            .baseUrl = profile.baseUrl,
            .apiKey = keyRes.value(),
            .model = profile.defaultModel,
            .timeoutMs = profile.timeoutMs,
        };

        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();

        auto reply = m_client.complete(svcConfig, req);
        auto *rawReply = reply.release();
        if (rawReply != nullptr) {
            rawReply->setParent(this);
            connect(rawReply, &ai::LlmReply::finished, this,
                [this, serviceId = profile.id, svcConfig, rawReply, timer]() {
                    const int latencyMs = static_cast<int>(timer->elapsed());
                    handleTestPingReply(serviceId, svcConfig, *rawReply, latencyMs);
                    rawReply->deleteLater();
                });
        }
    });
}

void AiSettingsController::handleTestPingReply(const QString &serviceId,
    const ai::ServiceConfig &svcConfig, const ai::LlmReply &reply, int latencyMs)
{
    const auto &res = reply.result();
    if (!res.ok()) {
        finishTest(serviceId, false, friendlyErrorMessage(res.error()), latencyMs);
        return;
    }

    QString replyText = res.value().content.trimmed();
    if (replyText.length() > 40) {
        replyText = replyText.left(40) + QStringLiteral("...");
    }
    const QString baseMsg = tr("Connected successfully, model replied: %1").arg(replyText);

    auto *probe = new ai::CapabilityProbe(m_client, svcConfig, this);
    connect(probe, &ai::CapabilityProbe::finished, this,
        [this, serviceId, baseMsg, latencyMs, probe]() {
            const auto &probeRes = probe->result();
            QString finalMsg = baseMsg;
            if (probeRes.ok()) {
                m_config.setCapabilities(serviceId, probeRes.value());
            } else {
                finalMsg += tr(" (Failed to probe capabilities)");
            }
            probe->deleteLater();
            finishTest(serviceId, true, finalMsg, latencyMs);
        });
    probe->start();
}

void AiSettingsController::finishTest(
    const QString &serviceId, bool ok, const QString &message, int latencyMs)
{
    m_testingServiceId.clear();
    emit testingServiceIdChanged();
    emit serviceTested(serviceId, ok, message, latencyMs);
}

QString AiSettingsController::friendlyErrorMessage(const core::Error &error)
{
    if (error.code == ai::errc::kAuth) {
        return tr("API key is invalid or unauthorized");
    }
    if (error.code == ai::errc::kNetwork) {
        return tr("Unable to connect to service");
    }
    if (error.code == ai::errc::kTimeout) {
        return tr("Connection timed out");
    }
    if (error.code == ai::errc::kRateLimited) {
        return tr("Request limit exceeded");
    }
    if (!error.message.isEmpty()) {
        return tr("Connection failed: %1").arg(error.message);
    }
    return tr("Connection failed");
}

QString AiSettingsController::routeServiceId(linernotes::ai::Purpose purpose) const
{
    return m_config.route(purpose).serviceId;
}

QString AiSettingsController::routeModel(linernotes::ai::Purpose purpose) const
{
    return m_config.route(purpose).model;
}

void AiSettingsController::setRoute(
    linernotes::ai::Purpose purpose, const QString &serviceId, const QString &model)
{
    m_config.setRoute(purpose,
        ai::PurposeRoute {
            .serviceId = serviceId.trimmed(),
            .model = model.trimmed(),
        });
}

bool AiSettingsController::playHistoryAllowed() const
{
    return m_privacy.isAllowed(ai::DataCategory::PlayHistory);
}

void AiSettingsController::setPlayHistoryAllowed(bool allowed)
{
    m_privacy.setAllowed(ai::DataCategory::PlayHistory, allowed);
}

bool AiSettingsController::momentsAllowed() const
{
    return m_privacy.isAllowed(ai::DataCategory::Moments);
}

void AiSettingsController::setMomentsAllowed(bool allowed)
{
    m_privacy.setAllowed(ai::DataCategory::Moments, allowed);
}

bool AiSettingsController::locationAllowed() const
{
    return m_privacy.isAllowed(ai::DataCategory::Location);
}

void AiSettingsController::setLocationAllowed(bool allowed)
{
    m_privacy.setAllowed(ai::DataCategory::Location, allowed);
}

UsageSummaryModel *AiSettingsController::usage()
{
    return &m_usageModel;
}

void AiSettingsController::refreshUsage(int days)
{
    m_usageModel.refresh(days);
}

void AiSettingsController::clearCache()
{
    const auto res = m_cache.clear();
    emit cacheCleared(res.ok());
}

} // namespace linernotes::ui
