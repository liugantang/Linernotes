// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Knowledge.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <butler/TitleVersion.h>

namespace linernotes::guide {

namespace {

QString normalizeArtist(const QString &artist)
{
    return primaryArtist(artist).normalized(QString::NormalizationForm_KC).toLower().trimmed();
}

QString normalizeTitle(const QString &title)
{
    const auto resolved = butler::resolveTitle(title, butler::classifySuffix);
    return resolved.baseTitle.normalized(QString::NormalizationForm_KC).toLower().trimmed();
}

} // namespace

QString primaryArtist(const QString &artist)
{
    static const QRegularExpression s_splitRegex(QStringLiteral(R"( / |; |、)"));
    const auto parts = artist.split(s_splitRegex);
    if (!parts.isEmpty()) {
        return parts.at(0).trimmed();
    }
    return artist.trimmed();
}

QString knowledgeKey(const KnowledgeQuery &q)
{
    const QString normArtist = normalizeArtist(q.artist);
    switch (q.kind) {
    case KnowledgeKind::Artist:
        return QStringLiteral("artist|%1").arg(normArtist);
    case KnowledgeKind::Song:
        return QStringLiteral("song|%1|%2").arg(normArtist, normalizeTitle(q.title));
    case KnowledgeKind::Album:
        return QStringLiteral("album|%1|%2").arg(normArtist, normalizeTitle(q.title));
    }
    return { };
}

QJsonObject knowledgeEntryToJson(const KnowledgeEntry &e)
{
    QJsonObject obj;
    switch (e.kind) {
    case KnowledgeKind::Artist:
        obj.insert(QStringLiteral("kind"), QStringLiteral("artist"));
        break;
    case KnowledgeKind::Song:
        obj.insert(QStringLiteral("kind"), QStringLiteral("song"));
        break;
    case KnowledgeKind::Album:
        obj.insert(QStringLiteral("kind"), QStringLiteral("album"));
        break;
    }
    obj.insert(QStringLiteral("qid"), e.qid);
    obj.insert(QStringLiteral("label"), e.label);
    obj.insert(QStringLiteral("description"), e.description);
    obj.insert(QStringLiteral("wikidataUrl"), e.wikidataUrl.toString());
    obj.insert(QStringLiteral("fetchedAtMs"), e.fetchedAtMs);

    if (e.summary.has_value()) {
        QJsonObject sumObj;
        sumObj.insert(QStringLiteral("lang"), e.summary->lang);
        sumObj.insert(QStringLiteral("title"), e.summary->title);
        sumObj.insert(QStringLiteral("extract"), e.summary->extract);
        sumObj.insert(QStringLiteral("pageUrl"), e.summary->pageUrl.toString());
        obj.insert(QStringLiteral("summary"), sumObj);
    }

    QJsonArray factsArr;
    for (const auto &fact : e.facts) {
        QJsonObject factObj;
        factObj.insert(QStringLiteral("property"), fact.property);
        factObj.insert(QStringLiteral("value"), fact.value);
        factsArr.append(factObj);
    }
    obj.insert(QStringLiteral("facts"), factsArr);

    return obj;
}

std::optional<KnowledgeEntry> knowledgeEntryFromJson(const QJsonObject &o)
{
    if (!o.contains(QStringLiteral("kind")) || !o.contains(QStringLiteral("qid"))
        || !o.contains(QStringLiteral("label")) || !o.contains(QStringLiteral("description"))
        || !o.contains(QStringLiteral("wikidataUrl"))
        || !o.contains(QStringLiteral("fetchedAtMs"))) {
        return std::nullopt;
    }

    KnowledgeEntry entry;
    const QString kindStr = o.value(QStringLiteral("kind")).toString();
    if (kindStr == QStringLiteral("artist")) {
        entry.kind = KnowledgeKind::Artist;
    } else if (kindStr == QStringLiteral("song")) {
        entry.kind = KnowledgeKind::Song;
    } else if (kindStr == QStringLiteral("album")) {
        entry.kind = KnowledgeKind::Album;
    } else {
        return std::nullopt;
    }

    entry.qid = o.value(QStringLiteral("qid")).toString();
    entry.label = o.value(QStringLiteral("label")).toString();
    entry.description = o.value(QStringLiteral("description")).toString();
    entry.wikidataUrl = QUrl(o.value(QStringLiteral("wikidataUrl")).toString());
    entry.fetchedAtMs = o.value(QStringLiteral("fetchedAtMs")).toInteger();

    if (o.contains(QStringLiteral("summary")) && o.value(QStringLiteral("summary")).isObject()) {
        const QJsonObject sumObj = o.value(QStringLiteral("summary")).toObject();
        WikipediaSummary summary;
        summary.lang = sumObj.value(QStringLiteral("lang")).toString();
        summary.title = sumObj.value(QStringLiteral("title")).toString();
        summary.extract = sumObj.value(QStringLiteral("extract")).toString();
        summary.pageUrl = QUrl(sumObj.value(QStringLiteral("pageUrl")).toString());
        entry.summary = summary;
    } else {
        entry.summary = std::nullopt;
    }

    if (o.contains(QStringLiteral("facts")) && o.value(QStringLiteral("facts")).isArray()) {
        const QJsonArray factsArr = o.value(QStringLiteral("facts")).toArray();
        for (const auto &val : factsArr) {
            if (!val.isObject()) {
                continue;
            }
            const QJsonObject factObj = val.toObject();
            KnowledgeFact fact;
            fact.property = factObj.value(QStringLiteral("property")).toString();
            fact.value = factObj.value(QStringLiteral("value")).toString();
            entry.facts.append(fact);
        }
    }

    return entry;
}

} // namespace linernotes::guide
