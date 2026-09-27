-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0008_partial_missing_index: Recreate idx_files_missing_since as partial index

DROP INDEX idx_files_missing_since;
CREATE INDEX idx_files_missing_since ON files(missing_since) WHERE missing_since IS NOT NULL;
