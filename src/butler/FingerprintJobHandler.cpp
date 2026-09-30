// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "FingerprintJobHandler.h"

#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent>

#include <audio/Fingerprint.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/FingerprintStore.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

namespace {

constexpr int kMaxFingerprintThreads = 4;

class FingerprintJobWorker final : public QObject {
    Q_OBJECT
public:
    FingerprintJobWorker(library::Database &db, const core::Clock &clock, qint64 fileId,
        const QFuture<core::Result<audio::RawFingerprint>> &future,
        std::function<void(const core::Result<void> &)> done)
        : m_db(db)
        , m_clock(clock)
        , m_fileId(fileId)
        , m_done(std::move(done))
    {
        connect(&m_watcher, &QFutureWatcher<core::Result<audio::RawFingerprint>>::finished, this,
            &FingerprintJobWorker::onFinished);
        m_watcher.setFuture(future);
    }

    ~FingerprintJobWorker() override
    {
        m_done = nullptr;
        m_watcher.disconnect(this);
    }

    Q_DISABLE_COPY_MOVE(FingerprintJobWorker)

private slots:
    void onFinished()
    {
        if (!m_done) {
            return;
        }
        const auto computeRes = m_watcher.result();
        library::FingerprintStore store(m_db, m_clock);

        if (computeRes.ok()) {
            const auto &fp = computeRes.value();
            const auto saveRes = store.save(m_fileId, fp.algorithm, fp.items);
            if (!saveRes.ok()) {
                auto done = std::move(m_done);
                m_done = nullptr;
                done(saveRes);
                return;
            }
        } else {
            const auto saveRes = store.saveFailure(m_fileId, computeRes.error().message);
            if (!saveRes.ok()) {
                auto done = std::move(m_done);
                m_done = nullptr;
                done(saveRes);
                return;
            }
        }

        auto done = std::move(m_done);
        m_done = nullptr;
        done({ });
    }

private:
    library::Database &m_db;
    const core::Clock &m_clock;
    qint64 m_fileId;
    std::function<void(const core::Result<void> &)> m_done;
    QFutureWatcher<core::Result<audio::RawFingerprint>> m_watcher;
};

} // namespace

FingerprintJobHandler::FingerprintJobHandler(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
    // 瓶颈是读文件而不是 CPU：曲库常在网络共享上，并发读多了反而更慢（实测 SMB 上 48
    // 路并发不如串行）
    m_pool.setMaxThreadCount(
        std::clamp(QThread::idealThreadCount() / 2, 1, kMaxFingerprintThreads));
}

FingerprintJobHandler::~FingerprintJobHandler()
{
    m_pool.waitForDone();
}

QString FingerprintJobHandler::kind() const
{
    return QStringLiteral("butler.fingerprint");
}

int FingerprintJobHandler::maxInFlight() const
{
    return m_pool.maxThreadCount();
}

ai::TokenUsage FingerprintJobHandler::estimate(
    const QString & /*itemKey*/, const QJsonObject & /*params*/) const
{
    return ai::TokenUsage {
        .promptTokens = 0,
        .completionTokens = 0,
    };
}

std::unique_ptr<QObject> FingerprintJobHandler::process(const QString &itemKey,
    const QJsonObject & /*params*/, std::function<void(const core::Result<void> &)> done)
{
    bool ok = false;
    const qint64 fileId = itemKey.toLongLong(&ok);
    if (!ok || fileId <= 0) {
        done(core::Error {
            .code = QString(errc::kFingerprintInvalidKey),
            .message = QStringLiteral("Invalid fileId in itemKey"),
            .detail = itemKey,
        });
        return nullptr;
    }

    const library::FingerprintStore store(m_db, m_clock);
    const auto pathRes = store.filePath(fileId);
    if (!pathRes.ok()) {
        done(pathRes.error());
        return nullptr;
    }

    const QString &path = pathRes.value();
    auto future
        = QtConcurrent::run(&m_pool, [path]() { return audio::Fingerprinter::computeFile(path); });

    return std::make_unique<FingerprintJobWorker>(m_db, m_clock, fileId, future, std::move(done));
}

} // namespace linernotes::butler

#include "FingerprintJobHandler.moc"
