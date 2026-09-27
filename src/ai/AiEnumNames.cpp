// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QMetaEnum>

#include <ai/AiEnumNames.h>

namespace linernotes::ai {

namespace {

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

} // namespace

QString purposeName(Purpose purpose)
{
    switch (purpose) {
    case Purpose::Cleanup:
        return QStringLiteral("cleanup");
    case Purpose::Query:
        return QStringLiteral("query");
    case Purpose::Dj:
        return QStringLiteral("dj");
    case Purpose::Guide:
        return QStringLiteral("guide");
    case Purpose::Narrative:
        return QStringLiteral("narrative");
    }
    return { };
}

std::optional<Purpose> purposeFromName(QStringView name)
{
    return enumFromName<Purpose>(name, purposeName);
}

} // namespace linernotes::ai
