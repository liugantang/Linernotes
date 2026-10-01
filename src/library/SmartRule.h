// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariant>
#include <Qt>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {

struct SmartCondition {
    Q_GADGET
    Q_PROPERTY(linernotes::library::SmartField field MEMBER field)
    Q_PROPERTY(linernotes::library::SmartOp op MEMBER op)
    Q_PROPERTY(QVariant value MEMBER value)
    Q_PROPERTY(QVariant value2 MEMBER value2)
public:
    SmartField field = SmartField::Title;
    SmartOp op = SmartOp::Contains;
    QVariant value; // 文本：QString；数值/天数：数字；Between 的下界
    QVariant value2; // 仅 Between 的上界（含）
    bool operator==(const SmartCondition &) const = default;
};

struct SmartRule {
    Q_GADGET
    Q_PROPERTY(linernotes::library::SmartMatch match MEMBER match)
    Q_PROPERTY(QList<linernotes::library::SmartCondition> conditions MEMBER conditions)
    Q_PROPERTY(linernotes::library::TrackSortKey sortKey MEMBER sortKey)
    Q_PROPERTY(Qt::SortOrder sortOrder MEMBER sortOrder)
    Q_PROPERTY(int limit READ getLimit WRITE setLimit)
    Q_PROPERTY(QString playedFrom READ getPlayedFrom WRITE setPlayedFrom)
    Q_PROPERTY(QString playedTo READ getPlayedTo WRITE setPlayedTo)
public:
    SmartMatch match = SmartMatch::All;
    QList<SmartCondition> conditions; // 为空表示匹配全部曲目
    TrackSortKey sortKey = TrackSortKey::Default;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    std::optional<int> limit; // 例：“最近添加的 50 首”= DateAdded 降序 + limit 50
    std::optional<QDate> playedFrom;
    std::optional<QDate> playedTo;
    bool operator==(const SmartRule &) const = default;

    [[nodiscard]] int getLimit() const { return limit.value_or(0); }
    void setLimit(int v) { limit = (v > 0) ? std::optional<int>(v) : std::nullopt; }

    [[nodiscard]] QString getPlayedFrom() const
    {
        return playedFrom.has_value() ? playedFrom->toString(Qt::ISODate) : QString();
    }
    void setPlayedFrom(const QString &v)
    {
        const QString trimmed = v.trimmed();
        if (trimmed.isEmpty()) {
            playedFrom = std::nullopt;
            return;
        }
        const QDate d = QDate::fromString(trimmed, Qt::ISODate);
        playedFrom = d.isValid() ? std::optional<QDate>(d) : std::nullopt;
    }

    [[nodiscard]] QString getPlayedTo() const
    {
        return playedTo.has_value() ? playedTo->toString(Qt::ISODate) : QString();
    }
    void setPlayedTo(const QString &v)
    {
        const QString trimmed = v.trimmed();
        if (trimmed.isEmpty()) {
            playedTo = std::nullopt;
            return;
        }
        const QDate d = QDate::fromString(trimmed, Qt::ISODate);
        playedTo = d.isValid() ? std::optional<QDate>(d) : std::nullopt;
    }

    [[nodiscard]] QString toJson() const; // 存入 playlists.rule
    [[nodiscard]] QJsonObject toJsonObject() const;
    static core::Result<SmartRule> fromJson(const QString &json);
    static core::Result<SmartRule> fromJsonObject(const QJsonObject &root);
    [[nodiscard]] core::Result<void> validate() const;
};

/// 该字段允许的运算符（UI 编辑器据此填下拉框；fromJson 据此校验）
QList<SmartOp> smartOpsFor(SmartField field);

/// 返回指定字段的类型，用于 UI 根据类型渲染不同输入控件
SmartFieldKind smartFieldKind(SmartField field);

} // namespace linernotes::library
