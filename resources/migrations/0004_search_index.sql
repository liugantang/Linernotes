-- SPDX-License-Identifier: GPL-3.0-or-later
-- SPDX-FileCopyrightText: 2026 Linernotes contributors
-- 0004_search_index: FTS5 full-text search index and dirty track tracking triggers

CREATE VIRTUAL TABLE search_index USING fts5(
    title, artist, album, album_artist, composer, aliases,
    romanized,
    tokenize = 'unicode61 remove_diacritics 2'
);

CREATE TABLE search_dirty (track_id INTEGER PRIMARY KEY) STRICT;

-- Triggers for effective_metadata synchronization
CREATE TRIGGER search_dirty_after_effective_metadata_insert
AFTER INSERT ON effective_metadata
BEGIN
    INSERT OR IGNORE INTO search_dirty(track_id) VALUES (NEW.track_id);
END;

CREATE TRIGGER search_dirty_after_effective_metadata_update
AFTER UPDATE ON effective_metadata
BEGIN
    INSERT OR IGNORE INTO search_dirty(track_id) VALUES (NEW.track_id);
END;

CREATE TRIGGER search_dirty_after_effective_metadata_delete
AFTER DELETE ON effective_metadata
BEGIN
    DELETE FROM search_index WHERE rowid = OLD.track_id;
    DELETE FROM search_dirty WHERE track_id = OLD.track_id;
END;

-- Triggers for artist_aliases synchronization
CREATE TRIGGER search_dirty_after_artist_aliases_insert
AFTER INSERT ON artist_aliases
BEGIN
    INSERT OR IGNORE INTO search_dirty(track_id)
    SELECT track_id FROM track_artists WHERE artist_id = NEW.artist_id;
END;

CREATE TRIGGER search_dirty_after_artist_aliases_update
AFTER UPDATE ON artist_aliases
BEGIN
    INSERT OR IGNORE INTO search_dirty(track_id)
    SELECT track_id FROM track_artists WHERE artist_id = NEW.artist_id;
    INSERT OR IGNORE INTO search_dirty(track_id)
    SELECT track_id FROM track_artists WHERE artist_id = OLD.artist_id;
END;

-- Triggers for artist_aliases deletion
CREATE TRIGGER search_dirty_after_artist_aliases_delete
AFTER DELETE ON artist_aliases
BEGIN
    INSERT OR IGNORE INTO search_dirty(track_id)
    SELECT track_id FROM track_artists WHERE artist_id = OLD.artist_id;
END;

-- Mark all existing tracks as dirty on migration
INSERT OR IGNORE INTO search_dirty(track_id)
SELECT id FROM tracks;
