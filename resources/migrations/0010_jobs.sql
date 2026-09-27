-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0010_jobs: Batch job queue tables

CREATE TABLE jobs (
    id INTEGER PRIMARY KEY,
    kind TEXT NOT NULL,                    -- JobHandler::kind()
    title TEXT NOT NULL,                   -- 给用户看的名称
    params TEXT NOT NULL DEFAULT '{}',     -- JSON 对象，原样交给 handler
    state TEXT NOT NULL CHECK(state IN ('running', 'paused', 'completed', 'cancelled')),
    last_error TEXT,                       -- 暂停原因（Error::code + ": " + message）
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
) STRICT;

CREATE TABLE job_items (
    job_id INTEGER NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
    seq INTEGER NOT NULL,                  -- 入队顺序，从 0 开始
    item_key TEXT NOT NULL,
    state TEXT NOT NULL DEFAULT 'pending' CHECK(state IN ('pending', 'done', 'failed')),
    error_code TEXT,
    PRIMARY KEY (job_id, seq)
) STRICT;
CREATE INDEX idx_job_items_state ON job_items(job_id, state);
