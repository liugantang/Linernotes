// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "JobHandler.h"

#include <QChar>
#include <QList>

namespace linernotes::ai {

namespace {

bool isCjk(uint cp)
{
    const auto s = QChar::script(cp);
    if (s == QChar::Script_Han || s == QChar::Script_Hiragana || s == QChar::Script_Katakana
        || s == QChar::Script_Hangul || s == QChar::Script_Bopomofo) {
        return true;
    }
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0x20000 && cp <= 0x2FA1F) || (cp >= 0xF900 && cp <= 0xFAFF)
        || (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0xAC00 && cp <= 0xD7AF)
        || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)
        || (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0x3000 && cp <= 0x303F)
        || (cp >= 0xFF00 && cp <= 0xFFEF)) {
        return true;
    }
    return false;
}

} // namespace

int roughTokenCount(const QString &text)
{
    if (text.isEmpty()) {
        return 0;
    }
    const QList<uint> ucs4 = text.toUcs4();
    int cjkCount = 0;
    int nonCjkCount = 0;
    for (const uint cp : ucs4) {
        if (isCjk(cp)) {
            ++cjkCount;
        } else {
            ++nonCjkCount;
        }
    }
    return cjkCount + ((nonCjkCount + 3) / 4);
}

} // namespace linernotes::ai
