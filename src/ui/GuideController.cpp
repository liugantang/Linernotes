// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "GuideController.h"

#include <QVariantMap>

#include <ai/AiEnums.h>
#include <guide/Wikidata.h>

namespace linernotes::ui {

GuideController::GuideController(guide::KnowledgeService &knowledgeService,
    guide::KnowledgeCardWriter &cardWriter, const ai::AiConfig &aiConfig, NowPlaying &nowPlaying,
    QObject *parent)
    : QObject(parent)
    , m_knowledgeService(knowledgeService)
    , m_cardWriter(cardWriter)
    , m_aiConfig(aiConfig)
    , m_nowPlaying(nowPlaying)
{
    connect(&m_nowPlaying, &NowPlaying::changed, this, &GuideController::onNowPlayingChanged);
    connect(&m_knowledgeService, &guide::KnowledgeService::finished, this,
        &GuideController::onKnowledgeFinished);
    connect(
        &m_cardWriter, &guide::KnowledgeCardWriter::delta, this, &GuideController::onWriterDelta);
    connect(&m_cardWriter, &guide::KnowledgeCardWriter::finished, this,
        &GuideController::onWriterFinished);

    if (m_nowPlaying.hasTrack()) {
        onNowPlayingChanged();
    }
}

bool GuideController::active() const
{
    return m_active;
}

void GuideController::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    emit activeChanged();

    if (m_active && m_nowPlaying.hasTrack()) {
        if (!m_lookupStarted || m_lastQueriedTrackId != m_currentTrackId) {
            startLookupsForCurrentTrack();
        }
    }
}

QVariantList GuideController::cards() const
{
    return m_cards;
}

bool GuideController::loading() const
{
    if (!m_nowPlaying.hasTrack() || !m_lookupStarted) {
        return false;
    }
    return !m_artistFinished || !m_songFinished || !m_albumFinished;
}

bool GuideController::failed() const
{
    if (!m_nowPlaying.hasTrack() || !m_lookupStarted) {
        return false;
    }
    return m_hasFailed;
}

void GuideController::retry()
{
    if (!m_nowPlaying.hasTrack()) {
        return;
    }
    m_cardWriter.cancelAll();
    cancelAndClear();
    startLookupsForCurrentTrack();
}

void GuideController::onNowPlayingChanged()
{
    if (!m_nowPlaying.hasTrack()) {
        m_cardWriter.cancelAll();
        m_currentTrackId = -1;
        m_currentArtist.clear();
        m_currentTitle.clear();
        m_currentAlbum.clear();
        m_lastQueriedTrackId = -1;
        cancelAndClear();
        emit cardsChanged();
        return;
    }

    const qint64 newTrackId = m_nowPlaying.trackId();
    const QString newArtist = m_nowPlaying.artist();
    const QString newTitle = m_nowPlaying.title();
    const QString newAlbum = m_nowPlaying.album();

    const bool trackChanged = (newTrackId != m_currentTrackId) || (newArtist != m_currentArtist)
        || (newTitle != m_currentTitle) || (newAlbum != m_currentAlbum);

    if (!trackChanged) {
        return;
    }

    m_cardWriter.cancelAll();
    m_currentTrackId = newTrackId;
    m_currentArtist = newArtist;
    m_currentTitle = newTitle;
    m_currentAlbum = newAlbum;

    cancelAndClear();

    if (m_active) {
        startLookupsForCurrentTrack();
    }
}

void GuideController::startLookupsForCurrentTrack()
{
    if (!m_nowPlaying.hasTrack() || m_currentArtist.trimmed().isEmpty()) {
        return;
    }

    m_lookupStarted = true;
    m_lastQueriedTrackId = m_currentTrackId;

    const guide::KnowledgeQuery artistQuery {
        .kind = guide::KnowledgeKind::Artist,
        .artist = m_currentArtist,
        .title = { },
    };
    const guide::KnowledgeQuery songQuery {
        .kind = guide::KnowledgeKind::Song,
        .artist = m_currentArtist,
        .title = m_currentTitle,
    };

    m_artistKey = guide::knowledgeKey(artistQuery);
    m_songKey = guide::knowledgeKey(songQuery);

    if (!m_currentAlbum.trimmed().isEmpty()) {
        const guide::KnowledgeQuery albumQuery {
            .kind = guide::KnowledgeKind::Album,
            .artist = m_currentArtist,
            .title = m_currentAlbum,
        };
        m_albumKey = guide::knowledgeKey(albumQuery);
        m_albumFinished = false;
    } else {
        m_albumKey.clear();
        m_albumFinished = true;
    }

    m_artistFinished = false;
    m_songFinished = false;
    m_hasFailed = false;

    m_songCard = CardState { .kind = guide::KnowledgeKind::Song };
    m_albumCard = CardState { .kind = guide::KnowledgeKind::Album };
    m_artistCard = CardState { .kind = guide::KnowledgeKind::Artist };

    rebuildCards();
    emit cardsChanged();

    m_knowledgeService.lookup(artistQuery);
}

void GuideController::applyFound(CardState &card, const guide::KnowledgeResult &result)
{
    card.found = true;
    card.entry = result.entry;
    card.isAi = false;
    if (m_aiConfig.resolve(ai::Purpose::Guide).has_value()) {
        card.writing = true;
        card.text.clear();
        m_cardWriter.write(result.key, result.entry);
    } else {
        card.writing = false;
        card.text = fallbackText(result.entry);
    }
}

void GuideController::startSongAndAlbumLookups()
{
    m_knowledgeService.lookup(guide::KnowledgeQuery {
        .kind = guide::KnowledgeKind::Song,
        .artist = m_currentArtist,
        .title = m_currentTitle,
    });
    if (!m_albumKey.isEmpty()) {
        m_knowledgeService.lookup(guide::KnowledgeQuery {
            .kind = guide::KnowledgeKind::Album,
            .artist = m_currentArtist,
            .title = m_currentAlbum,
        });
    }
}

void GuideController::onKnowledgeFinished(const guide::KnowledgeResult &result)
{
    if (result.key.isEmpty()) {
        return;
    }

    const bool isArtist = (result.key == m_artistKey);
    const bool isSong = (result.key == m_songKey);
    const bool isAlbum = (!m_albumKey.isEmpty() && result.key == m_albumKey);
    if (!isArtist && !isSong && !isAlbum) {
        return;
    }

    if (result.status == guide::KnowledgeStatus::Failed) {
        m_hasFailed = true;
    }

    if (isArtist) {
        m_artistFinished = true;
        if (result.status == guide::KnowledgeStatus::Failed) {
            // 艺人查询失败时歌曲/专辑不再发起（需要艺人的 QID）
            m_songFinished = true;
            m_albumFinished = true;
        } else {
            if (result.status == guide::KnowledgeStatus::Found) {
                applyFound(m_artistCard, result);
            }
            startSongAndAlbumLookups();
        }
    } else {
        CardState &card = isSong ? m_songCard : m_albumCard;
        (isSong ? m_songFinished : m_albumFinished) = true;
        if (result.status == guide::KnowledgeStatus::Found) {
            applyFound(card, result);
        }
    }

    rebuildCards();
    emit cardsChanged();
}

void GuideController::onWriterDelta(const QString &key, const QString &text)
{
    CardState *targetCard = nullptr;
    if (key == m_songKey) {
        targetCard = &m_songCard;
    } else if (key == m_albumKey) {
        targetCard = &m_albumCard;
    } else if (key == m_artistKey) {
        targetCard = &m_artistCard;
    }

    if (targetCard != nullptr && targetCard->found) {
        targetCard->text.append(text);
        targetCard->writing = true;
        rebuildCards();
        emit cardsChanged();
    }
}

void GuideController::onWriterFinished(
    const QString &key, const linernotes::core::Result<QString> &result)
{
    CardState *targetCard = nullptr;
    if (key == m_songKey) {
        targetCard = &m_songCard;
    } else if (key == m_albumKey) {
        targetCard = &m_albumCard;
    } else if (key == m_artistKey) {
        targetCard = &m_artistCard;
    }

    if (targetCard != nullptr && targetCard->found) {
        targetCard->writing = false;
        if (result.ok()) {
            targetCard->text = result.value();
            targetCard->isAi = true;
        } else {
            targetCard->text = fallbackText(targetCard->entry);
            targetCard->isAi = false;
        }
        rebuildCards();
        emit cardsChanged();
    }
}

void GuideController::cancelAndClear()
{
    m_lookupStarted = false;
    m_artistFinished = true;
    m_songFinished = true;
    m_albumFinished = true;
    m_hasFailed = false;

    m_artistKey.clear();
    m_songKey.clear();
    m_albumKey.clear();

    m_songCard = CardState { .kind = guide::KnowledgeKind::Song };
    m_albumCard = CardState { .kind = guide::KnowledgeKind::Album };
    m_artistCard = CardState { .kind = guide::KnowledgeKind::Artist };

    rebuildCards();
}

QString GuideController::fallbackText(const guide::KnowledgeEntry &entry)
{
    if (!entry.summary.has_value()) {
        return { };
    }
    QString extract = entry.summary->extract.trimmed();
    if (extract.isEmpty()) {
        return { };
    }
    if (extract.size() > 300) {
        extract = extract.left(300);
    }
    return extract + QStringLiteral(" [1]");
}

void GuideController::rebuildCards()
{
    m_cards.clear();
    if (m_songCard.found) {
        m_cards.append(buildCardMap(m_songCard));
    }
    if (m_albumCard.found) {
        m_cards.append(buildCardMap(m_albumCard));
    }
    if (m_artistCard.found) {
        m_cards.append(buildCardMap(m_artistCard));
    }
}

QVariantMap GuideController::buildCardMap(const CardState &card) const
{
    QVariantMap map;
    map.insert(QStringLiteral("kind"), QVariant::fromValue(card.kind));

    QString kindLabel;
    switch (card.kind) {
    case guide::KnowledgeKind::Song:
        kindLabel = tr("Song");
        break;
    case guide::KnowledgeKind::Album:
        kindLabel = tr("Album");
        break;
    case guide::KnowledgeKind::Artist:
        kindLabel = tr("Artist");
        break;
    }
    map.insert(QStringLiteral("kindLabel"), kindLabel);
    map.insert(QStringLiteral("title"), card.entry.label);
    map.insert(QStringLiteral("subtitle"), card.entry.description);
    map.insert(QStringLiteral("text"), card.text);
    map.insert(QStringLiteral("writing"), card.writing);
    map.insert(QStringLiteral("isAi"), card.isAi);

    QVariantList factsList;
    factsList.reserve(card.entry.facts.size());
    for (const auto &fact : card.entry.facts) {
        QVariantMap factMap;
        factMap.insert(QStringLiteral("name"), guide::factPropertyName(fact.property));
        factMap.insert(QStringLiteral("value"), fact.value);
        factsList.append(factMap);
    }
    map.insert(QStringLiteral("facts"), factsList);

    QVariantList sourcesList;
    if (card.entry.summary.has_value() && card.entry.summary->pageUrl.isValid()) {
        QVariantMap wikiSource;
        wikiSource.insert(QStringLiteral("index"), 1);
        wikiSource.insert(QStringLiteral("label"),
            QStringLiteral("Wikipedia (%1)").arg(card.entry.summary->lang));
        wikiSource.insert(QStringLiteral("url"), card.entry.summary->pageUrl.toString());
        sourcesList.append(wikiSource);
    }

    if (card.entry.wikidataUrl.isValid()) {
        QVariantMap wdSource;
        wdSource.insert(QStringLiteral("index"), 2);
        wdSource.insert(QStringLiteral("label"), QStringLiteral("Wikidata"));
        wdSource.insert(QStringLiteral("url"), card.entry.wikidataUrl.toString());
        sourcesList.append(wdSource);
    }
    map.insert(QStringLiteral("sources"), sourcesList);

    return map;
}

} // namespace linernotes::ui
