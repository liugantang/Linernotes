// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/JobHandler.h>
#include <core/Clock.h>
#include <core/Result.h>
#include <library/Database.h>

#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

namespace linernotes::ai {

struct JobInfo {
    qint64 id = 0;
    QString kind;
    QString title;
    JobState state = JobState::Paused;
    int total = 0;
    int done = 0;
    int failed = 0;
    QString lastError;
    qint64 createdAt = 0;
    bool operator==(const JobInfo &) const = default;
};

class JobQueue : public QObject {
    Q_OBJECT
public:
    JobQueue(library::Database &db, const core::Clock &clock, QObject *parent = nullptr);
    ~JobQueue() override;
    Q_DISABLE_COPY_MOVE(JobQueue)

    void registerHandler(std::unique_ptr<JobHandler> handler); // 同 kind 重复注册以后者为准

    /// 运行前预估：各项 estimate 之和。kind 未注册返回 kNotConfigured。
    [[nodiscard]] core::Result<TokenUsage> estimate(
        const QString &kind, const QStringList &items, const QJsonObject &params = { }) const;

    /// 写入数据库并立即开始运行，返回 job id。kind 未注册返回 kNotConfigured；items
    /// 为空也允许（直接 Completed）。
    core::Result<qint64> enqueue(const QString &kind, const QString &title,
        const QStringList &items, const QJsonObject &params = { });

    /// 程序启动时调用一次：数据库中 running 的 job 一律改为
    /// paused（重启后不自动花用户的钱，由用户继续）。
    core::Result<void> restore();

    void pause(qint64 jobId); // 不再派发新项；进行中的项正常完成并记录
    void resume(qint64 jobId); // 从 pending 项继续；kind 未注册时保持 paused 并写 last_error
    void cancel(
        qint64 jobId); // 销毁进行中的工作（其结果不记录，对应项保持 pending），状态 cancelled
    void retryFailed(qint64 jobId); // failed 项改回 pending；job 已 completed 则重新 running
    void remove(qint64 jobId); // 先 cancel 再删除（级联删 items）

    [[nodiscard]] QList<JobInfo> jobs() const; // 按 created_at 倒序
    [[nodiscard]] std::optional<JobInfo> job(qint64 jobId) const;

signals:
    void jobChanged(qint64 jobId); // 状态或进度变化

private:
    struct PendingItem {
        int seq = 0;
        QString itemKey;
    };

    void dispatchJob(qint64 jobId);
    void dispatchPendingItems(qint64 jobId, JobHandler *handler, const QJsonObject &params,
        const QList<PendingItem> &pendingItems);
    void handleItemDone(qint64 jobId, int seq, const core::Result<void> &result);
    void updateJobState(qint64 jobId, JobState state, const QString &lastError = { });
    void handleDbError(qint64 jobId, const core::Error &error);

    library::Database &m_db;
    const core::Clock &m_clock;
    std::unordered_map<qint64, std::unordered_map<int, std::unique_ptr<QObject>>> m_inFlight;
    std::map<QString, std::unique_ptr<JobHandler>> m_handlers;
};

} // namespace linernotes::ai
