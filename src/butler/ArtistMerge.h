// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSet>
#include <QString>

#include <butler/ArtistGroup.h>
#include <library/ArtistAliasCorrections.h>

#include <cstdint>

namespace linernotes::butler {

/// 规范艺人：trackCount 最多者；相同时取名字按 QString::compare 较小者。
qint64 pickCanonical(const QList<ArtistEntry> &members);

/// 规则组 → 把其余成员作为规范艺人的别名（locale nullopt）。
/// confidence 0.95，source Rule，reason 用 QT_TRANSLATE_NOOP 的英文固定串（"Same name with
/// different spelling"）。
QList<library::ArtistAliasProposal> groupProposals(const ArtistGroup &group);

} // namespace linernotes::butler
