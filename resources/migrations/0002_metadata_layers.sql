-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0002_metadata_layers: 3-layer metadata schema (raw_tags, corrections, user_overrides) and effective_metadata

CREATE TABLE correction_batches (
    id INTEGER PRIMARY KEY,
    source TEXT NOT NULL,
    description TEXT,
    created_at INTEGER NOT NULL,
    reverted_at INTEGER
) STRICT;

CREATE TABLE corrections (
    id INTEGER PRIMARY KEY,
    entity_type TEXT NOT NULL DEFAULT 'track' CHECK(entity_type IN ('track', 'album', 'artist')),
    entity_id INTEGER NOT NULL,
    field TEXT NOT NULL CHECK(entity_type != 'track' OR field IN ('title', 'artist', 'album', 'album_artist', 'genre', 'composer', 'year', 'track_number', 'track_total', 'disc_number', 'disc_total')),
    old_value TEXT,
    new_value TEXT,
    source TEXT NOT NULL,
    confidence REAL NOT NULL CHECK(confidence BETWEEN 0.0 AND 1.0),
    status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending', 'accepted', 'rejected', 'reverted')),
    reason TEXT,
    batch_id INTEGER REFERENCES correction_batches(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL,
    decided_at INTEGER
) STRICT;

CREATE INDEX idx_corrections_entity_field_status ON corrections(entity_type, entity_id, field, status);
CREATE INDEX idx_corrections_batch_id ON corrections(batch_id);
CREATE INDEX idx_corrections_status ON corrections(status);

CREATE TABLE user_overrides (
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    field TEXT NOT NULL CHECK(field IN ('title', 'artist', 'album', 'album_artist', 'genre', 'composer', 'year', 'track_number', 'track_total', 'disc_number', 'disc_total')),
    value TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    PRIMARY KEY(track_id, field)
) STRICT;

CREATE TABLE effective_metadata (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    title TEXT,
    artist TEXT,
    album TEXT,
    album_artist TEXT,
    genre TEXT,
    composer TEXT,
    year INTEGER,
    track_number INTEGER,
    track_total INTEGER,
    disc_number INTEGER,
    disc_total INTEGER,
    updated_at INTEGER NOT NULL
) STRICT;

CREATE INDEX idx_effective_metadata_album_disc_track ON effective_metadata(album, disc_number, track_number);
CREATE INDEX idx_effective_metadata_artist ON effective_metadata(artist);
CREATE INDEX idx_effective_metadata_album_artist ON effective_metadata(album_artist);
CREATE INDEX idx_effective_metadata_year ON effective_metadata(year);
CREATE INDEX idx_effective_metadata_genre ON effective_metadata(genre);

CREATE VIEW effective_metadata_view AS
SELECT
    track_id,
    title,
    artist,
    album,
    album_artist,
    genre,
    composer,
    CASE
        WHEN length(year_str) >= 4 AND substr(year_str, 1, 4) GLOB '[0-9][0-9][0-9][0-9]'
        THEN CAST(substr(year_str, 1, 4) AS INTEGER)
        ELSE NULL
    END AS year,
    CASE
        WHEN (
            CASE WHEN instr(track_num_str, '/') > 0 THEN substr(track_num_str, 1, instr(track_num_str, '/') - 1) ELSE track_num_str END
        ) IS NOT NULL
        AND trim(CASE WHEN instr(track_num_str, '/') > 0 THEN substr(track_num_str, 1, instr(track_num_str, '/') - 1) ELSE track_num_str END) != ''
        AND trim(CASE WHEN instr(track_num_str, '/') > 0 THEN substr(track_num_str, 1, instr(track_num_str, '/') - 1) ELSE track_num_str END) NOT GLOB '*[^0-9]*'
        THEN CAST(trim(CASE WHEN instr(track_num_str, '/') > 0 THEN substr(track_num_str, 1, instr(track_num_str, '/') - 1) ELSE track_num_str END) AS INTEGER)
        ELSE NULL
    END AS track_number,
    CASE
        WHEN track_total_str IS NOT NULL
        AND trim(track_total_str) != ''
        AND trim(track_total_str) NOT GLOB '*[^0-9]*'
        THEN CAST(trim(track_total_str) AS INTEGER)
        ELSE NULL
    END AS track_total,
    CASE
        WHEN (
            CASE WHEN instr(disc_num_str, '/') > 0 THEN substr(disc_num_str, 1, instr(disc_num_str, '/') - 1) ELSE disc_num_str END
        ) IS NOT NULL
        AND trim(CASE WHEN instr(disc_num_str, '/') > 0 THEN substr(disc_num_str, 1, instr(disc_num_str, '/') - 1) ELSE disc_num_str END) != ''
        AND trim(CASE WHEN instr(disc_num_str, '/') > 0 THEN substr(disc_num_str, 1, instr(disc_num_str, '/') - 1) ELSE disc_num_str END) NOT GLOB '*[^0-9]*'
        THEN CAST(trim(CASE WHEN instr(disc_num_str, '/') > 0 THEN substr(disc_num_str, 1, instr(disc_num_str, '/') - 1) ELSE disc_num_str END) AS INTEGER)
        ELSE NULL
    END AS disc_number,
    CASE
        WHEN disc_total_str IS NOT NULL
        AND trim(disc_total_str) != ''
        AND trim(disc_total_str) NOT GLOB '*[^0-9]*'
        THEN CAST(trim(disc_total_str) AS INTEGER)
        ELSE NULL
    END AS disc_total
FROM (
    SELECT
        t.id AS track_id,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'title')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'title')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'title' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'title' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'TITLE' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS title,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'artist')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'artist')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'artist' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'artist' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'ARTIST' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS artist,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'album')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'album')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'album' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'album' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'ALBUM' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS album,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'album_artist')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'album_artist')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'album_artist' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'album_artist' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'ALBUMARTIST' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS album_artist,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'genre')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'genre')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'genre' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'genre' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'GENRE' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS genre,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'composer')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'composer')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'composer' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'composer' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'COMPOSER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS composer,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'year')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'year')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'year' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'year' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'DATE' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS year_str,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'track_number')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'track_number')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'track_number' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'track_number' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'TRACKNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS track_num_str,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'track_total')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'track_total')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'track_total' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'track_total' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (
                CASE
                    WHEN (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'TRACKTOTAL' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)) IS NOT NULL
                    THEN (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'TRACKTOTAL' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
                    WHEN instr((SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'TRACKNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)), '/') > 0
                    THEN substr(
                        (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'TRACKNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)),
                        instr((SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'TRACKNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)), '/') + 1
                    )
                    ELSE NULL
                END
            )
        END AS track_total_str,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'disc_number')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'disc_number')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'disc_number' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'disc_number' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'DISCNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
        END AS disc_num_str,
        CASE
            WHEN EXISTS (SELECT 1 FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'disc_total')
            THEN (SELECT u.value FROM user_overrides u WHERE u.track_id = t.id AND u.field = 'disc_total')
            WHEN EXISTS (SELECT 1 FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'disc_total' AND c.status = 'accepted')
            THEN (SELECT c.new_value FROM corrections c WHERE c.entity_type = 'track' AND c.entity_id = t.id AND c.field = 'disc_total' AND c.status = 'accepted' ORDER BY c.decided_at DESC NULLS LAST, c.id DESC LIMIT 1)
            ELSE (
                CASE
                    WHEN (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'DISCTOTAL' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)) IS NOT NULL
                    THEN (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'DISCTOTAL' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1))
                    WHEN instr((SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'DISCNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)), '/') > 0
                    THEN substr(
                        (SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'DISCNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)),
                        instr((SELECT group_concat(r.value, ' / ' ORDER BY r.ordinal) FROM raw_tags r WHERE (r.track_id, r.key, r.priority, r.tag_type) = (SELECT r2.track_id, r2.key, r2.priority, r2.tag_type FROM raw_tags r2 WHERE r2.track_id = t.id AND r2.key = 'DISCNUMBER' ORDER BY r2.priority ASC, r2.tag_type ASC LIMIT 1)), '/') + 1
                    )
                    ELSE NULL
                END
            )
        END AS disc_total_str
    FROM tracks t
);

-- Triggers for effective_metadata synchronization
-- NOTE: The scanner writes all raw_tags for a track first, then updates tracks.tags_read_at.
-- Thus, refreshing triggers are attached to tracks(AFTER INSERT / AFTER UPDATE OF tags_read_at)
-- rather than raw_tags, avoiding repetitive recomputations per raw tag insertion.

CREATE TRIGGER effective_metadata_after_track_insert
AFTER INSERT ON tracks
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = NEW.id;
END;

CREATE TRIGGER effective_metadata_after_track_update_tags_read_at
AFTER UPDATE OF tags_read_at ON tracks
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = NEW.id;
END;

CREATE TRIGGER effective_metadata_after_correction_insert
AFTER INSERT ON corrections
WHEN NEW.entity_type = 'track'
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = NEW.entity_id;
END;

CREATE TRIGGER effective_metadata_after_correction_update
AFTER UPDATE ON corrections
WHEN NEW.entity_type = 'track' OR OLD.entity_type = 'track'
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = NEW.entity_id AND NEW.entity_type = 'track';

    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = OLD.entity_id AND OLD.entity_type = 'track' AND OLD.entity_id != NEW.entity_id;
END;

CREATE TRIGGER effective_metadata_after_correction_delete
AFTER DELETE ON corrections
WHEN OLD.entity_type = 'track'
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = OLD.entity_id;
END;

CREATE TRIGGER effective_metadata_after_override_insert
AFTER INSERT ON user_overrides
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = NEW.track_id;
END;

CREATE TRIGGER effective_metadata_after_override_update
AFTER UPDATE ON user_overrides
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = NEW.track_id;

    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = OLD.track_id AND OLD.track_id != NEW.track_id;
END;

CREATE TRIGGER effective_metadata_after_override_delete
AFTER DELETE ON user_overrides
BEGIN
    INSERT OR REPLACE INTO effective_metadata (
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total, updated_at
    )
    SELECT
        track_id, title, artist, album, album_artist, genre, composer,
        year, track_number, track_total, disc_number, disc_total,
        CAST(unixepoch('subsec') * 1000 AS INTEGER)
    FROM effective_metadata_view
    WHERE track_id = OLD.track_id;
END;

-- Populate effective_metadata for any pre-existing tracks
INSERT OR REPLACE INTO effective_metadata (
    track_id, title, artist, album, album_artist, genre, composer,
    year, track_number, track_total, disc_number, disc_total, updated_at
)
SELECT
    track_id, title, artist, album, album_artist, genre, composer,
    year, track_number, track_total, disc_number, disc_total,
    CAST(unixepoch('subsec') * 1000 AS INTEGER)
FROM effective_metadata_view;
