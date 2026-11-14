-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0029_lyrics: Table for cached lyrics

CREATE TABLE lyrics (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    source TEXT NOT NULL CHECK(source IN ('manual', 'lrclib', 'none')),
    text TEXT NOT NULL DEFAULT '',
    synced INTEGER NOT NULL DEFAULT 0 CHECK(synced IN (0, 1)),
    instrumental INTEGER NOT NULL DEFAULT 0 CHECK(instrumental IN (0, 1)),
    lrclib_id INTEGER,
    fetched_at INTEGER NOT NULL
) STRICT;
