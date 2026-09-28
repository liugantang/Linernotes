// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <butler/ArtistGroup.h>
#include <butler/MusicBrainz.h>
#include <library/ArtistAliasCorrections.h>

#include <cstdint>

namespace linernotes::butler {

/// 规范艺人：trackCount 最多者；相同时取名字按 QString::compare 较小者。
qint64 pickCanonical(const QList<ArtistEntry> &members);

/// 规则组 → 把其余成员作为规范艺人的别名（locale nullopt）。
/// confidence 0.95，source Rule，reason 用 QT_TRANSLATE_NOOP 的英文固定串（"Same name with
/// different spelling"）。
QList<library::ArtistAliasProposal> groupProposals(const ArtistGroup &group);

/// MusicBrainz 结果 → 提议。只采用 score ≥ 90 且 (name == ours 或某个 alias.name == ours) 的第一条
/// MB 艺人； 没有这样的条目 → 空列表。对采用的 MB 艺人：
///  - 其 name 与各 alias.name 中，除 ours 以外、在曲库 nameToId 中存在（精确匹配）的 → 合并提议：
///    规范艺人 = 这些曲库艺人与 ours 中 trackCount 最多者（用 pickCanonical），其余作为它的别名，
///    locale 取 MB 中同名 alias 的 locale（若存在且支持），confidence 0.95，source MusicBrainz；
///  - 带 locale 的 alias（en、ja、zh_Hans、zh_Hant、ko 以及它们的 "xx_YY" 形式）→
///    语言名提议：挂在上面的规范艺人上，locale 取原值，confidence
///    0.9（跳过已作为合并提议输出的名字）。 同一 locale 只取 primary 的那条，没有 primary
///    取第一条。
QList<library::ArtistAliasProposal> musicBrainzProposals(const ArtistEntry &ours,
    const QList<MbArtist> &results, const QHash<QString, ArtistEntry> &libraryByName);

} // namespace linernotes::butler
