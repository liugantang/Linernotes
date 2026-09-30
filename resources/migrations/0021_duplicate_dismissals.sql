-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0021_duplicate_dismissals: User dismissals of duplicate pairs

CREATE TABLE duplicate_dismissals (
    track_a INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    track_b INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (track_a, track_b),
    CHECK (track_a < track_b)
) STRICT;
