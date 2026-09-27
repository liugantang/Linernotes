// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "FakeLlmServer.h"

#include "TestSupport.h"

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>

namespace {

// 每写一块回到事件循环一次，让客户端收到多次 readyRead。
void writeChunksFrom(QTcpSocket *socket, const QList<QByteArray> &chunks, qsizetype index)
{
    QTimer::singleShot(0, socket, [socket, chunks, index]() {
        if (socket->state() == QAbstractSocket::UnconnectedState) {
            return;
        }
        if (index >= chunks.size()) {
            socket->disconnectFromHost();
            return;
        }
        socket->write(chunks.at(index));
        socket->flush();
        writeChunksFrom(socket, chunks, index + 1);
    });
}

} // namespace

namespace linernotes::test {

FakeLlmServer::FakeLlmServer()
    : m_server(new QTcpServer(this))
{
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        qFatal("FakeLlmServer failed to listen: %s", qPrintable(m_server->errorString()));
    }
    connect(m_server, &QTcpServer::newConnection, this, &FakeLlmServer::onNewConnection);
}

FakeLlmServer::~FakeLlmServer()
{
    if (m_server != nullptr) {
        m_server->close();
    }
}

QUrl FakeLlmServer::baseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server->serverPort()));
}

void FakeLlmServer::enqueue(Response response)
{
    m_responses.append(std::move(response));
}

QList<FakeLlmServer::Request> FakeLlmServer::requests() const
{
    return m_requests;
}

FakeLlmServer::Fixture FakeLlmServer::loadFixture(const QString &name)
{
    const QString path = fixturePath(QStringLiteral("llm/") + name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("FakeLlmServer::loadFixture: failed to open fixture file %s", qPrintable(path));
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        qFatal("FakeLlmServer::loadFixture: failed to parse JSON in %s: %s", qPrintable(path),
            qPrintable(parseErr.errorString()));
    }

    const QJsonObject root = doc.object();
    if (!root.value(QStringLiteral("request")).isObject()
        || !root.value(QStringLiteral("response")).isObject()) {
        qFatal("FakeLlmServer::loadFixture: invalid fixture structure in %s", qPrintable(path));
    }

    Fixture fixture;
    fixture.request = root.value(QStringLiteral("request")).toObject();

    const QJsonObject respObj = root.value(QStringLiteral("response")).toObject();
    fixture.response.status = respObj.value(QStringLiteral("status")).toInt(200);
    fixture.response.contentType = respObj.value(QStringLiteral("contentType"))
                                       .toString(QStringLiteral("application/json"))
                                       .toUtf8();

    const QJsonArray headersArr = respObj.value(QStringLiteral("headers")).toArray();
    for (const auto &hVal : headersArr) {
        if (hVal.isArray()) {
            const QJsonArray pairArr = hVal.toArray();
            if (pairArr.size() == 2) {
                fixture.response.headers.append(qMakePair(
                    pairArr.at(0).toString().toUtf8(), pairArr.at(1).toString().toUtf8()));
            }
        }
    }

    const QJsonArray chunksArr = respObj.value(QStringLiteral("chunks")).toArray();
    for (const auto &cVal : chunksArr) {
        fixture.response.chunks.append(cVal.toString().toUtf8());
    }

    return fixture;
}

FakeLlmServer::Response FakeLlmServer::sse(const QList<QByteArray> &dataPayloads)
{
    Response r;
    r.status = 200;
    r.contentType = "text/event-stream";
    r.headers.append(qMakePair(QByteArray("Cache-Control"), QByteArray("no-cache")));
    for (const auto &payload : dataPayloads) {
        r.chunks.append("data: " + payload + "\n\n");
    }
    r.chunks.append("data: [DONE]\n\n");
    return r;
}

FakeLlmServer::Response FakeLlmServer::json(const QJsonObject &body, int status)
{
    Response r;
    r.status = status;
    r.contentType = "application/json";
    r.chunks.append(QJsonDocument(body).toJson(QJsonDocument::Compact));
    return r;
}

void FakeLlmServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(
            socket, &QTcpSocket::readyRead, this, [this, socket]() { onSocketReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_socketBuffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void FakeLlmServer::onSocketReadyRead(QTcpSocket *socket)
{
    auto it = m_socketBuffers.find(socket);
    if (it == m_socketBuffers.end()) {
        it = m_socketBuffers.insert(socket, QByteArray());
    }
    QByteArray &buf = it.value();
    buf.append(socket->readAll());

    qsizetype headerEnd = buf.indexOf("\r\n\r\n");
    qsizetype delimiterLen = 4;
    if (headerEnd == -1) {
        headerEnd = buf.indexOf("\n\n");
        delimiterLen = 2;
    }
    if (headerEnd == -1) {
        return;
    }

    const QByteArray headerData = buf.left(headerEnd);
    const QList<QByteArray> headerLines = headerData.split('\n');
    if (headerLines.isEmpty()) {
        return;
    }

    const QByteArray requestLine = headerLines.first().trimmed();
    const QList<QByteArray> requestLineParts = requestLine.split(' ');
    if (requestLineParts.size() < 2) {
        return;
    }

    Request req;
    req.method = requestLineParts.at(0);
    req.path = requestLineParts.at(1);

    for (qsizetype i = 1; i < headerLines.size(); ++i) {
        const QByteArray line = headerLines.at(i).trimmed();
        const qsizetype colonIdx = line.indexOf(':');
        if (colonIdx != -1) {
            const QByteArray key = line.left(colonIdx).trimmed().toLower();
            const QByteArray val = line.mid(colonIdx + 1).trimmed();
            req.headers.insert(key, val);
        }
    }

    const qsizetype contentLength = req.headers.value("content-length").toInt();
    const qsizetype totalRequired = headerEnd + delimiterLen + contentLength;
    if (buf.size() < totalRequired) {
        return;
    }

    req.body = buf.mid(headerEnd + delimiterLen, contentLength);
    buf.remove(0, totalRequired);

    m_requests.append(req);
    processRequest(socket, req);
}

void FakeLlmServer::processRequest(QTcpSocket *socket, const Request & /*req*/)
{
    Response resp;
    if (!m_responses.isEmpty()) {
        resp = m_responses.takeFirst();
    } else {
        resp.status = 500;
        resp.contentType = "application/json";
        resp.chunks.append(R"({"error":"No response queued"})");
    }

    if (resp.hang) {
        return;
    }

    QByteArray statusReason = "OK";
    if (resp.status == 401) {
        statusReason = "Unauthorized";
    } else if (resp.status == 403) {
        statusReason = "Forbidden";
    } else if (resp.status == 404) {
        statusReason = "Not Found";
    } else if (resp.status == 429) {
        statusReason = "Too Many Requests";
    } else if (resp.status == 500) {
        statusReason = "Internal Server Error";
    } else if (resp.status == 503) {
        statusReason = "Service Unavailable";
    }

    QByteArray header = "HTTP/1.1 " + QByteArray::number(resp.status) + " " + statusReason + "\r\n";
    header += "Content-Type: " + resp.contentType + "\r\n";
    header += "Connection: close\r\n";
    for (const auto &h : resp.headers) {
        header += h.first + ": " + h.second + "\r\n";
    }
    header += "\r\n";

    if (resp.chunks.isEmpty()) {
        socket->write(header);
        socket->disconnectFromHost();
        return;
    }

    if (resp.chunks.size() == 1) {
        socket->write(header + resp.chunks.first());
        socket->disconnectFromHost();
        return;
    }

    // Multi-chunk response
    socket->write(header);
    socket->flush();

    writeChunksFrom(socket, resp.chunks, 0);
}

} // namespace linernotes::test
