-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0003_reserved_tables: Reserved tables for playback history, moments, audio features, embeddings, LLM cache, and change log

CREATE TABLE play_events (
    id INTEGER PRIMARY KEY,
    track_id INTEGER REFERENCES tracks(id) ON DELETE SET NULL,
    started_at INTEGER NOT NULL,
    ended_at INTEGER,
    played_ms INTEGER NOT NULL DEFAULT 0,
    track_duration_ms INTEGER,
    completed INTEGER NOT NULL DEFAULT 0 CHECK(completed IN (0, 1)),
    skipped INTEGER NOT NULL DEFAULT 0 CHECK(skipped IN (0, 1)),
    source TEXT NOT NULL DEFAULT 'local' CHECK(source IN ('local', 'lastfm', 'listenbrainz')),
    context TEXT,
    snapshot TEXT
) STRICT;

CREATE INDEX idx_play_events_track_started ON play_events(track_id, started_at);
CREATE INDEX idx_play_events_started_at ON play_events(started_at);

CREATE TABLE moments (
    id INTEGER PRIMARY KEY,
    track_id INTEGER REFERENCES tracks(id) ON DELETE SET NULL,
    title TEXT,
    body TEXT NOT NULL,
    happened_on TEXT,
    location TEXT,
    photo_path TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
) STRICT;

CREATE INDEX idx_moments_track_id ON moments(track_id);

CREATE TABLE audio_features (
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    extractor TEXT NOT NULL,
    extractor_version TEXT NOT NULL,
    data TEXT NOT NULL,
    computed_at INTEGER NOT NULL,
    PRIMARY KEY(track_id, extractor)
) STRICT;

CREATE TABLE embeddings (
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    model TEXT NOT NULL,
    dim INTEGER NOT NULL CHECK(dim > 0),
    dtype TEXT NOT NULL CHECK(dtype IN ('f32', 'f16', 'i8')),
    vector BLOB NOT NULL,
    computed_at INTEGER NOT NULL,
    PRIMARY KEY(track_id, model)
) STRICT;

CREATE TABLE llm_cache (
    key TEXT PRIMARY KEY,
    model TEXT NOT NULL,
    purpose TEXT NOT NULL,
    response TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER,
    hits INTEGER NOT NULL DEFAULT 0
) STRICT;

CREATE INDEX idx_llm_cache_expires_at ON llm_cache(expires_at);

CREATE TABLE change_log (
    id INTEGER PRIMARY KEY,
    entity_type TEXT NOT NULL,
    entity_id INTEGER NOT NULL,
    field TEXT,
    old_value TEXT,
    new_value TEXT,
    actor TEXT NOT NULL CHECK(actor IN ('user', 'scanner', 'rule', 'llm', 'writeback')),
    batch_id INTEGER REFERENCES correction_batches(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL
) STRICT;

CREATE INDEX idx_change_log_entity ON change_log(entity_type, entity_id);
CREATE INDEX idx_change_log_created_at ON change_log(created_at);
CREATE INDEX idx_change_log_batch_id ON change_log(batch_id);
