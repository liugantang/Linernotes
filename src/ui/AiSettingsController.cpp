// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AiSettingsController.h"

#include "ErrorText.h"

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
        m_modelCache.remove(profile.id);
    } else {
        profile.capabilities = std::nullopt;
    }

    const QString savedId = m_config.saveService(profile);
    if (!savedId.isEmpty()) {
        m_modelCache.remove(savedId);
    }
    return savedId;
}

void AiSettingsController::removeService(const QString &id)
{
    m_modelCache.remove(id);
    m_config.removeService(id);
    m_secrets.remove(id, this, [](const core::Result<void> &) { });
}

void AiSettingsController::setDefaultService(const QString &id)
{
    m_config.setDefaultServiceId(id);
}

QString AiSettingsController::defaultServiceId() const
{
    return m_config.defaultServiceId();
}

void AiSettingsController::setApiKey(const QString &serviceId, const QString &key)
{
    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty()) {
        m_secrets.remove(serviceId, this, [this, serviceId](const core::Result<void> &res) {
            if (res.ok()) {
                emit apiKeySaved(serviceId, true, tr("API key removed"));
            } else {
                emit apiKeySaved(serviceId, false,
                    tr("Failed to remove API key: %1").arg(userErrorText(res.error())));
            }
        });
    } else {
        m_secrets.write(
            serviceId, trimmedKey, this, [this, serviceId](const core::Result<void> &res) {
                if (res.ok()) {
                    emit apiKeySaved(serviceId, true, tr("API key saved"));
                } else {
                    emit apiKeySaved(serviceId, false,
                        tr("Failed to save API key: %1").arg(userErrorText(res.error())));
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

bool AiSettingsController::draftTesting() const
{
    return m_draftTesting;
}

void AiSettingsController::resolveApiKey(const QString &serviceId, const QString &explicitKey,
    std::function<void(const core::Result<QString> &)> callback)
{
    const QString trimmed = explicitKey.trimmed();
    if (!trimmed.isEmpty() || serviceId.trimmed().isEmpty()) {
        callback(trimmed);
        return;
    }
    m_secrets.read(serviceId.trimmed(), this, std::move(callback));
}

void AiSettingsController::executePing(const ai::ServiceConfig &svcConfig,
    std::function<void(const core::Result<ai::ChatResponse> &res, int latencyMs)> callback)
{
    const auto renderRes = m_prompts.render(QStringLiteral("common/ping"), { });
    if (!renderRes.ok()) {
        callback(renderRes.error(), 0);
        return;
    }

    ai::ChatRequest req;
    req.messages = ai::toMessages(renderRes.value());
    req.temperature = 0.0;
    req.maxTokens = 50;

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    auto reply = m_client.complete(svcConfig, req);
    auto *rawReply = reply.release();
    if (rawReply != nullptr) {
        rawReply->setParent(this);
        connect(rawReply, &ai::LlmReply::finished, this,
            [rawReply, timer, callback = std::move(callback)]() {
                const int latencyMs = static_cast<int>(timer->elapsed());
                callback(rawReply->result(), latencyMs);
                rawReply->deleteLater();
            });
    }
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

    resolveApiKey(profile.id, QString(), [this, profile](const core::Result<QString> &keyRes) {
        if (!keyRes.ok()) {
            finishTest(profile.id, false, tr("Failed to read API key"), 0);
            return;
        }

        const ai::ServiceConfig svcConfig {
            .baseUrl = profile.baseUrl,
            .apiKey = keyRes.value(),
            .model = profile.defaultModel,
            .timeoutMs = profile.timeoutMs,
        };

        executePing(svcConfig,
            [this, serviceId = profile.id, svcConfig](const core::Result<ai::ChatResponse> &res,
                int latencyMs) { handleTestPingReply(serviceId, svcConfig, res, latencyMs); });
    });
}

void AiSettingsController::testDraft(const QString &serviceId, const QString &baseUrl,
    const QString &model, const QString &apiKey, int timeoutMs)
{
    if (m_draftTesting) {
        return;
    }

    const QString trimmedModel = model.trimmed();
    if (trimmedModel.isEmpty()) {
        emit draftTested(false, tr("Choose a model first"), 0);
        return;
    }

    const QUrl url(baseUrl.trimmed());
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        emit draftTested(false, tr("Base URL must start with http:// or https://"), 0);
        return;
    }

    m_draftTesting = true;
    emit draftTestingChanged();

    resolveApiKey(serviceId, apiKey,
        [this, url, trimmedModel, timeoutMs](const core::Result<QString> &keyRes) {
            if (!keyRes.ok()) {
                m_draftTesting = false;
                emit draftTestingChanged();
                emit draftTested(false, tr("Failed to read API key"), 0);
                return;
            }

            const ai::ServiceConfig svcConfig {
                .baseUrl = url,
                .apiKey = keyRes.value(),
                .model = trimmedModel,
                .timeoutMs = timeoutMs > 0 ? timeoutMs : 60000,
            };

            executePing(
                svcConfig, [this](const core::Result<ai::ChatResponse> &res, int latencyMs) {
                    m_draftTesting = false;
                    emit draftTestingChanged();

                    if (!res.ok()) {
                        emit draftTested(false, friendlyErrorMessage(res.error()), latencyMs);
                        return;
                    }

                    emit draftTested(true, pingSuccessMessage(res.value()), latencyMs);
                });
        });
}

void AiSettingsController::fetchDraftModels(
    const QString &serviceId, const QString &baseUrl, const QString &apiKey)
{
    const quint64 gen = ++m_draftModelsGeneration;

    const QUrl url(baseUrl.trimmed());
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        emit draftModelsFetched(false, { }, tr("Base URL must start with http:// or https://"));
        return;
    }

    resolveApiKey(serviceId, apiKey, [this, gen, url](const core::Result<QString> &keyRes) {
        if (gen != m_draftModelsGeneration) {
            return;
        }

        const ai::ServiceConfig svcConfig {
            .baseUrl = url,
            .apiKey = keyRes.ok() ? keyRes.value() : QString(),
            .model = { },
            .timeoutMs = 60000,
        };

        m_client.listModels(svcConfig, this, [this, gen](const core::Result<QStringList> &res) {
            if (gen != m_draftModelsGeneration) {
                return;
            }

            if (res.ok()) {
                emit draftModelsFetched(true, res.value(), QString());
            } else {
                emit draftModelsFetched(false, { }, friendlyModelListErrorMessage(res.error()));
            }
        });
    });
}

void AiSettingsController::fetchServiceModels(const QString &serviceId, bool force)
{
    const QString trimmedId = serviceId.trimmed();
    if (trimmedId.isEmpty()) {
        emit serviceModelsFetched(serviceId, false, { }, tr("Service not found"));
        return;
    }

    if (!force && m_modelCache.contains(trimmedId)) {
        emit serviceModelsFetched(serviceId, true, m_modelCache.value(trimmedId), QString());
        return;
    }

    const auto profileOpt = m_config.service(trimmedId);
    if (!profileOpt.has_value()) {
        emit serviceModelsFetched(serviceId, false, { }, tr("Service not found"));
        return;
    }
    const ai::ServiceProfile &profile = *profileOpt;

    // 路由页每一行都会请求同一服务的列表，进行中的请求只发一次
    if (m_modelFetchesInFlight.contains(profile.id)) {
        return;
    }
    m_modelFetchesInFlight.insert(profile.id);

    m_secrets.read(profile.id, this, [this, profile](const core::Result<QString> &keyRes) {
        if (!keyRes.ok()) {
            m_modelFetchesInFlight.remove(profile.id);
            emit serviceModelsFetched(profile.id, false, { }, tr("Failed to read API key"));
            return;
        }

        const ai::ServiceConfig svcConfig {
            .baseUrl = profile.baseUrl,
            .apiKey = keyRes.value(),
            .model = profile.defaultModel,
            .timeoutMs = profile.timeoutMs,
        };

        m_client.listModels(
            svcConfig, this, [this, serviceId = profile.id](const core::Result<QStringList> &res) {
                m_modelFetchesInFlight.remove(serviceId);
                if (res.ok()) {
                    m_modelCache.insert(serviceId, res.value());
                    emit serviceModelsFetched(serviceId, true, res.value(), QString());
                } else {
                    emit serviceModelsFetched(
                        serviceId, false, { }, friendlyModelListErrorMessage(res.error()));
                }
            });
    });
}

QStringList AiSettingsController::serviceModels(const QString &serviceId) const
{
    return m_modelCache.value(serviceId);
}

void AiSettingsController::handleTestPingReply(const QString &serviceId,
    const ai::ServiceConfig &svcConfig, const core::Result<ai::ChatResponse> &res, int latencyMs)
{
    if (!res.ok()) {
        finishTest(serviceId, false, friendlyErrorMessage(res.error()), latencyMs);
        return;
    }

    const QString baseMsg = pingSuccessMessage(res.value());

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

QString AiSettingsController::pingSuccessMessage(const ai::ChatResponse &response)
{
    // 推理模型在 maxTokens 内可能只产出 reasoning，content 为空
    QString replyText = response.content.trimmed();
    if (replyText.isEmpty()) {
        return tr("Connected successfully");
    }
    if (replyText.length() > 40) {
        replyText = replyText.left(40) + QStringLiteral("...");
    }
    return tr("Connected successfully, model replied: %1").arg(replyText);
}

QString AiSettingsController::friendlyModelListErrorMessage(const core::Error &error)
{
    if (error.code == ai::errc::kHttp
        && (error.message.contains(QLatin1StringView("404"))
            || error.detail.contains(QLatin1StringView("404")))) {
        return tr("This service does not provide a model list; type the model name manually");
    }
    return friendlyErrorMessage(error);
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
