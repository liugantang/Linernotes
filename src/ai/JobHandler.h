// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <ai/ChatTypes.h>
#include <core/Result.h>

#include <functional>
#include <memory>

namespace linernotes::ai {

/// 由各功能实现，注册给 JobQueue。
class JobHandler {
public:
    JobHandler() = default;
    virtual ~JobHandler() = default;
    JobHandler(const JobHandler &) = delete;
    JobHandler &operator=(const JobHandler &) = delete;
    JobHandler(JobHandler &&) = delete;
    JobHandler &operator=(JobHandler &&) = delete;

    [[nodiscard]] virtual QString kind() const = 0; // 例如 "cleanup.normalize"
    [[nodiscard]] virtual int maxInFlight() const { return 2; } // 同一个 job 同时处理的条目数
    /// 预估一项的 token 用量（运行前展示给用户）。
    [[nodiscard]] virtual TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const = 0;
    /// 异步处理一项，必须恰好调用一次 done（可以在本调用内同步调用）。
    /// 返回的对象代表进行中的工作：JobQueue 在取消时销毁它，handler 应保证销毁即中止且之后不再调用
    /// done。
    virtual std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) = 0;
};

/// 粗略的 token 数估计：CJK 字符每个算 1，其余字符每 4 个算 1（向上取整）。供 handler 实现 estimate
/// 用。
int roughTokenCount(const QString &text);

} // namespace linernotes::ai
