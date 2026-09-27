-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0009_llm_usage: Track LLM token usage per request and cached hits

CREATE TABLE llm_usage (
    id INTEGER PRIMARY KEY,
    created_at INTEGER NOT NULL,           -- Unix 毫秒
    purpose TEXT NOT NULL,                 -- purposeName()
    service_id TEXT NOT NULL,
    model TEXT NOT NULL,
    prompt_tokens INTEGER NOT NULL DEFAULT 0,
    completion_tokens INTEGER NOT NULL DEFAULT 0,
    cached INTEGER NOT NULL DEFAULT 0 CHECK(cached IN (0, 1)),  -- 缓存命中（未发请求）
    ok INTEGER NOT NULL CHECK(ok IN (0, 1)),
    error_code TEXT,                       -- 失败时的 Error::code
    elapsed_ms INTEGER NOT NULL DEFAULT 0
) STRICT;
CREATE INDEX idx_llm_usage_created_at ON llm_usage(created_at);
