-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0017_cover_checked: Track cover art check timestamp in mb_album_matches

ALTER TABLE mb_album_matches ADD COLUMN cover_checked_at INTEGER;
