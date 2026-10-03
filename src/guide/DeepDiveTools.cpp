// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DeepDiveTools.h"

#include "KnowledgeService.h"
#include "LrcParser.h"
#include "LyricsService.h"
#include "LyricsTranslation.h"
#include "LyricsTranslationStore.h"
#include "Wikidata.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace linernotes::guide {

QList<ai::ToolSpec> deepDiveToolSpecs()
{
    QList<ai::ToolSpec> specs;

    // 1. lookup_knowledge
    {
        QJsonObject params;
        params.insert(QStringLiteral("type"), QStringLiteral("object"));

        QJsonObject props;
        QJsonObject kindProp;
        kindProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        QJsonArray kindEnum;
        kindEnum.append(QStringLiteral("artist"));
        kindEnum.append(QStringLiteral("song"));
        kindEnum.append(QStringLiteral("album"));
        kindProp.insert(QStringLiteral("enum"), kindEnum);
        kindProp.insert(QStringLiteral("description"),
            QStringLiteral("The kind of entity to look up: artist, song, or album."));
        props.insert(QStringLiteral("kind"), kindProp);

        QJsonObject artistProp;
        artistProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        artistProp.insert(QStringLiteral("description"), QStringLiteral("The name of the artist."));
        props.insert(QStringLiteral("artist"), artistProp);

        QJsonObject titleProp;
        titleProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        titleProp.insert(QStringLiteral("description"),
            QStringLiteral("The title of the song or album (optional for artist kind)."));
        props.insert(QStringLiteral("title"), titleProp);

        params.insert(QStringLiteral("properties"), props);
        QJsonArray required;
        required.append(QStringLiteral("kind"));
        required.append(QStringLiteral("artist"));
        params.insert(QStringLiteral("required"), required);

        ai::ToolSpec spec;
        spec.name = QStringLiteral("lookup_knowledge");
        spec.description = QStringLiteral(
            "Look up Wikipedia and Wikidata entries for an artist, song, or album (can look up "
            "any artist or work beyond the current song; less popular works may not be found).");
        spec.parameters = params;
        specs.append(spec);
    }

    // 2. search_library
    {
        QJsonObject params;
        params.insert(QStringLiteral("type"), QStringLiteral("object"));

        QJsonObject props;
        QJsonObject textProp;
        textProp.insert(QStringLiteral("type"), QStringLiteral("string"));
        textProp.insert(QStringLiteral("description"),
            QStringLiteral(
                "Search keyword for matching track titles, artist names, or album names."));
        props.insert(QStringLiteral("text"), textProp);

        params.insert(QStringLiteral("properties"), props);
        QJsonArray required;
        required.append(QStringLiteral("text"));
        params.insert(QStringLiteral("required"), required);

        ai::ToolSpec spec;
        spec.name = QStringLiteral("search_library");
        spec.description = QStringLiteral(
            "Search the user's local music library by keyword for tracks, albums, and artists (by "
            "title, artist, or album name), returning metadata, favorite status, and play counts.");
        spec.parameters = params;
        specs.append(spec);
    }

    // 3. get_lyrics
    {
        QJsonObject params;
        params.insert(QStringLiteral("type"), QStringLiteral("object"));
        params.insert(QStringLiteral("properties"), QJsonObject());

        ai::ToolSpec spec;
        spec.name = QStringLiteral("get_lyrics");
        spec.description = QStringLiteral(
            "Retrieve the lyrics of the current song being discussed in this conversation "
            "(including existing Chinese translations if available).");
        spec.parameters = params;
        specs.append(spec);
    }

    return specs;
}

QString lookupResultJson(const KnowledgeResult &r)
{
    if (r.status == KnowledgeStatus::Failed) {
        return QStringLiteral(R"({"error":"lookup failed"})");
    }
    if (r.status == KnowledgeStatus::NotFound) {
        return QStringLiteral(R"({"found":false})");
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("found"), true);
    obj.insert(QStringLiteral("name"), r.entry.label);
    obj.insert(QStringLiteral("description"), r.entry.description);

    QJsonArray factsArr;
    for (const auto &fact : r.entry.facts) {
        QJsonObject factObj;
        factObj.insert(QStringLiteral("name"), factPropertyName(fact.property));
        factObj.insert(QStringLiteral("value"), fact.value);
        factsArr.append(factObj);
    }
    obj.insert(QStringLiteral("facts"), factsArr);

    QString extract;
    if (r.entry.summary.has_value()) {
        extract = r.entry.summary->extract;
        if (extract.size() > 1500) {
            extract = extract.left(1500);
        }
    }
    obj.insert(QStringLiteral("extract"), extract);

    QJsonArray sourceIds;
    if (r.entry.summary.has_value() && !r.entry.summary->extract.isEmpty()) {
        sourceIds.append(1);
        sourceIds.append(2);
    } else {
        sourceIds.append(1);
    }
    obj.insert(QStringLiteral("source_ids"), sourceIds);

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString searchResultJson(const library::SearchResults &r, const QHash<qint64, int> &plays)
{
    QJsonObject root;

    QJsonArray tracksArr;
    for (const auto &t : r.tracks) {
        QJsonObject tObj;
        tObj.insert(QStringLiteral("title"), t.title);
        tObj.insert(QStringLiteral("artist"), t.artist);
        tObj.insert(QStringLiteral("album"), t.album);
        if (t.year.has_value()) {
            tObj.insert(QStringLiteral("year"), *t.year);
        } else {
            tObj.insert(QStringLiteral("year"), QJsonValue(QJsonValue::Null));
        }
        const int playCount = plays.contains(t.trackId) ? plays.value(t.trackId) : t.playCount;
        tObj.insert(QStringLiteral("plays"), playCount);
        tracksArr.append(tObj);
    }
    root.insert(QStringLiteral("tracks"), tracksArr);

    QJsonArray albumsArr;
    for (const auto &a : r.albums) {
        QJsonObject aObj;
        aObj.insert(QStringLiteral("title"), a.title);
        aObj.insert(QStringLiteral("artist"), a.albumArtist);
        if (a.year.has_value()) {
            aObj.insert(QStringLiteral("year"), *a.year);
        } else {
            aObj.insert(QStringLiteral("year"), QJsonValue(QJsonValue::Null));
        }
        albumsArr.append(aObj);
    }
    root.insert(QStringLiteral("albums"), albumsArr);

    QJsonArray artistsArr;
    for (const auto &ar : r.artists) {
        QJsonObject arObj;
        arObj.insert(QStringLiteral("name"), ar.name);
        artistsArr.append(arObj);
    }
    root.insert(QStringLiteral("artists"), artistsArr);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QString lyricsResultJson(const LyricsResult &r, const QStringList &translations)
{
    if (r.status == LyricsStatus::Instrumental) {
        return QStringLiteral(R"({"lyrics":null,"status":"instrumental"})");
    }
    if (r.status == LyricsStatus::NotFound) {
        return QStringLiteral(R"({"lyrics":null,"status":"not_found"})");
    }
    if (r.status == LyricsStatus::Failed) {
        return QStringLiteral(R"({"lyrics":null,"status":"failed"})");
    }

    const auto parsed = parseLyrics(r.lyrics.text);
    QStringList lines;
    for (qsizetype i = 0; i < parsed.lines.size(); ++i) {
        const auto &line = parsed.lines.at(i);
        if (line.text.trimmed().isEmpty()) {
            continue;
        }
        QString trans;
        if (i < translations.size() && !translations.at(i).trimmed().isEmpty()) {
            trans = translations.at(i).trimmed();
        } else if (!line.translation.trimmed().isEmpty()) {
            trans = line.translation.trimmed();
        }

        if (!trans.isEmpty()) {
            lines.append(line.text + QStringLiteral(" / ") + trans);
        } else {
            lines.append(line.text);
        }
    }

    QString joined = lines.join(QLatin1Char('\n'));
    if (joined.size() > 4000) {
        joined = joined.left(4000);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("lyrics"), joined);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

DeepDiveTools::DeepDiveTools(library::Database &db, KnowledgeService &knowledge,
    LyricsService &lyrics, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_knowledge(knowledge)
    , m_lyrics(lyrics)
    , m_clock(clock)
{
    connect(&m_knowledge, &KnowledgeService::finished, this, &DeepDiveTools::onKnowledgeFinished);
    connect(&m_lyrics, &LyricsService::finished, this, &DeepDiveTools::onLyricsFinished);
}

void DeepDiveTools::execute(
    const QString &callId, const QString &name, const QString &argumentsJson, qint64 contextTrackId)
{
    if (name == QStringLiteral("lookup_knowledge")) {
        executeLookupKnowledge(callId, argumentsJson);
    } else if (name == QStringLiteral("search_library")) {
        executeSearchLibrary(callId, argumentsJson);
    } else if (name == QStringLiteral("get_lyrics")) {
        executeGetLyrics(callId, contextTrackId);
    } else {
        emit executed(callId, QStringLiteral(R"({"error":"unknown tool"})"), { });
    }
}

void DeepDiveTools::executeLookupKnowledge(const QString &callId, const QString &argumentsJson)
{
    QJsonParseError parseErr;
    const auto doc = QJsonDocument::fromJson(argumentsJson.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit executed(callId, QStringLiteral(R"({"error":"invalid arguments"})"), { });
        return;
    }

    const auto obj = doc.object();
    const QString kindStr = obj.value(QStringLiteral("kind")).toString().trimmed();
    const QString artist = obj.value(QStringLiteral("artist")).toString().trimmed();
    const QString title = obj.value(QStringLiteral("title")).toString().trimmed();

    if (artist.isEmpty()
        || (kindStr != QStringLiteral("artist") && kindStr != QStringLiteral("song")
            && kindStr != QStringLiteral("album"))) {
        emit executed(callId, QStringLiteral(R"({"error":"invalid arguments"})"), { });
        return;
    }

    KnowledgeQuery query;
    query.artist = artist;
    query.title = title;
    if (kindStr == QStringLiteral("artist")) {
        query.kind = KnowledgeKind::Artist;
    } else if (kindStr == QStringLiteral("song")) {
        query.kind = KnowledgeKind::Song;
    } else if (kindStr == QStringLiteral("album")) {
        query.kind = KnowledgeKind::Album;
    }

    const QString key = knowledgeKey(query);
    m_pendingLookups.append(PendingLookup { .callId = callId, .key = key });
    m_knowledge.lookup(query);
}

void DeepDiveTools::executeSearchLibrary(const QString &callId, const QString &argumentsJson)
{
    QJsonParseError parseErr;
    const auto doc = QJsonDocument::fromJson(argumentsJson.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit executed(callId, QStringLiteral(R"({"error":"invalid arguments"})"), { });
        return;
    }

    const auto obj = doc.object();
    if (!obj.contains(QStringLiteral("text")) || !obj.value(QStringLiteral("text")).isString()) {
        emit executed(callId, QStringLiteral(R"({"error":"invalid arguments"})"), { });
        return;
    }

    const QString text = obj.value(QStringLiteral("text")).toString();
    const auto dbConn = m_db.connection();
    if (!dbConn.ok()) {
        emit executed(callId, QStringLiteral(R"({"error":"database error"})"), { });
        return;
    }

    const library::LibrarySearch searcher(dbConn.value());
    const auto searchRes = searcher.search(text, 15, 5);
    if (!searchRes.ok()) {
        emit executed(callId, QStringLiteral(R"({"error":"search failed"})"), { });
        return;
    }

    QHash<qint64, int> plays;
    for (const auto &t : searchRes.value().tracks) {
        plays.insert(t.trackId, t.playCount);
    }

    const QString resultJson = searchResultJson(searchRes.value(), plays);
    emit executed(callId, resultJson, { });
}

void DeepDiveTools::executeGetLyrics(const QString &callId, qint64 contextTrackId)
{
    if (contextTrackId <= 0) {
        const LyricsResult notFoundRes {
            .trackId = 0,
            .status = LyricsStatus::NotFound,
            .lyrics = { },
        };
        emit executed(callId, lyricsResultJson(notFoundRes, { }), { });
        return;
    }

    m_pendingLyrics.append(PendingLyrics { .callId = callId, .trackId = contextTrackId });
    m_lyrics.request(contextTrackId);
}

void DeepDiveTools::onKnowledgeFinished(const KnowledgeResult &result)
{
    QList<PendingLookup> matches;
    for (qsizetype i = m_pendingLookups.size() - 1; i >= 0; --i) {
        if (m_pendingLookups.at(i).key == result.key) {
            matches.append(m_pendingLookups.takeAt(i));
        }
    }
    if (matches.isEmpty()) {
        return;
    }

    const QString resultJson = lookupResultJson(result);
    DeepDiveSourceList sources;
    if (result.status == KnowledgeStatus::Found) {
        if (result.entry.summary.has_value() && !result.entry.summary->extract.isEmpty()) {
            sources.append(DeepDiveSource {
                .label = QStringLiteral("Wikipedia (%1)").arg(result.entry.summary->lang),
                .url = result.entry.summary->pageUrl,
            });
        }
        const QUrl wikiDataUrl = result.entry.wikidataUrl.isValid()
            ? result.entry.wikidataUrl
            : wikidataEntityPageUrl(result.entry.qid);
        if (wikiDataUrl.isValid()) {
            sources.append(DeepDiveSource {
                .label = QStringLiteral("Wikidata"),
                .url = wikiDataUrl,
            });
        }
    }

    for (const auto &p : matches) {
        emit executed(p.callId, resultJson, sources);
    }
}

void DeepDiveTools::onLyricsFinished(const LyricsResult &result)
{
    QList<PendingLyrics> matches;
    for (qsizetype i = m_pendingLyrics.size() - 1; i >= 0; --i) {
        if (m_pendingLyrics.at(i).trackId == result.trackId) {
            matches.append(m_pendingLyrics.takeAt(i));
        }
    }
    if (matches.isEmpty()) {
        return;
    }

    QStringList translations;
    if (result.status == LyricsStatus::Found) {
        const LyricsTranslationStore store(m_db, m_clock);
        const QString hash = lyricsHash(result.lyrics.text);
        const auto transRes = store.get(result.trackId, hash);
        if (transRes.ok() && transRes.value().has_value()) {
            translations = transRes.value().value();
        }
    }

    const QString resultJson = lyricsResultJson(result, translations);
    for (const auto &p : matches) {
        emit executed(p.callId, resultJson, { });
    }
}

} // namespace linernotes::guide
