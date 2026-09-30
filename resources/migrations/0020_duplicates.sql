-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0020_duplicates: Duplicate groups and members tables

CREATE TABLE duplicate_groups (
    id INTEGER PRIMARY KEY,
    kind TEXT NOT NULL CHECK(kind IN ('exact', 'same_recording', 'suspect')),
    created_at INTEGER NOT NULL
) STRICT;

CREATE TABLE duplicate_members (
    group_id INTEGER NOT NULL REFERENCES duplicate_groups(id) ON DELETE CASCADE,
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    keep_score REAL NOT NULL,
    recommended INTEGER NOT NULL CHECK(recommended IN (0, 1)),
    PRIMARY KEY (group_id, track_id)
) STRICT;
CREATE INDEX idx_duplicate_members_track_id ON duplicate_members(track_id);
