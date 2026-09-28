// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <butler/Mojibake.h>
#include <butler/MojibakeSource.h>
#include <library/CorrectionStore.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace linernotes::butler {

struct AmbiguousItem {
    int id = 0; // 组内编号，从 0 开始，给 LLM 引用
    qint64 trackId = 0;
    library::TagField field = library::TagField::Title;
    QString original { };
    QList<DecodeCandidate> candidates; // 前 3 名

    bool operator==(const AmbiguousItem &) const = default;
};

struct GroupAnalysis {
    QList<library::CorrectionProposal> proposals; // 规则层结论 + 文件名推断
    QList<std::pair<qint64, library::TagField>> irreparable; // 需要记 track_issues 的
    QList<AmbiguousItem> ambiguous; // 交给 LLM 的
    std::optional<SourceEncoding> encoding;

    bool operator==(const GroupAnalysis &) const = default;
};

GroupAnalysis analyzeGroup(const MojibakeGroup &group);

struct FilenameGuess {
    QString title { };
    QString artist { };
    std::optional<int> trackNumber;

    bool operator==(const FilenameGuess &) const = default;
};

FilenameGuess guessFromPath(const QString &filePath);
QString guessAlbumFromDirectory(const QString &directory);

} // namespace linernotes::butler
