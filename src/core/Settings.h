// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

#pragma once

#include <QAnyStringView>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>
#include <optional>

class QSettings;

namespace aimusic::core {

template <typename T> struct SettingKey {
    QAnyStringView name; // e.g. "player/volume"
    T defaultValue;

    constexpr SettingKey(QAnyStringView n, T d) noexcept(std::is_nothrow_move_constructible_v<T>)
        : name(n)
        , defaultValue(std::move(d))
    {
    }
};

namespace detail {

template <typename T> struct SettingConverter {
    static std::optional<T> fromVariant(const QVariant &var)
    {
        if (!var.isValid() || var.isNull()) {
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<T>().id()) {
            return var.value<T>();
        }
        if (var.canConvert<T>()) {
            return var.value<T>();
        }
        return std::nullopt;
    }
};

template <> struct SettingConverter<bool> {
    static std::optional<bool> fromVariant(const QVariant &var)
    {
        if (!var.isValid() || var.isNull()) {
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<bool>().id()) {
            return var.toBool();
        }
        if (var.userType() == QMetaType::fromType<QString>().id()) {
            const QString s = var.toString().trimmed();
            if (s.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || s == QStringLiteral("1")) {
                return true;
            }
            if (s.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0
                || s == QStringLiteral("0")) {
                return false;
            }
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<int>().id()
            || var.userType() == QMetaType::fromType<qint64>().id()) {
            const int val = var.toInt();
            if (val == 0) {
                return false;
            }
            if (val == 1) {
                return true;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
};

template <> struct SettingConverter<int> {
    static std::optional<int> fromVariant(const QVariant &var)
    {
        if (!var.isValid() || var.isNull()) {
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<int>().id()) {
            return var.toInt();
        }
        if (var.userType() == QMetaType::fromType<QString>().id()) {
            bool ok = false;
            const int val = var.toString().trimmed().toInt(&ok);
            if (ok) {
                return val;
            }
            return std::nullopt;
        }
        if (var.canConvert<int>()) {
            bool ok = false;
            const int val = var.toInt(&ok);
            if (ok) {
                return val;
            }
        }
        return std::nullopt;
    }
};

template <> struct SettingConverter<qint64> {
    static std::optional<qint64> fromVariant(const QVariant &var)
    {
        if (!var.isValid() || var.isNull()) {
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<qint64>().id()) {
            return var.toLongLong();
        }
        if (var.userType() == QMetaType::fromType<QString>().id()) {
            bool ok = false;
            const qint64 val = var.toString().trimmed().toLongLong(&ok);
            if (ok) {
                return val;
            }
            return std::nullopt;
        }
        if (var.canConvert<qint64>()) {
            bool ok = false;
            const qint64 val = var.toLongLong(&ok);
            if (ok) {
                return val;
            }
        }
        return std::nullopt;
    }
};

template <> struct SettingConverter<double> {
    static std::optional<double> fromVariant(const QVariant &var)
    {
        if (!var.isValid() || var.isNull()) {
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<double>().id()) {
            return var.toDouble();
        }
        if (var.userType() == QMetaType::fromType<int>().id()) {
            return static_cast<double>(var.toInt());
        }
        if (var.userType() == QMetaType::fromType<QString>().id()) {
            bool ok = false;
            const double val = var.toString().trimmed().toDouble(&ok);
            if (ok) {
                return val;
            }
            return std::nullopt;
        }
        if (var.canConvert<double>()) {
            bool ok = false;
            const double val = var.toDouble(&ok);
            if (ok) {
                return val;
            }
        }
        return std::nullopt;
    }
};

template <> struct SettingConverter<QString> {
    static std::optional<QString> fromVariant(const QVariant &var)
    {
        if (!var.isValid() || var.isNull()) {
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<QString>().id()) {
            return var.toString();
        }
        if (var.userType() == QMetaType::fromType<QStringList>().id()) {
            return std::nullopt;
        }
        if (var.canConvert<QString>()) {
            return var.toString();
        }
        return std::nullopt;
    }
};

template <> struct SettingConverter<QStringList> {
    static std::optional<QStringList> fromVariant(const QVariant &var)
    {
        if (!var.isValid() || var.isNull()) {
            return std::nullopt;
        }
        if (var.userType() == QMetaType::fromType<QStringList>().id()) {
            return var.toStringList();
        }
        if (var.userType() == QMetaType::fromType<QString>().id()) {
            return var.toStringList();
        }
        if (var.canConvert<QStringList>()) {
            return var.toStringList();
        }
        return std::nullopt;
    }
};

} // namespace detail

class Settings : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Settings)

public:
    explicit Settings(const QString &iniFilePath, QObject *parent = nullptr);
    ~Settings() override;

    template <typename T> T value(const SettingKey<T> &key) const
    {
        const QString keyName = key.name.toString();
        if (!containsKey(keyName)) {
            return key.defaultValue;
        }
        const QVariant raw = rawValue(keyName);
        const auto converted = detail::SettingConverter<T>::fromVariant(raw);
        if (converted.has_value()) {
            return *converted;
        }
        return key.defaultValue;
    }

    template <typename T> void setValue(const SettingKey<T> &key, const T &value)
    {
        const QString keyName = key.name.toString();
        if (containsKey(keyName)) {
            const QVariant raw = rawValue(keyName);
            const auto currentConverted = detail::SettingConverter<T>::fromVariant(raw);
            if (currentConverted.has_value() && *currentConverted == value) {
                return;
            }
        }
        setRawValue(keyName, QVariant::fromValue(value));
        emit changed(keyName);
    }

    template <typename T> void reset(const SettingKey<T> &key)
    {
        const QString keyName = key.name.toString();
        if (!containsKey(keyName)) {
            return;
        }
        removeKey(keyName);
        emit changed(keyName);
    }

    void sync();

signals:
    void changed(const QString &keyName);

private:
    bool containsKey(const QString &keyName) const;
    QVariant rawValue(const QString &keyName) const;
    void setRawValue(const QString &keyName, const QVariant &value);
    void removeKey(const QString &keyName);

    std::unique_ptr<QSettings> m_settings;
};

} // namespace aimusic::core
