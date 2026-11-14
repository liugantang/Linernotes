-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0031_knowledge: Tables for knowledge lookup and entry caching

CREATE TABLE knowledge_lookups (
    key TEXT PRIMARY KEY,
    qid TEXT,                 -- NULL = 查无结果
    fetched_at INTEGER NOT NULL
) STRICT;

CREATE TABLE knowledge_entries (
    qid TEXT PRIMARY KEY,
    kind TEXT NOT NULL CHECK(kind IN ('artist', 'song', 'album')),
    payload TEXT NOT NULL,    -- knowledgeEntryToJson
    fetched_at INTEGER NOT NULL
) STRICT;
