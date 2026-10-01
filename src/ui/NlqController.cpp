// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "NlqController.h"

#include <QCoreApplication>
#include <QTimeZone>

#include <library/LibraryQuery.h>
#include <library/LibrarySearch.h>
#include <nlq/LibrarySummary.h>
#include <nlq/OfflineParser.h>
#include <nlq/QueryRunner.h>
#include <ui/ErrorText.h>
#include <ui/Format.h>
#include <ui/SmartLabels.h>
#include <ui/UiLogging.h>

#include <algorithm>
#include <utility>

namespace linernotes::ui {

namespace {

QString offlineExplanation(const QString &ignored)
{
    if (ignored.isEmpty()) {
        return QCoreApplication::translate(
            "linernotes::ui::NlqController", "Understood with simple keyword rules.");
    }
    return QCoreApplication::translate("linernotes::ui::NlqController",
        "Understood with simple keyword rules; ignored \u201c%1\u201d.")
        .arg(ignored);
}

QVariantList populateTrackRows(const QList<library::TrackRow> &tracks)
{
    QVariantList rows;
    rows.reserve(tracks.size());
    for (const auto &t : tracks) {
        QVariantMap map;
        map.insert(QStringLiteral("trackId"), t.trackId);
        map.insert(QStringLiteral("title"), t.title);
        map.insert(QStringLiteral("artist"), t.artist);
        map.insert(QStringLiteral("album"), t.album);
        map.insert(QStringLiteral("albumId"), t.albumId.value_or(0));
        map.insert(QStringLiteral("durationText"), formatDuration(t.durationMs));
        map.insert(QStringLiteral("coverHash"), t.coverHash);
        rows.append(map);
    }
    return rows;
}

QVariantList populateAlbumRows(const QList<library::AlbumRow> &albums)
{
    QVariantList rows;
    rows.reserve(albums.size());
    for (const auto &a : albums) {
        QVariantMap map;
        map.insert(QStringLiteral("albumId"), a.albumId);
        map.insert(QStringLiteral("title"), a.title);
        map.insert(QStringLiteral("artist"), a.albumArtist);
        map.insert(
            QStringLiteral("year"), a.year.has_value() ? QVariant(a.year.value()) : QVariant());
        map.insert(QStringLiteral("coverHash"), a.coverHash);
        rows.append(map);
    }
    return rows;
}

QVariantList populateArtistRows(const QList<library::ArtistRow> &artists)
{
    QVariantList rows;
    rows.reserve(artists.size());
    for (const auto &ar : artists) {
        QVariantMap map;
        map.insert(QStringLiteral("artistId"), ar.artistId);
        map.insert(QStringLiteral("name"), ar.name);
        map.insert(QStringLiteral("trackCount"), ar.trackCount);
        rows.append(map);
    }
    return rows;
}

bool queryUsesPlayStats(const nlq::Query &query)
{
    if (query.rule.playedFrom.has_value() || query.rule.playedTo.has_value()) {
        return true;
    }
    for (const auto &cond : query.rule.conditions) {
        if (cond.field == library::SmartField::PlayCount
            || cond.field == library::SmartField::SkipCount
            || cond.field == library::SmartField::CompletedCount
            || cond.field == library::SmartField::LastPlayed) {
            return true;
        }
    }
    return query.sortKey == nlq::SortKey::PlayCount || query.sortKey == nlq::SortKey::LastPlayed;
}

} // namespace

NlqController::NlqController(library::Database &db, ai::LlmService &llm, ai::PromptLibrary &prompts,
    const ai::AiConfig &aiConfig, LibraryActions &actions, PlaylistController &playlists,
    SettingsController &settingsController, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_aiConfig(aiConfig)
    , m_actions(actions)
    , m_playlists(playlists)
    , m_settingsController(settingsController)
    , m_interpreter(llm, prompts, this)
{
    connect(
        &m_interpreter, &nlq::Interpreter::finished, this, &NlqController::onInterpreterFinished);
    connect(&m_aiConfig, &ai::AiConfig::changed, this, &NlqController::refreshLlmConfigured);
    refreshLlmConfigured();
}

NlqController::State NlqController::state() const
{
    return m_state;
}

bool NlqController::isLlmConfigured() const
{
    return m_llmConfigured;
}

bool NlqController::isOffline() const
{
    return !m_llmConfigured;
}

void NlqController::refreshLlmConfigured()
{
    const bool configured = m_aiConfig.resolve(ai::Purpose::Query).has_value();
    if (m_llmConfigured != configured) {
        m_llmConfigured = configured;
        emit llmConfiguredChanged();
        emit offlineChanged();
    }
}

QString NlqController::explanation() const
{
    return m_explanation;
}

QString NlqController::errorText() const
{
    return m_errorText;
}

nlq::Entity NlqController::entity() const
{
    return m_entity;
}

QVariantList NlqController::rows() const
{
    return m_rows;
}

QStringList NlqController::chips() const
{
    return m_chips;
}

QVariantMap NlqController::clarification() const
{
    return m_clarification;
}

bool NlqController::hasConversation() const
{
    return m_currentQuery.has_value();
}

QString NlqController::emptyHint() const
{
    return m_emptyHint;
}

QVariantList NlqController::relaxations() const
{
    return m_relaxations;
}

void NlqController::submit(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (isOffline()) {
        submitOffline(trimmed);
        return;
    }

    cancel();
    m_pendingClarifications.clear();
    m_clarification.clear();
    m_errorText.clear();
    m_emptyHint.clear();
    m_relaxations.clear();
    m_lastRelaxations.clear();
    m_state = State::Interpreting;
    emit stateChanged();
    emit errorTextChanged();
    emit clarificationChanged();
    emit emptyHintChanged();
    emit relaxationsChanged();

    const QDate today = QDate::currentDate();
    const QString timeZoneId = QString::fromUtf8(QTimeZone::systemTimeZoneId());

    if (!m_cachedSummaryDate.has_value() || m_cachedSummaryDate.value() != today
        || m_cachedSummaryText.isEmpty()) {
        const auto summaryRes = nlq::buildLibrarySummary(m_db, today, timeZoneId);
        if (!summaryRes.ok()) {
            m_state = State::Failed;
            m_errorText = userErrorText(summaryRes.error());
            emit stateChanged();
            emit errorTextChanged();
            return;
        }
        m_cachedSummaryDate = today;
        m_cachedSummaryText = nlq::renderLibrarySummary(summaryRes.value());
    }

    const std::optional<nlq::Query> previous = m_currentQuery;
    m_interpreter.interpret(trimmed, m_cachedSummaryText, previous);
}

void NlqController::submitOffline(const QString &text)
{
    cancel();
    m_pendingClarifications.clear();
    m_clarification.clear();
    m_errorText.clear();
    m_emptyHint.clear();
    m_relaxations.clear();
    m_lastRelaxations.clear();

    const QDate today = QDate::currentDate();
    auto parse = nlq::parseOffline(text, today, m_currentQuery);

    if (!parse.leftover.isEmpty()) {
        handleOfflineLeftover(text, parse);
        return;
    }

    m_explanation = offlineExplanation({ });
    emit explanationChanged();
    executeQuery(parse.query);
}

void NlqController::handleOfflineLeftover(const QString &text, nlq::OfflineParse &parse)
{
    const auto candRes = nlq::findArtistCandidates(m_db, parse.leftover);
    if (!candRes.ok()) {
        failWith(candRes.error());
        return;
    }

    const auto &candidates = candRes.value();
    if (candidates.size() == 1) {
        handleOfflineSingleArtist(parse, candidates.at(0));
        return;
    }
    if (candidates.size() > 1) {
        handleOfflineDisambiguation(parse, candidates);
        return;
    }
    if (!parse.matchedRule) {
        handleOfflineKeywordSearch(text);
        return;
    }
    handleOfflineIgnoredLeftover(parse);
}

void NlqController::handleOfflineSingleArtist(
    nlq::OfflineParse &parse, const nlq::ArtistCandidate &candidate)
{
    const library::SmartCondition artistCond {
        .field = library::SmartField::Artist,
        .op = library::SmartOp::Is,
        .value = candidate.name,
        .value2 = { },
    };
    parse.query.rule.conditions.append(artistCond);
    m_explanation = offlineExplanation({ });
    emit explanationChanged();
    executeQuery(parse.query);
}

void NlqController::handleOfflineDisambiguation(
    nlq::OfflineParse &parse, QList<nlq::ArtistCandidate> candidates)
{
    if (candidates.size() > 8) {
        candidates = candidates.mid(0, 8);
    }
    const library::SmartCondition artistCond {
        .field = library::SmartField::Artist,
        .op = library::SmartOp::Is,
        .value = parse.leftover,
        .value2 = { },
    };
    parse.query.rule.conditions.append(artistCond);

    const nlq::Clarification clar {
        .conditionIndex = static_cast<int>(parse.query.rule.conditions.size()) - 1,
        .mention = parse.leftover,
        .candidates = candidates,
    };
    m_resolution.query = parse.query;
    m_resolution.clarifications = { clar };
    m_pendingClarifications = { clar };
    m_explanation = offlineExplanation({ });
    m_state = State::NeedsChoice;
    updateClarificationProperty();
    emit explanationChanged();
    emit stateChanged();
    emit clarificationChanged();
}

void NlqController::handleOfflineKeywordSearch(const QString &text)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        failWith(connRes.error());
        return;
    }
    const library::LibrarySearch searcher(connRes.value());
    const auto searchRes = searcher.search(text);
    if (!searchRes.ok()) {
        failWith(searchRes.error());
        return;
    }

    m_rows = populateTrackRows(searchRes.value().tracks);
    m_entity = nlq::Entity::Track;
    m_chips.clear();
    m_explanation = tr("Keyword search: %1").arg(text);
    m_currentQuery.reset();
    m_lastRelaxations.clear();
    m_relaxations.clear();
    if (m_rows.isEmpty()) {
        m_emptyHint = tr("Nothing in your library matches.");
    } else {
        m_emptyHint.clear();
    }
    m_state = State::Ready;
    m_errorText.clear();

    emit stateChanged();
    emit errorTextChanged();
    emit explanationChanged();
    emit rowsChanged();
    emit chipsChanged();
    emit entityChanged();
    emit hasConversationChanged();
    emit emptyHintChanged();
    emit relaxationsChanged();
}

void NlqController::handleOfflineIgnoredLeftover(const nlq::OfflineParse &parse)
{
    m_explanation = offlineExplanation(parse.leftover);
    emit explanationChanged();
    executeQuery(parse.query);
}

void NlqController::onInterpreterFinished()
{
    if (m_state != State::Interpreting) {
        return;
    }

    const auto &res = m_interpreter.result();
    if (!res.ok()) {
        m_state = State::Failed;
        m_errorText = userErrorText(res.error());
        emit stateChanged();
        emit errorTextChanged();
        return;
    }

    const auto &interp = res.value();
    m_explanation = interp.explanation;
    emit explanationChanged();

    const auto resolveRes = nlq::resolveArtists(m_db, interp.query);
    if (!resolveRes.ok()) {
        m_state = State::Failed;
        m_errorText = userErrorText(resolveRes.error());
        emit stateChanged();
        emit errorTextChanged();
        return;
    }

    m_resolution = resolveRes.value();
    m_pendingClarifications = m_resolution.clarifications;

    if (!m_pendingClarifications.isEmpty()) {
        m_state = State::NeedsChoice;
        updateClarificationProperty();
        emit stateChanged();
        emit clarificationChanged();
    } else {
        m_clarification.clear();
        emit clarificationChanged();
        executeQuery(m_resolution.query);
    }
}

void NlqController::updateClarificationProperty()
{
    if (m_pendingClarifications.isEmpty()) {
        m_clarification.clear();
        return;
    }

    const auto &clar = m_pendingClarifications.first();
    QVariantMap map;
    map.insert(QStringLiteral("mention"), clar.mention);

    QVariantList candList;
    candList.reserve(clar.candidates.size());
    for (const auto &c : clar.candidates) {
        QVariantMap candMap;
        candMap.insert(QStringLiteral("artistId"), c.artistId);
        candMap.insert(QStringLiteral("name"), c.name);
        candMap.insert(QStringLiteral("trackCount"), c.trackCount);
        candList.append(candMap);
    }
    map.insert(QStringLiteral("candidates"), candList);

    m_clarification = map;
}

void NlqController::chooseCandidate(int index)
{
    if (m_state != State::NeedsChoice || m_pendingClarifications.isEmpty()) {
        return;
    }

    const auto currentClar = m_pendingClarifications.takeFirst();
    if (index < 0 || index >= currentClar.candidates.size()) {
        return;
    }

    const auto &choice = currentClar.candidates.at(index);
    m_resolution.query
        = nlq::applyArtistChoice(m_resolution.query, currentClar.conditionIndex, choice);

    if (!m_pendingClarifications.isEmpty()) {
        updateClarificationProperty();
        emit clarificationChanged();
    } else {
        m_clarification.clear();
        emit clarificationChanged();
        executeQuery(m_resolution.query);
    }
}

void NlqController::newConversation()
{
    cancel();
    m_currentQuery.reset();
    m_resolution = nlq::Resolution { };
    m_pendingClarifications.clear();
    m_clarification.clear();
    m_explanation.clear();
    m_errorText.clear();
    m_rows.clear();
    m_chips.clear();
    m_emptyHint.clear();
    m_relaxations.clear();
    m_lastRelaxations.clear();
    m_state = State::Idle;

    emit stateChanged();
    emit explanationChanged();
    emit errorTextChanged();
    emit rowsChanged();
    emit chipsChanged();
    emit clarificationChanged();
    emit entityChanged();
    emit hasConversationChanged();
    emit emptyHintChanged();
    emit relaxationsChanged();
}

void NlqController::cancel()
{
    if (m_interpreter.isRunning()) {
        m_interpreter.cancel();
    }
    if (m_state == State::Interpreting) {
        m_state = m_currentQuery.has_value() ? State::Ready : State::Idle;
        emit stateChanged();
    }
}

void NlqController::applyRelaxation(int index)
{
    if (!m_currentQuery.has_value() || index < 0 || index >= m_lastRelaxations.size()) {
        return;
    }

    const auto &rel = m_lastRelaxations.at(index);
    if (rel.kind == nlq::RelaxKind::RemoveCondition) {
        if (rel.conditionIndex >= 0
            && rel.conditionIndex < m_currentQuery->rule.conditions.size()) {
            m_currentQuery->rule.conditions.removeAt(rel.conditionIndex);
            executeQuery(m_currentQuery.value());
        }
    } else if (rel.kind == nlq::RelaxKind::RemovePlayWindow) {
        m_currentQuery->rule.playedFrom = std::nullopt;
        m_currentQuery->rule.playedTo = std::nullopt;
        executeQuery(m_currentQuery.value());
    }
}

void NlqController::invalidateSummaryCache()
{
    m_cachedSummaryDate.reset();
    m_cachedSummaryText.clear();
}

void NlqController::failWith(const core::Error &error)
{
    m_state = State::Failed;
    m_errorText = userErrorText(error);
    m_rows.clear();
    m_emptyHint.clear();
    m_relaxations.clear();
    m_lastRelaxations.clear();
    emit stateChanged();
    emit errorTextChanged();
    emit rowsChanged();
    emit chipsChanged();
    emit entityChanged();
    emit hasConversationChanged();
    emit emptyHintChanged();
    emit relaxationsChanged();
}

QVariantList NlqController::loadRows(const QList<qint64> &ids, nlq::Entity entity) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return { };
    }

    const library::LibraryQuery lq(connRes.value());
    if (entity == nlq::Entity::Track) {
        const auto tracksRes = lq.tracksByIds(ids);
        return tracksRes.ok() ? populateTrackRows(tracksRes.value()) : QVariantList { };
    }
    if (entity == nlq::Entity::Album) {
        const auto albumsRes = lq.albumsByIds(ids);
        return albumsRes.ok() ? populateAlbumRows(albumsRes.value()) : QVariantList { };
    }
    if (entity == nlq::Entity::Artist) {
        const auto artistsRes = lq.artistsByIds(ids);
        return artistsRes.ok() ? populateArtistRows(artistsRes.value()) : QVariantList { };
    }
    return { };
}

QString NlqController::relaxationText(const nlq::Relaxation &rel, const nlq::Query &query) const
{
    QString label;
    if (rel.kind == nlq::RelaxKind::RemoveCondition) {
        if (rel.conditionIndex >= 0 && rel.conditionIndex < query.rule.conditions.size()) {
            label = smartConditionLabel(query.rule.conditions.at(rel.conditionIndex));
        }
    } else if (rel.kind == nlq::RelaxKind::RemovePlayWindow) {
        label = smartPlayWindowLabel(query.rule);
    }
    return tr("Remove \"%1\" \u2192 %n result(s)", "", rel.resultCount).arg(label);
}

QString NlqController::chooseEmptyHint(
    const nlq::EmptyResultAnalysis &analysis, const nlq::Query &query) const
{
    if (analysis.windowBeforeHistory) {
        const QString dateStr = analysis.firstPlayed.has_value()
            ? analysis.firstPlayed->toString(Qt::ISODate)
            : QString();
        return tr("Play history starts on %1, so there are no plays in the selected period.")
            .arg(dateStr);
    }
    if (!analysis.firstPlayed.has_value() && queryUsesPlayStats(query)) {
        return tr("No play history yet.");
    }
    if (!analysis.relaxations.isEmpty()) {
        return tr("No results match all conditions.");
    }
    return tr("Nothing in your library matches.");
}

void NlqController::updateEmptyAnalysis(const nlq::QueryRunner &runner, const nlq::Query &query)
{
    const auto analysisRes = nlq::analyzeEmptyResult(m_db, runner, query);
    if (!analysisRes.ok()) {
        m_lastRelaxations.clear();
        m_relaxations.clear();
        m_emptyHint = tr("Nothing in your library matches.");
        return;
    }

    const auto &analysis = analysisRes.value();
    m_lastRelaxations = analysis.relaxations;
    m_relaxations.clear();
    m_relaxations.reserve(analysis.relaxations.size());
    for (const auto &rel : analysis.relaxations) {
        QVariantMap map;
        map.insert(QStringLiteral("kind"), static_cast<int>(rel.kind));
        map.insert(QStringLiteral("conditionIndex"), rel.conditionIndex);
        map.insert(QStringLiteral("count"), rel.resultCount);
        map.insert(QStringLiteral("text"), relaxationText(rel, query));
        m_relaxations.append(map);
    }

    m_emptyHint = chooseEmptyHint(analysis, query);
}

void NlqController::executeQuery(const nlq::Query &query)
{
    m_currentQuery = query;
    m_entity = query.entity;

    const nlq::QueryRunner runner(m_db, m_settingsController.playCountRule());
    const auto runRes = runner.run(query);

    m_chips = nlqChips(query);

    if (!runRes.ok()) {
        failWith(runRes.error());
        return;
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        failWith(connRes.error());
        return;
    }

    m_rows = loadRows(runRes.value(), query.entity);

    if (m_rows.isEmpty()) {
        updateEmptyAnalysis(runner, query);
    } else {
        m_emptyHint.clear();
        m_relaxations.clear();
        m_lastRelaxations.clear();
    }

    m_state = State::Ready;
    m_errorText.clear();

    emit stateChanged();
    emit errorTextChanged();
    emit rowsChanged();
    emit chipsChanged();
    emit entityChanged();
    emit hasConversationChanged();
    emit emptyHintChanged();
    emit relaxationsChanged();
}

QList<qint64> NlqController::collectAllTrackIds() const
{
    QList<qint64> trackIds;
    if (m_rows.isEmpty()) {
        return trackIds;
    }

    if (!m_currentQuery.has_value()) {
        if (m_entity == nlq::Entity::Track) {
            trackIds.reserve(m_rows.size());
            for (const auto &rowVar : m_rows) {
                const auto map = rowVar.toMap();
                trackIds.append(map.value(QStringLiteral("trackId")).toLongLong());
            }
        }
        return trackIds;
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return trackIds;
    }
    const library::LibraryQuery lq(connRes.value());

    if (m_currentQuery->entity == nlq::Entity::Track) {
        trackIds.reserve(m_rows.size());
        for (const auto &rowVar : m_rows) {
            const auto map = rowVar.toMap();
            trackIds.append(map.value(QStringLiteral("trackId")).toLongLong());
        }
    } else if (m_currentQuery->entity == nlq::Entity::Album) {
        for (const auto &rowVar : m_rows) {
            const auto map = rowVar.toMap();
            const qint64 albumId = map.value(QStringLiteral("albumId")).toLongLong();
            library::TrackFilter filter;
            filter.albumId = albumId;
            const auto tRes
                = lq.trackIds(filter, library::TrackSortKey::Default, Qt::AscendingOrder);
            if (tRes.ok()) {
                trackIds.append(tRes.value());
            }
        }
    } else if (m_currentQuery->entity == nlq::Entity::Artist) {
        for (const auto &rowVar : m_rows) {
            const auto map = rowVar.toMap();
            const qint64 artistId = map.value(QStringLiteral("artistId")).toLongLong();
            library::TrackFilter filter;
            filter.artistId = artistId;
            const auto tRes
                = lq.trackIds(filter, library::TrackSortKey::Default, Qt::AscendingOrder);
            if (tRes.ok()) {
                trackIds.append(tRes.value());
            }
        }
    }
    return trackIds;
}

void NlqController::playAll()
{
    const QList<qint64> tracks = collectAllTrackIds();
    if (!tracks.isEmpty()) {
        m_actions.playTracks(tracks, 0, core::PlaySource::Nlq);
    }
}

void NlqController::enqueueAll()
{
    const QList<qint64> tracks = collectAllTrackIds();
    if (!tracks.isEmpty()) {
        m_actions.enqueue(tracks);
    }
}

void NlqController::playRow(int index)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }

    if (!m_currentQuery.has_value() || m_currentQuery->entity == nlq::Entity::Track) {
        const QList<qint64> allTracks = collectAllTrackIds();
        if (!allTracks.isEmpty()) {
            m_actions.playTracks(allTracks, index, core::PlaySource::Nlq);
        }
        return;
    }

    if (m_currentQuery->entity == nlq::Entity::Album) {
        const auto map = m_rows.at(index).toMap();
        const qint64 albumId = map.value(QStringLiteral("albumId")).toLongLong();
        const auto connRes = m_db.connection();
        if (!connRes.ok()) {
            return;
        }
        const library::LibraryQuery lq(connRes.value());
        library::TrackFilter filter;
        filter.albumId = albumId;
        const auto tRes = lq.trackIds(filter, library::TrackSortKey::Default, Qt::AscendingOrder);
        if (tRes.ok() && !tRes.value().isEmpty()) {
            m_actions.playTracks(tRes.value(), 0, core::PlaySource::Nlq);
        }
    } else if (m_currentQuery->entity == nlq::Entity::Artist) {
        const auto map = m_rows.at(index).toMap();
        const qint64 artistId = map.value(QStringLiteral("artistId")).toLongLong();
        const auto connRes = m_db.connection();
        if (!connRes.ok()) {
            return;
        }
        const library::LibraryQuery lq(connRes.value());
        library::TrackFilter filter;
        filter.artistId = artistId;
        const auto tRes = lq.trackIds(filter, library::TrackSortKey::Default, Qt::AscendingOrder);
        if (tRes.ok() && !tRes.value().isEmpty()) {
            m_actions.playTracks(tRes.value(), 0, core::PlaySource::Nlq);
        }
    }
}

void NlqController::saveAsPlaylist(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QList<qint64> tracks = collectAllTrackIds();
    m_playlists.createManual(trimmed, tracks);
}

} // namespace linernotes::ui
