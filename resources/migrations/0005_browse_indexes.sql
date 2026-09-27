-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0005_browse_indexes: Materialized track_sort table, sync triggers, and covering browse indexes

CREATE TABLE track_sort (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    visible INTEGER NOT NULL,
    album_id INTEGER,
    album_artist_key TEXT,
    album TEXT,
    disc_number INTEGER,
    track_number INTEGER,
    path TEXT,
    title TEXT,
    artist TEXT,
    genre TEXT,
    year INTEGER,
    duration_ms INTEGER,
    added_at INTEGER
) STRICT;

-- Synchronization triggers for track_sort
-- Sync mapping:
-- visible          <- files (missing_since)
-- album_id         <- tracks (album_id)
-- album_artist_key <- effective_metadata (album_artist, artist)
-- album            <- effective_metadata (album)
-- disc_number      <- effective_metadata (disc_number)
-- track_number     <- effective_metadata (track_number)
-- path             <- files (path)
-- title            <- effective_metadata (title)
-- artist           <- effective_metadata (artist)
-- genre            <- effective_metadata (genre)
-- year             <- effective_metadata (year)
-- duration_ms      <- files (duration_ms)
-- added_at         <- files (first_seen_at)
CREATE TRIGGER track_sort_after_effective_metadata_insert
AFTER INSERT ON effective_metadata
BEGIN
    INSERT OR REPLACE INTO track_sort (
        track_id, visible, album_id, album_artist_key, album, disc_number, track_number,
        path, title, artist, genre, year, duration_ms, added_at
    )
    SELECT
        NEW.track_id,
        (CASE WHEN f.missing_since IS NULL THEN 1 ELSE 0 END),
        t.album_id,
        COALESCE(NEW.album_artist, NEW.artist),
        NEW.album,
        NEW.disc_number,
        NEW.track_number,
        f.path,
        NEW.title,
        NEW.artist,
        NEW.genre,
        NEW.year,
        f.duration_ms,
        f.first_seen_at
    FROM tracks t
    JOIN files f ON t.file_id = f.id
    WHERE t.id = NEW.track_id;
END;

CREATE TRIGGER track_sort_after_effective_metadata_update
AFTER UPDATE ON effective_metadata
BEGIN
    INSERT OR REPLACE INTO track_sort (
        track_id, visible, album_id, album_artist_key, album, disc_number, track_number,
        path, title, artist, genre, year, duration_ms, added_at
    )
    SELECT
        NEW.track_id,
        (CASE WHEN f.missing_since IS NULL THEN 1 ELSE 0 END),
        t.album_id,
        COALESCE(NEW.album_artist, NEW.artist),
        NEW.album,
        NEW.disc_number,
        NEW.track_number,
        f.path,
        NEW.title,
        NEW.artist,
        NEW.genre,
        NEW.year,
        f.duration_ms,
        f.first_seen_at
    FROM tracks t
    JOIN files f ON t.file_id = f.id
    WHERE t.id = NEW.track_id;
END;

CREATE TRIGGER track_sort_after_files_update
AFTER UPDATE OF missing_since, duration_ms, first_seen_at, path ON files
BEGIN
    UPDATE track_sort
    SET visible = (CASE WHEN NEW.missing_since IS NULL THEN 1 ELSE 0 END),
        duration_ms = NEW.duration_ms,
        added_at = NEW.first_seen_at,
        path = NEW.path
    WHERE track_id IN (SELECT id FROM tracks WHERE file_id = NEW.id);
END;

CREATE TRIGGER track_sort_after_tracks_update_album_id
AFTER UPDATE OF album_id ON tracks
BEGIN
    UPDATE track_sort
    SET album_id = NEW.album_id
    WHERE track_id = NEW.id;
END;

-- Full initial population for pre-existing tracks
INSERT OR REPLACE INTO track_sort (
    track_id, visible, album_id, album_artist_key, album, disc_number, track_number,
    path, title, artist, genre, year, duration_ms, added_at
)
SELECT
    t.id,
    (CASE WHEN f.missing_since IS NULL THEN 1 ELSE 0 END),
    t.album_id,
    COALESCE(em.album_artist, em.artist),
    em.album,
    em.disc_number,
    em.track_number,
    f.path,
    em.title,
    em.artist,
    em.genre,
    em.year,
    f.duration_ms,
    f.first_seen_at
FROM tracks t
JOIN files f ON t.file_id = f.id
LEFT JOIN effective_metadata em ON t.id = em.track_id;

-- Partial covering indexes for track sorting (WHERE visible = 1)
-- 1. Default Sort (album_artist_key, album, disc_number, track_number, path, track_id)
CREATE INDEX idx_track_sort_default_asc ON track_sort(
    album_artist_key IS NULL, album_artist_key,
    album IS NULL, album,
    disc_number IS NULL, disc_number,
    track_number IS NULL, track_number,
    path, track_id
) WHERE visible = 1;

CREATE INDEX idx_track_sort_default_desc ON track_sort(
    album_artist_key, album, disc_number, track_number, path, track_id
) WHERE visible = 1;

-- 2. Title Sort
CREATE INDEX idx_track_sort_title_asc ON track_sort(
    title IS NULL, title, track_id
) WHERE visible = 1;

CREATE INDEX idx_track_sort_title_desc ON track_sort(
    title, track_id
) WHERE visible = 1;

-- 3. Artist Sort
CREATE INDEX idx_track_sort_artist_asc ON track_sort(
    artist IS NULL, artist, track_id
) WHERE visible = 1;

CREATE INDEX idx_track_sort_artist_desc ON track_sort(
    artist, track_id
) WHERE visible = 1;

-- 4. Album Sort
CREATE INDEX idx_track_sort_album_asc ON track_sort(
    album IS NULL, album,
    disc_number IS NULL, disc_number,
    track_number IS NULL, track_number,
    track_id
) WHERE visible = 1;

CREATE INDEX idx_track_sort_album_desc ON track_sort(
    album, disc_number, track_number, track_id
) WHERE visible = 1;

-- 5. Year Sort
CREATE INDEX idx_track_sort_year_asc ON track_sort(
    year IS NULL, year,
    album IS NULL, album,
    disc_number IS NULL, disc_number,
    track_number IS NULL, track_number,
    track_id
) WHERE visible = 1;

CREATE INDEX idx_track_sort_year_desc ON track_sort(
    year, album, disc_number, track_number, track_id
) WHERE visible = 1;

-- 6. Duration Sort
CREATE INDEX idx_track_sort_duration_asc ON track_sort(
    duration_ms IS NULL, duration_ms, track_id
) WHERE visible = 1;

CREATE INDEX idx_track_sort_duration_desc ON track_sort(
    duration_ms, track_id
) WHERE visible = 1;

-- 7. DateAdded Sort
CREATE INDEX idx_track_sort_added_at_asc ON track_sort(
    added_at IS NULL, added_at, track_id
) WHERE visible = 1;

CREATE INDEX idx_track_sort_added_at_desc ON track_sort(
    added_at, track_id
) WHERE visible = 1;

-- Filter / helper indexes on track_sort
CREATE INDEX idx_track_sort_album_id ON track_sort(album_id, duration_ms) WHERE visible = 1;
CREATE INDEX idx_track_sort_genre ON track_sort(genre, track_id) WHERE visible = 1;

-- Indexes for Albums browsing
CREATE INDEX idx_albums_title_asc ON albums(title IS NULL, title, id);
CREATE INDEX idx_albums_title_desc ON albums(title, id);
CREATE INDEX idx_albums_artist_asc ON albums(album_artist IS NULL, album_artist, id);
CREATE INDEX idx_albums_artist_desc ON albums(album_artist, id);
CREATE INDEX idx_albums_year_asc ON albums(year IS NULL, year, id);
CREATE INDEX idx_albums_year_desc ON albums(year, id);
CREATE INDEX idx_albums_created_at_asc ON albums(created_at IS NULL, created_at, id);
CREATE INDEX idx_albums_created_at_desc ON albums(created_at, id);

-- Indexes for Artists browsing
CREATE INDEX idx_artists_name_asc ON artists(name IS NULL, name, id);
CREATE INDEX idx_artists_name_desc ON artists(name, id);

-- Junction table indexes
CREATE INDEX idx_track_artists_artist_role_track ON track_artists(artist_id, role, track_id);
CREATE INDEX idx_album_artists_artist_album ON album_artists(artist_id, album_id);

