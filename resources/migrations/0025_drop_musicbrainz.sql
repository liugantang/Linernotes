-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0025_drop_musicbrainz: Drop MusicBrainz and Cover Art tables

DROP TABLE IF EXISTS mb_track_matches;
DROP TABLE IF EXISTS mb_album_matches;
DROP TABLE IF EXISTS mb_cache;
