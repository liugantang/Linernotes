// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QMetaObject>
#include <QPointer>

#include <ai/AiLogging.h>
#include <ai/Errors.h>
#include <ai/KeychainSecretStore.h>
#include <keychain.h>

#include <memory>
#include <utility>

namespace linernotes::ai {

namespace {

core::Error secretStoreError(const QString &detail)
{
    return core::Error {
        .code = QString(errc::kSecretStore),
        .message = QStringLiteral("System keychain error"),
        .detail = detail,
    };
}

} // namespace

namespace {

template <typename F> void invokeAsync(QObject *context, F &&callback)
{
    if (context == nullptr) {
        return;
    }
    const QPointer<QObject> guard(context);
    QMetaObject::invokeMethod(
        context,
        [guard, cb = std::forward<F>(callback)]() {
            if (guard.data() != nullptr) {
                cb();
            }
        },
        Qt::QueuedConnection);
}

bool isFallbackError(QKeychain::Error err)
{
    return err == QKeychain::NoBackendAvailable || err == QKeychain::OtherError;
}

} // namespace

KeychainSecretStore::KeychainSecretStore(QObject *parent)
    : QObject(parent)
{
}

void KeychainSecretStore::triggerFallback(const QString &reason)
{
    m_fallbackToMemory = true;
    if (!m_warnedFallback) {
        m_warnedFallback = true;
        qCWarning(lcAi) << "System keychain unavailable (" << reason
                        << "), falling back to in-memory secret storage for this session";
    }
}

template <typename JobT, typename FallbackFn, typename FinishedFn>
void KeychainSecretStore::runJob(
    JobT *job, QObject *context, FallbackFn &&fallbackFn, FinishedFn &&finishedFn)
{
    const QPointer<QObject> contextGuard(context);
    job->setAutoDelete(true);
    connect(job, &QKeychain::Job::finished, this,
        [this, job, contextGuard, fallbackFn = std::forward<FallbackFn>(fallbackFn),
            finishedFn = std::forward<FinishedFn>(finishedFn)]() mutable {
            if (contextGuard.data() == nullptr) {
                return;
            }
            const auto err = job->error();
            if (isFallbackError(err)) {
                triggerFallback(job->errorString());
                fallbackFn(contextGuard.data());
            } else {
                finishedFn(contextGuard.data());
            }
        });
    job->start();
}

void KeychainSecretStore::read(const QString &account, QObject *context, ReadDone done)
{
    if (m_fallbackToMemory) {
        m_memoryStore.read(account, context, std::move(done));
        return;
    }

    if (context == nullptr) {
        return;
    }

    if (const auto it = m_cache.constFind(account); it != m_cache.constEnd()) {
        invokeAsync(context, [done = std::move(done), secret = it.value()]() {
            done(core::Result<QString>(secret));
        });
        return;
    }

    auto *job = new QKeychain::ReadPasswordJob(QStringLiteral("linernotes"), this);
    job->setKey(account);

    // 两个回调只会执行其一；共享同一个 done，避免一个复制、一个移动时的求值顺序问题。
    auto sharedDone = std::make_shared<ReadDone>(std::move(done));
    runJob(
        job, context,
        [this, account, sharedDone](
            QObject *ctx) mutable { m_memoryStore.read(account, ctx, std::move(*sharedDone)); },
        [this, job, account, sharedDone](QObject *) mutable {
            if (job->error() == QKeychain::NoError) {
                const QString secret = job->textData();
                m_cache.insert(account, secret);
                (*sharedDone)(core::Result<QString>(secret));
            } else if (job->error() == QKeychain::EntryNotFound) {
                (*sharedDone)(core::Result<QString>(QString()));
            } else {
                (*sharedDone)(core::Result<QString>(secretStoreError(job->errorString())));
            }
        });
}

void KeychainSecretStore::write(
    const QString &account, const QString &secret, QObject *context, WriteDone done)
{
    if (m_fallbackToMemory) {
        m_memoryStore.write(account, secret, context, std::move(done));
        return;
    }

    if (context == nullptr) {
        return;
    }

    auto *job = new QKeychain::WritePasswordJob(QStringLiteral("linernotes"), this);
    job->setKey(account);
    job->setTextData(secret);

    // 两个回调只会执行其一；共享同一个 done，避免一个复制、一个移动时的求值顺序问题。
    auto sharedDone = std::make_shared<WriteDone>(std::move(done));
    runJob(
        job, context,
        [this, account, secret, sharedDone](QObject *ctx) mutable {
            m_memoryStore.write(account, secret, ctx, std::move(*sharedDone));
        },
        [this, job, account, secret, sharedDone](QObject *) mutable {
            if (job->error() == QKeychain::NoError) {
                m_cache.insert(account, secret);
                (*sharedDone)(core::Result<void>());
            } else {
                (*sharedDone)(core::Result<void>(secretStoreError(job->errorString())));
            }
        });
}

void KeychainSecretStore::remove(const QString &account, QObject *context, WriteDone done)
{
    if (m_fallbackToMemory) {
        m_memoryStore.remove(account, context, std::move(done));
        return;
    }

    if (context == nullptr) {
        return;
    }

    auto *job = new QKeychain::DeletePasswordJob(QStringLiteral("linernotes"), this);
    job->setKey(account);

    // 两个回调只会执行其一；共享同一个 done，避免一个复制、一个移动时的求值顺序问题。
    auto sharedDone = std::make_shared<WriteDone>(std::move(done));
    runJob(
        job, context,
        [this, account, sharedDone](
            QObject *ctx) mutable { m_memoryStore.remove(account, ctx, std::move(*sharedDone)); },
        [this, job, account, sharedDone](QObject *) mutable {
            if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound) {
                m_cache.remove(account);
                (*sharedDone)(core::Result<void>());
            } else {
                (*sharedDone)(core::Result<void>(secretStoreError(job->errorString())));
            }
        });
}

} // namespace linernotes::ai
