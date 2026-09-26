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

namespace {

bool parseVersion(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("version"))) {
        return false;
    }
    const QJsonValue versionVal = json.value(QStringLiteral("version"));
    return versionVal.isDouble() && versionVal.toInt() == 1;
}

std::optional<QList<PlaybackSnapshot::Item>> parseItems(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("items"))) {
        return QList<PlaybackSnapshot::Item> { };
    }
    const QJsonValue itemsVal = json.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return std::nullopt;
    }
    const QJsonArray itemsArray = itemsVal.toArray();
    QList<PlaybackSnapshot::Item> items;
    items.reserve(itemsArray.size());
    for (const auto &elemVal : itemsArray) {
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
        PlaybackSnapshot::Item item;
        item.source = sourceVal.toString();
        if (elemObj.contains(QStringLiteral("trackId"))) {
            const QJsonValue trackIdVal = elemObj.value(QStringLiteral("trackId"));
            if (!trackIdVal.isDouble()) {
                return std::nullopt;
            }
            item.trackId = trackIdVal.toInteger(-1);
        }
        items.append(item);
    }
    return items;
}

std::optional<int> parseCurrentIndex(const QJsonObject &json, int itemCount)
{
    if (!json.contains(QStringLiteral("currentIndex"))) {
        return -1;
    }
    const QJsonValue curVal = json.value(QStringLiteral("currentIndex"));
    if (!curVal.isDouble()) {
        return std::nullopt;
    }
    const int idx = curVal.toInt(-1);
    return (idx >= 0 && idx < itemCount) ? idx : -1;
}

std::optional<double> parsePosition(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("position"))) {
        return 0.0;
    }
    const QJsonValue posVal = json.value(QStringLiteral("position"));
    if (!posVal.isDouble()) {
        return std::nullopt;
    }
    const double pos = posVal.toDouble(0.0);
    return (std::isnan(pos) || !std::isfinite(pos) || pos < 0.0) ? 0.0 : pos;
}

std::optional<PlayMode> parseMode(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("mode"))) {
        return PlayMode::Sequential;
    }
    const QJsonValue modeVal = json.value(QStringLiteral("mode"));
    if (modeVal.isString()) {
        const QString modeStr = modeVal.toString();
        if (modeStr == QStringLiteral("Sequential")) {
            return PlayMode::Sequential;
        }
        if (modeStr == QStringLiteral("RepeatAll")) {
            return PlayMode::RepeatAll;
        }
        if (modeStr == QStringLiteral("RepeatOne")) {
            return PlayMode::RepeatOne;
        }
        if (modeStr == QStringLiteral("Shuffle")) {
            return PlayMode::Shuffle;
        }
        return std::nullopt;
    }
    if (modeVal.isDouble()) {
        const int modeInt = modeVal.toInt(-1);
        if (modeInt == 0) {
            return PlayMode::Sequential;
        }
        if (modeInt == 1) {
            return PlayMode::RepeatAll;
        }
        if (modeInt == 2) {
            return PlayMode::RepeatOne;
        }
        if (modeInt == 3) {
            return PlayMode::Shuffle;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<int> parseVolume(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("volume"))) {
        return 100;
    }
    const QJsonValue volVal = json.value(QStringLiteral("volume"));
    if (!volVal.isDouble()) {
        return std::nullopt;
    }
    return std::clamp(volVal.toInt(100), 0, 100);
}

std::optional<bool> parseMuted(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("muted"))) {
        return false;
    }
    const QJsonValue muteVal = json.value(QStringLiteral("muted"));
    if (!muteVal.isBool()) {
        return std::nullopt;
    }
    return muteVal.toBool();
}

std::optional<QString> parseAudioDevice(const QJsonObject &json)
{
    if (!json.contains(QStringLiteral("audioDevice"))) {
        return QStringLiteral("auto");
    }
    const QJsonValue devVal = json.value(QStringLiteral("audioDevice"));
    if (!devVal.isString()) {
        return std::nullopt;
    }
    const QString dev = devVal.toString();
    return dev.isEmpty() ? QStringLiteral("auto") : dev;
}

} // namespace

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
    if (!parseVersion(json)) {
        return std::nullopt;
    }

    auto itemsOpt = parseItems(json);
    if (!itemsOpt.has_value()) {
        return std::nullopt;
    }

    auto currentIndexOpt = parseCurrentIndex(json, static_cast<int>(itemsOpt->size()));
    if (!currentIndexOpt.has_value()) {
        return std::nullopt;
    }

    auto positionOpt = parsePosition(json);
    if (!positionOpt.has_value()) {
        return std::nullopt;
    }

    auto modeOpt = parseMode(json);
    if (!modeOpt.has_value()) {
        return std::nullopt;
    }

    auto volumeOpt = parseVolume(json);
    if (!volumeOpt.has_value()) {
        return std::nullopt;
    }

    auto mutedOpt = parseMuted(json);
    if (!mutedOpt.has_value()) {
        return std::nullopt;
    }

    auto audioDeviceOpt = parseAudioDevice(json);
    if (!audioDeviceOpt.has_value()) {
        return std::nullopt;
    }

    PlaybackSnapshot snap;
    snap.items = std::move(*itemsOpt);
    snap.currentIndex = *currentIndexOpt;
    snap.position = *positionOpt;
    snap.mode = *modeOpt;
    snap.volume = *volumeOpt;
    snap.muted = *mutedOpt;
    snap.audioDevice = std::move(*audioDeviceOpt);
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

    auto snapshot = PlaybackSnapshot::fromJson(doc.object());
    if (!snapshot.has_value()) {
        qCWarning(lcPlayer) << "Invalid snapshot format in file:" << m_filePath;
        return std::nullopt;
    }

    return snapshot;
}

} // namespace linernotes::player
