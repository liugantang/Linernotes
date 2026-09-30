-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0023_translations: Cache table for translated titles and album names

CREATE TABLE text_translations (
    source_text TEXT NOT NULL,        -- 原文（trim 后）
    target_lang TEXT NOT NULL,        -- 'zh-Hans'
    translated TEXT NOT NULL,         -- 空串 = 不需要翻译（已是目标语言、纯专有名词/编号等）
    model TEXT NOT NULL,
    prompt_version INTEGER NOT NULL,
    translated_at INTEGER NOT NULL,
    PRIMARY KEY (source_text, target_lang)
) STRICT;
