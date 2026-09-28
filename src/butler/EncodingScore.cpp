// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EncodingScore.h"

#include "ButlerLogging.h"
#include "CharFrequency.h"

#include <QChar>
#include <QLatin1StringView>

#include <uchardet.h>
#include <unicode/uchar.h>
#include <unicode/ucnv.h>
#include <unicode/utypes.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace linernotes::butler::internal {

namespace {

struct UConverterDeleter {
    void operator()(UConverter *cnv) const
    {
        if (cnv != nullptr) {
            ucnv_close(cnv);
        }
    }
};

using UConverterPtr = std::unique_ptr<UConverter, UConverterDeleter>;

struct UchardetDeleter {
    void operator()(uchardet_t ud) const
    {
        if (ud != nullptr) {
            uchardet_delete(ud);
        }
    }
};

using UchardetPtr = std::unique_ptr<uchardet, UchardetDeleter>;

double calculateCharPenalty(char32_t cp)
{
    const auto cat = QChar::category(cp);
    double penalty = 0.0;

    // 控制字符（除常见空白符制表符、换行符外）
    if (cat == QChar::Other_Control || (cp <= 0x1F && cp != '\t' && cp != '\n' && cp != '\r')
        || (cp >= 0x7F && cp <= 0x9F)) {
        penalty += 2.0;
    }
    // 私用区
    if (cat == QChar::Other_PrivateUse || (cp >= 0xE000 && cp <= 0xF8FF)
        || (cp >= 0xF0000 && cp <= 0xFFFFD) || (cp >= 0x100000 && cp <= 0x10FFFD)) {
        penalty += 2.0;
    }
    // 未分配码位 / 非字符
    if (cat == QChar::Other_NotAssigned || (cp >= 0xFDD0 && cp <= 0xFDEF)
        || ((cp & 0xFFFE) == 0xFFFE)) {
        penalty += 2.0;
    }
    // 半角片假名惩罚
    if (cp >= 0xFF61 && cp <= 0xFF9F) {
        penalty += 0.5;
    }

    return penalty;
}

double scoreGbkChar(UConverter *cnv, const UChar *u16Buf, int32_t u16Len, char32_t cp)
{
    // 高频字（简体或繁体表）得 1.0
    if (isSimplifiedChineseHighFreq(cp) || isTraditionalChineseHighFreq(cp)) {
        return 1.0;
    }

    std::array<char, 8> outBytes { };
    UErrorCode encStatus = U_ZERO_ERROR;
    const int32_t encLen = ucnv_fromUChars(
        cnv, outBytes.data(), static_cast<int32_t>(outBytes.size()), u16Buf, u16Len, &encStatus);
    if (U_FAILURE(encStatus) != 0 || encLen <= 0) {
        return 0.0;
    }

    // GB2312 一级字区（0xB0–0xD7）或全角标点符号区（0xA1–0xA9）但不在高频表：0.4
    if (encLen == 2) {
        const auto b1 = static_cast<uint8_t>(outBytes.at(0));
        if ((b1 >= 0xB0 && b1 <= 0xD7) || (b1 >= 0xA1 && b1 <= 0xA9)) {
            return 0.4;
        }
    }

    // 其他合法 GBK 字符（如 GBK 扩展区二级字等）：0.15
    return 0.15;
}

double scoreBig5Char(UConverter *cnv, const UChar *u16Buf, int32_t u16Len, char32_t cp)
{
    // 高频繁体字得 1.0
    if (isTraditionalChineseHighFreq(cp)) {
        return 1.0;
    }

    std::array<char, 8> outBytes { };
    UErrorCode encStatus = U_ZERO_ERROR;
    const int32_t encLen = ucnv_fromUChars(
        cnv, outBytes.data(), static_cast<int32_t>(outBytes.size()), u16Buf, u16Len, &encStatus);
    if (U_FAILURE(encStatus) != 0 || encLen <= 0) {
        return 0.0;
    }

    if (encLen == 2) {
        const auto b1 = static_cast<uint8_t>(outBytes.at(0));
        const auto b2 = static_cast<uint8_t>(outBytes.at(1));
        const auto val = static_cast<uint16_t>((static_cast<uint16_t>(b1) << 8) | b2);
        // Big5 常用字区（0xA440–0xC67E）或符号区（0xA140–0xA3BF）：0.4
        if ((val >= 0xA440 && val <= 0xC67E) || (val >= 0xA140 && val <= 0xA3BF)) {
            return 0.4;
        }
    }

    // 其他合法 Big5 字符：0.15
    return 0.15;
}

double scoreEucKrChar(UConverter *cnv, const UChar *u16Buf, int32_t u16Len, char32_t cp)
{
    // 韩文高频音节得 1.0
    if (isKoreanHighFreq(cp)) {
        return 1.0;
    }

    std::array<char, 8> outBytes { };
    UErrorCode encStatus = U_ZERO_ERROR;
    const int32_t encLen = ucnv_fromUChars(
        cnv, outBytes.data(), static_cast<int32_t>(outBytes.size()), u16Buf, u16Len, &encStatus);
    if (U_FAILURE(encStatus) != 0 || encLen <= 0) {
        return 0.0;
    }

    // EUC-KR 解码出的汉字（Hanja）按其他合法字符 0.15
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0xF900 && cp <= 0xFAFF)) {
        return 0.15;
    }

    if (encLen == 2) {
        const auto b1 = static_cast<uint8_t>(outBytes.at(0));
        const auto b2 = static_cast<uint8_t>(outBytes.at(1));
        const auto val = static_cast<uint16_t>((static_cast<uint16_t>(b1) << 8) | b2);
        // KS X 1001 常用韩文音节（0xB0A1–0xC8FE）或常用符号（0xA1A1–0xA2FE）：0.4
        if ((val >= 0xB0A1 && val <= 0xC8FE) || (val >= 0xA1A1 && val <= 0xA2FE)) {
            return 0.4;
        }
    }

    // 其他合法 EUC-KR 字符：0.15
    return 0.15;
}

double scoreShiftJisChar(UConverter *cnv, const UChar *u16Buf, int32_t u16Len, char32_t cp)
{
    // Shift-JIS 保持原逻辑
    std::array<char, 8> outBytes { };
    UErrorCode encStatus = U_ZERO_ERROR;
    const int32_t encLen = ucnv_fromUChars(
        cnv, outBytes.data(), static_cast<int32_t>(outBytes.size()), u16Buf, u16Len, &encStatus);
    if (U_FAILURE(encStatus) != 0 || encLen <= 0) {
        return 0.0;
    }

    if (encLen == 2) {
        const auto b1 = static_cast<uint8_t>(outBytes.at(0));
        const auto b2 = static_cast<uint8_t>(outBytes.at(1));
        const auto val = static_cast<uint16_t>((static_cast<uint16_t>(b1) << 8) | b2);
        // 平假名/片假名（全角）、符号（0x8140–0x84BE）、JIS 第一水准（0x889F–0x9872）为常用字：1.0
        if ((val >= 0x8140 && val <= 0x84BE) || (val >= 0x889F && val <= 0x9872)
            || (cp >= 0x3040 && cp <= 0x30FF)) {
            return 1.0;
        }
    }

    // 其他合法 Shift-JIS 字符：0.3
    return 0.3;
}

double scoreUtf8Char(char32_t cp)
{
    // UTF-8 保持原逻辑：CJK、假名、韩文音节、全半角常见标点、拉丁扩展为常用字：1.0
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x3040 && cp <= 0x309F)
        || (cp >= 0x30A0 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF)
        || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF)
        || (cp >= 0x3130 && cp <= 0x318F) || (cp >= 0x3000 && cp <= 0x303F)
        || (cp >= 0xFF01 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6)
        || (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x2100 && cp <= 0x214F)
        || (cp >= 0x00C0 && cp <= 0x00FF) || (cp >= 0x0100 && cp <= 0x024F)) {
        return 1.0;
    }

    // 其他合法 UTF-8 字符：0.3
    return 0.3;
}

double scoreSingleChar(
    SourceEncoding encoding, UConverter *cnv, const UChar *u16Buf, int32_t u16Len, char32_t cp)
{
    switch (encoding) {
    case SourceEncoding::Gbk:
        return scoreGbkChar(cnv, u16Buf, u16Len, cp);
    case SourceEncoding::Big5:
        return scoreBig5Char(cnv, u16Buf, u16Len, cp);
    case SourceEncoding::ShiftJis:
        return scoreShiftJisChar(cnv, u16Buf, u16Len, cp);
    case SourceEncoding::EucKr:
        return scoreEucKrChar(cnv, u16Buf, u16Len, cp);
    case SourceEncoding::Utf8:
        return scoreUtf8Char(cp);
    }
    return 0.0;
}

} // namespace

const char *icuConverterName(SourceEncoding encoding)
{
    switch (encoding) {
    case SourceEncoding::Gbk:
        return "windows-936";
    case SourceEncoding::Big5:
        return "windows-950";
    case SourceEncoding::ShiftJis:
        return "windows-31j";
    case SourceEncoding::EucKr:
        return "windows-949";
    case SourceEncoding::Utf8:
        return "UTF-8";
    }
    return "windows-936";
}

std::optional<QString> strictDecode(const QByteArray &bytes, SourceEncoding encoding)
{
    if (bytes.isEmpty()) {
        return QString { };
    }

    UErrorCode status = U_ZERO_ERROR;
    const UConverterPtr cnv(ucnv_open(icuConverterName(encoding), &status));
    if (U_FAILURE(status) != 0 || cnv == nullptr) {
        return std::nullopt;
    }

    // 设置遇到非法/无法映射序列时立刻停止报错，确保严格解码
    ucnv_setToUCallBack(cnv.get(), UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
    if (U_FAILURE(status) != 0) {
        return std::nullopt;
    }

    int32_t capacity = std::max<int32_t>(32, (static_cast<int32_t>(bytes.size()) * 4) + 16);
    std::vector<UChar> target(static_cast<size_t>(capacity));

    int32_t len = ucnv_toUChars(cnv.get(), target.data(), capacity, bytes.constData(),
        static_cast<int32_t>(bytes.size()), &status);
    if (status == U_BUFFER_OVERFLOW_ERROR) {
        status = U_ZERO_ERROR;
        capacity = len + 4;
        target.resize(static_cast<size_t>(capacity));
        ucnv_reset(cnv.get());
        len = ucnv_toUChars(cnv.get(), target.data(), capacity, bytes.constData(),
            static_cast<int32_t>(bytes.size()), &status);
    }

    if (U_FAILURE(status) != 0 || len < 0) {
        return std::nullopt;
    }

    return QString::fromUtf16(target.data(), len);
}

bool matchesUchardetFamily(const QByteArray &bytes, SourceEncoding encoding)
{
    if (bytes.size() < 8) {
        return false;
    }

    const UchardetPtr ud(uchardet_new());
    if (ud == nullptr) {
        return false;
    }

    uchardet_handle_data(ud.get(), bytes.constData(), static_cast<size_t>(bytes.size()));
    uchardet_data_end(ud.get());

    const char *cs = uchardet_get_charset(ud.get());
    if (cs == nullptr) {
        return false;
    }

    const QString charset = QString::fromLatin1(cs).trimmed().toUpper();
    if (charset.isEmpty()) {
        return false;
    }

    switch (encoding) {
    case SourceEncoding::Gbk:
        return charset == QLatin1StringView("GB18030") || charset == QLatin1StringView("GB2312")
            || charset == QLatin1StringView("GBK") || charset == QLatin1StringView("WINDOWS-936")
            || charset == QLatin1StringView("CP936");
    case SourceEncoding::Big5:
        return charset == QLatin1StringView("BIG5") || charset == QLatin1StringView("BIG-5")
            || charset == QLatin1StringView("WINDOWS-950") || charset == QLatin1StringView("CP950");
    case SourceEncoding::ShiftJis:
        return charset == QLatin1StringView("SHIFT_JIS")
            || charset == QLatin1StringView("SHIFT-JIS") || charset == QLatin1StringView("SJIS")
            || charset == QLatin1StringView("WINDOWS-31J") || charset == QLatin1StringView("CP932");
    case SourceEncoding::EucKr:
        return charset == QLatin1StringView("EUC-KR") || charset == QLatin1StringView("EUC_KR")
            || charset == QLatin1StringView("UHC") || charset == QLatin1StringView("WINDOWS-949")
            || charset == QLatin1StringView("CP949");
    case SourceEncoding::Utf8:
        return charset == QLatin1StringView("UTF-8") || charset == QLatin1StringView("UTF8");
    }
    return false;
}

double calculateEncodingScore(
    const QString &text, const QByteArray &rawBytes, SourceEncoding encoding)
{
    // 打分算法设计思路（DEVELOPMENT 2.9 & 6.2 review）：
    // 1. 高频字表辅助三档打分：
    //    - 命中该语言高频字表（简体/繁体约 800 字、韩文约 400 音节）得 1.0。
    //    - 落在编码常用分区但未命中高频表得 0.4（GBK 一级字、Big5 常用字、KS X 1001 音节）。
    //    - 其他合法映射字符得 0.15（EUC-KR 解码出的 Hanja 亦按 0.15）。
    //    - Shift-JIS 与 UTF-8 保持原逻辑（常用区 1.0，其他合法 0.3）。
    // 2. 异常惩罚：控制字符、私用区、未分配码位各惩罚 2.0；半角片假名惩罚 0.5。
    // 3. uchardet 辅助：字节数 >= 8 且同族时在截断前加 0.05，最后再截断到 [0.0, 1.0]。

    double charScoreSum = 0.0;
    double penalty = 0.0;
    int nonAsciiCount = 0;

    UConverterPtr cnv;
    if (encoding != SourceEncoding::Utf8) {
        UErrorCode cnvStatus = U_ZERO_ERROR;
        cnv.reset(ucnv_open(icuConverterName(encoding), &cnvStatus));
        if (U_FAILURE(cnvStatus) != 0 || cnv == nullptr) {
            return 0.0;
        }
        ucnv_setFromUCallBack(
            cnv.get(), UCNV_FROM_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &cnvStatus);
        if (U_FAILURE(cnvStatus) != 0) {
            return 0.0;
        }
    }

    for (qsizetype i = 0; i < text.size(); ++i) {
        char32_t cp = 0;
        std::array<UChar, 2> u16Buf { };
        int32_t u16Len = 0;

        if (text.at(i).isHighSurrogate() && (i + 1) < text.size()
            && text.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            u16Buf.at(0) = static_cast<UChar>(text.at(i).unicode());
            u16Buf.at(1) = static_cast<UChar>(text.at(i + 1).unicode());
            u16Len = 2;
            ++i;
        } else {
            cp = text.at(i).unicode();
            u16Buf.at(0) = static_cast<UChar>(text.at(i).unicode());
            u16Len = 1;
        }

        penalty += calculateCharPenalty(cp);

        // ASCII 字符不参与非 ASCII 计分与统计
        if (cp <= 0x7F) {
            continue;
        }

        ++nonAsciiCount;
        charScoreSum += scoreSingleChar(encoding, cnv.get(), u16Buf.data(), u16Len, cp);
    }

    if (nonAsciiCount == 0) {
        return 0.0;
    }

    double rawScore = (charScoreSum - penalty) / static_cast<double>(nonAsciiCount);

    // uchardet 辅助加分（先加在原始分上，再统一截断）
    if (rawBytes.size() >= 8 && matchesUchardetFamily(rawBytes, encoding)) {
        rawScore += 0.05;
    }

    return std::clamp(rawScore, 0.0, 1.0);
}

} // namespace linernotes::butler::internal
