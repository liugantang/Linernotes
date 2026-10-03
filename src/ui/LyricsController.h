// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <guide/LrcParser.h>
#include <guide/Lyrics.h>
#include <guide/LyricsService.h>
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

public:
    explicit LyricsController(guide::LyricsService &lyricsService, player::Player &player,
        NowPlaying &nowPlaying, QObject *parent = nullptr);
    ~LyricsController() override = default;

    [[nodiscard]] QVariantList lines() const;
    [[nodiscard]] bool synced() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] guide::LyricsStatus status() const;
    [[nodiscard]] bool loading() const;
    [[nodiscard]] guide::LyricsSource source() const;
    [[nodiscard]] QString rawText() const;

    Q_INVOKABLE void seekToLine(int index);
    Q_INVOKABLE void saveManual(const QString &text);
    Q_INVOKABLE void importFile(const QUrl &fileUrl);
    Q_INVOKABLE void refetch();

signals:
    void linesChanged();
    void currentIndexChanged();
    void statusChanged();

private slots:
    void onNowPlayingChanged();
    void onLyricsFinished(const linernotes::guide::LyricsResult &result);
    void onPositionChanged(double position);

private:
    void clear();

    guide::LyricsService &m_lyricsService;
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
};

} // namespace linernotes::ui
