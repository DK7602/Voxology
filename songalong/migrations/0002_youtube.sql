-- Songs prepared from YouTube (search -> prepare -> play), the PC helpers that prepare them when
-- the cloud can't, and a "system" owner for the shared catalog of prepared songs.

CREATE TABLE IF NOT EXISTS yt_songs (
  video_id     TEXT PRIMARY KEY,
  status       TEXT NOT NULL,              -- working | waiting_pc | ready | failed
  song_id      TEXT,                       -- songs.id once ready
  title        TEXT NOT NULL DEFAULT '',
  channel      TEXT NOT NULL DEFAULT '',
  duration     REAL NOT NULL DEFAULT 0,
  stage        TEXT NOT NULL DEFAULT '',
  progress     REAL NOT NULL DEFAULT 0,
  error        TEXT NOT NULL DEFAULT '',
  source       TEXT NOT NULL DEFAULT '',   -- cloud | pc
  requested_by TEXT,
  claimed_by   TEXT,                       -- helper token hash while a PC works on it
  claimed_at   INTEGER,
  attempts     INTEGER NOT NULL DEFAULT 0,
  created_at   INTEGER NOT NULL,
  updated_at   INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS yt_songs_status ON yt_songs(status, updated_at);

CREATE TABLE IF NOT EXISTS helper_tokens (
  token_hash   TEXT PRIMARY KEY,
  user_id      TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name         TEXT NOT NULL DEFAULT '',
  created_at   INTEGER NOT NULL,
  last_seen_at INTEGER
);
CREATE INDEX IF NOT EXISTS helper_tokens_user ON helper_tokens(user_id);

-- Owner of the shared catalog. '!' is not a valid password hash, so nobody can sign in as it.
INSERT OR IGNORE INTO users (id, email, name, password_hash, settings, created_at, password_changed_at)
VALUES ('system', 'catalog@songalong.invalid', 'SongAlong', '!', '{}', 0, 0);
