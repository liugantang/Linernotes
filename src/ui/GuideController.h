// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <ai/AiConfig.h>
#include <core/Result.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <guide/KnowledgeCardWriter.h>
#include <guide/KnowledgeService.h>
#include <ui/NowPlaying.h>

namespace linernotes::ui {

class GuideController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(GuideController)

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVariantList cards READ cards NOTIFY cardsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY cardsChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY cardsChanged)

public:
    explicit GuideController(guide::KnowledgeService &knowledgeService,
        guide::KnowledgeCardWriter &cardWriter, const ai::AiConfig &aiConfig,
        NowPlaying &nowPlaying, QObject *parent = nullptr);
    ~GuideController() override = default;

    [[nodiscard]] bool active() const;
    void setActive(bool active);

    [[nodiscard]] QVariantList cards() const;
    [[nodiscard]] bool loading() const;
    [[nodiscard]] bool failed() const;

    Q_INVOKABLE void retry();

signals:
    void activeChanged();
    void cardsChanged();

private slots:
    void onNowPlayingChanged();
    void onKnowledgeFinished(const linernotes::guide::KnowledgeResult &result);
    void onWriterDelta(const QString &key, const QString &text);
    void onWriterFinished(const QString &key, const linernotes::core::Result<QString> &result);

private:
    struct CardState {
        guide::KnowledgeKind kind = guide::KnowledgeKind::Artist;
        bool found = false;
        guide::KnowledgeEntry entry { };
        QString text { };
        bool writing = false;
        bool isAi = false;
    };

    void startLookupsForCurrentTrack();
    void startSongAndAlbumLookups();
    void applyFound(CardState &card, const guide::KnowledgeResult &result);
    void cancelAndClear();
    void rebuildCards();
    static QString fallbackText(const guide::KnowledgeEntry &entry);
    QVariantMap buildCardMap(const CardState &card) const;

    guide::KnowledgeService &m_knowledgeService;
    guide::KnowledgeCardWriter &m_cardWriter;
    const ai::AiConfig &m_aiConfig;
    NowPlaying &m_nowPlaying;

    bool m_active = false;
    qint64 m_currentTrackId = -1;
    QString m_currentArtist;
    QString m_currentTitle;
    QString m_currentAlbum;

    qint64 m_lastQueriedTrackId = -1;
    bool m_lookupStarted = false;

    QString m_artistKey;
    QString m_songKey;
    QString m_albumKey;

    bool m_artistFinished = true;
    bool m_songFinished = true;
    bool m_albumFinished = true;
    bool m_hasFailed = false;

    CardState m_songCard;
    CardState m_albumCard;
    CardState m_artistCard;

    QVariantList m_cards;
};

} // namespace linernotes::ui
