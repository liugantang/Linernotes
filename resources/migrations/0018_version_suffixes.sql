-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0018_version_suffixes: Cache table for unknown title version suffix classifications

CREATE TABLE version_suffixes (
    suffix_key TEXT PRIMARY KEY,      -- butler::suffixKey()
    role TEXT NOT NULL CHECK(role IN ('version', 'annotation', 'title_part')),
    version_type TEXT CHECK(version_type IN ('studio', 'live', 'remaster', 'acoustic', 'remix', 'demo', 'instrumental', 'edit', 'alternate')),
    confidence REAL NOT NULL CHECK(confidence BETWEEN 0.0 AND 1.0),
    reason TEXT,
    model TEXT NOT NULL,
    prompt_version INTEGER NOT NULL,
    classified_at INTEGER NOT NULL
) STRICT;
