-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0016_mb_matches: MusicBrainz release and recording match tables

CREATE TABLE mb_album_matches (
    album_id INTEGER PRIMARY KEY REFERENCES albums(id) ON DELETE CASCADE,
    status TEXT NOT NULL CHECK(status IN ('matched', 'ambiguous', 'no_match')),
    release_id TEXT,           -- matched / ambiguous 时为最高分者
    release_group_id TEXT,
    score REAL,
    label TEXT,                -- 厂牌，多个用 "; " 连接
    matched_at INTEGER NOT NULL
) STRICT;

CREATE TABLE mb_track_matches (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    release_id TEXT NOT NULL,
    recording_id TEXT NOT NULL,
    score REAL NOT NULL
) STRICT;
