// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AiContext.h"

#include "UiLogging.h"

#include <QStringList>

namespace linernotes::ui {

namespace {
QStringList buildPromptDirs(const QString &promptsDir)
{
    QStringList dirs;
    if (!promptsDir.isEmpty()) {
        dirs.append(promptsDir);
    }
    dirs.append(QStringLiteral(":/prompts"));
    return dirs;
}
} // namespace

AiContext::AiContext(core::Settings &settings, library::Database &db, const core::Clock &clock,
    const QString &promptsDir, QObject *parent)
    : QObject(parent)
    , m_config(settings)
    , m_client(m_network)
    , m_cache(db, clock)
    , m_usage(db)
    , m_privacy(settings)
    , m_prompts(buildPromptDirs(promptsDir))
    , m_llm(m_config, m_secrets, m_client, m_cache, m_usage, m_privacy, clock)
    , m_jobs(db, clock)
    , m_settingsController(
          m_config, m_secrets, m_client, m_privacy, m_usage, m_cache, m_prompts, clock)
{
}

void AiContext::onDatabaseReady()
{
    const auto restoreRes = m_jobs.restore();
    if (!restoreRes.ok()) {
        qCWarning(lcUi, "Failed to restore job queue on database ready: %s",
            qPrintable(restoreRes.error().toString()));
    }
    const auto purgeRes = m_cache.purgeExpired();
    if (!purgeRes.ok()) {
        qCWarning(lcUi, "Failed to purge expired LLM cache on database ready: %s",
            qPrintable(purgeRes.error().toString()));
    }
}

AiSettingsController *AiContext::settingsController()
{
    return &m_settingsController;
}

ai::LlmService &AiContext::llm()
{
    return m_llm;
}

ai::JobQueue &AiContext::jobs()
{
    return m_jobs;
}

ai::PromptLibrary &AiContext::prompts()
{
    return m_prompts;
}

} // namespace linernotes::ui
