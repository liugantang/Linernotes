// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <core/Result.h>

#include <functional>

namespace linernotes::ai {

/// 异步的密钥存取接口。account 为服务 id。回调在调用线程的事件循环中执行；
/// context 被销毁则不再回调。读取不存在的 account 返回空串（不是错误）。
class SecretStore {
public:
    virtual ~SecretStore() = default;
    Q_DISABLE_COPY_MOVE(SecretStore)

    using ReadDone = std::function<void(core::Result<QString>)>;
    using WriteDone = std::function<void(core::Result<void>)>;

    SecretStore() = default;

    virtual void read(const QString &account, QObject *context, ReadDone done) = 0;
    virtual void write(
        const QString &account, const QString &secret, QObject *context, WriteDone done) = 0;
    virtual void remove(const QString &account, QObject *context, WriteDone done) = 0;
};

/// 仅内存，进程结束即丢失。用于密钥环不可用时的退化，也用于测试。
class MemorySecretStore final : public SecretStore {
public:
    MemorySecretStore() = default;
    ~MemorySecretStore() override = default;
    Q_DISABLE_COPY_MOVE(MemorySecretStore)

    void read(const QString &account, QObject *context, ReadDone done) override;
    void write(
        const QString &account, const QString &secret, QObject *context, WriteDone done) override;
    void remove(const QString &account, QObject *context, WriteDone done) override;

private:
    QHash<QString, QString> m_secrets;
};

} // namespace linernotes::ai
