// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlaybackSnapshot.h"

#include "PlayerLogging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <utility>

namespace linernotes::player {

QJsonObject PlaybackSnapshot::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("version"), 1);

    QJsonArray itemsArray;
    for (const auto &item : items) {
        QJsonObject itemObj;
        itemObj.insert(QStringLiteral("source"), item.source);
        itemObj.insert(QStringLiteral("trackId"), item.trackId);
        itemsArray.append(itemObj);
    }
    json.insert(QStringLiteral("items"), itemsArray);
    json.insert(QStringLiteral("currentIndex"), currentIndex);
    json.insert(QStringLiteral("position"), position);

    QString modeStr;
    switch (mode) {
    case PlayMode::Sequential:
        modeStr = QStringLiteral("Sequential");
        break;
    case PlayMode::RepeatAll:
        modeStr = QStringLiteral("RepeatAll");
        break;
    case PlayMode::RepeatOne:
        modeStr = QStringLiteral("RepeatOne");
        break;
    case PlayMode::Shuffle:
        modeStr = QStringLiteral("Shuffle");
        break;
    }
    json.insert(QStringLiteral("mode"), modeStr);
    json.insert(QStringLiteral("volume"), volume);
    json.insert(QStringLiteral("muted"), muted);
    json.insert(QStringLiteral("audioDevice"), audioDevice);

    return json;
}

std::optional<PlaybackSnapshot> PlaybackSnapshot::fromJson(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("version"))) {
        return std::nullopt;
    }
    const QJsonValue versionVal = json.value(QStringLiteral("version"));
    if (!versionVal.isDouble() || versionVal.toInt() != 1) {
        return std::nullopt;
    }

    PlaybackSnapshot snap;

    if (json.contains(QStringLiteral("items"))) {
        const QJsonValue itemsVal = json.value(QStringLiteral("items"));
        if (!itemsVal.isArray()) {
            return std::nullopt;
        }
        const QJsonArray itemsArray = itemsVal.toArray();
        snap.items.reserve(itemsArray.size());
        for (const QJsonValue &elemVal : itemsArray) {
            if (!elemVal.isObject()) {
                return std::nullopt;
            }
            const QJsonObject elemObj = elemVal.toObject();
            if (!elemObj.contains(QStringLiteral("source"))) {
                return std::nullopt;
            }
            const QJsonValue sourceVal = elemObj.value(QStringLiteral("source"));
            if (!sourceVal.isString()) {
                return std::nullopt;
            }
            Item item;
            item.source = sourceVal.toString();
            if (elemObj.contains(QStringLiteral("trackId"))) {
                const QJsonValue trackIdVal = elemObj.value(QStringLiteral("trackId"));
                if (!trackIdVal.isDouble()) {
                    return std::nullopt;
                }
                item.trackId = trackIdVal.toInteger(-1);
            }
            snap.items.append(item);
        }
    }

    if (json.contains(QStringLiteral("currentIndex"))) {
        const QJsonValue curVal = json.value(QStringLiteral("currentIndex"));
        if (!curVal.isDouble()) {
            return std::nullopt;
        }
        const int idx = curVal.toInt(-1);
        snap.currentIndex = (idx >= 0 && idx < snap.items.size()) ? idx : -1;
    } else {
        snap.currentIndex = -1;
    }

    if (json.contains(QStringLiteral("position"))) {
        const QJsonValue posVal = json.value(QStringLiteral("position"));
        if (!posVal.isDouble()) {
            return std::nullopt;
        }
        const double pos = posVal.toDouble(0.0);
        snap.position = (std::isnan(pos) || !std::isfinite(pos) || pos < 0.0) ? 0.0 : pos;
    } else {
        snap.position = 0.0;
    }

    if (json.contains(QStringLiteral("mode"))) {
        const QJsonValue modeVal = json.value(QStringLiteral("mode"));
        if (modeVal.isString()) {
            const QString modeStr = modeVal.toString();
            if (modeStr == QStringLiteral("Sequential")) {
                snap.mode = PlayMode::Sequential;
            } else if (modeStr == QStringLiteral("RepeatAll")) {
                snap.mode = PlayMode::RepeatAll;
            } else if (modeStr == QStringLiteral("RepeatOne")) {
                snap.mode = PlayMode::RepeatOne;
            } else if (modeStr == QStringLiteral("Shuffle")) {
                snap.mode = PlayMode::Shuffle;
            } else {
                return std::nullopt;
            }
        } else if (modeVal.isDouble()) {
            const int modeInt = modeVal.toInt(-1);
            if (modeInt == 0) {
                snap.mode = PlayMode::Sequential;
            } else if (modeInt == 1) {
                snap.mode = PlayMode::RepeatAll;
            } else if (modeInt == 2) {
                snap.mode = PlayMode::RepeatOne;
            } else if (modeInt == 3) {
                snap.mode = PlayMode::Shuffle;
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    } else {
        snap.mode = PlayMode::Sequential;
    }

    if (json.contains(QStringLiteral("volume"))) {
        const QJsonValue volVal = json.value(QStringLiteral("volume"));
        if (!volVal.isDouble()) {
            return std::nullopt;
        }
        snap.volume = std::clamp(volVal.toInt(100), 0, 100);
    } else {
        snap.volume = 100;
    }

    if (json.contains(QStringLiteral("muted"))) {
        const QJsonValue muteVal = json.value(QStringLiteral("muted"));
        if (!muteVal.isBool()) {
            return std::nullopt;
        }
        snap.muted = muteVal.toBool();
    } else {
        snap.muted = false;
    }

    if (json.contains(QStringLiteral("audioDevice"))) {
        const QJsonValue devVal = json.value(QStringLiteral("audioDevice"));
        if (!devVal.isString()) {
            return std::nullopt;
        }
        const QString dev = devVal.toString();
        snap.audioDevice = dev.isEmpty() ? QStringLiteral("auto") : dev;
    } else {
        snap.audioDevice = QStringLiteral("auto");
    }

    return snap;
}

PlaybackStateStore::PlaybackStateStore(QString filePath)
    : m_filePath(std::move(filePath))
{
}

bool PlaybackStateStore::save(const PlaybackSnapshot &snapshot) const
{
    if (m_filePath.isEmpty()) {
        qCWarning(lcPlayer) << "Cannot save snapshot: file path is empty";
        return false;
    }

    const QFileInfo fileInfo(m_filePath);
    const QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qCWarning(lcPlayer) << "Failed to create directory for snapshot file:" << dir.path();
        return false;
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcPlayer) << "Failed to open snapshot file for writing:" << m_filePath
                            << file.errorString();
        return false;
    }

    const QJsonDocument doc(snapshot.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 bytesWritten = file.write(data);
    if (bytesWritten != data.size()) {
        qCWarning(lcPlayer) << "Failed to write all snapshot data to:" << m_filePath
                            << file.errorString();
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        qCWarning(lcPlayer) << "Failed to commit snapshot file:" << m_filePath
                            << file.errorString();
        return false;
    }

    return true;
}

std::optional<PlaybackSnapshot> PlaybackStateStore::load() const
{
    if (m_filePath.isEmpty() || !QFile::exists(m_filePath)) {
        return std::nullopt;
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcPlayer) << "Failed to open snapshot file for reading:" << m_filePath
                            << file.errorString();
        return std::nullopt;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcPlayer) << "Failed to parse snapshot JSON file:" << m_filePath
                            << parseError.errorString();
        return std::nullopt;
    }

    const auto snapshot = PlaybackSnapshot::fromJson(doc.object());
    if (!snapshot.has_value()) {
        qCWarning(lcPlayer) << "Invalid snapshot format in file:" << m_filePath;
        return std::nullopt;
    }

    return snapshot;
}

} // namespace linernotes::player
