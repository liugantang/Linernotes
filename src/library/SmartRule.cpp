// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SmartRule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaEnum>

#include <library/EnumNames.h>
#include <library/Errors.h>
#include <library/LibraryQuery.h>

namespace linernotes::library {

namespace {

core::Error ruleError(const QString &message, const QString &detail = QString())
{
    return core::Error {
        .code = QString(errc::kPlaylistRuleInvalid),
        .message = message,
        .detail = detail,
    };
}

QString smartFieldName(SmartField f)
{
    switch (f) {
    case SmartField::Title:
        return QStringLiteral("title");
    case SmartField::Artist:
        return QStringLiteral("artist");
    case SmartField::Album:
        return QStringLiteral("album");
    case SmartField::AlbumArtist:
        return QStringLiteral("albumArtist");
    case SmartField::Genre:
        return QStringLiteral("genre");
    case SmartField::Codec:
        return QStringLiteral("codec");
    case SmartField::Year:
        return QStringLiteral("year");
    case SmartField::Rating:
        return QStringLiteral("rating");
    case SmartField::DurationSec:
        return QStringLiteral("durationSec");
    case SmartField::Favorite:
        return QStringLiteral("favorite");
    case SmartField::DateAdded:
        return QStringLiteral("dateAdded");
    }
    return QStringLiteral("title");
}

QString smartOpName(SmartOp op)
{
    switch (op) {
    case SmartOp::Contains:
        return QStringLiteral("contains");
    case SmartOp::NotContains:
        return QStringLiteral("notContains");
    case SmartOp::Is:
        return QStringLiteral("is");
    case SmartOp::IsNot:
        return QStringLiteral("isNot");
    case SmartOp::StartsWith:
        return QStringLiteral("startsWith");
    case SmartOp::Equals:
        return QStringLiteral("equals");
    case SmartOp::NotEquals:
        return QStringLiteral("notEquals");
    case SmartOp::Greater:
        return QStringLiteral("greater");
    case SmartOp::Less:
        return QStringLiteral("less");
    case SmartOp::Between:
        return QStringLiteral("between");
    case SmartOp::IsTrue:
        return QStringLiteral("isTrue");
    case SmartOp::IsFalse:
        return QStringLiteral("isFalse");
    case SmartOp::InLastDays:
        return QStringLiteral("inLastDays");
    case SmartOp::NotInLastDays:
        return QStringLiteral("notInLastDays");
    }
    return QStringLiteral("contains");
}

QString trackSortKeyName(TrackSortKey k)
{
    switch (k) {
    case TrackSortKey::Default:
        return QStringLiteral("default");
    case TrackSortKey::Title:
        return QStringLiteral("title");
    case TrackSortKey::Artist:
        return QStringLiteral("artist");
    case TrackSortKey::Album:
        return QStringLiteral("album");
    case TrackSortKey::Year:
        return QStringLiteral("year");
    case TrackSortKey::Duration:
        return QStringLiteral("duration");
    case TrackSortKey::DateAdded:
        return QStringLiteral("dateAdded");
    case TrackSortKey::PlaylistOrder:
        return QStringLiteral("playlistOrder");
    }
    return QStringLiteral("default");
}

core::Result<SmartCondition> parseCondition(const QJsonObject &cObj)
{
    const auto fieldOpt = detail::enumFromName<SmartField>(
        cObj.value(QStringLiteral("field")).toString(), smartFieldName);
    if (!fieldOpt.has_value()) {
        return ruleError(QStringLiteral("Unknown smart rule field"),
            cObj.value(QStringLiteral("field")).toString());
    }

    const auto opOpt
        = detail::enumFromName<SmartOp>(cObj.value(QStringLiteral("op")).toString(), smartOpName);
    if (!opOpt.has_value()) {
        return ruleError(QStringLiteral("Unknown smart rule operator"),
            cObj.value(QStringLiteral("op")).toString());
    }

    SmartCondition cond;
    cond.field = fieldOpt.value();
    cond.op = opOpt.value();

    if (cond.op == SmartOp::Between) {
        cond.value = cObj.value(QStringLiteral("value")).toVariant();
        cond.value2 = cObj.value(QStringLiteral("value2")).toVariant();
    } else if (cond.op != SmartOp::IsTrue && cond.op != SmartOp::IsFalse) {
        cond.value = cObj.value(QStringLiteral("value")).toVariant();
    }

    return cond;
}

core::Result<QList<SmartCondition>> parseConditions(const QJsonValue &value)
{
    if (!value.isArray()) {
        return ruleError(QStringLiteral("Conditions must be an array"), QString());
    }
    const QJsonArray condArray = value.toArray();
    QList<SmartCondition> conditions;
    conditions.reserve(condArray.size());
    for (const auto &item : condArray) {
        if (!item.isObject()) {
            return ruleError(QStringLiteral("Condition item must be an object"), QString());
        }
        auto condRes = parseCondition(item.toObject());
        if (!condRes.ok()) {
            return condRes.error();
        }
        conditions.append(condRes.value());
    }
    return conditions;
}

} // namespace

QList<SmartOp> smartOpsFor(SmartField field)
{
    switch (field) {
    case SmartField::Title:
    case SmartField::Artist:
    case SmartField::Album:
    case SmartField::AlbumArtist:
    case SmartField::Genre:
    case SmartField::Codec:
        return {
            SmartOp::Contains,
            SmartOp::NotContains,
            SmartOp::Is,
            SmartOp::IsNot,
            SmartOp::StartsWith,
        };
    case SmartField::Year:
    case SmartField::Rating:
    case SmartField::DurationSec:
        return {
            SmartOp::Equals,
            SmartOp::NotEquals,
            SmartOp::Greater,
            SmartOp::Less,
            SmartOp::Between,
        };
    case SmartField::Favorite:
        return {
            SmartOp::IsTrue,
            SmartOp::IsFalse,
        };
    case SmartField::DateAdded:
        return {
            SmartOp::InLastDays,
            SmartOp::NotInLastDays,
        };
    }
    return { };
}

SmartFieldKind smartFieldKind(SmartField field)
{
    switch (field) {
    case SmartField::Title:
    case SmartField::Artist:
    case SmartField::Album:
    case SmartField::AlbumArtist:
    case SmartField::Genre:
    case SmartField::Codec:
        return SmartFieldKind::Text;
    case SmartField::Year:
    case SmartField::Rating:
    case SmartField::DurationSec:
        return SmartFieldKind::Number;
    case SmartField::Favorite:
        return SmartFieldKind::Bool;
    case SmartField::DateAdded:
        return SmartFieldKind::Date;
    }
    Q_UNREACHABLE_RETURN(SmartFieldKind::Text);
}

namespace {

core::Result<void> validateCondition(const SmartCondition &cond)
{
    if (!smartOpsFor(cond.field).contains(cond.op)) {
        return ruleError(QStringLiteral("Operator not applicable to field"),
            QStringLiteral("%1 %2").arg(smartFieldName(cond.field), smartOpName(cond.op)));
    }

    if (cond.op == SmartOp::IsTrue || cond.op == SmartOp::IsFalse) {
        return { };
    }

    if (cond.op == SmartOp::Between) {
        if (!cond.value.isValid() || !cond.value2.isValid()) {
            return ruleError(QStringLiteral("Between operator requires value and value2"));
        }
        if (!cond.value.canConvert<double>() || !cond.value2.canConvert<double>()) {
            return ruleError(QStringLiteral("Between values must be numbers"));
        }
        return { };
    }

    if (!cond.value.isValid()) {
        return ruleError(QStringLiteral("Condition requires value"));
    }

    if (cond.op == SmartOp::Equals || cond.op == SmartOp::NotEquals || cond.op == SmartOp::Greater
        || cond.op == SmartOp::Less || cond.op == SmartOp::InLastDays
        || cond.op == SmartOp::NotInLastDays) {
        if (!cond.value.canConvert<double>()) {
            return ruleError(QStringLiteral("Numeric value required"));
        }
        return { };
    }

    // Text operators
    if (!cond.value.canConvert<QString>()) {
        return ruleError(QStringLiteral("Text value required"));
    }
    return { };
}

} // namespace

core::Result<void> SmartRule::validate() const
{
    for (const auto &cond : conditions) {
        if (auto res = validateCondition(cond); !res.ok()) {
            return res;
        }
    }

    if (sortKey == TrackSortKey::PlaylistOrder) {
        return ruleError(QStringLiteral("Unknown or invalid sort key in smart rule"),
            QStringLiteral("playlistOrder"));
    }

    if (limit.has_value() && limit.value() <= 0) {
        return ruleError(QStringLiteral("Limit must be a positive integer"));
    }

    return { };
}

QString SmartRule::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("match"),
        match == SmartMatch::All ? QStringLiteral("all") : QStringLiteral("any"));

    QJsonArray condArray;
    for (const auto &cond : conditions) {
        QJsonObject cObj;
        cObj.insert(QStringLiteral("field"), smartFieldName(cond.field));
        cObj.insert(QStringLiteral("op"), smartOpName(cond.op));
        if (cond.op == SmartOp::IsTrue || cond.op == SmartOp::IsFalse) {
            // No value needed
        } else if (cond.op == SmartOp::Between) {
            cObj.insert(QStringLiteral("value"), QJsonValue::fromVariant(cond.value));
            cObj.insert(QStringLiteral("value2"), QJsonValue::fromVariant(cond.value2));
        } else {
            cObj.insert(QStringLiteral("value"), QJsonValue::fromVariant(cond.value));
        }
        condArray.append(cObj);
    }
    root.insert(QStringLiteral("conditions"), condArray);

    root.insert(QStringLiteral("sortKey"), trackSortKeyName(sortKey));
    root.insert(QStringLiteral("sortOrder"),
        sortOrder == Qt::AscendingOrder ? QStringLiteral("asc") : QStringLiteral("desc"));

    if (limit.has_value()) {
        root.insert(QStringLiteral("limit"), limit.value());
    }

    const QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

core::Result<SmartRule> SmartRule::fromJson(const QString &json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return ruleError(
            QStringLiteral("Failed to parse smart rule JSON"), parseError.errorString());
    }

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("version"))
        || root.value(QStringLiteral("version")).toInt() != 1) {
        return ruleError(QStringLiteral("Invalid or unsupported smart rule version"), json);
    }

    SmartRule rule;

    const QString matchStr = root.value(QStringLiteral("match")).toString();
    if (matchStr == QStringLiteral("all")) {
        rule.match = SmartMatch::All;
    } else if (matchStr == QStringLiteral("any")) {
        rule.match = SmartMatch::Any;
    } else {
        return ruleError(QStringLiteral("Invalid match mode in smart rule"), matchStr);
    }

    auto condsRes = parseConditions(root.value(QStringLiteral("conditions")));
    if (!condsRes.ok()) {
        return condsRes.error();
    }
    rule.conditions = condsRes.value();

    if (root.contains(QStringLiteral("sortKey"))) {
        const auto keyOpt = detail::enumFromName<TrackSortKey>(
            root.value(QStringLiteral("sortKey")).toString(), trackSortKeyName);
        if (!keyOpt.has_value()) {
            return ruleError(QStringLiteral("Unknown or invalid sort key in smart rule"),
                root.value(QStringLiteral("sortKey")).toString());
        }
        rule.sortKey = keyOpt.value();
    }

    if (root.contains(QStringLiteral("sortOrder"))) {
        const QString orderStr = root.value(QStringLiteral("sortOrder")).toString();
        if (orderStr == QStringLiteral("asc")) {
            rule.sortOrder = Qt::AscendingOrder;
        } else if (orderStr == QStringLiteral("desc")) {
            rule.sortOrder = Qt::DescendingOrder;
        } else {
            return ruleError(QStringLiteral("Invalid sort order in smart rule"), orderStr);
        }
    }

    if (root.contains(QStringLiteral("limit"))) {
        const QJsonValue limVal = root.value(QStringLiteral("limit"));
        if (!limVal.isDouble()) {
            return ruleError(QStringLiteral("Limit must be a positive integer"), json);
        }
        rule.limit = limVal.toInt();
    }

    auto valRes = rule.validate();
    if (!valRes.ok()) {
        return valRes.error();
    }

    return rule;
}

} // namespace linernotes::library
