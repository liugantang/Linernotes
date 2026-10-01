-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0027_title_matches: Cache table for cross-script title match decisions

CREATE TABLE title_matches (
    key_a TEXT NOT NULL,           -- exactKey，key_a < key_b
    key_b TEXT NOT NULL,
    same INTEGER NOT NULL,         -- 1 = 同一首歌的不同写法
    confidence REAL NOT NULL,
    reason TEXT NOT NULL,
    model TEXT NOT NULL,
    prompt_version INTEGER NOT NULL,
    decided_at INTEGER NOT NULL,   -- 毫秒，core::Clock
    PRIMARY KEY (key_a, key_b)
) STRICT;
