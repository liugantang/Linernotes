// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <butler/AlbumInfoSource.h>
#include <core/Result.h>
#include <library/CorrectionStore.h>

#include <cstdint>

namespace linernotes::butler {

enum class AlbumInfoEvidence : std::uint8_t { Path, Tags, Knowledge };

struct AlbumInfoProposals {
    QList<library::CorrectionProposal> fromEvidence; // path / tags：可按阈值自动接受
    QList<library::CorrectionProposal> fromKnowledge; // knowledge：一律不自动接受

    bool operator==(const AlbumInfoProposals &other) const = default;
};

QHash<QString, QString> albumInfoPromptVars(const QList<AlbumInfoInput> &albums);
QJsonObject albumInfoSchema();

// 解析并校验 LLM 结果，直接生成修正提议：
core::Result<AlbumInfoProposals> buildAlbumInfoProposals(
    const QJsonValue &value, const QList<AlbumInfoInput> &albums);

} // namespace linernotes::butler
