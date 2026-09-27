-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0007_track_play_stats: Track playback statistics cache table and album play completion view

CREATE TABLE track_play_stats (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    play_count INTEGER NOT NULL DEFAULT 0,
    last_played_at INTEGER,
    total_played_ms INTEGER NOT NULL DEFAULT 0,
    avg_completion REAL,
    skip_count INTEGER NOT NULL DEFAULT 0
) STRICT;

CREATE VIEW album_play_completion AS
SELECT
    t.album_id AS album_id,
    COUNT(t.id) AS track_count,
    COUNT(CASE WHEN s.play_count > 0 THEN 1 END) AS played_track_count,
    COUNT(CASE WHEN s.play_count > 0 THEN 1 END) * 1.0 / COUNT(t.id) AS completion
FROM tracks t
LEFT JOIN track_play_stats s ON t.id = s.track_id
WHERE t.album_id IS NOT NULL
GROUP BY t.album_id;
