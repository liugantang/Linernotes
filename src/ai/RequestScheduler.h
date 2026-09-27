// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <core/Clock.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QTimer;

namespace linernotes::ai {

/// 按服务 id 控制并发与速率。只在主线程使用。
class RequestScheduler : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(RequestScheduler)

public:
    /// 返回的 Ticket 销毁时：若尚未放行则从队列移除；若已放行则归还并发名额（RAII）。
    class Ticket {
    public:
        ~Ticket();
        Q_DISABLE_COPY_MOVE(Ticket)

    private:
        friend class RequestScheduler;
        struct State;
        explicit Ticket(std::shared_ptr<State> state, QPointer<RequestScheduler> scheduler);

        std::shared_ptr<State> m_state;
        QPointer<RequestScheduler> m_scheduler;
    };

    explicit RequestScheduler(const core::Clock &clock, QObject *parent = nullptr);
    ~RequestScheduler() override = default;

    /// 排队申请一个请求名额。满足“并发数 < maxConcurrent、令牌桶有令牌、服务未暂停”时
    /// 调用 granted（异步，在事件循环中）。按申请顺序 FIFO 放行。
    std::unique_ptr<Ticket> acquire(const QString &serviceId, int maxConcurrent,
        int requestsPerMinute, std::function<void()> granted);

    /// 429 时调用：该服务在 durationMs 内不放行新请求（已放行的不受影响）。
    void pauseService(const QString &serviceId, qint64 durationMs);

private:
    struct QueuedRequest {
        quint64 id = 0;
        std::shared_ptr<Ticket::State> state;
        std::function<void()> granted;
    };

    struct ServiceState {
        int maxConcurrent = 2;
        int requestsPerMinute = 0;
        int inFlightCount = 0;
        qint64 pausedUntilMs = 0;
        double tokens = 0.0;
        qint64 lastRefillTimeMs = 0;
        bool initialized = false;
        QList<QueuedRequest> queue;
    };

    static std::optional<qint64> waitMsFor(const ServiceState &service, qint64 nowMs);

    void processQueue(const QString &serviceId);
    void processAllQueues();
    void onTicketDestroyed(const std::shared_ptr<Ticket::State> &state);
    void scheduleNextCheck();

    const core::Clock &m_clock;
    QHash<QString, ServiceState> m_services;
    quint64 m_nextTicketId = 1;
    QTimer *m_timer = nullptr;
};

} // namespace linernotes::ai
