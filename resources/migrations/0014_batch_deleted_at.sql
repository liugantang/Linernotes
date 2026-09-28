-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0014_batch_deleted_at: Soft delete support for correction batches

ALTER TABLE correction_batches ADD COLUMN deleted_at INTEGER;
