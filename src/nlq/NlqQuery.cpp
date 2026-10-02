// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "NlqQuery.h"

#include <QJsonArray>

#include <library/Errors.h>
#include <nlq/Errors.h>

#include <algorithm>

namespace linernotes::nlq {

QString entityToString(Entity entity)
{
    switch (entity) {
    case Entity::Track:
        return QStringLiteral("track");
    case Entity::Album:
        return QStringLiteral("album");
    case Entity::Artist:
        return QStringLiteral("artist");
    }
    return QStringLiteral("track");
}

std::optional<Entity> entityFromString(QStringView name)
{
    if (name == QLatin1StringView("track")) {
        return Entity::Track;
    }
    if (name == QLatin1StringView("album")) {
        return Entity::Album;
    }
    if (name == QLatin1StringView("artist")) {
        return Entity::Artist;
    }
    return std::nullopt;
}

QString sortKeyToString(SortKey sortKey)
{
    switch (sortKey) {
    case SortKey::Default:
        return QStringLiteral("default");
    case SortKey::PlayCount:
        return QStringLiteral("playCount");
    case SortKey::LastPlayed:
        return QStringLiteral("lastPlayed");
    case SortKey::Rating:
        return QStringLiteral("rating");
    case SortKey::Year:
        return QStringLiteral("year");
    case SortKey::DateAdded:
        return QStringLiteral("dateAdded");
    case SortKey::Duration:
        return QStringLiteral("duration");
    case SortKey::Random:
        return QStringLiteral("random");
    }
    return QStringLiteral("default");
}

std::optional<SortKey> sortKeyFromString(QStringView name)
{
    if (name == QLatin1StringView("default")) {
        return SortKey::Default;
    }
    if (name == QLatin1StringView("playCount")) {
        return SortKey::PlayCount;
    }
    if (name == QLatin1StringView("lastPlayed")) {
        return SortKey::LastPlayed;
    }
    if (name == QLatin1StringView("rating")) {
        return SortKey::Rating;
    }
    if (name == QLatin1StringView("year")) {
        return SortKey::Year;
    }
    if (name == QLatin1StringView("dateAdded")) {
        return SortKey::DateAdded;
    }
    if (name == QLatin1StringView("duration")) {
        return SortKey::Duration;
    }
    if (name == QLatin1StringView("random")) {
        return SortKey::Random;
    }
    return std::nullopt;
}

core::Result<void> Query::validate() const
{
    if (limit < 1 || limit > 500) {
        return core::Error {
            .code = QString(errc::kQueryInvalid),
            .message = QStringLiteral("Limit out of range [1, 500]"),
            .detail = QString::number(limit),
        };
    }
    if (similarTo.has_value()) {
        if (entity != Entity::Track) {
            return core::Error {
                .code = QString(errc::kQueryInvalid),
                .message = QStringLiteral("similarTo is only supported for track entity"),
                .detail = QString(),
            };
        }
        if (!similarTo->current && similarTo->titles.isEmpty()) {
            return core::Error {
                .code = QString(errc::kQueryInvalid),
                .message
                = QStringLiteral("similarTo requires non-empty titles when current is false"),
                .detail = QString(),
            };
        }
    }
    return rule.validate();
}

QJsonObject Query::toJson() const
{
    QJsonObject obj = rule.toJsonObject();
    obj.remove(QStringLiteral("version"));
    obj.remove(QStringLiteral("sortKey"));
    obj.remove(QStringLiteral("sortOrder"));
    obj.remove(QStringLiteral("limit"));

    obj.insert(QStringLiteral("entity"), entityToString(entity));
    obj.insert(QStringLiteral("sort"), sortKeyToString(sortKey));
    obj.insert(QStringLiteral("order"),
        sortOrder == Qt::AscendingOrder ? QStringLiteral("asc") : QStringLiteral("desc"));
    obj.insert(QStringLiteral("limit"), limit);

    if (similarTo.has_value()) {
        QJsonObject stObj;
        if (similarTo->current) {
            stObj.insert(QStringLiteral("current"), true);
        }
        if (!similarTo->titles.isEmpty()) {
            QJsonArray titleArr;
            for (const auto &t : similarTo->titles) {
                titleArr.append(t);
            }
            stObj.insert(QStringLiteral("title"), titleArr);
        }
        if (!similarTo->artists.isEmpty()) {
            QJsonArray artistArr;
            for (const auto &a : similarTo->artists) {
                artistArr.append(a);
            }
            stObj.insert(QStringLiteral("artist"), artistArr);
        }
        obj.insert(QStringLiteral("similarTo"), stObj);
    }

    return obj;
}

namespace {

QStringList stringListFromJson(const QJsonValue &value)
{
    if (value.isString()) {
        return { value.toString() };
    }
    QStringList list;
    const auto arr = value.toArray();
    for (const auto &v : arr) {
        if (v.isString()) {
            list.append(v.toString());
        }
    }
    return list;
}

core::Result<SimilarTo> similarToFromJson(const QJsonValue &value)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kQueryInvalid),
            .message = QStringLiteral("similarTo must be an object"),
            .detail = QString(),
        };
    }
    const QJsonObject stObj = value.toObject();
    SimilarTo st;
    st.current = stObj.value(QStringLiteral("current")).toBool(false);
    st.titles = stringListFromJson(stObj.value(QStringLiteral("title")));
    st.artists = stringListFromJson(stObj.value(QStringLiteral("artist")));
    return st;
}

} // namespace

core::Result<Query> Query::fromJson(const QJsonObject &obj)
{
    Query q;

    if (obj.contains(QStringLiteral("entity"))) {
        const auto entOpt = entityFromString(obj.value(QStringLiteral("entity")).toString());
        if (!entOpt.has_value()) {
            return core::Error {
                .code = QString(errc::kQueryInvalid),
                .message = QStringLiteral("Unknown entity in query"),
                .detail = obj.value(QStringLiteral("entity")).toString(),
            };
        }
        q.entity = entOpt.value();
    } else {
        q.entity = Entity::Track;
    }

    if (obj.contains(QStringLiteral("similarTo"))) {
        auto stRes = similarToFromJson(obj.value(QStringLiteral("similarTo")));
        if (!stRes.ok()) {
            return stRes.error();
        }
        q.similarTo = stRes.value();
    }

    if (obj.contains(QStringLiteral("sort"))) {
        const auto sortOpt = sortKeyFromString(obj.value(QStringLiteral("sort")).toString());
        if (!sortOpt.has_value()) {
            return core::Error {
                .code = QString(errc::kQueryInvalid),
                .message = QStringLiteral("Unknown sort key in query"),
                .detail = obj.value(QStringLiteral("sort")).toString(),
            };
        }
        q.sortKey = sortOpt.value();
    } else {
        q.sortKey = SortKey::Default;
    }

    if (obj.contains(QStringLiteral("order"))) {
        const QString orderStr = obj.value(QStringLiteral("order")).toString();
        if (orderStr == QStringLiteral("asc")) {
            q.sortOrder = Qt::AscendingOrder;
        } else if (orderStr == QStringLiteral("desc")) {
            q.sortOrder = Qt::DescendingOrder;
        } else {
            return core::Error {
                .code = QString(errc::kQueryInvalid),
                .message = QStringLiteral("Unknown sort order in query"),
                .detail = orderStr,
            };
        }
    } else {
        q.sortOrder = Qt::DescendingOrder;
    }

    if (obj.contains(QStringLiteral("limit"))) {
        const QJsonValue limVal = obj.value(QStringLiteral("limit"));
        if (limVal.isDouble()) {
            q.limit = std::clamp(limVal.toInt(), 1, 500);
        } else {
            return core::Error {
                .code = QString(errc::kQueryInvalid),
                .message = QStringLiteral("Limit must be an integer"),
                .detail = limVal.toVariant().toString(),
            };
        }
    } else {
        q.limit = 50;
    }

    QJsonObject ruleObj = obj;
    ruleObj.remove(QStringLiteral("limit"));
    ruleObj.remove(QStringLiteral("similarTo"));
    if (!ruleObj.contains(QStringLiteral("match"))) {
        ruleObj.insert(QStringLiteral("match"), QStringLiteral("all"));
    }
    auto ruleRes = library::SmartRule::fromJsonObject(ruleObj);
    if (!ruleRes.ok()) {
        return ruleRes.error();
    }
    q.rule = ruleRes.value();

    auto valRes = q.validate();
    if (!valRes.ok()) {
        return valRes.error();
    }

    return q;
}

} // namespace linernotes::nlq
