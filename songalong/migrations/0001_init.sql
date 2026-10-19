-- SongAlong schema. IF NOT EXISTS everywhere so a re-run is harmless.

CREATE TABLE IF NOT EXISTS users (
  id            TEXT PRIMARY KEY,
  email         TEXT NOT NULL UNIQUE COLLATE NOCASE,
  name          TEXT NOT NULL DEFAULT '',
  password_hash TEXT NOT NULL,             -- pbkdf2-sha256$<iterations>$<salt b64>$<hash b64>
  settings      TEXT NOT NULL DEFAULT '{}',
  created_at    INTEGER NOT NULL,
  password_changed_at INTEGER NOT NULL
);

-- Only a SHA-256 of the cookie token is stored, so a database leak gives no working sessions.
CREATE TABLE IF NOT EXISTS sessions (
  token_hash  TEXT PRIMARY KEY,
  user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at  INTEGER NOT NULL,
  expires_at  INTEGER NOT NULL,
  user_agent  TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS sessions_user ON sessions(user_id);

CREATE TABLE IF NOT EXISTS password_resets (
  token_hash  TEXT PRIMARY KEY,
  user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at  INTEGER NOT NULL,
  expires_at  INTEGER NOT NULL,
  used_at     INTEGER
);
CREATE INDEX IF NOT EXISTS password_resets_user ON password_resets(user_id);

-- Failed sign-ins and reset requests, counted per key (email or IP) in a sliding window.
CREATE TABLE IF NOT EXISTS rate_limits (
  key         TEXT PRIMARY KEY,
  window_start INTEGER NOT NULL,
  count       INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS songs (
  id          TEXT PRIMARY KEY,
  owner_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  title       TEXT NOT NULL,
  artist      TEXT NOT NULL DEFAULT '',
  genre       TEXT NOT NULL DEFAULT '',
  song_key    TEXT NOT NULL DEFAULT '',
  bpm         REAL NOT NULL DEFAULT 0,
  chords      TEXT NOT NULL DEFAULT '',    -- distinct chord names, space separated (search + "similar")
  is_public   INTEGER NOT NULL DEFAULT 0,
  data        TEXT NOT NULL,               -- the full song document (JSON)
  created_at  INTEGER NOT NULL,
  updated_at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS songs_owner ON songs(owner_id, updated_at DESC);
CREATE INDEX IF NOT EXISTS songs_public ON songs(is_public, updated_at DESC);

-- Songs a user saved (hearted). song_id may be a built-in demo id, so no foreign key.
CREATE TABLE IF NOT EXISTS favorites (
  user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  song_id     TEXT NOT NULL,
  created_at  INTEGER NOT NULL,
  PRIMARY KEY (user_id, song_id)
);

-- Per-user, per-song practice state: last settings used and how often it was played.
CREATE TABLE IF NOT EXISTS song_progress (
  user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  song_id     TEXT NOT NULL,
  settings    TEXT NOT NULL DEFAULT '{}',  -- transpose, capo, instrument, speed, loop
  play_count  INTEGER NOT NULL DEFAULT 0,
  last_played_at INTEGER NOT NULL,
  PRIMARY KEY (user_id, song_id)
);
CREATE INDEX IF NOT EXISTS song_progress_song ON song_progress(song_id);

CREATE TABLE IF NOT EXISTS playlists (
  id          TEXT PRIMARY KEY,
  user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name        TEXT NOT NULL,
  created_at  INTEGER NOT NULL,
  updated_at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS playlists_user ON playlists(user_id);

CREATE TABLE IF NOT EXISTS playlist_songs (
  playlist_id TEXT NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
  song_id     TEXT NOT NULL,
  position    INTEGER NOT NULL,
  added_at    INTEGER NOT NULL,
  PRIMARY KEY (playlist_id, song_id)
);

-- Uploaded audio files (stored in R2 under `key`).
CREATE TABLE IF NOT EXISTS audio_files (
  key         TEXT PRIMARY KEY,
  owner_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name        TEXT NOT NULL,
  content_type TEXT NOT NULL,
  size        INTEGER NOT NULL,
  created_at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS audio_owner ON audio_files(owner_id);
