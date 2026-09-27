// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QUrl>

#include <ai/AiEnums.h>

namespace linernotes::core {
class Settings;
}

namespace linernotes::ai {

/// 隐私策略关口，控制发往云端 LLM 的用户数据类别。
/// 功能代码预期用法：构造上下文时先用 isAllowedFor 剔除不允许的数据，
/// 再把实际带上的类别填进 LlmCall::dataCategories；LlmService 的检查是兜底，防止漏剔。
class PrivacyGuard : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PrivacyGuard)

public:
    explicit PrivacyGuard(core::Settings &settings, QObject *parent = nullptr);
    ~PrivacyGuard() override = default;

    [[nodiscard]] bool isAllowed(DataCategory category) const; // 用户开关（针对云端）
    void setAllowed(DataCategory category, bool allowed); // 写入 Settings，值变化时发 changed

    /// 本机或局域网服务不受开关限制：数据没有离开用户自己的设备/网络。
    [[nodiscard]] static bool isLocalService(const QUrl &baseUrl);
    [[nodiscard]] bool isAllowedFor(DataCategory category, const QUrl &baseUrl) const;

    /// 返回 categories 中对该服务不允许的类别；为空表示可以发送。
    [[nodiscard]] QList<DataCategory> blocked(
        const QSet<DataCategory> &categories, const QUrl &baseUrl) const;

signals:
    void changed();

private:
    core::Settings &m_settings;
};

} // namespace linernotes::ai
