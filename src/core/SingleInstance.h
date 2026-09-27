// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <core/Result.h>

#include <cstdint>

namespace linernotes::core {

class SingleInstance : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SingleInstance)

public:
    /// key 用于区分实例：调用方传入数据目录的绝对路径，内部取其 SHA-1 前 16 位十六进制拼成
    /// 服务名 "linernotes-<hash>"（不同 LINERNOTES_HOME 互不影响，测试与截图脚本可以和日常
    /// 使用的实例并存）。
    explicit SingleInstance(const QString &key, QObject *parent = nullptr);
    ~SingleInstance() override;

    enum class Role : std::uint8_t { Primary, Secondary };

    /// 尝试成为主实例：先连接已有服务器，连上 → Secondary；连不上 → 用 QLockFile（放在
    /// QDir::tempPath() 下，同名 + ".lock"）保护，removeServer 清理残留后 listen → Primary。
    /// 竞争失败时再连一次；都失败返回错误。
    Result<Role> start();

    /// Secondary 用：把消息发给主实例，等待写完（超时 1 s）。
    Result<void> sendMessage(const QStringList &args);

signals:
    /// Primary 收到其他实例的消息
    void messageReceived(const QStringList &args);

private:
    void onNewConnection();

    QString m_serverName;
    QString m_lockFilePath;
    QLocalServer m_server;
};

} // namespace linernotes::core
