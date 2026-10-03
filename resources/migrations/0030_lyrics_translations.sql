-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0030_lyrics_translations: Table for cached lyrics translations

CREATE TABLE lyrics_translations (
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    lyrics_hash TEXT NOT NULL,
    target_lang TEXT NOT NULL,
    lines TEXT NOT NULL,          -- JSON 数组，与解析后的歌词行等长
    model TEXT NOT NULL,
    prompt_version INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (track_id, target_lang)
) STRICT;
