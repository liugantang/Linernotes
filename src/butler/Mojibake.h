// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringView>

#include <cstdint>
#include <optional>

namespace linernotes::butler {

enum class SourceEncoding : std::uint8_t {
    Gbk,
    Big5,
    ShiftJis,
    EucKr,
    Utf8,
};

/// 获取编码名称："gbk", "big5", "shift_jis", "euc-kr", "utf-8"，写进修正理由/日志用
QString encodingName(SourceEncoding e);

struct DecodeCandidate {
    SourceEncoding encoding = SourceEncoding::Gbk;
    QString text { };
    double score = 0.0; // [0,1]，越高越像该语言的正常文本
    bool operator==(const DecodeCandidate &) const = default;
};

/// 快速预筛：字符串是否可能是“CJK/UTF-8 字节被当成 Latin-1/Windows-1252 读出来”的乱码。
/// 只用作预筛，最终是否修复由 candidates 的得分决定。
bool looksLikeMojibake(QStringView text);

/// 已被替换成问号、无法从字节恢复的损坏（如 "???"、"??? - ??"、"周??"）：
/// 去掉空白与 ASCII 标点后，'?' 占比 ≥ 50% 且至少 2 个 '?'。纯 ASCII 的正常标题如 "Why?" 不算。
bool looksIrreparable(QStringView text);

/// 把乱码字符串还原成原始字节：全部字符 ≤ U+00FF 时按 Latin-1；否则尝试按 Windows-1252
/// （€ ‚ ƒ „ … † ‡ ˆ ‰ Š ‹ Œ Ž ‘ ’ “ ” • – — ˜ ™ š › œ ž Ÿ 映射回 0x80–0x9F）；都不行返回 nullopt。
std::optional<QByteArray> recoverBytes(QStringView text);

/// 对字节尝试所有 SourceEncoding 的严格解码（遇到非法序列即放弃该编码），
/// 返回合法候选，按 score 降序。纯 ASCII 字节返回空列表。
QList<DecodeCandidate> decodeCandidates(const QByteArray &bytes);

struct GroupDecision {
    std::optional<SourceEncoding> encoding; // 组内共同编码；没有任何合法候选时为空
    QList<std::optional<QString>>
        texts; // 与输入一一对应；该项无法用共同编码解码时取它自己的最佳候选，都没有则 nullopt
    double confidence = 0.0; // [0,1]
    bool ambiguous = false; // 需要交给 LLM（见下）
};

/// 同专辑（同目录）曲目共享编码判断：对每种编码把各项得分求和（非法的项记 0），取总分最高者。
GroupDecision decideGroup(const QList<QByteArray> &items);

} // namespace linernotes::butler
