// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

namespace linernotes::test {

class FakeLlmServer : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FakeLlmServer)
public:
    struct Response {
        int status = 200;
        QByteArray contentType = "application/json";
        QList<QPair<QByteArray, QByteArray>> headers; // 额外响应头，如 Retry-After
        QList<QByteArray> chunks; // 依次写出；SSE 用多块模拟增量；只有一块时就是普通响应体
        bool hang = false; // true：读完请求后不回应（测超时）
    };

    struct Request {
        QByteArray method;
        QByteArray path;
        QMap<QByteArray, QByteArray> headers; // 小写键
        QByteArray body;
    };

    struct Fixture {
        QJsonObject request; // 录制时的请求体
        Response response;
    };

    FakeLlmServer(); // 构造即监听；失败 qFatal
    ~FakeLlmServer() override;

    QUrl baseUrl() const; // http://127.0.0.1:<port>/v1
    void enqueue(Response response); // 按请求到达顺序依次使用；队列空时回 500
    QList<Request> requests() const;

    /// 读取 tests/fixtures/llm/ 下的文件（参数为相对 llm/ 的文件名）；文件缺失或格式不对时 qFatal。
    static Fixture loadFixture(const QString &name);

    static Response sse(const QList<QByteArray>
            &dataPayloads); // 把每个 payload 包成 "data: ...\n\n" 一块，最后追加 "data: [DONE]\n\n"
    static Response json(const QJsonObject &body, int status = 200);

private:
    void onNewConnection();
    void onSocketReadyRead(QTcpSocket *socket);
    void processRequest(QTcpSocket *socket, const Request &req);

    QTcpServer *m_server = nullptr;
    QList<Response> m_responses;
    QList<Request> m_requests;
    QMap<QTcpSocket *, QByteArray> m_socketBuffers;
};

} // namespace linernotes::test
