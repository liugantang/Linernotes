// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QMetaEnum>
#include <QString>
#include <QStringView>

#include <optional>

namespace linernotes::library::detail {

/// 按名字反查枚举值：遍历 Q_ENUM_NS / Q_ENUM 登记的全部取值，与名字函数比对，保证两个方向一致。
template <typename E, typename NameFunc>
std::optional<E> enumFromName(QStringView name, NameFunc nameOf)
{
    const QMetaEnum meta = QMetaEnum::fromType<E>();
    for (int i = 0; i < meta.keyCount(); ++i) {
        const auto value = static_cast<E>(meta.value(i));
        if (nameOf(value) == name) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace linernotes::library::detail
