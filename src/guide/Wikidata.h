// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <core/Result.h>
#include <guide/GuideEnums.h>

#include <optional>

namespace linernotes::guide {

struct WikidataSearchHit {
    QString id { };
    QString label { };
    QString description { };
    bool operator==(const WikidataSearchHit &) const = default;
};

struct WikidataTime {
    QString text { }; // precision 11 -> "2014-08-20", 10 -> "2014-08", 9 -> "2014", lower -> ""
    bool operator==(const WikidataTime &) const = default;
};

struct WikidataEntity {
    QString id { };
    QHash<QString, QString> labels; // "zh" / "ja" / "en" -> text
    QHash<QString, QString> descriptions;
    QHash<QString, QStringList> itemClaims; // property -> list of referenced QIDs
    QHash<QString, WikidataTime> timeClaims; // property -> first time value
    QHash<QString, QString> sitelinkTitles; // "zhwiki" -> article title
    QHash<QString, QUrl> sitelinkUrls; // "zhwiki" -> article URL
    bool operator==(const WikidataEntity &) const = default;
};

struct KnowledgeFact {
    QString property { };
    QString value { };
    bool operator==(const KnowledgeFact &) const = default;
};

QUrl wikidataSearchUrl(const QString &text, const QString &lang);
QUrl wikidataEntitiesUrl(const QStringList &ids);
QUrl wikidataLabelsUrl(const QStringList &ids);
QUrl wikidataEntityPageUrl(const QString &id);

core::Result<QList<WikidataSearchHit>> parseWikidataSearch(const QByteArray &json);
core::Result<QHash<QString, WikidataEntity>> parseWikidataEntities(const QByteArray &json);

QString preferredLabel(const WikidataEntity &entity);
QStringList searchLanguagesFor(const QString &name);

std::optional<QString> pickArtist(
    const QList<WikidataSearchHit> &hits, const QHash<QString, WikidataEntity> &entities);
std::optional<QString> pickSong(const QList<WikidataSearchHit> &hits,
    const QHash<QString, WikidataEntity> &entities, const QString &artistQid);
std::optional<QString> pickAlbum(const QList<WikidataSearchHit> &hits,
    const QHash<QString, WikidataEntity> &entities, const QString &artistQid);

QStringList factPropertiesFor(KnowledgeKind kind);
QStringList referencedIds(const WikidataEntity &entity, KnowledgeKind kind);
QList<KnowledgeFact> extractFacts(const WikidataEntity &entity, KnowledgeKind kind,
    const QHash<QString, WikidataEntity> &labelEntities);
QString factPropertyName(const QString &property);

} // namespace linernotes::guide
