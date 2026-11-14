// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <ai/AiConfig.h>
#include <core/Result.h>
#include <core/Settings.h>
#include <guide/LineExplainer.h>
#include <guide/LrcParser.h>
#include <guide/Lyrics.h>
#include <guide/LyricsService.h>
#include <guide/LyricsTranslator.h>
#include <player/Player.h>
#include <ui/NowPlaying.h>

namespace linernotes::ui {

class LyricsController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LyricsController)

    Q_PROPERTY(QVariantList lines READ lines NOTIFY linesChanged)
    Q_PROPERTY(bool synced READ synced NOTIFY linesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(linernotes::guide::LyricsStatus status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY statusChanged)
    Q_PROPERTY(linernotes::guide::LyricsSource source READ source NOTIFY linesChanged)
    Q_PROPERTY(QString rawText READ rawText NOTIFY linesChanged)
    Q_PROPERTY(bool translationAvailable READ translationAvailable NOTIFY translationChanged)
    Q_PROPERTY(bool showTranslation READ showTranslation WRITE setShowTranslation NOTIFY
            translationChanged)
    Q_PROPERTY(bool translating READ translating NOTIFY translationChanged)
    Q_PROPERTY(QString translationError READ translationError NOTIFY translationChanged)
    Q_PROPERTY(int explainIndex READ explainIndex NOTIFY explanationChanged)
    Q_PROPERTY(QString explanationLine READ explanationLine NOTIFY explanationChanged)
    Q_PROPERTY(QString explanation READ explanation NOTIFY explanationChanged)
    Q_PROPERTY(bool explaining READ explaining NOTIFY explanationChanged)
    Q_PROPERTY(QString explanationError READ explanationError NOTIFY explanationChanged)
    Q_PROPERTY(bool explainAvailable READ explainAvailable NOTIFY explanationChanged)

public:
    explicit LyricsController(guide::LyricsService &lyricsService,
        guide::LyricsTranslator &translator, guide::LineExplainer &explainer,
        const ai::AiConfig &aiConfig, core::Settings &settings, player::Player &player,
        NowPlaying &nowPlaying, QObject *parent = nullptr);
    ~LyricsController() override = default;

    [[nodiscard]] QVariantList lines() const;
    [[nodiscard]] bool synced() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] guide::LyricsStatus status() const;
    [[nodiscard]] bool loading() const;
    [[nodiscard]] guide::LyricsSource source() const;
    [[nodiscard]] QString rawText() const;
    [[nodiscard]] bool translationAvailable() const;
    [[nodiscard]] bool showTranslation() const;
    void setShowTranslation(bool show);
    [[nodiscard]] bool translating() const;
    [[nodiscard]] QString translationError() const;
    [[nodiscard]] int explainIndex() const;
    [[nodiscard]] QString explanationLine() const;
    [[nodiscard]] QString explanation() const;
    [[nodiscard]] bool explaining() const;
    [[nodiscard]] QString explanationError() const;
    [[nodiscard]] bool explainAvailable() const;

    Q_INVOKABLE void seekToLine(int index);
    Q_INVOKABLE void saveManual(const QString &text);
    Q_INVOKABLE void importFile(const QUrl &fileUrl);
    Q_INVOKABLE void refetch();
    Q_INVOKABLE void retryTranslation();
    Q_INVOKABLE void explainLine(int index);
    Q_INVOKABLE void closeExplanation();

signals:
    void linesChanged();
    void currentIndexChanged();
    void statusChanged();
    void translationChanged();
    void explanationChanged();

private slots:
    void onNowPlayingChanged();
    void onLyricsFinished(const linernotes::guide::LyricsResult &result);
    void onTranslationFinished(qint64 trackId, const linernotes::core::Result<QStringList> &result);
    void onPositionChanged(double position);
    void onExplainDelta(qint64 trackId, int lineIndex, const QString &text);
    void onExplainFinished(
        qint64 trackId, int lineIndex, const linernotes::core::Result<QString> &result);

private:
    void clear();
    void startTranslation();
    void rebuildLines();

    guide::LyricsService &m_lyricsService;
    guide::LyricsTranslator &m_translator;
    guide::LineExplainer &m_explainer;
    const ai::AiConfig &m_aiConfig;
    core::Settings &m_settings;
    player::Player &m_player;
    NowPlaying &m_nowPlaying;

    qint64 m_currentTrackId = -1;
    guide::ParsedLyrics m_parsedLyrics;
    QVariantList m_lines;
    bool m_synced = false;
    int m_currentIndex = -1;
    guide::LyricsStatus m_status = guide::LyricsStatus::NotFound;
    bool m_loading = false;
    guide::LyricsSource m_source = guide::LyricsSource::Lrclib;
    QString m_rawText;

    QStringList m_llmTranslations;
    bool m_translating = false;
    QString m_translationError;

    int m_explainIndex = -1;
    QString m_explanationLine;
    QString m_explanation;
    bool m_explaining = false;
    QString m_explanationError;
};

} // namespace linernotes::ui
