-- The order a player arranged their library in (song ids, JSON array).
CREATE TABLE IF NOT EXISTS library_order (
  user_id    TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
  song_ids   TEXT NOT NULL DEFAULT '[]',
  updated_at INTEGER NOT NULL
);
