// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <guide/GuideEnums.h>
#include <guide/Wikidata.h>
#include <guide/Wikipedia.h>

#include <cstdint>
#include <optional>

namespace linernotes::guide {

struct KnowledgeQuery {
    KnowledgeKind kind = KnowledgeKind::Artist;
    QString artist { };
    QString title { };
    bool operator==(const KnowledgeQuery &) const = default;
};

struct KnowledgeEntry {
    KnowledgeKind kind = KnowledgeKind::Artist;
    QString qid { };
    QString label { };
    QString description { };
    QUrl wikidataUrl { };
    std::optional<WikipediaSummary> summary;
    QList<KnowledgeFact> facts;
    qint64 fetchedAtMs = 0;
    bool operator==(const KnowledgeEntry &) const = default;
};

struct KnowledgeResult {
    QString key { };
    KnowledgeStatus status = KnowledgeStatus::NotFound;
    KnowledgeEntry entry { };
    bool operator==(const KnowledgeResult &) const = default;
};

QString primaryArtist(const QString &artist);
QString knowledgeKey(const KnowledgeQuery &q);

QJsonObject knowledgeEntryToJson(const KnowledgeEntry &e);
std::optional<KnowledgeEntry> knowledgeEntryFromJson(const QJsonObject &o);

} // namespace linernotes::guide
