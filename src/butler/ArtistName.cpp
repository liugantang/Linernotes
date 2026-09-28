// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QChar>
#include <QLatin1Char>
#include <QLatin1StringView>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <butler/ArtistName.h>
#include <unicode/translit.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/uscript.h>
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

icu::Transliterator *getAnyToLatin()
{
    thread_local const std::unique_ptr<icu::Transliterator> s_trans = []() {
        UErrorCode status = U_ZERO_ERROR;
        auto *t = icu::Transliterator::createInstance(
            icu::UnicodeString::fromUTF8("Any-Latin; Latin-ASCII"), UTRANS_FORWARD, status);
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

bool isHan(uint cp)
{
    UErrorCode status = U_ZERO_ERROR;
    const UScriptCode sc = uscript_getScript(static_cast<UChar32>(cp), &status);
    if (status <= U_ZERO_ERROR && sc == USCRIPT_HAN) {
        return true;
    }
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0x20000 && cp <= 0x2FA1F) || (cp >= 0xF900 && cp <= 0xFAFF)
        || (cp >= 0x2E80 && cp <= 0x2EFF) || (cp >= 0x2F00 && cp <= 0x2FDF)
        || (cp >= 0x2FF0 && cp <= 0x2FFF) || cp == 0x3005 || cp == 0x3006 || cp == 0x3007) {
        return true;
    }
    return false;
}

bool isKanaOrHangul(uint cp)
{
    UErrorCode status = U_ZERO_ERROR;
    const UScriptCode sc = uscript_getScript(static_cast<UChar32>(cp), &status);
    if (status <= U_ZERO_ERROR) {
        if (sc == USCRIPT_HIRAGANA || sc == USCRIPT_KATAKANA || sc == USCRIPT_HANGUL
            || sc == USCRIPT_KATAKANA_OR_HIRAGANA || sc == USCRIPT_BOPOMOFO) {
            return true;
        }
    }
    if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0xAC00 && cp <= 0xD7AF)
        || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)
        || (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0xFF65 && cp <= 0xFF9F)
        || (cp >= 0x3100 && cp <= 0x312F) || (cp >= 0x31A0 && cp <= 0x31BF) || cp == 0x30FC) {
        return true;
    }
    return false;
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

QString normalizeRomajiToken(QString token)
{
    while (token.contains(QLatin1StringView("ou"))) {
        token.replace(QLatin1StringView("ou"), QLatin1StringView("o"));
    }
    while (token.contains(QLatin1StringView("oo"))) {
        token.replace(QLatin1StringView("oo"), QLatin1StringView("o"));
    }
    while (token.contains(QLatin1StringView("uu"))) {
        token.replace(QLatin1StringView("uu"), QLatin1StringView("u"));
    }
    while (token.contains(QLatin1StringView("oh"))) {
        token.replace(QLatin1StringView("oh"), QLatin1StringView("o"));
    }
    if (token.endsWith(QLatin1Char('h')) && token.length() > 1) {
        const QChar prev = token.at(token.length() - 2);
        if (prev == QLatin1Char('a') || prev == QLatin1Char('e') || prev == QLatin1Char('i')
            || prev == QLatin1Char('o') || prev == QLatin1Char('u')) {
            token.chop(1);
        }
    }
    return token;
}

} // namespace

bool containsCjk(QStringView text)
{
    const auto ucs4 = text.toUcs4();
    return std::ranges::any_of(ucs4, [](uint cp) { return isHan(cp) || isKanaOrHangul(cp); });
}

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

QStringList romanTokens(QStringView name)
{
    if (name.trimmed().isEmpty() || containsCjk(name)) {
        return { };
    }

    QString text = transliterateWith(getAnyToLatin(), name.toString());
    text = text.toLower();

    static const QRegularExpression s_splitRegex(QStringLiteral(R"([^a-z0-9]+)"));
    const QStringList rawTokens = text.split(s_splitRegex, Qt::SkipEmptyParts);

    QStringList tokens;
    tokens.reserve(rawTokens.size());
    for (const auto &raw : rawTokens) {
        const QString norm = normalizeRomajiToken(raw);
        if (!norm.isEmpty()) {
            tokens.append(norm);
        }
    }

    std::ranges::sort(tokens);
    return tokens;
}

} // namespace linernotes::butler
