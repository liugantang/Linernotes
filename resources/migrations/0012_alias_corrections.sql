-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0012_alias_corrections: Add locale column to corrections table for artist alias corrections

ALTER TABLE corrections ADD COLUMN locale TEXT;
