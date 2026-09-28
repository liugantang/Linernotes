// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSet>
#include <QString>

#include <butler/ArtistGroup.h>
#include <butler/MusicBrainz.h>
#include <library/ArtistAliasCorrections.h>

#include <cstdint>
#include <optional>

namespace linernotes::butler {

/// 规范艺人：trackCount 最多者；相同时取名字按 QString::compare 较小者。
qint64 pickCanonical(const QList<ArtistEntry> &members);

/// 规则组 → 把其余成员作为规范艺人的别名（locale nullopt）。
/// confidence 0.95，source Rule，reason 用 QT_TRANSLATE_NOOP 的英文固定串（"Same name with
/// different spelling"）。
QList<library::ArtistAliasProposal> groupProposals(const ArtistGroup &group);

/// 选定与 ours 对应的 MB 艺人：score ≥ 90 且 (name == ours 或某个 alias.name == ours)
/// 的第一条；没有 → nullopt。
std::optional<MbArtist> adoptMbArtist(const QString &ours, const QList<MbArtist> &results);

/// mb 的带 locale 的 alias（en、ja、zh_Hans、zh_Hant、ko 及 "xx_YY" 形式）→ 挂在 ours
/// 上的语言名提议， confidence 0.9，source MusicBrainz；同一 locale 只取 primary 的那条，没有
/// primary 取第一条； 跳过与 ours 名字相同的、以及 libraryNames
/// 中存在的名字（曲库里的艺人交给分组合并处理）。
QList<library::ArtistAliasProposal> musicBrainzAliasProposals(
    const ArtistEntry &ours, const MbArtist &mb, const QSet<QString> &libraryNames);

} // namespace linernotes::butler
