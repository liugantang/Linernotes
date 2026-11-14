// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DeepDiveController.h"

#include <QVariantMap>

#include <ai/AiEnums.h>
#include <ui/ErrorText.h>

namespace linernotes::ui {

bool containsUnverifiedMarker(const QString &text)
{
    return text.contains(QString::fromUtf8("†"));
}

QString formatGuideSubject(const QString &title, const QString &artist)
{
    QString trimmedTitle = title.trimmed();
    QString trimmedArtist = artist.trimmed();
    if (!trimmedTitle.isEmpty() && !trimmedArtist.isEmpty()) {
        return QStringLiteral("%1 — %2").arg(trimmedTitle, trimmedArtist);
    }
    if (!trimmedTitle.isEmpty()) {
        return trimmedTitle;
    }
    return trimmedArtist;
}

DeepDiveController::DeepDiveController(guide::DeepDiveSession &session, NowPlaying &nowPlaying,
    const ai::AiConfig &aiConfig, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_nowPlaying(nowPlaying)
    , m_aiConfig(aiConfig)
    , m_lastAvailable(available())
{
    connect(&m_nowPlaying, &NowPlaying::changed, this, &DeepDiveController::onNowPlayingChanged);
    connect(&m_aiConfig, &ai::AiConfig::changed, this, &DeepDiveController::onAiConfigChanged);

    connect(
        &m_session, &guide::DeepDiveSession::turnStarted, this, &DeepDiveController::onTurnStarted);
    connect(&m_session, &guide::DeepDiveSession::toolUsed, this, &DeepDiveController::onToolUsed);
    connect(&m_session, &guide::DeepDiveSession::delta, this, &DeepDiveController::onDelta);
    connect(&m_session, &guide::DeepDiveSession::turnFinished, this,
        &DeepDiveController::onTurnFinished);
}

QVariantList DeepDiveController::messages() const
{
    QVariantList list;
    list.reserve(m_messages.size());
    for (const auto &msg : m_messages) {
        list.append(messageToVariant(msg));
    }
    return list;
}

bool DeepDiveController::busy() const
{
    return m_session.busy();
}

bool DeepDiveController::available() const
{
    return m_aiConfig.resolve(ai::Purpose::Guide).has_value();
}

QString DeepDiveController::subject() const
{
    if (!m_boundTitle.isEmpty() || !m_boundArtist.isEmpty()) {
        return formatGuideSubject(m_boundTitle, m_boundArtist);
    }
    if (m_nowPlaying.hasTrack()) {
        return formatGuideSubject(m_nowPlaying.title(), m_nowPlaying.artist());
    }
    return { };
}

bool DeepDiveController::stale() const
{
    return m_stale;
}

void DeepDiveController::ask(const QString &question)
{
    if (m_session.busy()) {
        return;
    }
    const QString trimmed = question.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (m_messages.isEmpty()) {
        guide::DeepDiveContext ctx;
        ctx.trackId = m_nowPlaying.trackId();
        ctx.title = m_nowPlaying.title();
        ctx.artist = m_nowPlaying.artist();
        ctx.album = m_nowPlaying.album();
        ctx.year = std::nullopt;
        m_session.reset(ctx);
        m_boundTrackId = ctx.trackId;
        m_boundTitle = ctx.title;
        m_boundArtist = ctx.artist;
        m_boundAlbum = ctx.album;
        m_stale = false;
    } else {
        // If last turn failed, remove the failed assistant message and preceding matching user
        // message
        if (m_messages.size() >= 2 && m_messages.last().role == QStringLiteral("assistant")
            && !m_messages.last().error.isEmpty()) {
            const auto &prevUser = m_messages.at(m_messages.size() - 2);
            if (prevUser.role == QStringLiteral("user") && prevUser.text == trimmed) {
                m_messages.removeLast();
                m_messages.removeLast();
            }
        }
    }

    MessageItem userMsg;
    userMsg.role = QStringLiteral("user");
    userMsg.text = trimmed;
    m_messages.append(userMsg);

    MessageItem assistantMsg;
    assistantMsg.role = QStringLiteral("assistant");
    assistantMsg.pending = true;
    m_messages.append(assistantMsg);

    emit messagesChanged();
    emit busyChanged();

    m_session.ask(trimmed);
}

void DeepDiveController::newChat()
{
    m_session.cancel();

    guide::DeepDiveContext ctx;
    ctx.trackId = m_nowPlaying.trackId();
    ctx.title = m_nowPlaying.title();
    ctx.artist = m_nowPlaying.artist();
    ctx.album = m_nowPlaying.album();
    ctx.year = std::nullopt;
    m_session.reset(ctx);

    m_boundTrackId = ctx.trackId;
    m_boundTitle = ctx.title;
    m_boundArtist = ctx.artist;
    m_boundAlbum = ctx.album;
    m_stale = false;

    m_messages.clear();

    emit messagesChanged();
    emit busyChanged();
}

void DeepDiveController::stop()
{
    m_session.cancel();

    if (!m_messages.isEmpty() && m_messages.last().pending) {
        auto &msg = m_messages.last();
        msg.pending = false;
        msg.hasUnverified = containsUnverifiedMarker(msg.text);
    }

    emit messagesChanged();
    emit busyChanged();
}

void DeepDiveController::onNowPlayingChanged()
{
    syncStaleState();
}

void DeepDiveController::onAiConfigChanged()
{
    const bool currentAvailable = available();
    if (currentAvailable != m_lastAvailable) {
        m_lastAvailable = currentAvailable;
        emit availableChanged();
    }
}

void DeepDiveController::onTurnStarted()
{
    if (!m_messages.isEmpty() && m_messages.last().pending) {
        auto &msg = m_messages.last();
        msg.text.clear();
        msg.error.clear();
        emit messagesChanged();
    }
}

void DeepDiveController::onToolUsed(const QString &name, const QString &summary)
{
    Q_UNUSED(name);
    if (!m_messages.isEmpty() && m_messages.last().pending) {
        m_messages.last().tools.append(summary);
        emit messagesChanged();
    }
}

void DeepDiveController::onDelta(const QString &text)
{
    if (!m_messages.isEmpty() && m_messages.last().pending) {
        auto &msg = m_messages.last();
        msg.text.append(text);
        msg.hasUnverified = containsUnverifiedMarker(msg.text);
        emit messagesChanged();
    }
}

void DeepDiveController::onTurnFinished(
    const core::Result<QString> &answer, const guide::DeepDiveSourceList &sources)
{
    if (!m_messages.isEmpty() && m_messages.last().pending) {
        auto &msg = m_messages.last();
        msg.pending = false;
        if (answer.ok()) {
            msg.text = answer.value();
            msg.sources = sources;
            msg.hasUnverified = containsUnverifiedMarker(msg.text);
            msg.error.clear();
        } else {
            msg.error = userErrorText(answer.error());
        }
        emit messagesChanged();
    }
    emit busyChanged();
}

void DeepDiveController::syncStaleState()
{
    if (m_messages.isEmpty()) {
        if (m_stale) {
            m_stale = false;
            emit messagesChanged();
        }
        return;
    }

    const bool isStale = (m_boundTrackId != m_nowPlaying.trackId());
    if (m_stale != isStale) {
        m_stale = isStale;
        emit messagesChanged();
    }
}

QVariantMap DeepDiveController::messageToVariant(const MessageItem &msg)
{
    QVariantMap map;
    map.insert(QStringLiteral("role"), msg.role);
    map.insert(QStringLiteral("text"), msg.text);

    QVariantList toolsList;
    toolsList.reserve(msg.tools.size());
    for (const auto &tool : msg.tools) {
        toolsList.append(tool);
    }
    map.insert(QStringLiteral("tools"), toolsList);

    QVariantList sourcesList;
    sourcesList.reserve(msg.sources.size());
    for (const auto &src : msg.sources) {
        QVariantMap srcMap;
        srcMap.insert(QStringLiteral("label"), src.label);
        srcMap.insert(QStringLiteral("url"), src.url);
        sourcesList.append(srcMap);
    }
    map.insert(QStringLiteral("sources"), sourcesList);

    map.insert(QStringLiteral("hasUnverified"), msg.hasUnverified);
    map.insert(QStringLiteral("pending"), msg.pending);
    map.insert(QStringLiteral("error"), msg.error);
    return map;
}

} // namespace linernotes::ui
