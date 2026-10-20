-- Songs a player took off a list: their Home "Keep playing" row (until they play it again) or
-- Discover.
CREATE TABLE IF NOT EXISTS hidden_songs (
  user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  song_id    TEXT NOT NULL,
  list       TEXT NOT NULL,   -- 'recent' or 'discover'
  hidden_at  INTEGER NOT NULL,
  PRIMARY KEY (user_id, song_id, list)
);
