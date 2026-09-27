// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QObject>

#include <ai/ChatTypes.h>
#include <ai/SseParser.h>
#include <core/Result.h>

#include <memory>
#include <optional>

class QNetworkReply;

namespace linernotes::ai {

class LlmClient;

struct DeleteLater {
    void operator()(QObject *obj) const;
};

/// 一次请求的进行中状态。由 LlmClient 创建，调用方持有（std::unique_ptr）。
/// 线程：只在创建它的线程使用。
class LlmReply : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LlmReply)
public:
    ~LlmReply() override; // 未完成时中止网络请求，不再发信号
    bool isFinished() const;
    const core::Result<ChatResponse> &result() const; // isFinished() 后才可调用
    int httpStatus() const; // 未收到响应时为 0
    std::optional<qint64>
    retryAfterMs() const; // 429/503 的 Retry-After（秒数形式），供后续限速使用
    qint64 elapsedMs() const; // 从发出到完成
    void abort(); // 以 errc::kAborted 结束（会发 finished）

signals:
    void delta(const QString &text); // 仅流式：content 增量
    void finished(); // 恰好一次；发出后不再访问成员，槽函数中可以直接销毁本对象

private:
    friend class LlmClient;

    explicit LlmReply(QNetworkReply *reply, QString model, bool stream, QObject *parent = nullptr);

    void onReadyRead();
    void onNetworkFinished();
    void extractResponseDetails();
    void finishWithNetworkOrHttpError();
    void finishWithError(core::Error error);
    void finishWithSuccess(ChatResponse response);
    void releaseNetworkReply(bool abortTransfer);

    std::unique_ptr<QNetworkReply, DeleteLater> m_networkReply;
    QString m_model;
    bool m_stream = false;
    bool m_finished = false;
    bool m_aborted = false;
    bool m_receivedDone = false;
    int m_httpStatus = 0;
    std::optional<qint64> m_retryAfterMs;
    QElapsedTimer m_timer;
    qint64 m_elapsedMs = 0;
    core::Result<ChatResponse> m_result {
        core::Error { }
    }; // 完成前是占位值，result() 断言 m_finished

    SseParser m_sseParser;
    ChatResponse m_accumulatedResponse;
    bool m_hadParseError = false;
    QString m_parseErrorMsg;

    QByteArray m_responseBody;
};

} // namespace linernotes::ai
