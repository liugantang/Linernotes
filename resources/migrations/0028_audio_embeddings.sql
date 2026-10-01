-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0028_audio_embeddings: Table for audio neural embeddings and similarity search

DROP TABLE embeddings;

CREATE TABLE audio_embeddings (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    model TEXT NOT NULL,          -- 模型与切片方案标识，如 'msclap2023-3x7s'；与当前值不同即需重算
    content_hash TEXT,            -- 计算时 files.content_hash；与当前值不同即过期
    vector BLOB,                  -- 1024 个 float16，小端；失败时为 NULL
    error TEXT,                   -- 失败原因；成功时为 NULL
    computed_at INTEGER NOT NULL
) STRICT;
