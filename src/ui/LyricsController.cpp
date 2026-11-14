// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LyricsController.h"

#include "AppSettings.h"
#include "ErrorText.h"
#include "UiLogging.h"

#include <QFile>
#include <QVariantMap>

#include <ai/AiEnums.h>
#include <guide/LocalLyrics.h>
#include <guide/LyricsTranslation.h>

namespace linernotes::ui {

LyricsController::LyricsController(guide::LyricsService &lyricsService,
    guide::LyricsTranslator &translator, guide::LineExplainer &explainer,
    const ai::AiConfig &aiConfig, core::Settings &settings, player::Player &player,
    NowPlaying &nowPlaying, QObject *parent)
    : QObject(parent)
    , m_lyricsService(lyricsService)
    , m_translator(translator)
    , m_explainer(explainer)
    , m_aiConfig(aiConfig)
    , m_settings(settings)
    , m_player(player)
    , m_nowPlaying(nowPlaying)
{
    connect(&m_nowPlaying, &NowPlaying::changed, this, &LyricsController::onNowPlayingChanged);
    connect(&m_lyricsService, &guide::LyricsService::finished, this,
        &LyricsController::onLyricsFinished);
    connect(
        &m_player, &player::Player::positionChanged, this, &LyricsController::onPositionChanged);
    connect(&m_translator, &guide::LyricsTranslator::finished, this,
        &LyricsController::onTranslationFinished);
    connect(&m_explainer, &guide::LineExplainer::delta, this, &LyricsController::onExplainDelta);
    connect(
        &m_explainer, &guide::LineExplainer::finished, this, &LyricsController::onExplainFinished);
    connect(&m_aiConfig, &ai::AiConfig::changed, this, &LyricsController::translationChanged);
    connect(&m_aiConfig, &ai::AiConfig::changed, this, &LyricsController::explanationChanged);

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

bool LyricsController::translationAvailable() const
{
    return m_status == guide::LyricsStatus::Found
        && m_aiConfig.resolve(ai::Purpose::Guide).has_value()
        && guide::needsTranslation(m_parsedLyrics);
}

bool LyricsController::showTranslation() const
{
    return m_settings.value(kLyricsShowTranslation);
}

void LyricsController::setShowTranslation(bool show)
{
    if (show == showTranslation()) {
        return;
    }
    m_settings.setValue(kLyricsShowTranslation, show);
    rebuildLines();
    emit linesChanged();
    emit translationChanged();
    if (show && translationAvailable()) {
        if (m_llmTranslations.isEmpty() && !m_translating) {
            startTranslation();
        }
    }
}

bool LyricsController::translating() const
{
    return m_translating;
}

QString LyricsController::translationError() const
{
    return m_translationError;
}

int LyricsController::explainIndex() const
{
    return m_explainIndex;
}

QString LyricsController::explanationLine() const
{
    return m_explanationLine;
}

QString LyricsController::explanation() const
{
    return m_explanation;
}

bool LyricsController::explaining() const
{
    return m_explaining;
}

QString LyricsController::explanationError() const
{
    return m_explanationError;
}

bool LyricsController::explainAvailable() const
{
    return m_status == guide::LyricsStatus::Found
        && m_aiConfig.resolve(ai::Purpose::Guide).has_value();
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
    m_translator.cancel();
    closeExplanation();
    m_translating = false;
    m_translationError.clear();
    m_llmTranslations.clear();
    emit translationChanged();

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
    m_translator.cancel();
    closeExplanation();
    m_translating = false;
    m_translationError.clear();
    m_llmTranslations.clear();
    emit translationChanged();

    m_loading = true;
    emit statusChanged();
    m_lyricsService.refetch(m_currentTrackId);
}

void LyricsController::retryTranslation()
{
    if (m_currentTrackId <= 0 || !translationAvailable()) {
        return;
    }
    startTranslation();
}

void LyricsController::explainLine(int index)
{
    if (m_currentTrackId <= 0 || !explainAvailable()) {
        return;
    }
    if (index < 0 || index >= m_parsedLyrics.lines.size()) {
        return;
    }
    if (m_parsedLyrics.lines.at(index).text.trimmed().isEmpty()) {
        return;
    }

    m_explainer.cancel();
    m_explainIndex = index;
    m_explanationLine = m_parsedLyrics.lines.at(index).text;
    m_explanation.clear();
    m_explaining = true;
    m_explanationError.clear();
    emit explanationChanged();

    guide::LineExplainRequest req;
    req.trackId = m_currentTrackId;
    req.lineIndex = index;
    req.title = m_nowPlaying.title();
    req.artist = m_nowPlaying.artist();
    req.album = m_nowPlaying.album();
    req.lyrics = m_parsedLyrics;

    if (showTranslation() && !m_llmTranslations.isEmpty()) {
        req.translations = m_llmTranslations;
    } else {
        QStringList trans;
        trans.reserve(m_parsedLyrics.lines.size());
        for (const auto &line : m_parsedLyrics.lines) {
            trans.append(line.translation);
        }
        req.translations = trans;
    }

    m_explainer.explain(req);
}

void LyricsController::closeExplanation()
{
    m_explainer.cancel();
    m_explainIndex = -1;
    m_explanationLine.clear();
    m_explanation.clear();
    m_explaining = false;
    m_explanationError.clear();
    emit explanationChanged();
}

void LyricsController::startTranslation()
{
    if (m_currentTrackId <= 0 || m_status != guide::LyricsStatus::Found
        || !translationAvailable()) {
        return;
    }
    m_translating = true;
    m_translationError.clear();
    emit translationChanged();
    m_translator.translate(
        m_currentTrackId, m_rawText, m_parsedLyrics, m_nowPlaying.title(), m_nowPlaying.artist());
}

void LyricsController::rebuildLines()
{
    m_lines.clear();
    m_lines.reserve(m_parsedLyrics.lines.size());
    const bool useLlm = showTranslation();
    for (qsizetype i = 0; i < m_parsedLyrics.lines.size(); ++i) {
        const auto &line = m_parsedLyrics.lines.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("text"), line.text);
        QString translation = line.translation;
        if (translation.isEmpty() && useLlm && i < m_llmTranslations.size()) {
            translation = m_llmTranslations.at(i);
        }
        map.insert(QStringLiteral("translation"), translation);
        map.insert(QStringLiteral("timeMs"),
            line.timeMs.has_value() ? *line.timeMs : static_cast<qint64>(-1));
        m_lines.append(map);
    }
}

void LyricsController::onNowPlayingChanged()
{
    const bool hasTrack = m_nowPlaying.hasTrack();
    const qint64 trackId = hasTrack ? m_nowPlaying.trackId() : -1;
    if (trackId == m_currentTrackId) {
        return;
    }
    m_translator.cancel();
    closeExplanation();
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
    m_translating = false;
    m_translationError.clear();
    m_llmTranslations.clear();
    const bool indexChanged = (m_currentIndex != -1);
    m_currentIndex = -1;
    emit linesChanged();
    emit statusChanged();
    emit translationChanged();
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
    m_translator.cancel();
    closeExplanation();
    m_translating = false;
    m_translationError.clear();
    m_llmTranslations.clear();

    if (result.status == guide::LyricsStatus::Found) {
        m_source = result.lyrics.source;
        m_rawText = result.lyrics.text;
        m_parsedLyrics = guide::parseLyrics(result.lyrics.text);
        m_synced = m_parsedLyrics.synced;
        rebuildLines();
        const auto posMs = static_cast<qint64>(m_player.position() * 1000.0);
        const int newIndex = guide::lineIndexAt(m_parsedLyrics, posMs);
        const bool indexChanged = (newIndex != m_currentIndex);
        m_currentIndex = newIndex;
        emit linesChanged();
        emit statusChanged();
        emit translationChanged();
        if (indexChanged) {
            emit currentIndexChanged();
        }
        if (showTranslation() && translationAvailable()) {
            startTranslation();
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
        emit translationChanged();
        if (indexChanged) {
            emit currentIndexChanged();
        }
    }
}

void LyricsController::onTranslationFinished(
    qint64 trackId, const core::Result<QStringList> &result)
{
    if (trackId != m_currentTrackId) {
        return;
    }
    m_translating = false;
    if (!result.ok()) {
        m_translationError = userErrorText(result.error());
    } else {
        m_translationError.clear();
        m_llmTranslations = result.value();
        rebuildLines();
        emit linesChanged();
    }
    emit translationChanged();
}

void LyricsController::onExplainDelta(qint64 trackId, int lineIndex, const QString &text)
{
    if (trackId != m_currentTrackId || lineIndex != m_explainIndex) {
        return;
    }
    m_explanation.append(text);
    emit explanationChanged();
}

void LyricsController::onExplainFinished(
    qint64 trackId, int lineIndex, const core::Result<QString> &result)
{
    if (trackId != m_currentTrackId || lineIndex != m_explainIndex) {
        return;
    }
    m_explaining = false;
    if (!result.ok()) {
        m_explanationError = userErrorText(result.error());
    } else {
        m_explanation = result.value();
        m_explanationError.clear();
    }
    emit explanationChanged();
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
    m_translator.cancel();
    closeExplanation();
    m_loading = false;
    m_currentTrackId = -1;
    m_parsedLyrics = guide::ParsedLyrics { };
    m_lines.clear();
    m_synced = false;
    m_rawText.clear();
    m_status = guide::LyricsStatus::NotFound;
    m_source = guide::LyricsSource::Lrclib;
    m_translating = false;
    m_translationError.clear();
    m_llmTranslations.clear();
    const bool indexChanged = (m_currentIndex != -1);
    m_currentIndex = -1;
    emit linesChanged();
    emit statusChanged();
    emit translationChanged();
    if (indexChanged) {
        emit currentIndexChanged();
    }
}

} // namespace linernotes::ui
