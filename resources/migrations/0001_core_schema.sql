-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0001_core_schema: Core database tables, polymorphic favorites triggers, and indexes

CREATE TABLE covers (
    id INTEGER PRIMARY KEY,
    hash TEXT NOT NULL UNIQUE,
    mime TEXT NOT NULL,
    width INTEGER,
    height INTEGER,
    source TEXT NOT NULL CHECK(source IN ('embedded', 'folder', 'online')),
    source_path TEXT,
    created_at INTEGER NOT NULL
) STRICT;

CREATE TABLE library_roots (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    excludes TEXT NOT NULL DEFAULT '[]',
    added_at INTEGER NOT NULL
) STRICT;

CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    root_id INTEGER NOT NULL REFERENCES library_roots(id) ON DELETE CASCADE,
    path TEXT NOT NULL UNIQUE,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    content_hash TEXT,
    container TEXT,
    codec TEXT,
    duration_ms INTEGER,
    bitrate INTEGER,
    sample_rate INTEGER,
    bit_depth INTEGER,
    channels INTEGER,
    has_embedded_cover INTEGER NOT NULL DEFAULT 0 CHECK(has_embedded_cover IN (0, 1)),
    cover_id INTEGER REFERENCES covers(id) ON DELETE SET NULL,
    scan_error TEXT,
    first_seen_at INTEGER NOT NULL,
    scanned_at INTEGER NOT NULL,
    missing_since INTEGER
) STRICT;

CREATE INDEX idx_files_root_id ON files(root_id);
CREATE INDEX idx_files_cover_id ON files(cover_id);
CREATE INDEX idx_files_content_hash ON files(content_hash);
CREATE INDEX idx_files_missing_since ON files(missing_since);

CREATE TABLE albums (
    id INTEGER PRIMARY KEY,
    title TEXT NOT NULL,
    album_artist TEXT,
    year INTEGER,
    mbid TEXT,
    cover_id INTEGER REFERENCES covers(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL
) STRICT;

CREATE INDEX idx_albums_cover_id ON albums(cover_id);
CREATE INDEX idx_albums_title_artist ON albums(title, album_artist);

CREATE TABLE works (
    id INTEGER PRIMARY KEY,
    title TEXT NOT NULL,
    created_at INTEGER NOT NULL
) STRICT;

CREATE TABLE tracks (
    id INTEGER PRIMARY KEY,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    cue_index INTEGER,
    start_ms INTEGER,
    end_ms INTEGER,
    album_id INTEGER REFERENCES albums(id) ON DELETE SET NULL,
    work_id INTEGER REFERENCES works(id) ON DELETE SET NULL,
    tags_read_at INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    UNIQUE(file_id, cue_index)
) STRICT;

CREATE UNIQUE INDEX idx_tracks_file_id_unique ON tracks(file_id) WHERE cue_index IS NULL;
CREATE INDEX idx_tracks_file_id ON tracks(file_id);
CREATE INDEX idx_tracks_album_id ON tracks(album_id);
CREATE INDEX idx_tracks_work_id ON tracks(work_id);

CREATE TABLE raw_tags (
    id INTEGER PRIMARY KEY,
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    tag_type TEXT NOT NULL CHECK(tag_type IN ('id3v1', 'id3v2', 'xiph', 'ape', 'mp4', 'asf', 'riff', 'cue', 'other')),
    priority INTEGER NOT NULL,
    key TEXT NOT NULL,
    ordinal INTEGER NOT NULL DEFAULT 0,
    value TEXT NOT NULL,
    raw_bytes BLOB,
    raw_encoding TEXT,
    UNIQUE(track_id, tag_type, key, ordinal)
) STRICT;

CREATE INDEX idx_raw_tags_track_id ON raw_tags(track_id);
CREATE INDEX idx_raw_tags_track_key ON raw_tags(track_id, key, priority, tag_type, ordinal);

CREATE TABLE artists (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    sort_name TEXT,
    mbid TEXT,
    created_at INTEGER NOT NULL
) STRICT;

CREATE INDEX idx_artists_name ON artists(name);

CREATE TABLE artist_aliases (
    id INTEGER PRIMARY KEY,
    artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE,
    alias TEXT NOT NULL,
    locale TEXT,
    kind TEXT NOT NULL CHECK(kind IN ('original', 'variant', 'translation', 'transliteration', 'romanization')),
    source TEXT NOT NULL CHECK(source IN ('tag', 'user', 'rule', 'llm', 'musicbrainz')),
    created_at INTEGER NOT NULL,
    UNIQUE(artist_id, alias)
) STRICT;

CREATE INDEX idx_artist_aliases_artist_id ON artist_aliases(artist_id);

CREATE TABLE track_artists (
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK(role IN ('artist', 'featured', 'composer', 'lyricist', 'arranger', 'producer', 'remixer', 'performer', 'conductor')),
    position INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(track_id, artist_id, role)
) STRICT;

CREATE INDEX idx_track_artists_artist_id ON track_artists(artist_id);

CREATE TABLE album_artists (
    album_id INTEGER NOT NULL REFERENCES albums(id) ON DELETE CASCADE,
    artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE,
    position INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(album_id, artist_id)
) STRICT;

CREATE INDEX idx_album_artists_artist_id ON album_artists(artist_id);

CREATE TABLE playlists (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    kind TEXT NOT NULL CHECK(kind IN ('manual', 'smart')),
    rule TEXT CHECK(kind != 'smart' OR rule IS NOT NULL),
    position INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
) STRICT;

CREATE TABLE playlist_items (
    id INTEGER PRIMARY KEY,
    playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
    track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
    position INTEGER NOT NULL,
    added_at INTEGER NOT NULL
) STRICT;

CREATE INDEX idx_playlist_items_playlist_pos ON playlist_items(playlist_id, position);
CREATE INDEX idx_playlist_items_track_id ON playlist_items(track_id);

CREATE TABLE favorites (
    entity_type TEXT NOT NULL CHECK(entity_type IN ('track', 'album', 'artist')),
    entity_id INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY(entity_type, entity_id)
) STRICT;

CREATE TRIGGER favorites_cleanup_track AFTER DELETE ON tracks
BEGIN
    DELETE FROM favorites WHERE entity_type = 'track' AND entity_id = OLD.id;
END;

CREATE TRIGGER favorites_cleanup_album AFTER DELETE ON albums
BEGIN
    DELETE FROM favorites WHERE entity_type = 'album' AND entity_id = OLD.id;
END;

CREATE TRIGGER favorites_cleanup_artist AFTER DELETE ON artists
BEGIN
    DELETE FROM favorites WHERE entity_type = 'artist' AND entity_id = OLD.id;
END;

CREATE TABLE ratings (
    track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE,
    rating INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 5),
    updated_at INTEGER NOT NULL
) STRICT;
