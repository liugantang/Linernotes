// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QMetaObject>
#include <QTimer>

#include <ai/RequestScheduler.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace linernotes::ai {

namespace {

// 一是 Clock 可注入，测试推进 ManualClock 后不必等真实时长；二是防系统休眠等时钟跳变。
constexpr qint64 kMaxTimerIntervalMs = 250;

} // namespace

struct RequestScheduler::Ticket::State {
    quint64 id = 0;
    QString serviceId;
    bool alive = true;
    bool granted = false;
};

RequestScheduler::Ticket::Ticket(std::shared_ptr<State> state, QPointer<RequestScheduler> scheduler)
    : m_state(std::move(state))
    , m_scheduler(std::move(scheduler))
{
}

RequestScheduler::Ticket::~Ticket()
{
    if (m_state != nullptr) {
        m_state->alive = false;
        if (m_scheduler != nullptr) {
            m_scheduler->onTicketDestroyed(m_state);
        }
    }
}

RequestScheduler::RequestScheduler(const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_clock(clock)
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &RequestScheduler::processAllQueues);
}

std::unique_ptr<RequestScheduler::Ticket> RequestScheduler::acquire(const QString &serviceId,
    int maxConcurrent, int requestsPerMinute, std::function<void()> granted)
{
    const int clampedConcurrent = std::max(1, maxConcurrent);
    const int clampedRpm = std::max(0, requestsPerMinute);
    const qint64 now = m_clock.nowMs();

    auto it = m_services.find(serviceId);
    if (it == m_services.end()) {
        it = m_services.insert(serviceId, ServiceState { });
    }
    ServiceState &service = it.value();

    if (!service.initialized) {
        service.initialized = true;
        service.maxConcurrent = clampedConcurrent;
        service.requestsPerMinute = clampedRpm;
        service.tokens = (clampedRpm > 0) ? clampedRpm : 0.0;
        service.lastRefillTimeMs = now;
    } else {
        if (service.requestsPerMinute > 0) {
            const int oldCapacity = std::max(1, service.requestsPerMinute);
            const qint64 elapsedMs = now - service.lastRefillTimeMs;
            if (elapsedMs > 0) {
                const double tokensToAdd
                    = (static_cast<double>(elapsedMs) * service.requestsPerMinute) / 60000.0;
                service.tokens
                    = std::min(static_cast<double>(oldCapacity), service.tokens + tokensToAdd);
                service.lastRefillTimeMs = now;
            }
        } else if (clampedRpm > 0) {
            service.tokens = clampedRpm;
            service.lastRefillTimeMs = now;
        }
        service.maxConcurrent = clampedConcurrent;
        service.requestsPerMinute = clampedRpm;
        if (clampedRpm > 0) {
            service.tokens = std::min(service.tokens, static_cast<double>(clampedRpm));
        }
    }

    const quint64 ticketId = m_nextTicketId++;
    auto state = std::make_shared<Ticket::State>();
    state->id = ticketId;
    state->serviceId = serviceId;
    state->alive = true;
    state->granted = false;

    QueuedRequest req;
    req.id = ticketId;
    req.state = state;
    req.granted = std::move(granted);
    service.queue.append(std::move(req));

    auto ticket = std::unique_ptr<Ticket>(new Ticket(state, this));

    processQueue(serviceId);

    return ticket;
}

void RequestScheduler::pauseService(const QString &serviceId, qint64 durationMs)
{
    if (durationMs <= 0) {
        return;
    }
    auto it = m_services.find(serviceId);
    if (it == m_services.end()) {
        it = m_services.insert(serviceId, ServiceState { });
    }
    ServiceState &service = it.value();
    const qint64 until = m_clock.nowMs() + durationMs;
    service.pausedUntilMs = std::max(service.pausedUntilMs, until);

    scheduleNextCheck();
}

std::optional<qint64> RequestScheduler::waitMsFor(const ServiceState &service, qint64 nowMs)
{
    if (service.queue.isEmpty()) {
        return std::nullopt;
    }

    qint64 waitMs = 0;
    if (nowMs < service.pausedUntilMs) {
        waitMs = std::max(waitMs, service.pausedUntilMs - nowMs);
    }
    if (service.requestsPerMinute > 0 && service.tokens < 1.0) {
        const double tokensNeeded = 1.0 - service.tokens;
        const double msNeeded
            = (tokensNeeded * 60000.0) / static_cast<double>(service.requestsPerMinute);
        const qint64 tokenWait
            = std::max(static_cast<qint64>(1), static_cast<qint64>(std::ceil(msNeeded)));
        waitMs = std::max(waitMs, tokenWait);
    }
    return waitMs;
}

void RequestScheduler::processQueue(const QString &serviceId)
{
    auto it = m_services.find(serviceId);
    if (it == m_services.end()) {
        return;
    }
    ServiceState &service = it.value();
    const qint64 now = m_clock.nowMs();

    if (now < service.pausedUntilMs) {
        scheduleNextCheck();
        return;
    }

    if (service.requestsPerMinute > 0) {
        const int capacity = std::max(1, service.requestsPerMinute);
        const qint64 elapsedMs = now - service.lastRefillTimeMs;
        if (elapsedMs > 0) {
            const double tokensToAdd
                = (static_cast<double>(elapsedMs) * service.requestsPerMinute) / 60000.0;
            service.tokens = std::min(static_cast<double>(capacity), service.tokens + tokensToAdd);
            service.lastRefillTimeMs = now;
        }
    }

    while (!service.queue.isEmpty()) {
        if (!service.queue.first().state->alive) {
            service.queue.removeFirst();
            continue;
        }

        if (service.inFlightCount >= service.maxConcurrent) {
            break;
        }

        if (service.requestsPerMinute > 0) {
            if (service.tokens < 1.0) {
                break;
            }
            service.tokens -= 1.0;
        }

        QueuedRequest req = service.queue.takeFirst();
        service.inFlightCount++;
        req.state->granted = true;

        QMetaObject::invokeMethod(
            this,
            [state = req.state, granted = std::move(req.granted)]() {
                if (state->alive && granted) {
                    granted();
                }
            },
            Qt::QueuedConnection);
    }

    scheduleNextCheck();
}

void RequestScheduler::processAllQueues()
{
    const auto keys = m_services.keys();
    for (const auto &key : keys) {
        processQueue(key);
    }
}

void RequestScheduler::onTicketDestroyed(const std::shared_ptr<Ticket::State> &state)
{
    if (state == nullptr) {
        return;
    }
    const QString serviceId = state->serviceId;
    auto it = m_services.find(serviceId);
    if (it == m_services.end()) {
        return;
    }
    ServiceState &service = it.value();

    if (!state->granted) {
        for (int i = 0; i < service.queue.size(); ++i) {
            if (service.queue.at(i).id == state->id) {
                service.queue.removeAt(i);
                break;
            }
        }
        scheduleNextCheck();
    } else {
        if (service.inFlightCount > 0) {
            service.inFlightCount--;
        }
        processQueue(serviceId);
    }
}

void RequestScheduler::scheduleNextCheck()
{
    const qint64 now = m_clock.nowMs();
    std::optional<qint64> minWaitMs;

    for (const auto &service : std::as_const(m_services)) {
        const auto wait = waitMsFor(service, now);
        if (!wait.has_value()) {
            continue;
        }
        if (!minWaitMs.has_value() || *wait < *minWaitMs) {
            minWaitMs = wait;
        }
    }

    if (!minWaitMs.has_value()) {
        m_timer->stop();
        return;
    }

    const qint64 wait = *minWaitMs;
    const qint64 interval = (wait > 0)
        ? std::max(static_cast<qint64>(1), std::min(wait, kMaxTimerIntervalMs))
        : kMaxTimerIntervalMs;

    if (!m_timer->isActive()) {
        m_timer->start(static_cast<int>(interval));
    }
}

} // namespace linernotes::ai
