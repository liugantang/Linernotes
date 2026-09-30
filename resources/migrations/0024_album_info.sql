-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0024_album_info: Records checked albums for LLM-based album info completion

CREATE TABLE album_info_checks (
    album_id INTEGER PRIMARY KEY REFERENCES albums(id) ON DELETE CASCADE,
    checked_at INTEGER NOT NULL,
    model TEXT NOT NULL,
    prompt_version INTEGER NOT NULL
) STRICT;
