// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <ai/SecretStore.h>

namespace linernotes::ai {

/// QtKeychain 实现（service 名 "linernotes"，key 为 account）。
/// 第一次操作返回“密钥环不可用”类错误（QKeychain::NoBackendAvailable / OtherError 等）时，
/// 打一次 qCWarning，此后所有操作转交内部的 MemorySecretStore（本次运行有效）。
/// 已经写进内存的密钥在读取时也要能读到。
class KeychainSecretStore final : public QObject, public SecretStore {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KeychainSecretStore)

public:
    explicit KeychainSecretStore(QObject *parent = nullptr);
    ~KeychainSecretStore() override = default;

    void read(const QString &account, QObject *context, ReadDone done) override;
    void write(
        const QString &account, const QString &secret, QObject *context, WriteDone done) override;
    void remove(const QString &account, QObject *context, WriteDone done) override;

private:
    void triggerFallback(const QString &reason);
    template <typename JobT, typename FallbackFn, typename FinishedFn>
    void runJob(JobT *job, QObject *context, FallbackFn &&fallbackFn, FinishedFn &&finishedFn);

    MemorySecretStore m_memoryStore;
    QHash<QString, QString> m_cache;
    bool m_fallbackToMemory = false;
    bool m_warnedFallback = false;
};

} // namespace linernotes::ai
