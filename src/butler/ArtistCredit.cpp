// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistCredit.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <optional>

namespace linernotes::butler {

namespace {

std::optional<QStringList> parseStringList(const QJsonValue &val)
{
    if (!val.isArray()) {
        return std::nullopt;
    }
    QStringList result;
    for (const auto &elem : val.toArray()) {
        if (!elem.isString()) {
            return std::nullopt;
        }
        const QString str = elem.toString().trimmed();
        if (!str.isEmpty() && !result.contains(str)) {
            result.append(str);
        }
    }
    return result;
}

std::optional<CreditPerformer> parsePerformer(const QJsonValue &val)
{
    if (!val.isObject()) {
        return std::nullopt;
    }
    const QJsonObject obj = val.toObject();
    if (!obj.contains(QStringLiteral("name"))) {
        return std::nullopt;
    }
    const QJsonValue nameVal = obj.value(QStringLiteral("name"));
    if (!nameVal.isString()) {
        return std::nullopt;
    }
    const QString name = nameVal.toString().trimmed();
    if (name.isEmpty()) {
        return std::nullopt;
    }
    QStringList aka;
    if (obj.contains(QStringLiteral("aka"))) {
        const auto akaOpt = parseStringList(obj.value(QStringLiteral("aka")));
        if (!akaOpt.has_value()) {
            return std::nullopt;
        }
        aka = *akaOpt;
    }
    return CreditPerformer {
        .name = name,
        .aka = aka,
    };
}

std::optional<QList<CreditPerformer>> parsePerformers(const QJsonValue &val)
{
    if (!val.isArray()) {
        return std::nullopt;
    }
    const QJsonArray arr = val.toArray();
    if (arr.isEmpty()) {
        return std::nullopt;
    }
    QList<CreditPerformer> performers;
    performers.reserve(arr.size());
    for (const auto &elem : arr) {
        const auto pOpt = parsePerformer(elem);
        if (!pOpt.has_value()) {
            return std::nullopt;
        }
        performers.append(*pOpt);
    }
    if (performers.isEmpty()) {
        return std::nullopt;
    }
    return performers;
}

} // namespace

QJsonObject toJson(const ArtistCredit &credit)
{
    QJsonObject obj;

    QJsonArray performersArr;
    for (const auto &p : credit.performers) {
        QJsonObject pObj;
        pObj.insert(QStringLiteral("name"), p.name);
        QJsonArray akaArr;
        for (const auto &aka : p.aka) {
            akaArr.append(aka);
        }
        pObj.insert(QStringLiteral("aka"), akaArr);
        performersArr.append(pObj);
    }
    obj.insert(QStringLiteral("performers"), performersArr);

    QJsonArray rolesArr;
    for (const auto &role : credit.roles) {
        rolesArr.append(role);
    }
    obj.insert(QStringLiteral("roles"), rolesArr);
    obj.insert(QStringLiteral("confidence"), credit.confidence);
    obj.insert(QStringLiteral("reason"), credit.reason);

    return obj;
}

std::optional<ArtistCredit> artistCreditFromJson(const QJsonObject &obj)
{
    if (!obj.contains(QStringLiteral("performers")) || !obj.contains(QStringLiteral("confidence"))
        || !obj.contains(QStringLiteral("reason"))) {
        return std::nullopt;
    }

    const auto performersOpt = parsePerformers(obj.value(QStringLiteral("performers")));
    if (!performersOpt.has_value()) {
        return std::nullopt;
    }

    QStringList roles;
    if (obj.contains(QStringLiteral("roles"))) {
        const auto rolesOpt = parseStringList(obj.value(QStringLiteral("roles")));
        if (!rolesOpt.has_value()) {
            return std::nullopt;
        }
        roles = *rolesOpt;
    }

    const QJsonValue confVal = obj.value(QStringLiteral("confidence"));
    if (!confVal.isDouble()) {
        return std::nullopt;
    }
    const double confidence = std::clamp(confVal.toDouble(), 0.0, 1.0);

    const QJsonValue reasonVal = obj.value(QStringLiteral("reason"));
    if (!reasonVal.isString()) {
        return std::nullopt;
    }

    return ArtistCredit {
        .performers = *performersOpt,
        .roles = roles,
        .confidence = confidence,
        .reason = reasonVal.toString(),
    };
}

QString normalizedValue(const ArtistCredit &credit)
{
    QStringList names;
    names.reserve(credit.performers.size());
    for (const auto &p : credit.performers) {
        names.append(p.name);
    }
    return names.join(QStringLiteral(" / "));
}

} // namespace linernotes::butler
