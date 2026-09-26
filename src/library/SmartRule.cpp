// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SmartRule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <library/Errors.h>
#include <library/LibraryQuery.h>

namespace linernotes::library {

namespace {

QString fieldToString(SmartField f)
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

std::optional<SmartField> fieldFromString(const QString &s)
{
    if (s == QStringLiteral("title")) {
        return SmartField::Title;
    }
    if (s == QStringLiteral("artist")) {
        return SmartField::Artist;
    }
    if (s == QStringLiteral("album")) {
        return SmartField::Album;
    }
    if (s == QStringLiteral("albumArtist")) {
        return SmartField::AlbumArtist;
    }
    if (s == QStringLiteral("genre")) {
        return SmartField::Genre;
    }
    if (s == QStringLiteral("codec")) {
        return SmartField::Codec;
    }
    if (s == QStringLiteral("year")) {
        return SmartField::Year;
    }
    if (s == QStringLiteral("rating")) {
        return SmartField::Rating;
    }
    if (s == QStringLiteral("durationSec")) {
        return SmartField::DurationSec;
    }
    if (s == QStringLiteral("favorite")) {
        return SmartField::Favorite;
    }
    if (s == QStringLiteral("dateAdded")) {
        return SmartField::DateAdded;
    }
    return std::nullopt;
}

QString opToString(SmartOp op)
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

std::optional<SmartOp> opFromString(const QString &s)
{
    if (s == QStringLiteral("contains")) {
        return SmartOp::Contains;
    }
    if (s == QStringLiteral("notContains")) {
        return SmartOp::NotContains;
    }
    if (s == QStringLiteral("is")) {
        return SmartOp::Is;
    }
    if (s == QStringLiteral("isNot")) {
        return SmartOp::IsNot;
    }
    if (s == QStringLiteral("startsWith")) {
        return SmartOp::StartsWith;
    }
    if (s == QStringLiteral("equals")) {
        return SmartOp::Equals;
    }
    if (s == QStringLiteral("notEquals")) {
        return SmartOp::NotEquals;
    }
    if (s == QStringLiteral("greater")) {
        return SmartOp::Greater;
    }
    if (s == QStringLiteral("less")) {
        return SmartOp::Less;
    }
    if (s == QStringLiteral("between")) {
        return SmartOp::Between;
    }
    if (s == QStringLiteral("isTrue")) {
        return SmartOp::IsTrue;
    }
    if (s == QStringLiteral("isFalse")) {
        return SmartOp::IsFalse;
    }
    if (s == QStringLiteral("inLastDays")) {
        return SmartOp::InLastDays;
    }
    if (s == QStringLiteral("notInLastDays")) {
        return SmartOp::NotInLastDays;
    }
    return std::nullopt;
}

QString sortKeyToString(TrackSortKey k)
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

std::optional<TrackSortKey> sortKeyFromString(const QString &s)
{
    if (s == QStringLiteral("default")) {
        return TrackSortKey::Default;
    }
    if (s == QStringLiteral("title")) {
        return TrackSortKey::Title;
    }
    if (s == QStringLiteral("artist")) {
        return TrackSortKey::Artist;
    }
    if (s == QStringLiteral("album")) {
        return TrackSortKey::Album;
    }
    if (s == QStringLiteral("year")) {
        return TrackSortKey::Year;
    }
    if (s == QStringLiteral("duration")) {
        return TrackSortKey::Duration;
    }
    if (s == QStringLiteral("dateAdded")) {
        return TrackSortKey::DateAdded;
    }
    if (s == QStringLiteral("playlistOrder")) {
        return TrackSortKey::PlaylistOrder;
    }
    return std::nullopt;
}

core::Error ruleError(const QString &message, const QString &detail = QString())
{
    return core::Error {
        .code = QString(errc::kPlaylistRuleInvalid),
        .message = message,
        .detail = detail,
    };
}

core::Result<SmartCondition> parseCondition(const QJsonObject &cObj)
{
    const auto fieldOpt = fieldFromString(cObj.value(QStringLiteral("field")).toString());
    if (!fieldOpt.has_value()) {
        return ruleError(QStringLiteral("Unknown smart rule field"),
            cObj.value(QStringLiteral("field")).toString());
    }

    const auto opOpt = opFromString(cObj.value(QStringLiteral("op")).toString());
    if (!opOpt.has_value()) {
        return ruleError(QStringLiteral("Unknown smart rule operator"),
            cObj.value(QStringLiteral("op")).toString());
    }

    const SmartField field = fieldOpt.value();
    const SmartOp op = opOpt.value();
    if (!smartOpsFor(field).contains(op)) {
        return ruleError(QStringLiteral("Operator not applicable to field"),
            QStringLiteral("%1 %2").arg(fieldToString(field), opToString(op)));
    }

    SmartCondition cond;
    cond.field = field;
    cond.op = op;

    if (op == SmartOp::IsTrue || op == SmartOp::IsFalse) {
        return cond;
    }

    if (!cObj.contains(QStringLiteral("value"))) {
        return ruleError(QStringLiteral("Condition requires value"));
    }

    if (op == SmartOp::Between) {
        if (!cObj.contains(QStringLiteral("value2"))) {
            return ruleError(QStringLiteral("Between operator requires value and value2"));
        }
        const QJsonValue val1 = cObj.value(QStringLiteral("value"));
        const QJsonValue val2 = cObj.value(QStringLiteral("value2"));
        if (!val1.isDouble() || !val2.isDouble()) {
            return ruleError(QStringLiteral("Between values must be numbers"));
        }
        cond.value = val1.toVariant();
        cond.value2 = val2.toVariant();
        return cond;
    }

    if (op == SmartOp::Equals || op == SmartOp::NotEquals || op == SmartOp::Greater
        || op == SmartOp::Less || op == SmartOp::InLastDays || op == SmartOp::NotInLastDays) {
        const QJsonValue val = cObj.value(QStringLiteral("value"));
        if (!val.isDouble()) {
            return ruleError(QStringLiteral("Numeric value required"));
        }
        cond.value = val.toVariant();
        return cond;
    }

    // Text operators
    const QJsonValue val = cObj.value(QStringLiteral("value"));
    if (!val.isString()) {
        return ruleError(QStringLiteral("Text value required"));
    }
    cond.value = val.toString();
    return cond;
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

QString SmartRule::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("match"),
        match == Match::All ? QStringLiteral("all") : QStringLiteral("any"));

    QJsonArray condArray;
    for (const auto &cond : conditions) {
        QJsonObject cObj;
        cObj.insert(QStringLiteral("field"), fieldToString(cond.field));
        cObj.insert(QStringLiteral("op"), opToString(cond.op));
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

    root.insert(QStringLiteral("sortKey"), sortKeyToString(sortKey));
    root.insert(QStringLiteral("sortOrder"),
        sortOrder == Qt::AscendingOrder ? QStringLiteral("asc") : QStringLiteral("desc"));

    if (limit.has_value()) {
        root.insert(QStringLiteral("limit"), limit.value());
    }

    const QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

namespace {

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
        rule.match = Match::All;
    } else if (matchStr == QStringLiteral("any")) {
        rule.match = Match::Any;
    } else {
        return ruleError(QStringLiteral("Invalid match mode in smart rule"), matchStr);
    }

    auto condsRes = parseConditions(root.value(QStringLiteral("conditions")));
    if (!condsRes.ok()) {
        return condsRes.error();
    }
    rule.conditions = condsRes.value();

    if (root.contains(QStringLiteral("sortKey"))) {
        const auto keyOpt = sortKeyFromString(root.value(QStringLiteral("sortKey")).toString());
        if (!keyOpt.has_value() || keyOpt.value() == TrackSortKey::PlaylistOrder) {
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
        if (!limVal.isDouble() || limVal.toInt() <= 0) {
            return ruleError(QStringLiteral("Limit must be a positive integer"), json);
        }
        rule.limit = limVal.toInt();
    }

    return rule;
}

} // namespace linernotes::library
