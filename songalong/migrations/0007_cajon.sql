-- Cajon parts: once a song is ready, the cajon container finds its drums in the background
-- (worker/cajon.ts). The jobs wait here, one per prepared YouTube song.
ALTER TABLE yt_songs ADD COLUMN cajon TEXT;                        -- NULL (not asked yet), queued, working, waiting (YouTube said no; tries later), ready, failed
ALTER TABLE yt_songs ADD COLUMN cajon_at INTEGER;                  -- when the job is due, or started
ALTER TABLE yt_songs ADD COLUMN cajon_tries INTEGER NOT NULL DEFAULT 0;
ALTER TABLE yt_songs ADD COLUMN cajon_error TEXT NOT NULL DEFAULT '';
ALTER TABLE yt_songs ADD COLUMN cajon_engine INTEGER NOT NULL DEFAULT 0;
CREATE INDEX IF NOT EXISTS yt_songs_cajon ON yt_songs(cajon, cajon_at);
