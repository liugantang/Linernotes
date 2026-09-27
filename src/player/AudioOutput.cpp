// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AudioOutput.h"

#include "MpvHandle.h"
#include "PlayerLogging.h"

#include <QVariantMap>

namespace linernotes::player {

namespace {

bool parseBoolVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }
    if (value.metaType().id() == QMetaType::QString) {
        const QString s = value.toString();
        return (s.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0
            || s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
            || s == QLatin1String("1"));
    }
    if (value.canConvert<qint64>()) {
        return value.toLongLong() != 0;
    }
    return value.toBool();
}

QVariantList parseAudioDeviceList(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return { };
    }
    const QVariantList rawList = value.toList();
    QVariantList devices;
    devices.reserve(rawList.size());
    for (const QVariant &itemVar : rawList) {
        const QVariantMap itemMap = itemVar.toMap();
        const QString name = itemMap.value(QStringLiteral("name")).toString();
        QString description = itemMap.value(QStringLiteral("description")).toString();
        if (description.isEmpty()) {
            description = name;
        }
        QVariantMap dev;
        dev.insert(QStringLiteral("name"), name);
        dev.insert(QStringLiteral("description"), description);
        devices.append(dev);
    }
    return devices;
}

} // namespace

AudioOutput::AudioOutput(MpvHandle &mpv)
    : m_mpv(mpv)
{
    if (!m_mpv.isValid()) {
        return;
    }

    m_mpv.observeProperty(QStringLiteral("audio-device"));
    m_mpv.observeProperty(QStringLiteral("audio-exclusive"));

    const QVariant devVar = m_mpv.property(QStringLiteral("audio-device"));
    if (devVar.isValid() && !devVar.isNull()) {
        const QString devName = devVar.toString();
        if (!devName.isEmpty()) {
            m_audioDevice = devName;
        }
    }

    const QVariant exclVar = m_mpv.property(QStringLiteral("audio-exclusive"));
    if (exclVar.isValid() && !exclVar.isNull()) {
        m_exclusiveMode = parseBoolVariant(exclVar);
    }

    connect(&m_mpv, &MpvHandle::propertyChanged, this, &AudioOutput::onPropertyChanged);
}

QVariantList AudioOutput::audioDevices() const
{
    return m_audioDevices;
}

QString AudioOutput::audioDevice() const
{
    return m_audioDevice;
}

bool AudioOutput::exclusiveMode() const
{
    return m_exclusiveMode;
}

bool AudioOutput::isDevicesRefreshed() const
{
    return m_audioDevicesRefreshed;
}

void AudioOutput::refreshAudioDevices()
{
    if (!m_mpv.isValid()) {
        return;
    }
    if (!m_audioDevicesRefreshed) {
        m_audioDevicesRefreshed = true;
        m_mpv.observeProperty(QStringLiteral("audio-device-list"));
    }
    const QVariant devListVar = m_mpv.property(QStringLiteral("audio-device-list"));
    if (devListVar.isValid()) {
        handleAudioDeviceListChanged(devListVar);
    }
}

bool AudioOutput::selectAudioDevice(const QString &name)
{
    if (name == QStringLiteral("auto")) {
        qCDebug(lcPlayer) << "selectAudioDevice() to auto";
        if (!m_mpv.isValid()) {
            return false;
        }
        return m_mpv.setProperty(QStringLiteral("audio-device"), QStringLiteral("auto"));
    }

    if (!m_audioDevicesRefreshed) {
        refreshAudioDevices();
    }

    bool found = false;
    for (const QVariant &devVar : m_audioDevices) {
        if (devVar.toMap().value(QStringLiteral("name")).toString() == name) {
            found = true;
            break;
        }
    }

    if (!found) {
        qCWarning(lcPlayer) << "Cannot select audio device" << name
                            << "because it is not in audioDevices list";
        return false;
    }

    qCDebug(lcPlayer) << "selectAudioDevice() to" << name;
    if (!m_mpv.isValid()) {
        return false;
    }

    return m_mpv.setProperty(QStringLiteral("audio-device"), name);
}

void AudioOutput::setExclusiveMode(bool exclusive)
{
    if (exclusive == m_exclusiveMode) {
        return;
    }
    qCDebug(lcPlayer) << "setExclusiveMode() to" << exclusive;
    if (!m_mpv.isValid()) {
        return;
    }
    m_mpv.setProperty(QStringLiteral("audio-exclusive"), exclusive);
}

void AudioOutput::onPropertyChanged(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("audio-device-list")) {
        handleAudioDeviceListChanged(value);
    } else if (name == QStringLiteral("audio-device")) {
        handleAudioDeviceChanged(value);
    } else if (name == QStringLiteral("audio-exclusive")) {
        handleAudioExclusiveChanged(value);
    }
}

void AudioOutput::handleAudioDeviceListChanged(const QVariant &value)
{
    const QVariantList devices = parseAudioDeviceList(value);
    if (devices != m_audioDevices) {
        m_audioDevices = devices;
        qCDebug(lcPlayer) << "Audio device list changed, count:" << m_audioDevices.size();

        bool containsCurrent = false;
        for (const QVariant &devVar : m_audioDevices) {
            if (devVar.toMap().value(QStringLiteral("name")).toString() == m_audioDevice) {
                containsCurrent = true;
                break;
            }
        }
        if (!containsCurrent && !m_audioDevice.isEmpty()) {
            qCInfo(lcPlayer) << "Selected audio device" << m_audioDevice
                             << "is no longer present in audioDevices list";
        }

        emit audioDevicesChanged();
    }
}

void AudioOutput::handleAudioDeviceChanged(const QVariant &value)
{
    if (value.isValid() && !value.isNull()) {
        const QString name = value.toString();
        if (!name.isEmpty() && name != m_audioDevice) {
            m_audioDevice = name;
            qCDebug(lcPlayer) << "Audio device changed to:" << m_audioDevice;
            emit audioDeviceChanged(m_audioDevice);
        }
    }
}

void AudioOutput::handleAudioExclusiveChanged(const QVariant &value)
{
    if (value.isValid() && !value.isNull()) {
        const bool exclusive = parseBoolVariant(value);
        if (exclusive != m_exclusiveMode) {
            m_exclusiveMode = exclusive;
            qCDebug(lcPlayer) << "Exclusive mode changed to:" << m_exclusiveMode;
            emit exclusiveModeChanged(m_exclusiveMode);
        }
    }
}

} // namespace linernotes::player
