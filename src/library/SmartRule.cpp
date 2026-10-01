// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SmartRule.h"

#include <QDate>
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
    case SmartField::PlayCount:
        return QStringLiteral("playCount");
    case SmartField::SkipCount:
        return QStringLiteral("skipCount");
    case SmartField::CompletedCount:
        return QStringLiteral("completedCount");
    case SmartField::LastPlayed:
        return QStringLiteral("lastPlayed");
    case SmartField::VersionType:
        return QStringLiteral("versionType");
    case SmartField::Language:
        return QStringLiteral("language");
    case SmartField::AlbumFavorite:
        return QStringLiteral("albumFavorite");
    case SmartField::ArtistFavorite:
        return QStringLiteral("artistFavorite");
    case SmartField::AlbumCompletion:
        return QStringLiteral("albumCompletion");
    case SmartField::Keyword:
        return QStringLiteral("keyword");
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
        const QJsonValue valJson = cObj.value(QStringLiteral("value"));
        if (valJson.isArray()) {
            QStringList list;
            const QJsonArray arr = valJson.toArray();
            list.reserve(arr.size());
            for (const auto &item : arr) {
                list.append(item.toString());
            }
            cond.value = list;
        } else {
            cond.value = valJson.toVariant();
        }
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

core::Result<SmartMatch> parseMatch(const QJsonObject &root)
{
    const QString matchStr = root.value(QStringLiteral("match")).toString();
    if (matchStr == QStringLiteral("all")) {
        return SmartMatch::All;
    }
    if (matchStr == QStringLiteral("any")) {
        return SmartMatch::Any;
    }
    return ruleError(QStringLiteral("Invalid match mode in smart rule"), matchStr);
}

core::Result<std::optional<QDate>> parseOptionalDate(const QJsonObject &root, const QString &key)
{
    if (!root.contains(key)) {
        return std::optional<QDate>();
    }
    const QString str = root.value(key).toString();
    const QDate d = QDate::fromString(str, Qt::ISODate);
    if (!d.isValid()) {
        return ruleError(QStringLiteral("Invalid %1 date").arg(key), str);
    }
    return std::optional<QDate>(d);
}

core::Result<TrackSortKey> parseSortKey(const QJsonObject &root)
{
    if (!root.contains(QStringLiteral("sortKey"))) {
        return TrackSortKey::Default;
    }
    const auto keyOpt = detail::enumFromName<TrackSortKey>(
        root.value(QStringLiteral("sortKey")).toString(), trackSortKeyName);
    if (!keyOpt.has_value()) {
        return ruleError(QStringLiteral("Unknown or invalid sort key in smart rule"),
            root.value(QStringLiteral("sortKey")).toString());
    }
    return keyOpt.value();
}

core::Result<Qt::SortOrder> parseSortOrder(const QJsonObject &root)
{
    if (!root.contains(QStringLiteral("sortOrder"))) {
        return Qt::AscendingOrder;
    }
    const QString orderStr = root.value(QStringLiteral("sortOrder")).toString();
    if (orderStr == QStringLiteral("asc")) {
        return Qt::AscendingOrder;
    }
    if (orderStr == QStringLiteral("desc")) {
        return Qt::DescendingOrder;
    }
    return ruleError(QStringLiteral("Invalid sort order in smart rule"), orderStr);
}

core::Result<std::optional<int>> parseLimit(const QJsonObject &root, const QString &json)
{
    if (!root.contains(QStringLiteral("limit"))) {
        return std::optional<int>();
    }
    const QJsonValue limVal = root.value(QStringLiteral("limit"));
    if (!limVal.isDouble()) {
        return ruleError(QStringLiteral("Limit must be a positive integer"), json);
    }
    return std::optional<int>(limVal.toInt());
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
    case SmartField::Keyword:
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
    case SmartField::PlayCount:
    case SmartField::SkipCount:
    case SmartField::CompletedCount:
    case SmartField::AlbumCompletion:
        return {
            SmartOp::Equals,
            SmartOp::NotEquals,
            SmartOp::Greater,
            SmartOp::Less,
            SmartOp::Between,
        };
    case SmartField::Favorite:
    case SmartField::AlbumFavorite:
    case SmartField::ArtistFavorite:
        return {
            SmartOp::IsTrue,
            SmartOp::IsFalse,
        };
    case SmartField::DateAdded:
    case SmartField::LastPlayed:
        return {
            SmartOp::InLastDays,
            SmartOp::NotInLastDays,
            SmartOp::Between,
        };
    case SmartField::VersionType:
    case SmartField::Language:
        return {
            SmartOp::Is,
            SmartOp::IsNot,
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
    case SmartField::Keyword:
        return SmartFieldKind::Text;
    case SmartField::Year:
    case SmartField::Rating:
    case SmartField::DurationSec:
    case SmartField::PlayCount:
    case SmartField::SkipCount:
    case SmartField::CompletedCount:
    case SmartField::AlbumCompletion:
        return SmartFieldKind::Number;
    case SmartField::Favorite:
    case SmartField::AlbumFavorite:
    case SmartField::ArtistFavorite:
        return SmartFieldKind::Bool;
    case SmartField::DateAdded:
    case SmartField::LastPlayed:
        return SmartFieldKind::Date;
    case SmartField::VersionType:
    case SmartField::Language:
        return SmartFieldKind::Enum;
    }
    Q_UNREACHABLE_RETURN(SmartFieldKind::Text);
}

namespace {

core::Result<void> validateTextCondition(const SmartCondition &cond)
{
    if (cond.value.userType() == QMetaType::QStringList) {
        if (cond.value.toStringList().isEmpty()) {
            return ruleError(QStringLiteral("Text array value must not be empty"));
        }
        return { };
    }
    if (cond.value.userType() == QMetaType::QVariantList) {
        const auto list = cond.value.toList();
        if (list.isEmpty()) {
            return ruleError(QStringLiteral("Text array value must not be empty"));
        }
        for (const auto &item : list) {
            if (item.userType() != QMetaType::QString && !item.canConvert<QString>()) {
                return ruleError(QStringLiteral("Text array elements must be strings"));
            }
        }
        return { };
    }
    if (!cond.value.isValid() || !cond.value.canConvert<QString>()
        || cond.value.userType() == QMetaType::Bool) {
        return ruleError(QStringLiteral("Text value required"));
    }
    return { };
}

core::Result<void> validateNumberCondition(const SmartCondition &cond)
{
    if (cond.op == SmartOp::Between) {
        if (!cond.value.isValid() || !cond.value2.isValid()) {
            return ruleError(QStringLiteral("Between operator requires value and value2"));
        }
        if (isSmartTextList(cond.value) || isSmartTextList(cond.value2)
            || !cond.value.canConvert<double>() || !cond.value2.canConvert<double>()) {
            return ruleError(QStringLiteral("Between values must be numbers"));
        }
        return { };
    }

    if (!cond.value.isValid()) {
        return ruleError(QStringLiteral("Condition requires value"));
    }

    if (isSmartTextList(cond.value) || !cond.value.canConvert<double>()) {
        return ruleError(QStringLiteral("Numeric value required"));
    }
    return { };
}

core::Result<void> validateBoolCondition(const SmartCondition & /*cond*/)
{
    return { };
}

core::Result<void> validateDateCondition(const SmartCondition &cond)
{
    if (cond.op == SmartOp::Between) {
        if (!cond.value.isValid() || !cond.value2.isValid()) {
            return ruleError(QStringLiteral("Between operator requires value and value2"));
        }
        if (!cond.value.canConvert<QString>() || !cond.value2.canConvert<QString>()) {
            return ruleError(QStringLiteral("Date Between values must be strings"));
        }
        const QDate d1 = QDate::fromString(cond.value.toString(), Qt::ISODate);
        const QDate d2 = QDate::fromString(cond.value2.toString(), Qt::ISODate);
        if (!d1.isValid() || !d2.isValid()) {
            return ruleError(QStringLiteral("Date Between values must be valid yyyy-MM-dd dates"));
        }
        if (d1 > d2) {
            return ruleError(QStringLiteral("Date Between from date must be <= to date"));
        }
        return { };
    }

    if (!cond.value.isValid() || !cond.value.canConvert<double>()) {
        return ruleError(QStringLiteral("Numeric value required for days"));
    }
    return { };
}

core::Result<void> validateEnumCondition(const SmartCondition &cond)
{
    if (!cond.value.isValid() || !cond.value.canConvert<QString>()) {
        return ruleError(QStringLiteral("Enum value required"));
    }
    const QString valStr = cond.value.toString();
    if (cond.field == SmartField::VersionType) {
        if (!versionTypeFromString(valStr).has_value()) {
            return ruleError(QStringLiteral("Unknown versionType value"), valStr);
        }
    } else if (cond.field == SmartField::Language) {
        if (!trackLanguageFromString(valStr).has_value()) {
            return ruleError(QStringLiteral("Unknown language value"), valStr);
        }
    }
    return { };
}

core::Result<void> validateCondition(const SmartCondition &cond)
{
    if (!smartOpsFor(cond.field).contains(cond.op)) {
        return ruleError(QStringLiteral("Operator not applicable to field"),
            QStringLiteral("%1 %2").arg(smartFieldName(cond.field), smartOpName(cond.op)));
    }

    switch (smartFieldKind(cond.field)) {
    case SmartFieldKind::Text:
        return validateTextCondition(cond);
    case SmartFieldKind::Number:
        return validateNumberCondition(cond);
    case SmartFieldKind::Bool:
        return validateBoolCondition(cond);
    case SmartFieldKind::Date:
        return validateDateCondition(cond);
    case SmartFieldKind::Enum:
        return validateEnumCondition(cond);
    }
    Q_UNREACHABLE_RETURN(ruleError(QStringLiteral("Unknown field kind")));
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

    if (playedFrom.has_value() && playedTo.has_value()) {
        if (playedFrom.value() > playedTo.value()) {
            return ruleError(QStringLiteral("playedFrom must be <= playedTo"));
        }
    }

    return { };
}

QJsonObject SmartRule::toJsonObject() const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("match"),
        match == SmartMatch::All ? QStringLiteral("all") : QStringLiteral("any"));

    if (playedFrom.has_value()) {
        root.insert(QStringLiteral("playedFrom"), playedFrom->toString(Qt::ISODate));
    }
    if (playedTo.has_value()) {
        root.insert(QStringLiteral("playedTo"), playedTo->toString(Qt::ISODate));
    }

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

    return root;
}

QString SmartRule::toJson() const
{
    const QJsonDocument doc(toJsonObject());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

core::Result<SmartRule> SmartRule::fromJsonObject(const QJsonObject &root)
{
    SmartRule rule;

    const auto matchRes = parseMatch(root);
    if (!matchRes.ok()) {
        return matchRes.error();
    }
    rule.match = matchRes.value();

    const auto pfRes = parseOptionalDate(root, QStringLiteral("playedFrom"));
    if (!pfRes.ok()) {
        return pfRes.error();
    }
    rule.playedFrom = pfRes.value();

    const auto ptRes = parseOptionalDate(root, QStringLiteral("playedTo"));
    if (!ptRes.ok()) {
        return ptRes.error();
    }
    rule.playedTo = ptRes.value();

    if (root.contains(QStringLiteral("conditions"))) {
        auto condsRes = parseConditions(root.value(QStringLiteral("conditions")));
        if (!condsRes.ok()) {
            return condsRes.error();
        }
        rule.conditions = condsRes.value();
    }

    const auto sortKeyRes = parseSortKey(root);
    if (!sortKeyRes.ok()) {
        return sortKeyRes.error();
    }
    rule.sortKey = sortKeyRes.value();

    const auto sortOrderRes = parseSortOrder(root);
    if (!sortOrderRes.ok()) {
        return sortOrderRes.error();
    }
    rule.sortOrder = sortOrderRes.value();

    const auto limitRes = parseLimit(root, QString());
    if (!limitRes.ok()) {
        return limitRes.error();
    }
    rule.limit = limitRes.value();

    auto valRes = rule.validate();
    if (!valRes.ok()) {
        return valRes.error();
    }

    return rule;
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

    return fromJsonObject(root);
}

QStringList smartTextValues(const QVariant &value)
{
    if (value.userType() == QMetaType::QStringList) {
        return value.toStringList();
    }
    if (value.userType() == QMetaType::QVariantList) {
        const auto varList = value.toList();
        QStringList list;
        list.reserve(varList.size());
        for (const auto &v : varList) {
            list.append(v.toString());
        }
        return list;
    }
    if (value.isValid()) {
        return { value.toString() };
    }
    return { };
}

bool isSmartTextList(const QVariant &value)
{
    return value.userType() == QMetaType::QStringList
        || value.userType() == QMetaType::QVariantList;
}

} // namespace linernotes::library
