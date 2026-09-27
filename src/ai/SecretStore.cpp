// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QMetaObject>
#include <QPointer>

#include <ai/SecretStore.h>

namespace linernotes::ai {

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

} // namespace

void MemorySecretStore::read(const QString &account, QObject *context, ReadDone done)
{
    const QString secret = m_secrets.value(account, QString());
    invokeAsync(
        context, [done = std::move(done), secret]() { done(core::Result<QString>(secret)); });
}

void MemorySecretStore::write(
    const QString &account, const QString &secret, QObject *context, WriteDone done)
{
    m_secrets.insert(account, secret);
    invokeAsync(context, [done = std::move(done)]() { done(core::Result<void>()); });
}

void MemorySecretStore::remove(const QString &account, QObject *context, WriteDone done)
{
    m_secrets.remove(account);
    invokeAsync(context, [done = std::move(done)]() { done(core::Result<void>()); });
}

} // namespace linernotes::ai
