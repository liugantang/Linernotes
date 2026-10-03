// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LyricsController.h"

#include "UiLogging.h"

#include <QFile>
#include <QVariantMap>

#include <guide/LocalLyrics.h>

namespace linernotes::ui {

LyricsController::LyricsController(guide::LyricsService &lyricsService, player::Player &player,
    NowPlaying &nowPlaying, QObject *parent)
    : QObject(parent)
    , m_lyricsService(lyricsService)
    , m_player(player)
    , m_nowPlaying(nowPlaying)
{
    connect(&m_nowPlaying, &NowPlaying::changed, this, &LyricsController::onNowPlayingChanged);
    connect(&m_lyricsService, &guide::LyricsService::finished, this,
        &LyricsController::onLyricsFinished);
    connect(
        &m_player, &player::Player::positionChanged, this, &LyricsController::onPositionChanged);

    if (m_nowPlaying.hasTrack()) {
        onNowPlayingChanged();
    }
}

QVariantList LyricsController::lines() const
{
    return m_lines;
}

bool LyricsController::synced() const
{
    return m_synced;
}

int LyricsController::currentIndex() const
{
    return m_currentIndex;
}

guide::LyricsStatus LyricsController::status() const
{
    return m_status;
}

bool LyricsController::loading() const
{
    return m_loading;
}

guide::LyricsSource LyricsController::source() const
{
    return m_source;
}

QString LyricsController::rawText() const
{
    return m_rawText;
}

void LyricsController::seekToLine(int index)
{
    if (!m_synced || index < 0 || index >= m_parsedLyrics.lines.size()) {
        return;
    }
    const auto &line = m_parsedLyrics.lines.at(index);
    if (line.timeMs.has_value()) {
        m_player.seek(static_cast<double>(*line.timeMs) / 1000.0);
    }
}

void LyricsController::saveManual(const QString &text)
{
    if (m_currentTrackId <= 0) {
        return;
    }
    m_loading = true;
    emit statusChanged();
    const auto res = m_lyricsService.saveManual(m_currentTrackId, text);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to save manual lyrics: %s", qPrintable(res.error().message));
    }
}

void LyricsController::importFile(const QUrl &fileUrl)
{
    if (m_currentTrackId <= 0) {
        return;
    }
    const QString filePath = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.path();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcUi, "Failed to open lyrics file: %s", qPrintable(filePath));
        return;
    }
    const QByteArray data = file.readAll();
    const QString text = guide::decodeLyricsFile(data);
    saveManual(text);
}

void LyricsController::refetch()
{
    if (m_currentTrackId <= 0) {
        return;
    }
    m_loading = true;
    emit statusChanged();
    m_lyricsService.refetch(m_currentTrackId);
}

void LyricsController::onNowPlayingChanged()
{
    const bool hasTrack = m_nowPlaying.hasTrack();
    const qint64 trackId = hasTrack ? m_nowPlaying.trackId() : -1;
    if (trackId == m_currentTrackId) {
        return;
    }
    m_currentTrackId = trackId;
    if (!hasTrack || trackId <= 0) {
        clear();
        return;
    }
    m_loading = true;
    m_parsedLyrics = guide::ParsedLyrics { };
    m_lines.clear();
    m_synced = false;
    m_rawText.clear();
    m_status = guide::LyricsStatus::NotFound;
    m_source = guide::LyricsSource::Lrclib;
    const bool indexChanged = (m_currentIndex != -1);
    m_currentIndex = -1;
    emit linesChanged();
    emit statusChanged();
    if (indexChanged) {
        emit currentIndexChanged();
    }

    m_lyricsService.request(m_currentTrackId);
}

void LyricsController::onLyricsFinished(const guide::LyricsResult &result)
{
    if (result.trackId != m_currentTrackId) {
        return;
    }
    m_loading = false;
    m_status = result.status;
    if (result.status == guide::LyricsStatus::Found) {
        m_source = result.lyrics.source;
        m_rawText = result.lyrics.text;
        m_parsedLyrics = guide::parseLyrics(result.lyrics.text);
        m_synced = m_parsedLyrics.synced;
        m_lines.clear();
        m_lines.reserve(m_parsedLyrics.lines.size());
        for (const auto &line : m_parsedLyrics.lines) {
            QVariantMap map;
            map.insert(QStringLiteral("text"), line.text);
            map.insert(QStringLiteral("translation"), line.translation);
            map.insert(QStringLiteral("timeMs"),
                line.timeMs.has_value() ? *line.timeMs : static_cast<qint64>(-1));
            m_lines.append(map);
        }
        const auto posMs = static_cast<qint64>(m_player.position() * 1000.0);
        const int newIndex = guide::lineIndexAt(m_parsedLyrics, posMs);
        const bool indexChanged = (newIndex != m_currentIndex);
        m_currentIndex = newIndex;
        emit linesChanged();
        emit statusChanged();
        if (indexChanged) {
            emit currentIndexChanged();
        }
    } else {
        m_parsedLyrics = guide::ParsedLyrics { };
        m_lines.clear();
        m_synced = false;
        m_rawText.clear();
        const bool indexChanged = (m_currentIndex != -1);
        m_currentIndex = -1;
        emit linesChanged();
        emit statusChanged();
        if (indexChanged) {
            emit currentIndexChanged();
        }
    }
}

void LyricsController::onPositionChanged(double position)
{
    if (!m_synced || m_currentTrackId <= 0) {
        return;
    }
    const auto posMs = static_cast<qint64>(position * 1000.0);
    const int newIndex = guide::lineIndexAt(m_parsedLyrics, posMs);
    if (newIndex != m_currentIndex) {
        m_currentIndex = newIndex;
        emit currentIndexChanged();
    }
}

void LyricsController::clear()
{
    m_loading = false;
    m_currentTrackId = -1;
    m_parsedLyrics = guide::ParsedLyrics { };
    m_lines.clear();
    m_synced = false;
    m_rawText.clear();
    m_status = guide::LyricsStatus::NotFound;
    m_source = guide::LyricsSource::Lrclib;
    const bool indexChanged = (m_currentIndex != -1);
    m_currentIndex = -1;
    emit linesChanged();
    emit statusChanged();
    if (indexChanged) {
        emit currentIndexChanged();
    }
}

} // namespace linernotes::ui
