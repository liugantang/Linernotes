-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0019_track_versions: Add grouping_key to works and create track_versions table

ALTER TABLE works ADD COLUMN grouping_key TEXT;
CREATE UNIQUE INDEX idx_works_grouping_key ON works(grouping_key);

CREATE TABLE track_versions (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    base_title TEXT NOT NULL,
    version_type TEXT NOT NULL CHECK(version_type IN ('studio', 'live', 'remaster', 'acoustic', 'remix', 'demo', 'instrumental', 'edit', 'alternate')),
    unresolved INTEGER NOT NULL CHECK(unresolved IN (0, 1)),
    updated_at INTEGER NOT NULL
) STRICT;
