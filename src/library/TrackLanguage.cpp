// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackLanguage.h"

#include <QChar>
#include <QStringView>

#include <algorithm>

namespace linernotes::library {

namespace {

bool hasKana(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) {
        const auto u = c.unicode();
        return (u >= 0x3040 && u <= 0x30FF) || (u >= 0x31F0 && u <= 0x31FF)
            || c.script() == QChar::Script_Hiragana || c.script() == QChar::Script_Katakana;
    });
}

bool hasHangul(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) {
        const auto u = c.unicode();
        return (u >= 0xAC00 && u <= 0xD7AF) || (u >= 0x1100 && u <= 0x11FF)
            || (u >= 0x3130 && u <= 0x318F) || (u >= 0xA960 && u <= 0xA97F)
            || (u >= 0xD7B0 && u <= 0xD7FF) || c.script() == QChar::Script_Hangul;
    });
}

bool hasHan(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) {
        const auto u = c.unicode();
        return (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0xF900 && u <= 0xFAFF) || c.script() == QChar::Script_Han;
    });
}

bool hasLatin(QStringView s)
{
    return std::ranges::any_of(s, [](QChar c) {
        return c.isLetter()
            && (c.script() == QChar::Script_Latin || (c >= u'A' && c <= u'Z')
                || (c >= u'a' && c <= u'z'));
    });
}

} // namespace

TrackLanguage inferTrackLanguage(const QString &title, const QString &album, const QString &artist)
{
    if (hasKana(title) || hasKana(album) || hasKana(artist)) {
        return TrackLanguage::Japanese;
    }
    if (hasHangul(title) || hasHangul(album) || hasHangul(artist)) {
        return TrackLanguage::Korean;
    }
    if (hasHan(title) || hasHan(album)) {
        return TrackLanguage::Chinese;
    }
    if (hasLatin(title)) {
        return TrackLanguage::Western;
    }
    if (hasHan(artist)) {
        return TrackLanguage::Chinese;
    }
    return TrackLanguage::Other;
}

} // namespace linernotes::library
