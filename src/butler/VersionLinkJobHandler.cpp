// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "VersionLinkJobHandler.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QtConcurrent/QtConcurrent>

#include <ai/PromptLibrary.h>
#include <butler/ButlerLogging.h>
#include <butler/Errors.h>
#include <butler/VersionLinker.h>
#include <core/Clock.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

namespace {

class VersionLinkTask final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(VersionLinkTask)

public:
    VersionLinkTask(library::Database &db, const core::Clock &clock, int suffixPromptVersion,
        int titleMatchPromptVersion, std::function<void(const core::Result<void> &)> done)
        : m_done(std::move(done))
    {
        connect(&m_watcher, &QFutureWatcher<core::Result<VersionLinkStats>>::finished, this,
            &VersionLinkTask::onFinished);

        auto future
            = QtConcurrent::run([&db, &clock, suffixPromptVersion,
                                    titleMatchPromptVersion]() -> core::Result<VersionLinkStats> {
                  const VersionLinker linker(db, clock);
                  return linker.linkAll(suffixPromptVersion, titleMatchPromptVersion);
              });
        m_watcher.setFuture(future);
    }

    ~VersionLinkTask() override
    {
        m_watcher.cancel();
        m_watcher.waitForFinished();
    }

private:
    void onFinished()
    {
        const auto res = m_watcher.result();
        if (res.ok()) {
            const auto &stats = res.value();
            qCInfo(lcButler, "Version linking completed: tracks=%d, works=%d, unresolved=%d",
                stats.tracks, stats.works, stats.unresolved);
            if (m_done) {
                m_done({ });
            }
        } else {
            qCWarning(lcButler, "Version linking failed: %s", qPrintable(res.error().toString()));
            if (m_done) {
                m_done(res.error());
            }
        }
    }

    QFutureWatcher<core::Result<VersionLinkStats>> m_watcher;
    std::function<void(const core::Result<void> &)> m_done;
};

} // namespace

VersionLinkJobHandler::VersionLinkJobHandler(
    library::Database &db, const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString VersionLinkJobHandler::kind() const
{
    return QStringLiteral("butler.version_link");
}

int VersionLinkJobHandler::maxInFlight() const
{
    return 1;
}

ai::TokenUsage VersionLinkJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    Q_UNUSED(itemKey);
    Q_UNUSED(params);
    return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
}

std::unique_ptr<QObject> VersionLinkJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    Q_UNUSED(params);
    if (itemKey != QLatin1StringView("all")) {
        done(core::Error {
            .code = QString(errc::kVersionLinkInvalidKey),
            .message = QStringLiteral("Invalid itemKey for version_link: expected 'all'"),
            .detail = itemKey,
        });
        return nullptr;
    }

    int suffixPromptVersion = 0;
    if (const auto promptRes = m_prompts.load(QStringLiteral("cleanup/version_suffix"));
        promptRes.ok()) {
        suffixPromptVersion = promptRes.value().version;
    } else {
        qCWarning(lcButler, "Failed to load cleanup/version_suffix prompt: %s",
            qPrintable(promptRes.error().toString()));
    }

    int titleMatchPromptVersion = 0;
    if (const auto promptRes = m_prompts.load(QStringLiteral("cleanup/title_match"));
        promptRes.ok()) {
        titleMatchPromptVersion = promptRes.value().version;
    } else {
        qCWarning(lcButler, "Failed to load cleanup/title_match prompt: %s",
            qPrintable(promptRes.error().toString()));
    }

    return std::make_unique<VersionLinkTask>(
        m_db, m_clock, suffixPromptVersion, titleMatchPromptVersion, std::move(done));
}

} // namespace linernotes::butler

#include "VersionLinkJobHandler.moc"
