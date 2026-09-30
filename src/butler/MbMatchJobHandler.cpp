// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MbMatchJobHandler.h"

#include <QJsonObject>
#include <QObject>

#include <butler/AlbumMatch.h>
#include <butler/ButlerLogging.h>
#include <butler/Errors.h>
#include <butler/MbMatchPlanner.h>
#include <butler/MbMatchSource.h>
#include <butler/MusicBrainz.h>
#include <butler/MusicBrainzClient.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>

#include <memory>
#include <utility>

namespace linernotes::butler {

namespace {

class MbMatchTask final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MbMatchTask)

public:
    MbMatchTask(library::Database &db, MusicBrainzClient &mb, const core::Clock &clock,
        qint64 batchId, std::optional<double> autoAccept, MbAlbumInput input,
        std::function<void(const core::Result<void> &)> done)
        : m_db(db)
        , m_mb(mb)
        , m_clock(clock)
        , m_batchId(batchId)
        , m_autoAccept(autoAccept)
        , m_input(std::move(input))
        , m_done(std::move(done))
    {
    }

    ~MbMatchTask() override = default;

    void start()
    {
        const QUrl searchUrl = releaseSearchUrl(m_input.searchTitle, m_input.searchArtist, 10);
        m_currentFetch = m_mb.get(searchUrl);
        if (m_currentFetch == nullptr) {
            finish(core::Error {
                .code = QString(errc::kMbNetwork),
                .message = QStringLiteral("Failed to initiate MB search"),
                .detail = { },
            });
            return;
        }
        connect(m_currentFetch.get(), &MbFetch::finished, this, &MbMatchTask::onSearchFinished);
    }

private:
    void finish(const core::Result<void> &res)
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_currentFetch.reset();
        if (m_done) {
            m_done(res);
        }
    }

    void onSearchFinished()
    {
        if (m_finished || m_currentFetch == nullptr) {
            return;
        }
        const auto &fetchRes = m_currentFetch->result();
        if (!fetchRes.ok()) {
            finish(fetchRes.error());
            return;
        }

        const auto parseRes = parseReleaseSearch(fetchRes.value());
        if (!parseRes.ok()) {
            finish(parseRes.error());
            return;
        }

        m_candidateIds
            = selectCandidates(parseRes.value(), static_cast<int>(m_input.tracks.size()));
        if (m_candidateIds.isEmpty()) {
            const MbAlbumResult result { .status = MbMatchStatus::NoMatch };
            const MbMatchSource source(m_db);
            const auto saveRes = source.saveMatch(m_input.albumId, result, m_clock.nowMs());
            if (!saveRes.ok()) {
                finish(saveRes.error());
                return;
            }
            qCInfo(lcButler, "MB match album %lld: status=no_match, score=0.00, proposals=0",
                m_input.albumId);
            finish({ });
            return;
        }

        m_candidateIndex = 0;
        fetchNextCandidate();
    }

    void fetchNextCandidate()
    {
        if (m_finished) {
            return;
        }
        if (m_candidateIndex >= m_candidateIds.size()) {
            evaluateMatches();
            return;
        }

        const QString releaseId = m_candidateIds.at(m_candidateIndex);
        const QUrl url = releaseUrl(releaseId);
        m_currentFetch = m_mb.get(url);
        if (m_currentFetch == nullptr) {
            finish(core::Error {
                .code = QString(errc::kMbNetwork),
                .message = QStringLiteral("Failed to initiate MB release fetch"),
                .detail = releaseId,
            });
            return;
        }
        connect(
            m_currentFetch.get(), &MbFetch::finished, this, &MbMatchTask::onReleaseDetailFinished);
    }

    void onReleaseDetailFinished()
    {
        if (m_finished || m_currentFetch == nullptr) {
            return;
        }
        const auto &fetchRes = m_currentFetch->result();
        if (!fetchRes.ok()) {
            finish(fetchRes.error());
            return;
        }

        const auto parseRes = parseRelease(fetchRes.value());
        if (!parseRes.ok()) {
            qCWarning(lcButler, "Failed to parse release %s for album %lld: %s",
                qPrintable(m_candidateIds.at(m_candidateIndex)), m_input.albumId,
                qPrintable(parseRes.error().toString()));
        } else {
            m_fetchedReleases.append(parseRes.value());
        }

        ++m_candidateIndex;
        fetchNextCandidate();
    }

    void evaluateMatches()
    {
        if (m_fetchedReleases.isEmpty()) {
            const MbAlbumResult result { .status = MbMatchStatus::NoMatch };
            const MbMatchSource source(m_db);
            const auto saveRes = source.saveMatch(m_input.albumId, result, m_clock.nowMs());
            if (!saveRes.ok()) {
                finish(saveRes.error());
                return;
            }
            qCInfo(lcButler, "MB match album %lld: status=no_match, score=0.00, proposals=0",
                m_input.albumId);
            finish({ });
            return;
        }

        const LocalAlbum local = m_input.toLocalAlbum();
        const MatchDecision decision = decide(local, m_fetchedReleases);

        if (!decision.best.has_value()) {
            const MbAlbumResult result { .status = MbMatchStatus::NoMatch };
            const MbMatchSource source(m_db);
            const auto saveRes = source.saveMatch(m_input.albumId, result, m_clock.nowMs());
            if (!saveRes.ok()) {
                finish(saveRes.error());
                return;
            }
            qCInfo(lcButler, "MB match album %lld: status=no_match, score=0.00, proposals=0",
                m_input.albumId);
            finish({ });
            return;
        }

        std::optional<MbRelease> bestRelease;
        for (const auto &rel : m_fetchedReleases) {
            if (rel.id == decision.best->releaseId) {
                bestRelease = rel;
                break;
            }
        }

        if (decision.ambiguous) {
            MbAlbumResult result;
            result.status = MbMatchStatus::Ambiguous;
            result.release = bestRelease;
            result.match = decision.best;

            const MbMatchSource source(m_db);
            const auto saveRes = source.saveMatch(m_input.albumId, result, m_clock.nowMs());
            if (!saveRes.ok()) {
                finish(saveRes.error());
                return;
            }
            qCInfo(lcButler, "MB match album %lld: status=ambiguous, score=%.2f, proposals=0",
                m_input.albumId, decision.best->score);
            finish({ });
            return;
        }

        MbAlbumResult result;
        result.status = MbMatchStatus::Matched;
        result.release = bestRelease;
        result.match = decision.best;

        const MbMatchSource source(m_db);
        const auto saveRes = source.saveMatch(m_input.albumId, result, m_clock.nowMs());
        if (!saveRes.ok()) {
            finish(saveRes.error());
            return;
        }

        int proposalCount = 0;
        if (bestRelease.has_value()) {
            const auto proposals = buildProposals(m_input, *bestRelease, *decision.best);
            proposalCount = static_cast<int>(proposals.size());
            if (!proposals.isEmpty()) {
                library::CorrectionStore corrStore(m_db, m_clock);
                const auto addRes = corrStore.addProposals(m_batchId, proposals, m_autoAccept);
                if (!addRes.ok()) {
                    finish(addRes.error());
                    return;
                }
            }
        }

        qCInfo(lcButler, "MB match album %lld: status=matched, score=%.2f, proposals=%d",
            m_input.albumId, decision.best->score, proposalCount);
        finish({ });
    }

    library::Database &m_db;
    MusicBrainzClient &m_mb;
    const core::Clock &m_clock;
    qint64 m_batchId = 0;
    std::optional<double> m_autoAccept;
    MbAlbumInput m_input;
    std::function<void(const core::Result<void> &)> m_done;

    QStringList m_candidateIds;
    int m_candidateIndex = 0;
    QList<MbRelease> m_fetchedReleases;
    std::unique_ptr<MbFetch> m_currentFetch;
    bool m_finished = false;
};

} // namespace

MbMatchJobHandler::MbMatchJobHandler(
    library::Database &db, MusicBrainzClient &mb, const core::Clock &clock)
    : m_db(db)
    , m_mb(mb)
    , m_clock(clock)
{
}

QString MbMatchJobHandler::kind() const
{
    return QStringLiteral("butler.mb_match");
}

int MbMatchJobHandler::maxInFlight() const
{
    return 1;
}

ai::TokenUsage MbMatchJobHandler::estimate(const QString &itemKey, const QJsonObject &params) const
{
    Q_UNUSED(itemKey);
    Q_UNUSED(params);
    return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
}

std::unique_ptr<QObject> MbMatchJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    bool ok = false;
    const qint64 albumId = itemKey.toLongLong(&ok);
    if (!ok || albumId <= 0) {
        done(core::Error {
            .code = QString(errc::kMbMatchInvalidKey),
            .message = QStringLiteral("Invalid albumId in itemKey"),
            .detail = itemKey,
        });
        return nullptr;
    }

    const qint64 batchId = params.value(QStringLiteral("batchId")).toInteger(0);
    if (batchId <= 0) {
        done(core::Error {
            .code = QString(errc::kMbMatchInvalidKey),
            .message = QStringLiteral("Missing or invalid batchId in params"),
            .detail = QString::number(batchId),
        });
        return nullptr;
    }

    std::optional<double> autoAcceptThreshold;
    if (params.contains(QStringLiteral("autoAccept"))
        && !params.value(QStringLiteral("autoAccept")).isNull()) {
        autoAcceptThreshold = params.value(QStringLiteral("autoAccept")).toDouble();
    }

    const MbMatchSource source(m_db);
    const auto loadRes = source.load(albumId);
    if (!loadRes.ok()) {
        done(loadRes.error());
        return nullptr;
    }

    auto input = loadRes.value();
    if (input.tracks.isEmpty()) {
        qCInfo(lcButler, "MB match album %lld: no tracks, skipped", albumId);
        done({ });
        return nullptr;
    }

    auto task = std::make_unique<MbMatchTask>(
        m_db, m_mb, m_clock, batchId, autoAcceptThreshold, std::move(input), std::move(done));
    task->start();
    return task;
}

} // namespace linernotes::butler

#include "MbMatchJobHandler.moc"
