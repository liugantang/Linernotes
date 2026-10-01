// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "NlqController.h"

#include <QTimeZone>

#include <library/LibraryQuery.h>
#include <nlq/LibrarySummary.h>
#include <nlq/QueryRunner.h>
#include <ui/ErrorText.h>
#include <ui/Format.h>
#include <ui/SmartLabels.h>
#include <ui/UiLogging.h>

#include <algorithm>
#include <utility>

namespace linernotes::ui {

namespace {

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

void NlqController::refreshLlmConfigured()
{
    const bool configured = m_aiConfig.resolve(ai::Purpose::Query).has_value();
    if (m_llmConfigured != configured) {
        m_llmConfigured = configured;
        emit llmConfiguredChanged();
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

QVariantList NlqController::chips() const
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

void NlqController::submit(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (!isLlmConfigured()) {
        m_state = State::Failed;
        m_errorText = tr("AI service is not configured.");
        emit stateChanged();
        emit errorTextChanged();
        return;
    }

    cancel();
    m_pendingClarifications.clear();
    m_clarification.clear();
    m_errorText.clear();
    m_state = State::Interpreting;
    emit stateChanged();
    emit errorTextChanged();
    emit clarificationChanged();

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
    m_state = State::Idle;

    emit stateChanged();
    emit explanationChanged();
    emit errorTextChanged();
    emit rowsChanged();
    emit chipsChanged();
    emit clarificationChanged();
    emit hasConversationChanged();
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

void NlqController::removeChip(int index)
{
    if (!m_currentQuery.has_value()) {
        return;
    }
    if (index >= 0 && index < m_currentQuery->rule.conditions.size()) {
        m_currentQuery->rule.conditions.removeAt(index);
        executeQuery(m_currentQuery.value());
    }
}

library::SmartCondition NlqController::conditionAt(int index) const
{
    if (m_currentQuery.has_value() && index >= 0
        && index < m_currentQuery->rule.conditions.size()) {
        return m_currentQuery->rule.conditions.at(index);
    }
    return { };
}

void NlqController::setCondition(int index, const library::SmartCondition &cond)
{
    if (!m_currentQuery.has_value()) {
        m_currentQuery = nlq::Query { };
    }
    if (index >= 0 && index < m_currentQuery->rule.conditions.size()) {
        m_currentQuery->rule.conditions.replace(index, cond);
    } else if (index == m_currentQuery->rule.conditions.size()) {
        m_currentQuery->rule.conditions.append(cond);
    }
    executeQuery(m_currentQuery.value());
}

void NlqController::setPlayWindow(const QString &from, const QString &to)
{
    if (!m_currentQuery.has_value()) {
        m_currentQuery = nlq::Query { };
    }
    m_currentQuery->rule.setPlayedFrom(from);
    m_currentQuery->rule.setPlayedTo(to);
    executeQuery(m_currentQuery.value());
}

void NlqController::setSort(nlq::SortKey key, Qt::SortOrder order)
{
    if (!m_currentQuery.has_value()) {
        m_currentQuery = nlq::Query { };
    }
    m_currentQuery->sortKey = key;
    m_currentQuery->sortOrder = order;
    executeQuery(m_currentQuery.value());
}

void NlqController::setLimit(int limit)
{
    if (!m_currentQuery.has_value()) {
        m_currentQuery = nlq::Query { };
    }
    m_currentQuery->limit = std::clamp(limit, 1, 500);
    executeQuery(m_currentQuery.value());
}

void NlqController::setEntity(nlq::Entity entity)
{
    if (!m_currentQuery.has_value()) {
        m_currentQuery = nlq::Query { };
    }
    m_currentQuery->entity = entity;
    executeQuery(m_currentQuery.value());
}

void NlqController::invalidateSummaryCache()
{
    m_cachedSummaryDate.reset();
    m_cachedSummaryText.clear();
}

void NlqController::executeQuery(const nlq::Query &query)
{
    m_currentQuery = query;
    m_entity = query.entity;

    const nlq::QueryRunner runner(m_db, m_settingsController.playCountRule());
    const auto runRes = runner.run(query);

    m_chips = nlqChipsToVariantList(nlqChips(query));

    if (!runRes.ok()) {
        m_state = State::Failed;
        m_errorText = userErrorText(runRes.error());
        m_rows.clear();
        emit stateChanged();
        emit errorTextChanged();
        emit rowsChanged();
        emit chipsChanged();
        emit entityChanged();
        emit hasConversationChanged();
        return;
    }

    const QList<qint64> &ids = runRes.value();
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        m_state = State::Failed;
        m_errorText = userErrorText(connRes.error());
        m_rows.clear();
        emit stateChanged();
        emit errorTextChanged();
        emit rowsChanged();
        emit chipsChanged();
        emit entityChanged();
        emit hasConversationChanged();
        return;
    }

    const library::LibraryQuery lq(connRes.value());
    if (query.entity == nlq::Entity::Track) {
        const auto tracksRes = lq.tracksByIds(ids);
        m_rows = tracksRes.ok() ? populateTrackRows(tracksRes.value()) : QVariantList { };
    } else if (query.entity == nlq::Entity::Album) {
        const auto albumsRes = lq.albumsByIds(ids);
        m_rows = albumsRes.ok() ? populateAlbumRows(albumsRes.value()) : QVariantList { };
    } else if (query.entity == nlq::Entity::Artist) {
        const auto artistsRes = lq.artistsByIds(ids);
        m_rows = artistsRes.ok() ? populateArtistRows(artistsRes.value()) : QVariantList { };
    }

    m_state = State::Ready;
    m_errorText.clear();

    emit stateChanged();
    emit errorTextChanged();
    emit rowsChanged();
    emit chipsChanged();
    emit entityChanged();
    emit hasConversationChanged();
}

QList<qint64> NlqController::collectAllTrackIds() const
{
    QList<qint64> trackIds;
    if (!m_currentQuery.has_value() || m_rows.isEmpty()) {
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
    if (index < 0 || index >= m_rows.size() || !m_currentQuery.has_value()) {
        return;
    }

    if (m_currentQuery->entity == nlq::Entity::Track) {
        const QList<qint64> allTracks = collectAllTrackIds();
        if (!allTracks.isEmpty()) {
            m_actions.playTracks(allTracks, index, core::PlaySource::Nlq);
        }
    } else if (m_currentQuery->entity == nlq::Entity::Album) {
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
