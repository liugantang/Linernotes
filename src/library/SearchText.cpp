// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SearchText.h"

#include <QChar>
#include <QList>
#include <QRegularExpression>
#include <QStringList>

#include <unicode/normalizer2.h>
#include <unicode/translit.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/uscript.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace linernotes::library::search {

namespace {

icu::Transliterator *getHansToHant()
{
    thread_local const std::unique_ptr<icu::Transliterator> s_trans = []() {
        UErrorCode status = U_ZERO_ERROR;
        auto *t = icu::Transliterator::createInstance(
            icu::UnicodeString::fromUTF8("Hans-Hant"), UTRANS_FORWARD, status);
        return std::unique_ptr<icu::Transliterator>(t);
    }();
    return s_trans.get();
}

icu::Transliterator *getHantToHans()
{
    thread_local const std::unique_ptr<icu::Transliterator> s_trans = []() {
        UErrorCode status = U_ZERO_ERROR;
        auto *t = icu::Transliterator::createInstance(
            icu::UnicodeString::fromUTF8("Hant-Hans"), UTRANS_FORWARD, status);
        return std::unique_ptr<icu::Transliterator>(t);
    }();
    return s_trans.get();
}

icu::Transliterator *getHanToLatin()
{
    thread_local const std::unique_ptr<icu::Transliterator> s_trans = []() {
        UErrorCode status = U_ZERO_ERROR;
        auto *t = icu::Transliterator::createInstance(
            icu::UnicodeString::fromUTF8("Han-Latin; Latin-ASCII"), UTRANS_FORWARD, status);
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
        || (cp >= 0x20000 && cp <= 0x2FA1F) || (cp >= 0xF900 && cp <= 0xFAFF)) {
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
        || (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0xFF65 && cp <= 0xFF9F)) {
        return true;
    }
    return false;
}

bool isCjk(uint cp)
{
    return isHan(cp) || isKanaOrHangul(cp);
}

void appendCodePoint(QString &out, uint cp)
{
    if (cp <= 0xFFFF) {
        out.append(QChar(static_cast<char16_t>(cp)));
    } else {
        out.append(QChar::highSurrogate(cp));
        out.append(QChar::lowSurrogate(cp));
    }
}

QString spaceCjk(const QString &normalized)
{
    const QList<uint> ucs4 = normalized.toUcs4();
    QString result;
    result.reserve(normalized.size() * 2);

    bool prevIsCjk = false;
    bool prevIsSpace = true;

    for (const uint cp : ucs4) {
        if (QChar::isSpace(cp)) {
            if (!prevIsSpace && !result.isEmpty()) {
                result.append(u' ');
                prevIsSpace = true;
            }
            prevIsCjk = false;
            continue;
        }

        const bool cjk = isCjk(cp);
        const bool needSpace = (cjk || prevIsCjk) && !prevIsSpace && !result.isEmpty();
        if (needSpace) {
            result.append(u' ');
        }
        appendCodePoint(result, cp);
        prevIsCjk = cjk;
        prevIsSpace = false;
    }

    return result.trimmed();
}

struct ScriptFlags {
    bool hasHan = false;
    bool hasKanaOrHangul = false;
};

ScriptFlags detectScripts(const QString &text)
{
    ScriptFlags flags;
    const QList<uint> ucs4 = text.toUcs4();
    for (const uint cp : ucs4) {
        if (isHan(cp)) {
            flags.hasHan = true;
        } else if (isKanaOrHangul(cp)) {
            flags.hasKanaOrHangul = true;
        }
    }
    return flags;
}

QStringList extractAlphanumericTokens(const QString &rawText)
{
    const QString norm = normalize(rawText);
    return norm.split(QRegularExpression(QStringLiteral(R"([^a-zA-Z0-9]+)")), Qt::SkipEmptyParts);
}

QString buildHanVariants(const QStringList &tokens)
{
    if (tokens.isEmpty()) {
        return { };
    }
    const QString syllables = tokens.join(u' ');
    const QString joined = tokens.join(QString());
    QString initials;
    for (const auto &tok : tokens) {
        if (!tok.isEmpty() && tok.at(0).isLetter()) {
            initials.append(tok.at(0));
        }
    }

    QStringList parts;
    if (!syllables.isEmpty()) {
        parts.append(syllables);
    }
    if (!joined.isEmpty() && joined != syllables) {
        parts.append(joined);
    }
    if (!initials.isEmpty() && initials != joined && initials != syllables) {
        parts.append(initials);
    }
    return parts.join(u' ');
}

QString buildKanaVariants(const QStringList &tokens)
{
    if (tokens.isEmpty()) {
        return { };
    }
    const QString spaced = tokens.join(u' ');
    const QString joined = tokens.join(QString());

    QStringList parts;
    if (!spaced.isEmpty()) {
        parts.append(spaced);
    }
    if (!joined.isEmpty() && joined != spaced) {
        parts.append(joined);
    }
    return parts.join(u' ');
}

struct Segment {
    enum class Type : std::uint8_t { Cjk, Alphanumeric };
    Type type { Type::Alphanumeric };
    QString text;
};

struct WordSegments {
    QList<Segment> segments;
};

void pushSegmentIfNotEmpty(WordSegments &ws, Segment &seg, bool &hasCurrent)
{
    if (hasCurrent) {
        ws.segments.append(seg);
        seg = { };
        hasCurrent = false;
    }
}

WordSegments parseSingleWordSegments(const QString &rawWord)
{
    WordSegments ws;
    Segment currentSeg;
    bool hasCurrent = false;

    const QList<uint> ucs4 = rawWord.toUcs4();
    for (const uint cp : ucs4) {
        if (isCjk(cp)) {
            if (hasCurrent && currentSeg.type != Segment::Type::Cjk) {
                pushSegmentIfNotEmpty(ws, currentSeg, hasCurrent);
            }
            if (!hasCurrent) {
                currentSeg.type = Segment::Type::Cjk;
                hasCurrent = true;
            }
            appendCodePoint(currentSeg.text, cp);
        } else if (QChar::isLetterOrNumber(cp)) {
            if (hasCurrent && currentSeg.type != Segment::Type::Alphanumeric) {
                pushSegmentIfNotEmpty(ws, currentSeg, hasCurrent);
            }
            if (!hasCurrent) {
                currentSeg.type = Segment::Type::Alphanumeric;
                hasCurrent = true;
            }
            appendCodePoint(currentSeg.text, cp);
        } else {
            pushSegmentIfNotEmpty(ws, currentSeg, hasCurrent);
        }
    }
    pushSegmentIfNotEmpty(ws, currentSeg, hasCurrent);
    return ws;
}

QList<WordSegments> parseAllWords(const QStringList &rawWords)
{
    QList<WordSegments> parsedWords;
    for (const auto &rawWord : rawWords) {
        auto ws = parseSingleWordSegments(rawWord);
        if (!ws.segments.isEmpty()) {
            parsedWords.append(std::move(ws));
        }
    }
    return parsedWords;
}

QString formatSegmentTerm(const Segment &seg, bool isLastSegmentOfQuery)
{
    if (seg.type == Segment::Type::Cjk) {
        const QString spaced = spaceCjk(seg.text);
        if (spaced.isEmpty()) {
            return { };
        }
        QString escaped = spaced;
        escaped.replace(u'"', QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }

    QString escaped = seg.text;
    escaped.replace(u'"', QStringLiteral("\"\""));
    if (isLastSegmentOfQuery) {
        return QStringLiteral("\"%1\"*").arg(escaped);
    }
    return QStringLiteral("\"%1\"").arg(escaped);
}

} // namespace

QString normalize(const QString &text)
{
    if (text.isEmpty()) {
        return { };
    }
    UErrorCode status = U_ZERO_ERROR;
    const icu::Normalizer2 *norm = icu::Normalizer2::getNFKCCasefoldInstance(status);
    if (status > U_ZERO_ERROR || norm == nullptr) {
        return text.toLower();
    }
    const icu::UnicodeString ustr(text.utf16(), static_cast<int32_t>(text.length()));
    icu::UnicodeString result;
    norm->normalize(ustr, result, status);
    if (status > U_ZERO_ERROR) {
        return text.toLower();
    }
    return QString::fromUtf16(result.getBuffer(), static_cast<qsizetype>(result.length()));
}

QString indexText(const QString &text)
{
    const QString norm = normalize(text);
    if (norm.isEmpty()) {
        return { };
    }

    const QString baseSpaced = spaceCjk(norm);
    QStringList resultParts;
    if (!baseSpaced.isEmpty()) {
        resultParts.append(baseSpaced);
    }

    auto *hansToHant = getHansToHant();
    auto *hantToHans = getHantToHans();

    const QString hant = transliterateWith(hansToHant, norm);
    const QString hans = transliterateWith(hantToHans, norm);

    if (hant != norm) {
        const QString hantSpaced = spaceCjk(hant);
        if (!hantSpaced.isEmpty() && hantSpaced != baseSpaced) {
            resultParts.append(hantSpaced);
        }
    }

    if (hans != norm) {
        const QString hansSpaced = spaceCjk(hans);
        if (!hansSpaced.isEmpty() && hansSpaced != baseSpaced
            && !resultParts.contains(hansSpaced)) {
            resultParts.append(hansSpaced);
        }
    }

    return resultParts.join(u' ');
}

QString romanized(const QString &text)
{
    const QString norm = normalize(text);
    if (norm.isEmpty()) {
        return { };
    }

    const auto flags = detectScripts(norm);
    if (!flags.hasHan && !flags.hasKanaOrHangul) {
        return { };
    }

    if (flags.hasHan) {
        auto *hanToLatin = flags.hasKanaOrHangul ? getAnyToLatin() : getHanToLatin();
        const QString transliterated = transliterateWith(hanToLatin, norm);
        return buildHanVariants(extractAlphanumericTokens(transliterated));
    }

    // Only Kana / Hangul
    auto *anyToLatin = getAnyToLatin();
    const QString transliterated = transliterateWith(anyToLatin, norm);
    return buildKanaVariants(extractAlphanumericTokens(transliterated));
}

QString buildMatchQuery(const QString &userInput)
{
    const QString norm = normalize(userInput);
    if (norm.trimmed().isEmpty()) {
        return { };
    }

    const QStringList rawWords
        = norm.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
    if (rawWords.isEmpty()) {
        return { };
    }

    const auto parsedWords = parseAllWords(rawWords);
    if (parsedWords.isEmpty()) {
        return { };
    }

    QStringList allTerms;
    const qsizetype wordCount = parsedWords.size();
    for (qsizetype w = 0; w < wordCount; ++w) {
        const bool isLastWord = (w == wordCount - 1);
        const auto &ws = parsedWords.at(w);
        const qsizetype segCount = ws.segments.size();

        for (qsizetype s = 0; s < segCount; ++s) {
            const bool isLastOfQuery = (isLastWord && s == segCount - 1);
            const QString term = formatSegmentTerm(ws.segments.at(s), isLastOfQuery);
            if (!term.isEmpty()) {
                allTerms.append(term);
            }
        }
    }

    if (allTerms.isEmpty()) {
        return { };
    }

    return allTerms.join(QStringLiteral(" AND "));
}

} // namespace linernotes::library::search
