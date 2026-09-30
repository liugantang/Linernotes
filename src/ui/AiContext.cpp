// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AiContext.h"

#include "UiLogging.h"

#include <QStringList>

#include <butler/ArtistCreditJobHandler.h>
#include <butler/ArtistMergeJobHandler.h>
#include <butler/CoverArtJobHandler.h>
#include <butler/DuplicateJobHandler.h>
#include <butler/FingerprintJobHandler.h>
#include <butler/MbMatchJobHandler.h>
#include <butler/MojibakeJobHandler.h>
#include <butler/VersionLinkJobHandler.h>
#include <butler/VersionSuffixJobHandler.h>

#include <memory>

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

AiContext::AiContext(core::Settings &settings, library::Database &db, library::CoverStore &covers,
    const core::Clock &clock, const QString &promptsDir, QObject *parent)
    : QObject(parent)
    , m_musicBrainz(m_network, db, clock)
    , m_config(settings)
    , m_client(m_network)
    , m_cache(db, clock)
    , m_usage(db)
    , m_privacy(settings)
    , m_prompts(buildPromptDirs(promptsDir))
    , m_debugLog(settings)
    , m_llm(m_config, m_secrets, m_client, m_cache, m_usage, m_privacy, m_debugLog, clock)
    , m_jobs(db, clock)
    , m_settingsController(
          m_config, m_secrets, m_client, m_privacy, m_usage, m_cache, m_prompts, clock)
    , m_debugController(m_debugLog, m_llm)
{
    m_jobs.registerHandler(
        std::make_unique<butler::MojibakeJobHandler>(db, m_llm, m_prompts, clock));
    m_jobs.registerHandler(
        std::make_unique<butler::ArtistCreditJobHandler>(db, m_llm, m_prompts, clock));
    m_jobs.registerHandler(
        std::make_unique<butler::VersionSuffixJobHandler>(db, m_llm, m_prompts, clock));
    m_jobs.registerHandler(std::make_unique<butler::VersionLinkJobHandler>(db, m_prompts, clock));
    m_jobs.registerHandler(
        std::make_unique<butler::ArtistMergeJobHandler>(db, m_llm, m_prompts, clock));
    m_jobs.registerHandler(std::make_unique<butler::FingerprintJobHandler>(db, clock));
    m_jobs.registerHandler(std::make_unique<butler::DuplicateJobHandler>(db, clock));
    m_jobs.registerHandler(std::make_unique<butler::MbMatchJobHandler>(db, m_musicBrainz, clock));
    m_jobs.registerHandler(
        std::make_unique<butler::CoverArtJobHandler>(db, m_network, covers, clock,
            QUrl(QString::fromLatin1(butler::kCoverArtArchiveBase.data(),
                static_cast<qsizetype>(butler::kCoverArtArchiveBase.size())))));
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

LlmDebugController *AiContext::debugController()
{
    return &m_debugController;
}

ai::LlmService &AiContext::llm()
{
    return m_llm;
}

ai::LlmDebugLog &AiContext::debugLog()
{
    return m_debugLog;
}

ai::JobQueue &AiContext::jobs()
{
    return m_jobs;
}

ai::PromptLibrary &AiContext::prompts()
{
    return m_prompts;
}

const ai::AiConfig &AiContext::config() const
{
    return m_config;
}

} // namespace linernotes::ui
