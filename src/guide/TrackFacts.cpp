// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackFacts.h"

#include <guide/KnowledgeStore.h>
#include <guide/Wikidata.h>
#include <library/LibraryQuery.h>

namespace linernotes::guide {

core::Result<TrackKnowledge> cachedTrackKnowledge(
    library::Database &db, const core::Clock &clock, qint64 trackId)
{
    if (trackId <= 0) {
        return TrackKnowledge { };
    }

    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const library::LibraryQuery query(conn);
    const auto rowsRes = query.tracksByIds({ trackId });
    if (!rowsRes.ok()) {
        return rowsRes.error();
    }
    if (rowsRes.value().isEmpty()) {
        return TrackKnowledge { };
    }

    const auto &row = rowsRes.value().at(0);
    TrackKnowledge knowledge;
    const KnowledgeStore store(db, clock);

    if (!row.artist.trimmed().isEmpty()) {
        if (!row.title.trimmed().isEmpty()) {
            const KnowledgeQuery songQuery {
                .kind = KnowledgeKind::Song,
                .artist = row.artist,
                .title = row.title,
            };
            const auto songRes = store.get(knowledgeKey(songQuery));
            if (!songRes.ok()) {
                return songRes.error();
            }
            if (songRes.value().state == LookupState::Found) {
                knowledge.song = songRes.value().entry;
            }
        }

        if (!row.album.trimmed().isEmpty()) {
            const KnowledgeQuery albumQuery {
                .kind = KnowledgeKind::Album,
                .artist = row.artist,
                .title = row.album,
            };
            const auto albumRes = store.get(knowledgeKey(albumQuery));
            if (!albumRes.ok()) {
                return albumRes.error();
            }
            if (albumRes.value().state == LookupState::Found) {
                knowledge.album = albumRes.value().entry;
            }
        }

        const KnowledgeQuery artistQuery {
            .kind = KnowledgeKind::Artist,
            .artist = row.artist,
            .title = { },
        };
        const auto artistRes = store.get(knowledgeKey(artistQuery));
        if (!artistRes.ok()) {
            return artistRes.error();
        }
        if (artistRes.value().state == LookupState::Found) {
            knowledge.artist = artistRes.value().entry;
        }
    }

    return knowledge;
}

QList<TrackFact> trackFacts(const TrackKnowledge &knowledge)
{
    QList<TrackFact> facts;

    auto appendEntryFacts = [&facts](const std::optional<KnowledgeEntry> &optEntry) {
        if (!optEntry.has_value()) {
            return;
        }
        const auto &entry = *optEntry;
        for (const auto &fact : entry.facts) {
            facts.append(TrackFact {
                .kind = entry.kind,
                .subject = entry.label,
                .text = factPropertyName(fact.property) + QStringLiteral("：") + fact.value,
                .sourceUrl = entry.wikidataUrl,
            });
        }
        if (entry.summary.has_value()) {
            const auto &sum = *entry.summary;
            facts.append(TrackFact {
                .kind = entry.kind,
                .subject = entry.label,
                .text = sum.extract,
                .sourceUrl = sum.pageUrl,
            });
        }
    };

    appendEntryFacts(knowledge.song);
    appendEntryFacts(knowledge.album);
    appendEntryFacts(knowledge.artist);

    return facts;
}

} // namespace linernotes::guide
