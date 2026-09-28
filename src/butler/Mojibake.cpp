// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Mojibake.h"

#include "ButlerLogging.h"
#include "EncodingScore.h"

#include <QChar>
#include <QLatin1StringView>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>

namespace linernotes::butler {

namespace {

constexpr std::array<SourceEncoding, 5> kAllEncodings {
    SourceEncoding::Gbk,
    SourceEncoding::Big5,
    SourceEncoding::ShiftJis,
    SourceEncoding::EucKr,
    SourceEncoding::Utf8,
};

// Windows-1252 扩展字符（0x80–0x9F）映射回对应单字节
constexpr std::optional<uint8_t> cp1252CharToByte(char16_t u)
{
    switch (u) {
    case 0x20AC:
        return 0x80;
    case 0x201A:
        return 0x82;
    case 0x0192:
        return 0x83;
    case 0x201E:
        return 0x84;
    case 0x2026:
        return 0x85;
    case 0x2020:
        return 0x86;
    case 0x2021:
        return 0x87;
    case 0x02C6:
        return 0x88;
    case 0x2030:
        return 0x89;
    case 0x0160:
        return 0x8A;
    case 0x2039:
        return 0x8B;
    case 0x0152:
        return 0x8C;
    case 0x017D:
        return 0x8E;
    case 0x2018:
        return 0x91;
    case 0x2019:
        return 0x92;
    case 0x201C:
        return 0x93;
    case 0x201D:
        return 0x94;
    case 0x2022:
        return 0x95;
    case 0x2013:
        return 0x96;
    case 0x2014:
        return 0x97;
    case 0x02DC:
        return 0x98;
    case 0x2122:
        return 0x99;
    case 0x0161:
        return 0x9A;
    case 0x203A:
        return 0x9B;
    case 0x0153:
        return 0x9C;
    case 0x017E:
        return 0x9E;
    case 0x0178:
        return 0x9F;
    default:
        return std::nullopt;
    }
}

bool isAsciiOnly(const QByteArray &bytes)
{
    return std::ranges::all_of(bytes, [](char b) { return static_cast<uint8_t>(b) < 0x80; });
}

struct EncodingStats {
    SourceEncoding encoding = SourceEncoding::Gbk;
    double totalScore = 0.0;
    int validCount = 0;
};

std::array<EncodingStats, 5> computeGroupEncodingStats(const QList<QByteArray> &items)
{
    std::array<EncodingStats, 5> stats { };
    for (size_t i = 0; i < kAllEncodings.size(); ++i) {
        stats.at(i).encoding = kAllEncodings.at(i);
    }

    for (const auto &item : items) {
        const bool isAscii = isAsciiOnly(item);

        for (size_t i = 0; i < kAllEncodings.size(); ++i) {
            const auto enc = kAllEncodings.at(i);
            const auto decoded = internal::strictDecode(item, enc);
            if (decoded.has_value()) {
                if (!isAscii) {
                    const double score = internal::calculateEncodingScore(*decoded, item, enc);
                    stats.at(i).totalScore += score;
                }
                stats.at(i).validCount += 1;
            }
        }
    }

    return stats;
}

QList<std::optional<QString>> buildFallbackGroupTexts(const QList<QByteArray> &items)
{
    QList<std::optional<QString>> texts;
    texts.reserve(items.size());
    for (const auto &item : items) {
        const auto cands = decodeCandidates(item);
        if (!cands.isEmpty()) {
            texts.append(cands.first().text);
        } else if (!item.isEmpty()) {
            texts.append(QString::fromUtf8(item));
        } else {
            texts.append(std::nullopt);
        }
    }
    return texts;
}

QList<std::optional<QString>> buildDecidedGroupTexts(
    const QList<QByteArray> &items, SourceEncoding sharedEncoding)
{
    QList<std::optional<QString>> texts;
    texts.reserve(items.size());
    for (const auto &item : items) {
        const auto decoded = internal::strictDecode(item, sharedEncoding);
        if (decoded.has_value()) {
            texts.append(decoded);
        } else {
            const auto cands = decodeCandidates(item);
            if (!cands.isEmpty()) {
                texts.append(cands.first().text);
            } else {
                texts.append(std::nullopt);
            }
        }
    }
    return texts;
}

} // namespace

QString encodingName(SourceEncoding e)
{
    switch (e) {
    case SourceEncoding::Gbk:
        return QStringLiteral("gbk");
    case SourceEncoding::Big5:
        return QStringLiteral("big5");
    case SourceEncoding::ShiftJis:
        return QStringLiteral("shift_jis");
    case SourceEncoding::EucKr:
        return QStringLiteral("euc-kr");
    case SourceEncoding::Utf8:
        return QStringLiteral("utf-8");
    }
    return QStringLiteral("gbk");
}

bool looksLikeMojibake(QStringView text)
{
    if (text.isEmpty()) {
        return false;
    }

    // 1. 含 C1 控制字符（U+0080–U+009F）在正常排版文本中绝不会出现，直接视为可疑乱码
    for (const QChar c : text) {
        if (c.unicode() >= 0x0080 && c.unicode() <= 0x009F) {
            return true;
        }
    }

    // 2. 尝试还原字节
    const auto bytesOpt = recoverBytes(text);
    if (!bytesOpt.has_value()) {
        return false;
    }

    // 检查是否存在至少一段 >= 2 个连续的 U+00A0–U+00FF / Windows-1252 特殊字符
    int maxRun = 0;
    int currentRun = 0;
    for (const QChar c : text) {
        if ((c.unicode() >= 0x00A0 && c.unicode() <= 0x00FF)
            || cp1252CharToByte(c.unicode()).has_value()) {
            ++currentRun;
            maxRun = std::max(maxRun, currentRun);
        } else {
            currentRun = 0;
        }
    }

    if (maxRun < 2) {
        return false;
    }

    // 解码候选的最佳得分需 >= 0.6，避免误判合法的重音拉丁文本
    const auto candidates = decodeCandidates(*bytesOpt);
    return (!candidates.isEmpty() && candidates.first().score >= 0.6);
}

bool looksIrreparable(QStringView text)
{
    int questionCount = 0;
    int remainingCount = 0;

    for (const QChar c : text) {
        if (c.isSpace()) {
            continue;
        }
        // 去除空白与 ASCII 标点（'?' 除外）
        if (c.unicode() <= 127 && std::ispunct(static_cast<unsigned char>(c.toLatin1())) != 0
            && c != u'?') {
            continue;
        }
        if (c == u'?') {
            ++questionCount;
        }
        ++remainingCount;
    }

    if (remainingCount == 0 || questionCount < 2) {
        return false;
    }

    return (static_cast<double>(questionCount) / static_cast<double>(remainingCount)) >= 0.5;
}

std::optional<QByteArray> recoverBytes(QStringView text)
{
    if (text.isEmpty()) {
        return QByteArray();
    }

    bool allLatin1 = true;
    for (const QChar c : text) {
        if (c.unicode() > 0x00FF) {
            allLatin1 = false;
            break;
        }
    }

    if (allLatin1) {
        QByteArray bytes;
        bytes.reserve(text.size());
        for (const QChar c : text) {
            bytes.append(static_cast<char>(static_cast<uint8_t>(c.unicode())));
        }
        return bytes;
    }

    // 尝试按 Windows-1252 映射表还原
    QByteArray bytes;
    bytes.reserve(text.size());
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (u <= 0x00FF) {
            bytes.append(static_cast<char>(static_cast<uint8_t>(u)));
        } else {
            const auto mapped = cp1252CharToByte(u);
            if (!mapped.has_value()) {
                return std::nullopt;
            }
            bytes.append(static_cast<char>(*mapped));
        }
    }

    return bytes;
}

QList<DecodeCandidate> decodeCandidates(const QByteArray &bytes)
{
    if (bytes.isEmpty() || isAsciiOnly(bytes)) {
        return { };
    }

    QList<DecodeCandidate> candidates;
    for (const auto enc : kAllEncodings) {
        const auto textOpt = internal::strictDecode(bytes, enc);
        if (!textOpt.has_value()) {
            continue;
        }
        const double score = internal::calculateEncodingScore(*textOpt, bytes, enc);
        candidates.append(DecodeCandidate {
            .encoding = enc,
            .text = *textOpt,
            .score = score,
        });
    }

    std::ranges::stable_sort(candidates,
        [](const DecodeCandidate &a, const DecodeCandidate &b) { return a.score > b.score; });

    return candidates;
}

GroupDecision decideGroup(const QList<QByteArray> &items)
{
    if (items.isEmpty()) {
        return GroupDecision {
            .encoding = std::nullopt,
            .texts = { },
            .confidence = 0.0,
            .ambiguous = false,
        };
    }

    auto stats = computeGroupEncodingStats(items);
    std::ranges::stable_sort(stats,
        [](const EncodingStats &a, const EncodingStats &b) { return a.totalScore > b.totalScore; });

    const auto &best = stats.at(0);
    const auto &secondBest = stats.at(1);

    if (best.totalScore <= 0.0) {
        return GroupDecision {
            .encoding = std::nullopt,
            .texts = buildFallbackGroupTexts(items),
            .confidence = 0.0,
            .ambiguous = false,
        };
    }

    const auto n = static_cast<double>(items.size());
    const double avgScore = best.totalScore / n;
    const double avgDiff = (best.totalScore - secondBest.totalScore) / n;

    // 置信度判断：平均得分 < 0.6 或与次高编码差距 < 0.15 时判定为模棱两可（需要 LLM 仲裁）
    const bool ambiguous = (avgScore < 0.6 || avgDiff < 0.15);
    const double confidence
        = std::clamp((0.5 * avgScore) + (0.5 * std::min(1.0, avgDiff / 0.3)), 0.0, 1.0);

    return GroupDecision {
        .encoding = best.encoding,
        .texts = buildDecidedGroupTexts(items, best.encoding),
        .confidence = confidence,
        .ambiguous = ambiguous,
    };
}

} // namespace linernotes::butler
