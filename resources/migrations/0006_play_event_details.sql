-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0006_play_event_details: Add play_source, skip_position_ms, paused_ms, and device to play_events

ALTER TABLE play_events ADD COLUMN play_source TEXT NOT NULL DEFAULT 'unknown'
    CHECK(play_source IN ('unknown','library','album','artist','playlist','search','queue','nlq','dj','external'));
ALTER TABLE play_events ADD COLUMN skip_position_ms INTEGER;
ALTER TABLE play_events ADD COLUMN paused_ms INTEGER NOT NULL DEFAULT 0;
ALTER TABLE play_events ADD COLUMN device TEXT;
