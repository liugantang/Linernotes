// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <ai/AiConfig.h>
#include <ai/JobQueue.h>
#include <ai/KeychainSecretStore.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmDebugLog.h>
#include <ai/LlmService.h>
#include <ai/PrivacyGuard.h>
#include <ai/PromptLibrary.h>
#include <ai/UsageStore.h>
#include <butler/MusicBrainzClient.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <ui/AiSettingsController.h>
#include <ui/LlmDebugController.h>

namespace linernotes::core {
class Settings;
}

namespace linernotes::ui {

class AiContext : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AiContext)

public:
    /// promptsDir：用户覆盖模板目录（<配置目录>/prompts）；为空时只用内置模板。
    AiContext(core::Settings &settings, library::Database &db, const core::Clock &clock,
        const QString &promptsDir, QObject *parent = nullptr);
    ~AiContext() override = default;

    /// 数据库打开后由 AppContext::start() 调用：JobQueue::restore()、LlmCache::purgeExpired()。
    void onDatabaseReady();

    [[nodiscard]] AiSettingsController *settingsController();
    [[nodiscard]] LlmDebugController *debugController();
    [[nodiscard]] ai::LlmService &llm();
    [[nodiscard]] ai::LlmDebugLog &debugLog();
    [[nodiscard]] ai::JobQueue &jobs();
    [[nodiscard]] ai::PromptLibrary &prompts();
    [[nodiscard]] const ai::AiConfig &config() const;

private:
    // 声明顺序即依赖顺序
    QNetworkAccessManager m_network;
    butler::MusicBrainzClient m_musicBrainz;
    ai::AiConfig m_config;
    ai::KeychainSecretStore m_secrets;
    ai::LlmClient m_client;
    ai::LlmCache m_cache;
    ai::UsageStore m_usage;
    ai::PrivacyGuard m_privacy;
    ai::PromptLibrary m_prompts;
    ai::LlmDebugLog m_debugLog;
    ai::LlmService m_llm;
    ai::JobQueue m_jobs;
    AiSettingsController m_settingsController;
    LlmDebugController m_debugController;
};

} // namespace linernotes::ui
