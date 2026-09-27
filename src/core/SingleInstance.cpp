// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QLocalSocket>
#include <QLockFile>

#include <core/Logging.h>

#include <memory>

namespace linernotes::core {

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1);
    const QString hexHash = QString::fromUtf8(hash.toHex().left(16));
    m_serverName = QStringLiteral("linernotes-%1").arg(hexHash);
    m_lockFilePath = QDir(QDir::tempPath()).filePath(m_serverName + QStringLiteral(".lock"));

    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
}

SingleInstance::~SingleInstance()
{
    if (m_server.isListening()) {
        m_server.close();
        QLocalServer::removeServer(m_serverName);
    }
}

Result<SingleInstance::Role> SingleInstance::start()
{
    // 1. 先连接已有服务器，连上 → Secondary
    {
        QLocalSocket socket;
        socket.connectToServer(m_serverName);
        if (socket.waitForConnected(200)) {
            socket.disconnectFromServer();
            return Role::Secondary;
        }
    }

    // 2. 连不上 → 用 QLockFile 保护，removeServer 清理残留后 listen → Primary
    QLockFile lockFile(m_lockFilePath);
    lockFile.setStaleLockTime(5000);
    if (lockFile.tryLock(500)) {
        QLocalServer::removeServer(m_serverName);
        if (!m_server.listen(m_serverName)) {
            lockFile.unlock();
            return Error {
                .code = QStringLiteral("single_instance.listen"),
                .message = QStringLiteral("Failed to listen on local server"),
                .detail = m_server.errorString(),
            };
        }
        lockFile.unlock();
        return Role::Primary;
    }

    // 3. 竞争失败时再连一次；都失败返回错误
    {
        QLocalSocket socket;
        socket.connectToServer(m_serverName);
        if (socket.waitForConnected(1000)) {
            socket.disconnectFromServer();
            return Role::Secondary;
        }
        return Error {
            .code = QStringLiteral("single_instance.start"),
            .message
            = QStringLiteral("Failed to start single instance server or connect to primary"),
            .detail = socket.errorString(),
        };
    }
}

Result<void> SingleInstance::sendMessage(const QStringList &args)
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(1000)) {
        return Error {
            .code = QStringLiteral("single_instance.connect"),
            .message = QStringLiteral("Failed to connect to primary instance"),
            .detail = socket.errorString(),
        };
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    constexpr quint8 kVersion = 1;
    stream << kVersion << args;

    if (socket.write(buffer) == -1) {
        return Error {
            .code = QStringLiteral("single_instance.write"),
            .message = QStringLiteral("Failed to write to primary instance"),
            .detail = socket.errorString(),
        };
    }

    if (!socket.waitForBytesWritten(1000)) {
        return Error {
            .code = QStringLiteral("single_instance.timeout"),
            .message = QStringLiteral("Timeout waiting for message to be written"),
            .detail = socket.errorString(),
        };
    }

    socket.disconnectFromServer();
    if (socket.state() == QLocalSocket::ConnectedState) {
        socket.waitForDisconnected(1000);
    }

    return { };
}

void SingleInstance::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *client = m_server.nextPendingConnection();
        if (client == nullptr) {
            continue;
        }

        auto buffer = std::make_shared<QByteArray>();
        connect(client, &QLocalSocket::readyRead, client,
            [client, buffer]() { buffer->append(client->readAll()); });
        connect(client, &QLocalSocket::disconnected, this, [this, client, buffer]() {
            buffer->append(client->readAll());
            if (!buffer->isEmpty()) {
                QDataStream stream(*buffer);
                stream.setVersion(QDataStream::Qt_6_8);
                quint8 version = 0;
                stream >> version;
                if (version != 1) {
                    qCWarning(lcCore, "SingleInstance: unsupported message version %u",
                        static_cast<unsigned int>(version));
                } else {
                    QStringList args;
                    stream >> args;
                    if (stream.status() != QDataStream::Ok) {
                        qCWarning(lcCore, "SingleInstance: failed to deserialize message");
                    } else {
                        emit messageReceived(args);
                    }
                }
            }
            client->deleteLater();
        });
    }
}

} // namespace linernotes::core
