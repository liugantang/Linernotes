-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0011_butler: Butler foundation tables and correction batch kind

ALTER TABLE correction_batches ADD COLUMN kind TEXT NOT NULL DEFAULT 'manual';

CREATE TABLE track_issues (
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    kind TEXT NOT NULL,
    field TEXT NOT NULL DEFAULT '',
    detail TEXT,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (track_id, kind, field)
) STRICT;

CREATE TABLE mb_cache (
    url TEXT PRIMARY KEY,
    body TEXT NOT NULL,
    fetched_at INTEGER NOT NULL
) STRICT;
