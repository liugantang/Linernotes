// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlaySource.h"

#include <QLatin1StringView>
#include <QMetaEnum>

namespace linernotes::core {

QString playSourceToString(PlaySource source)
{
    const auto meta = QMetaEnum::fromType<PlaySource>();
    const char *key = meta.valueToKey(static_cast<int>(source));
    if (key == nullptr) {
        return QStringLiteral("unknown");
    }
    return QString::fromLatin1(key).toLower();
}

PlaySource playSourceFromString(QStringView str)
{
    const auto meta = QMetaEnum::fromType<PlaySource>();
    const int count = meta.keyCount();
    for (int i = 0; i < count; ++i) {
        const char *key = meta.key(i);
        if (key != nullptr && str.compare(QLatin1StringView(key), Qt::CaseInsensitive) == 0) {
            return static_cast<PlaySource>(meta.value(i));
        }
    }
    return PlaySource::Unknown;
}

} // namespace linernotes::core
