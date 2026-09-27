// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <ai/AiEnums.h>
#include <ai/StructuredOutput.h>
#include <core/Settings.h>

#include <optional>

namespace linernotes::ai {

struct ServiceProfile {
    QString id; // 创建时生成（QUuid::createUuid().toString(QUuid::WithoutBraces)），不变
    QString name; // 显示名
    QUrl baseUrl;
    QString defaultModel;
    int timeoutMs = 60000;
    std::optional<Capabilities> capabilities; // 未探测为空
    int maxConcurrent = 2; // 同时进行的请求数上限，≥1
    int requestsPerMinute = 0; // 0 = 不限
    bool operator==(const ServiceProfile &) const = default;
};

struct PurposeRoute {
    QString serviceId; // 空 = 默认服务
    QString model; // 空 = 所用服务的 defaultModel
    bool operator==(const PurposeRoute &) const = default;
};

struct ResolvedService {
    ServiceProfile profile;
    QString model; // 实际使用的模型名
};

/// AI 服务配置，持久化在 core::Settings 中。只在主线程使用。
/// 假设自身是这些设置键（ai/services、ai/defaultService、ai/routes）的唯一写入者，构造时一次性加载到内存。
/// 存储格式：`ai/services` = JSON 数组文本（ServiceProfile，不含 Key）；`ai/defaultService` = id；
/// `ai/routes` = JSON 对象文本 { "<purposeName>": {"service": id, "model": name} }。
/// 读取时容忍损坏的 JSON（当作空并打 warning），不崩溃。
class AiConfig : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AiConfig)

public:
    explicit AiConfig(core::Settings &settings, QObject *parent = nullptr);
    ~AiConfig() override = default;

    QList<ServiceProfile> services() const;
    std::optional<ServiceProfile> service(const QString &id) const;
    /// 按 id 新增或替换；id 为空时生成新 id。返回最终 id。
    /// 这是第一个服务时自动设为默认服务。
    QString saveService(ServiceProfile profile);
    /// 删除服务：若是默认服务，默认改为剩下的第一个（没有则为空）；指向它的 route 清空为默认。
    void removeService(const QString &id);
    void setCapabilities(const QString &id, const Capabilities &caps);

    QString defaultServiceId() const;
    void setDefaultServiceId(const QString &id); // id 不存在时忽略

    PurposeRoute route(Purpose purpose) const;
    void setRoute(Purpose purpose, const PurposeRoute &route);

    /// route → 服务（route 指向的服务不存在时回退到默认服务）→ 模型（route.model 或
    /// profile.defaultModel）。 没有任何可用服务、或最终模型名为空时返回 nullopt（此时 AI
    /// 功能不可用，播放器照常工作）。
    std::optional<ResolvedService> resolve(Purpose purpose) const;

signals:
    void changed(); // 任何配置变化后发出一次

private:
    void load();
    void persist();

    core::Settings &m_settings;
    QList<ServiceProfile> m_services;
    QString m_defaultServiceId;
    QHash<Purpose, PurposeRoute> m_routes;
};

} // namespace linernotes::ai
