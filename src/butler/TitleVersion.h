// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <library/LibraryEnums.h>

#include <cstdint>
#include <functional>

namespace linernotes::butler {

enum class SuffixRole : std::uint8_t {
    Version, // 版本标记，从标题去掉，并决定版本类型
    Annotation, // 注记（feat.、bonus track、采样率、CV 署名、来源说明），从标题去掉，不影响版本类型
    TitlePart, // 标题本身的一部分，保留
    Unknown, // 规则无法判断
};

struct SuffixClass {
    SuffixRole role = SuffixRole::Unknown;
    library::VersionType type = library::VersionType::Studio; // 仅 role == Version 时有意义
    bool operator==(const SuffixClass &) const = default;
};

struct TitleSuffix {
    QString text { }; // 去掉括号/分隔符后的内文，已 trim
    qsizetype start = 0; // 该后缀（含括号或分隔符前的空白）在原标题中的起始位置
    bool operator==(const TitleSuffix &) const = default;
};

/// 从标题末尾依次剥离后缀，最多 3 个，返回顺序为从外到内（最末尾的在前）。剥离规则：
/// - 末尾的成对括号：() （） [] ［］ 【】
/// 〔〕（按配对找左括号，内部可含其他种类的括号；左括号找不到则停止）
/// - 末尾的 “ - xxx” / “ – xxx” / “ — xxx”：分隔符两侧都必须有空白（`Re-Boot`、`X-Day` 这类不算）
/// - 末尾成对的波浪号 “〜xxx〜” / “～xxx～” / “~xxx~”
/// 剥离后剩余标题（trim 后）为空则不剥这一个并停止；内文为空的后缀跳过但照样剥掉（如 “()”）。
QList<TitleSuffix> splitSuffixes(const QString &title);

/// 规则分类，只认明确的关键词，其他一律 Unknown。匹配前 NFKC + 小写。按下列顺序第一个命中的为准：
///  1. Instrumental：instrumental、inst（独立词或 “inst.”）、off vocal /
///  off-vocal、karaoke、カラオケ、からおけ、伴奏、backing track
///  2. Live：独立词 live（允许后接 # 或数字，如 live#10）、ライブ、现场、現場
///  3. Remaster：remaster、remastered、リマスター
///  4. Demo：独立词 demo、デモ
///  5. Acoustic：acoustic、unplugged、アコースティック；或者含 piano / ピアノ 且
///  (整个后缀就是它，或含 ver / version / arrange)
///  6. Remix：remix、独立词 mix、リミックス、extended、rework
///  7. Edit：tv size、tvサイズ、tv ver、tv edit、radio edit、single ver / single version、short ver
///  / short version、ショート、独立词 edit
///  8. Alternate：独立词 ver / ver. / version、バージョン（上面都没命中的具名版本，如 album
///  ver.、english ver.、crossroads version）
///  9. Annotation：以 feat. / feat / ft. / featuring 开头；bonus track、ボーナストラック；含
///  数字+khz 或 数字+bit；hi-res、high-resolution；
///     以 cv. / cv: / cv 开头；以 from 开头；整个后缀是 4 位年份
/// 不命中 → Unknown。不写任何艺人名、作品名之类的名单。
SuffixClass classifySuffix(const QString &suffixText);

/// 后缀的缓存键（7.6b 用来去重、缓存 LLM 结果）：exactKey(suffixText)（butler/ArtistName.h）。
QString suffixKey(const QString &suffixText);

struct TitleVersion {
    QString baseTitle { }; // 去掉 Version / Annotation 后缀后的标题，trim
    library::VersionType type = library::VersionType::Studio;
    bool unresolved = false; // 过程中遇到 Unknown
    bool operator==(const TitleVersion &) const = default;
};

/// 从外到内处理 splitSuffixes 的结果，classify 给出每个后缀的角色：
/// - Version / Annotation：剥掉，继续看下一个
/// - TitlePart：停止（它及其左边都保留）
/// - Unknown：unresolved = true 并停止（保守：不剥）
/// baseTitle = title.left(最内侧被剥掉的后缀的 start).trimmed()，没剥则为 title.trimmed()。
/// type：被剥掉的 Version 后缀中，按 classifySuffix 的优先顺序（Instrumental > Live > Remaster >
/// Demo > Acoustic > Remix > Edit > Alternate）取最高者；没有则 Studio。
TitleVersion resolveTitle(
    const QString &title, const std::function<SuffixClass(const QString &)> &classify);

} // namespace linernotes::butler
