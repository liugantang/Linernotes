// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <core/Clock.h>
#include <core/Result.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <library/Database.h>

#include <optional>

namespace linernotes::guide {

struct TrackFact {
    KnowledgeKind kind = KnowledgeKind::Song;
    QString subject { };
    QString text { };
    QUrl sourceUrl { };
    bool operator==(const TrackFact &) const = default;
};

struct TrackKnowledge {
    std::optional<KnowledgeEntry> song;
    std::optional<KnowledgeEntry> album;
    std::optional<KnowledgeEntry> artist;
    bool operator==(const TrackKnowledge &) const = default;
};

// 只读缓存：用曲目的 artist / title / album 组 knowledgeKey，查 KnowledgeStore；过期或没有的为
// nullopt
core::Result<TrackKnowledge> cachedTrackKnowledge(
    library::Database &db, const core::Clock &clock, qint64 trackId);

// 展开成事实列表：每条结构化事实一条（text = "<factPropertyName>：<value>"，来源为 wikidataUrl）；
// 有 Wikipedia 摘要时再加一条（text = 摘要原文，来源为 summary.pageUrl）。顺序：歌曲、专辑、艺人。
QList<TrackFact> trackFacts(const TrackKnowledge &knowledge);

} // namespace linernotes::guide
