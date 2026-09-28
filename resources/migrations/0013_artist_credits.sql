-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0013_artist_credits: Cache table for artist credit parsing results

CREATE TABLE artist_credits (
    value TEXT PRIMARY KEY,        -- 原始字段值（trim 后）
    result TEXT NOT NULL,          -- ArtistCredit 的 JSON
    model TEXT NOT NULL,
    prompt_version INTEGER NOT NULL,
    parsed_at INTEGER NOT NULL     -- 毫秒时间戳，取自 core::Clock
) STRICT;
