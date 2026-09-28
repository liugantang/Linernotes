// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QChar>
#include <QList>
#include <QString>
#include <QStringView>

#include <butler/ArtistName.h>
#include <unicode/translit.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace linernotes::butler {

namespace {

icu::Transliterator *getTradToSimp()
{
    thread_local const std::unique_ptr<icu::Transliterator> s_trans = []() {
        UErrorCode status = U_ZERO_ERROR;
        auto *t = icu::Transliterator::createInstance(
            icu::UnicodeString::fromUTF8("Traditional-Simplified"), UTRANS_FORWARD, status);
        if (U_FAILURE(status) != 0 || t == nullptr) {
            status = U_ZERO_ERROR;
            t = icu::Transliterator::createInstance(
                icu::UnicodeString::fromUTF8("Hant-Hans"), UTRANS_FORWARD, status);
        }
        return std::unique_ptr<icu::Transliterator>(t);
    }();
    return s_trans.get();
}

QString transliterateWith(icu::Transliterator *trans, const QString &text)
{
    if (trans == nullptr || text.isEmpty()) {
        return text;
    }
    icu::UnicodeString ustr(text.utf16(), static_cast<int32_t>(text.length()));
    trans->transliterate(ustr);
    return QString::fromUtf16(ustr.getBuffer(), static_cast<qsizetype>(ustr.length()));
}

bool isPunctuationSymbolOrSeparator(uint cp)
{
    if (QChar::isSpace(cp)) {
        return true;
    }
    const auto cat = QChar::category(cp);
    switch (cat) {
    case QChar::Punctuation_Connector:
    case QChar::Punctuation_Dash:
    case QChar::Punctuation_Open:
    case QChar::Punctuation_Close:
    case QChar::Punctuation_InitialQuote:
    case QChar::Punctuation_FinalQuote:
    case QChar::Punctuation_Other:
    case QChar::Symbol_Math:
    case QChar::Symbol_Currency:
    case QChar::Symbol_Modifier:
    case QChar::Symbol_Other:
    case QChar::Separator_Space:
    case QChar::Separator_Line:
    case QChar::Separator_Paragraph:
    case QChar::Other_Control:
    case QChar::Other_Format:
        return true;
    default:
        return false;
    }
}

} // namespace

QString exactKey(QStringView name)
{
    if (name.trimmed().isEmpty()) {
        return { };
    }
    QString text = name.toString().normalized(QString::NormalizationForm_KC);
    text = text.toCaseFolded();
    text = transliterateWith(getTradToSimp(), text);

    QString result;
    result.reserve(text.size());
    for (const uint cp : text.toUcs4()) {
        if (isPunctuationSymbolOrSeparator(cp)) {
            continue;
        }
        if (cp <= 0xFFFF) {
            result.append(QChar(static_cast<char16_t>(cp)));
        } else {
            result.append(QChar::highSurrogate(cp));
            result.append(QChar::lowSurrogate(cp));
        }
    }
    return result;
}

} // namespace linernotes::butler
