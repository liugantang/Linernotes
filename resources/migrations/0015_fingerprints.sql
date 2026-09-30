-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0015_fingerprints: Acoustic fingerprints storage

CREATE TABLE fingerprints (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    content_hash TEXT,          -- 计算时 files.content_hash 的值；与当前值不同即过期
    algorithm INTEGER,
    fingerprint BLOB,           -- 小端 uint32 序列；失败时为 NULL
    error TEXT,                 -- 失败原因；成功时为 NULL
    computed_at INTEGER NOT NULL
) STRICT;
