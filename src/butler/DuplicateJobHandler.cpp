// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DuplicateJobHandler.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QtConcurrent/QtConcurrent>

#include <butler/ButlerLogging.h>
#include <butler/DuplicateFinder.h>
#include <butler/DuplicateSource.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

namespace {

struct DuplicateJobStats {
    int exact = 0;
    int sameRecording = 0;
    int suspect = 0;
};

class DuplicateJobTask final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DuplicateJobTask)

public:
    DuplicateJobTask(library::Database &db, const core::Clock &clock,
        std::function<void(const core::Result<void> &)> done)
        : m_done(std::move(done))
    {
        connect(&m_watcher, &QFutureWatcher<core::Result<DuplicateJobStats>>::finished, this,
            &DuplicateJobTask::onFinished);

        auto future = QtConcurrent::run([&db, &clock]() -> core::Result<DuplicateJobStats> {
            const DuplicateSource source(db, clock);
            const auto tracksRes = source.loadTracks(true);
            if (!tracksRes.ok()) {
                return tracksRes.error();
            }
            const auto &tracks = tracksRes.value();
            const auto groups = findDuplicates(tracks);
            const auto saveRes = source.saveGroups(groups, tracks);
            if (!saveRes.ok()) {
                return saveRes.error();
            }

            DuplicateJobStats stats;
            for (const auto &g : groups) {
                switch (g.kind) {
                case DuplicateKind::Exact:
                    ++stats.exact;
                    break;
                case DuplicateKind::SameRecording:
                    ++stats.sameRecording;
                    break;
                case DuplicateKind::Suspect:
                    ++stats.suspect;
                    break;
                }
            }
            return stats;
        });
        m_watcher.setFuture(future);
    }

    ~DuplicateJobTask() override
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
            qCInfo(lcButler,
                "Duplicate detection completed: exact=%d, same_recording=%d, suspect=%d",
                stats.exact, stats.sameRecording, stats.suspect);
            if (m_done) {
                m_done({ });
            }
        } else {
            qCWarning(
                lcButler, "Duplicate detection failed: %s", qPrintable(res.error().toString()));
            if (m_done) {
                m_done(res.error());
            }
        }
    }

    QFutureWatcher<core::Result<DuplicateJobStats>> m_watcher;
    std::function<void(const core::Result<void> &)> m_done;
};

} // namespace

DuplicateJobHandler::DuplicateJobHandler(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

QString DuplicateJobHandler::kind() const
{
    return QStringLiteral("butler.duplicates");
}

int DuplicateJobHandler::maxInFlight() const
{
    return 1;
}

ai::TokenUsage DuplicateJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    Q_UNUSED(itemKey);
    Q_UNUSED(params);
    return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
}

std::unique_ptr<QObject> DuplicateJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    Q_UNUSED(params);
    if (itemKey != QLatin1StringView("all")) {
        done(core::Error {
            .code = QString(errc::kDuplicatesInvalidKey),
            .message = QStringLiteral("Invalid itemKey for duplicates: expected 'all'"),
            .detail = itemKey,
        });
        return nullptr;
    }

    return std::make_unique<DuplicateJobTask>(m_db, m_clock, std::move(done));
}

} // namespace linernotes::butler

#include "DuplicateJobHandler.moc"
