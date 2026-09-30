-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0022_writebacks: writebacks and writeback_files tables for tag writeback and revert

CREATE TABLE writebacks (
    id INTEGER PRIMARY KEY,
    batch_id INTEGER REFERENCES correction_batches(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL,
    reverted_at INTEGER
) STRICT;

CREATE TABLE writeback_files (
    writeback_id INTEGER NOT NULL REFERENCES writebacks(id) ON DELETE CASCADE,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    path TEXT NOT NULL,
    fields TEXT NOT NULL,            -- 要写的字段 JSON：{"title": "...", "track_number": "3", ...}（持久化名照 EnumNames）
    snapshot TEXT,                   -- 写前 TagSnapshot JSON（写之前先存）
    status TEXT NOT NULL CHECK(status IN ('pending', 'written', 'failed', 'reverted', 'revert_failed', 'skipped')),
    error TEXT,
    written_size INTEGER,            -- 写后文件大小与 mtime（毫秒），撤销前用来判断文件是否被别人改过
    written_mtime INTEGER,
    PRIMARY KEY (writeback_id, file_id)
) STRICT;
