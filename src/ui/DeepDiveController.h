// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <ai/AiConfig.h>
#include <core/Result.h>
#include <guide/DeepDiveSession.h>
#include <guide/DeepDiveTools.h>
#include <ui/NowPlaying.h>

namespace linernotes::ui {

/// Pure helper to check if text contains unverified model knowledge marker '†'.
[[nodiscard]] bool containsUnverifiedMarker(const QString &text);

/// Pure helper to format subject string "Title — Artist".
[[nodiscard]] QString formatGuideSubject(const QString &title, const QString &artist);

class DeepDiveController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DeepDiveController)

    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString subject READ subject NOTIFY messagesChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY messagesChanged)

public:
    explicit DeepDiveController(guide::DeepDiveSession &session, NowPlaying &nowPlaying,
        const ai::AiConfig &aiConfig, QObject *parent = nullptr);
    ~DeepDiveController() override = default;

    [[nodiscard]] QVariantList messages() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool available() const;
    [[nodiscard]] QString subject() const;
    [[nodiscard]] bool stale() const;

    Q_INVOKABLE void ask(const QString &question);
    Q_INVOKABLE void newChat();
    Q_INVOKABLE void stop();

signals:
    void messagesChanged();
    void busyChanged();
    void availableChanged();

private slots:
    void onNowPlayingChanged();
    void onAiConfigChanged();
    void onTurnStarted();
    void onToolUsed(const QString &name, const QString &summary);
    void onDelta(const QString &text);
    void onTurnFinished(const linernotes::core::Result<QString> &answer,
        const linernotes::guide::DeepDiveSourceList &sources);

private:
    struct MessageItem {
        QString role { }; // "user" or "assistant"
        QString text { };
        QStringList tools;
        guide::DeepDiveSourceList sources;
        bool hasUnverified = false;
        bool pending = false;
        QString error { };
    };

    void syncStaleState();
    static QVariantMap messageToVariant(const MessageItem &msg);

    guide::DeepDiveSession &m_session;
    NowPlaying &m_nowPlaying;
    const ai::AiConfig &m_aiConfig;

    qint64 m_boundTrackId = -1;
    QString m_boundTitle;
    QString m_boundArtist;
    QString m_boundAlbum;
    bool m_stale = false;
    bool m_lastAvailable = false;

    QList<MessageItem> m_messages;
};

} // namespace linernotes::ui
