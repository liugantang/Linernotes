// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Wikidata.h"

#include "Errors.h"
#include "GuideUtils.h"

#include <QChar>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringView>
#include <QUrlQuery>

#include <algorithm>
#include <array>
#include <span>

namespace linernotes::guide {

namespace {

// Musical group types (P31 values for Artist)
inline constexpr std::array<QLatin1StringView, 5> kArtistGroupTypes = {
    QLatin1StringView("Q215380"), // musical group
    QLatin1StringView("Q2088357"), // musical ensemble
    QLatin1StringView("Q5741069"), // rock band
    QLatin1StringView("Q9212979"), // musical duo
    QLatin1StringView("Q281643"), // trio
};

// Musical occupations for human (P106 values when P31 has Q5)
inline constexpr std::array<QLatin1StringView, 10> kMusicOccupations = {
    QLatin1StringView("Q177220"), // singer
    QLatin1StringView("Q639669"), // musician
    QLatin1StringView("Q36834"), // composer
    QLatin1StringView("Q488205"), // singer-songwriter
    QLatin1StringView("Q753110"), // songwriter
    QLatin1StringView("Q183945"), // record producer
    QLatin1StringView("Q130857"), // disc jockey
    QLatin1StringView("Q622807"), // seiyū
    QLatin1StringView("Q855091"), // guitarist
    QLatin1StringView("Q486748"), // pianist
};

// Song types (P31 values)
inline constexpr std::array<QLatin1StringView, 4> kSongTypes = {
    QLatin1StringView("Q7366"), // song
    QLatin1StringView("Q134556"), // single
    QLatin1StringView("Q105543609"), // musical work/composition
    QLatin1StringView("Q2188189"), // musical work
};

// Album types (P31 values)
inline constexpr std::array<QLatin1StringView, 6> kAlbumTypes = {
    QLatin1StringView("Q482994"), // album
    QLatin1StringView("Q208569"), // studio album
    QLatin1StringView("Q169930"), // EP
    QLatin1StringView("Q209939"), // live album
    QLatin1StringView("Q222910"), // compilation album
    QLatin1StringView("Q4176708"), // soundtrack album
};

QString formatWikidataTime(const QString &timeStr, int precision)
{
    QString raw = timeStr;
    if (raw.startsWith(QLatin1Char('+'))) {
        raw = raw.mid(1);
    }
    if (precision >= 11) {
        return raw.left(10);
    }
    if (precision == 10) {
        return raw.left(7);
    }
    if (precision == 9) {
        return raw.left(4);
    }
    return { };
}

bool hasKana(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) {
        const auto u = c.unicode();
        return (u >= 0x3040 && u <= 0x30FF) || (u >= 0x31F0 && u <= 0x31FF)
            || c.script() == QChar::Script_Hiragana || c.script() == QChar::Script_Katakana;
    });
}

bool hasHan(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) {
        const auto u = c.unicode();
        return (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0xF900 && u <= 0xFAFF) || c.script() == QChar::Script_Han;
    });
}

bool containsAny(const QStringList &list, std::span<const QLatin1StringView> targets)
{
    return std::ranges::any_of(list, [&targets](const QString &item) {
        return std::ranges::any_of(
            targets, [&item](QLatin1StringView target) { return item == target; });
    });
}

bool isArtistEntity(const WikidataEntity &entity)
{
    const QStringList p31 = entity.itemClaims.value(QStringLiteral("P31"));
    if (containsAny(p31, kArtistGroupTypes)) {
        return true;
    }
    if (p31.contains(QStringLiteral("Q5"))) {
        const QStringList p106 = entity.itemClaims.value(QStringLiteral("P106"));
        if (containsAny(p106, kMusicOccupations)) {
            return true;
        }
    }
    return false;
}

bool isSongEntity(const WikidataEntity &entity)
{
    const QStringList p31 = entity.itemClaims.value(QStringLiteral("P31"));
    return containsAny(p31, kSongTypes);
}

bool isAlbumEntity(const WikidataEntity &entity)
{
    const QStringList p31 = entity.itemClaims.value(QStringLiteral("P31"));
    return containsAny(p31, kAlbumTypes);
}

void parseEntityLabelsAndDescriptions(const QJsonObject &entityObj, WikidataEntity &entity)
{
    const QJsonObject labelsObj = entityObj.value(QStringLiteral("labels")).toObject();
    for (auto it = labelsObj.begin(); it != labelsObj.end(); ++it) {
        if (it.value().isObject()) {
            const QString val = it.value().toObject().value(QStringLiteral("value")).toString();
            entity.labels.insert(it.key(), val);
        }
    }

    const QJsonObject descObj = entityObj.value(QStringLiteral("descriptions")).toObject();
    for (auto it = descObj.begin(); it != descObj.end(); ++it) {
        if (it.value().isObject()) {
            const QString val = it.value().toObject().value(QStringLiteral("value")).toString();
            entity.descriptions.insert(it.key(), val);
        }
    }
}

// 单条陈述：引用实体追加到 itemQids；时间值只记该属性的第一个
void parseStatement(const QString &property, const QJsonObject &stmtObj, QStringList &itemQids,
    WikidataEntity &entity)
{
    const QJsonObject mainsnak = stmtObj.value(QStringLiteral("mainsnak")).toObject();
    if (mainsnak.value(QStringLiteral("snaktype")).toString() != QStringLiteral("value")) {
        return;
    }
    const QJsonObject datavalue = mainsnak.value(QStringLiteral("datavalue")).toObject();
    const QString valType = datavalue.value(QStringLiteral("type")).toString();
    const QJsonObject valObj = datavalue.value(QStringLiteral("value")).toObject();

    if (valType == QStringLiteral("wikibase-entityid")) {
        const QString qid = valObj.value(QStringLiteral("id")).toString();
        if (!qid.isEmpty()) {
            itemQids.append(qid);
        }
        return;
    }
    if (valType == QStringLiteral("time") && !entity.timeClaims.contains(property)) {
        const QString timeStr = valObj.value(QStringLiteral("time")).toString();
        const int precision = valObj.value(QStringLiteral("precision")).toInt();
        const QString formattedTime = formatWikidataTime(timeStr, precision);
        if (!formattedTime.isEmpty()) {
            entity.timeClaims.insert(property, WikidataTime { .text = formattedTime });
        }
    }
}

void parseEntityClaims(const QJsonObject &entityObj, WikidataEntity &entity)
{
    const QJsonObject claimsObj = entityObj.value(QStringLiteral("claims")).toObject();
    for (auto it = claimsObj.begin(); it != claimsObj.end(); ++it) {
        if (!it.value().isArray()) {
            continue;
        }
        const QJsonArray statements = it.value().toArray();
        QStringList itemQids;
        for (const auto &stmtVal : statements) {
            if (stmtVal.isObject()) {
                parseStatement(it.key(), stmtVal.toObject(), itemQids, entity);
            }
        }
        if (!itemQids.isEmpty()) {
            entity.itemClaims.insert(it.key(), itemQids);
        }
    }
}

void parseEntitySitelinks(const QJsonObject &entityObj, WikidataEntity &entity)
{
    const QJsonObject sitelinksObj = entityObj.value(QStringLiteral("sitelinks")).toObject();
    for (auto it = sitelinksObj.begin(); it != sitelinksObj.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject linkObj = it.value().toObject();
        const QString siteKey = it.key();
        const QString title = linkObj.value(QStringLiteral("title")).toString();
        if (!title.isEmpty()) {
            entity.sitelinkTitles.insert(siteKey, title);
        }
        if (linkObj.contains(QStringLiteral("url"))) {
            const QString urlStr = linkObj.value(QStringLiteral("url")).toString();
            if (!urlStr.isEmpty()) {
                entity.sitelinkUrls.insert(siteKey, QUrl(urlStr));
            }
        }
    }
}

WikidataEntity parseSingleEntity(const QString &key, const QJsonObject &entityObj)
{
    WikidataEntity entity;
    entity.id = entityObj.value(QStringLiteral("id")).toString(key);
    parseEntityLabelsAndDescriptions(entityObj, entity);
    parseEntityClaims(entityObj, entity);
    parseEntitySitelinks(entityObj, entity);
    return entity;
}

} // namespace

QUrl wikidataSearchUrl(const QString &text, const QString &lang)
{
    QUrl url(QStringLiteral("https://www.wikidata.org/w/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("wbsearchentities"));
    query.addQueryItem(QStringLiteral("search"), queryValue(text));
    query.addQueryItem(QStringLiteral("language"), lang);
    query.addQueryItem(QStringLiteral("uselang"), lang);
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("item"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);
    return url;
}

QUrl wikidataEntitiesUrl(const QStringList &ids)
{
    QUrl url(QStringLiteral("https://www.wikidata.org/w/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("wbgetentities"));
    query.addQueryItem(QStringLiteral("ids"), queryValue(ids.join(QLatin1Char('|'))));
    query.addQueryItem(
        QStringLiteral("props"), QStringLiteral("labels|descriptions|claims|sitelinks/urls"));
    query.addQueryItem(QStringLiteral("languages"), QStringLiteral("zh|ja|en"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);
    return url;
}

QUrl wikidataLabelsUrl(const QStringList &ids)
{
    QUrl url(QStringLiteral("https://www.wikidata.org/w/api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("action"), QStringLiteral("wbgetentities"));
    query.addQueryItem(QStringLiteral("ids"), queryValue(ids.join(QLatin1Char('|'))));
    query.addQueryItem(QStringLiteral("props"), QStringLiteral("labels"));
    query.addQueryItem(QStringLiteral("languages"), QStringLiteral("zh|ja|en"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(query);
    return url;
}

QUrl wikidataEntityPageUrl(const QString &id)
{
    return QUrl(QStringLiteral("https://www.wikidata.org/wiki/") + id);
}

core::Result<QList<WikidataSearchHit>> parseWikidataSearch(const QByteArray &json)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kParseFailed),
            .message = QStringLiteral("Failed to parse Wikidata search results"),
            .detail = parseErr.error != QJsonParseError::NoError
                ? parseErr.errorString()
                : QStringLiteral("Invalid JSON object"),
        };
    }

    const QJsonObject root = doc.object();
    const QJsonArray searchArr = root.value(QStringLiteral("search")).toArray();
    QList<WikidataSearchHit> hits;
    hits.reserve(searchArr.size());
    for (const auto &val : searchArr) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject item = val.toObject();
        WikidataSearchHit hit;
        hit.id = item.value(QStringLiteral("id")).toString();
        hit.label = item.value(QStringLiteral("label")).toString();
        hit.description = item.value(QStringLiteral("description")).toString();
        hits.append(hit);
    }
    return hits;
}

core::Result<QHash<QString, WikidataEntity>> parseWikidataEntities(const QByteArray &json)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kParseFailed),
            .message = QStringLiteral("Failed to parse Wikidata entities"),
            .detail = parseErr.error != QJsonParseError::NoError
                ? parseErr.errorString()
                : QStringLiteral("Invalid JSON object"),
        };
    }

    const QJsonObject root = doc.object();
    const QJsonObject entitiesObj = root.value(QStringLiteral("entities")).toObject();
    QHash<QString, WikidataEntity> result;

    for (auto it = entitiesObj.begin(); it != entitiesObj.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject entityObj = it.value().toObject();
        if (entityObj.contains(QStringLiteral("missing"))) {
            continue;
        }

        const WikidataEntity entity = parseSingleEntity(it.key(), entityObj);
        result.insert(entity.id, entity);
    }

    return result;
}

QString preferredLabel(const WikidataEntity &entity)
{
    static const auto s_langs = {
        QStringLiteral("zh"),
        QStringLiteral("ja"),
        QStringLiteral("en"),
    };
    for (const auto &lang : s_langs) {
        if (const auto it = entity.labels.find(lang); it != entity.labels.end() && !it->isEmpty()) {
            return *it;
        }
    }
    return entity.id;
}

QStringList searchLanguagesFor(const QString &name)
{
    if (hasKana(name)) {
        return { QStringLiteral("ja") };
    }
    if (hasHan(name)) {
        return { QStringLiteral("ja"), QStringLiteral("zh") };
    }
    return { QStringLiteral("en"), QStringLiteral("ja") };
}

std::optional<QString> pickArtist(
    const QList<WikidataSearchHit> &hits, const QHash<QString, WikidataEntity> &entities)
{
    for (const auto &hit : hits) {
        const auto it = entities.find(hit.id);
        if (it != entities.end() && isArtistEntity(*it)) {
            return hit.id;
        }
    }
    return std::nullopt;
}

std::optional<QString> pickSong(const QList<WikidataSearchHit> &hits,
    const QHash<QString, WikidataEntity> &entities, const QString &artistQid)
{
    if (artistQid.isEmpty()) {
        return std::nullopt;
    }
    for (const auto &hit : hits) {
        const auto it = entities.find(hit.id);
        if (it != entities.end() && isSongEntity(*it)) {
            const QStringList performers = it->itemClaims.value(QStringLiteral("P175"));
            if (performers.contains(artistQid)) {
                return hit.id;
            }
        }
    }
    return std::nullopt;
}

std::optional<QString> pickAlbum(const QList<WikidataSearchHit> &hits,
    const QHash<QString, WikidataEntity> &entities, const QString &artistQid)
{
    if (artistQid.isEmpty()) {
        return std::nullopt;
    }
    for (const auto &hit : hits) {
        const auto it = entities.find(hit.id);
        if (it != entities.end() && isAlbumEntity(*it)) {
            const QStringList performers = it->itemClaims.value(QStringLiteral("P175"));
            if (performers.contains(artistQid)) {
                return hit.id;
            }
        }
    }
    return std::nullopt;
}

QStringList factPropertiesFor(KnowledgeKind kind)
{
    switch (kind) {
    case KnowledgeKind::Artist:
        return {
            QStringLiteral("P571"),
            QStringLiteral("P569"),
            QStringLiteral("P27"),
            QStringLiteral("P495"),
            QStringLiteral("P136"),
            QStringLiteral("P264"),
            QStringLiteral("P527"),
            QStringLiteral("P463"),
        };
    case KnowledgeKind::Song:
        return {
            QStringLiteral("P577"),
            QStringLiteral("P175"),
            QStringLiteral("P86"),
            QStringLiteral("P676"),
            QStringLiteral("P162"),
            QStringLiteral("P136"),
            QStringLiteral("P264"),
            QStringLiteral("P361"),
            QStringLiteral("P144"),
            QStringLiteral("P155"),
            QStringLiteral("P156"),
        };
    case KnowledgeKind::Album:
        return {
            QStringLiteral("P577"),
            QStringLiteral("P175"),
            QStringLiteral("P162"),
            QStringLiteral("P136"),
            QStringLiteral("P264"),
            QStringLiteral("P155"),
            QStringLiteral("P156"),
        };
    }
    return { };
}

QStringList referencedIds(const WikidataEntity &entity, KnowledgeKind kind)
{
    const QStringList properties = factPropertiesFor(kind);
    QStringList result;
    for (const auto &prop : properties) {
        const auto it = entity.itemClaims.find(prop);
        if (it != entity.itemClaims.end()) {
            for (const auto &qid : *it) {
                if (!qid.isEmpty() && !result.contains(qid)) {
                    result.append(qid);
                }
            }
        }
    }
    return result;
}

QList<KnowledgeFact> extractFacts(const WikidataEntity &entity, KnowledgeKind kind,
    const QHash<QString, WikidataEntity> &labelEntities)
{
    const QStringList properties = factPropertiesFor(kind);
    QList<KnowledgeFact> facts;

    for (const auto &prop : properties) {
        // Check time claims
        if (const auto tIt = entity.timeClaims.find(prop);
            tIt != entity.timeClaims.end() && !tIt->text.isEmpty()) {
            facts.append(KnowledgeFact {
                .property = prop,
                .value = tIt->text,
            });
            continue;
        }

        // Check item claims
        if (const auto iIt = entity.itemClaims.find(prop);
            iIt != entity.itemClaims.end() && !iIt->isEmpty()) {
            QStringList labelList;
            labelList.reserve(iIt->size());
            for (const auto &qid : *iIt) {
                if (const auto lIt = labelEntities.find(qid); lIt != labelEntities.end()) {
                    labelList.append(preferredLabel(*lIt));
                } else {
                    labelList.append(qid);
                }
            }
            if (!labelList.isEmpty()) {
                facts.append(KnowledgeFact {
                    .property = prop,
                    .value = labelList.join(QStringLiteral("、")),
                });
            }
        }
    }

    return facts;
}

QString factPropertyName(const QString &property)
{
    if (property == QStringLiteral("P577")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Release date");
    }
    if (property == QStringLiteral("P571")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Inception");
    }
    if (property == QStringLiteral("P569")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Date of birth");
    }
    if (property == QStringLiteral("P27")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Country of citizenship");
    }
    if (property == QStringLiteral("P495")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Country of origin");
    }
    if (property == QStringLiteral("P136")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Genre");
    }
    if (property == QStringLiteral("P264")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Record label");
    }
    if (property == QStringLiteral("P527")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Members");
    }
    if (property == QStringLiteral("P463")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Member of");
    }
    if (property == QStringLiteral("P175")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Performer");
    }
    if (property == QStringLiteral("P86")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Composer");
    }
    if (property == QStringLiteral("P676")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Lyricist");
    }
    if (property == QStringLiteral("P162")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Producer");
    }
    if (property == QStringLiteral("P361")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Part of");
    }
    if (property == QStringLiteral("P144")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Based on");
    }
    if (property == QStringLiteral("P155")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Follows");
    }
    if (property == QStringLiteral("P156")) {
        return QCoreApplication::translate("linernotes::guide::Wikidata", "Followed by");
    }
    return property;
}

} // namespace linernotes::guide
