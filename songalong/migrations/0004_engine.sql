-- Which version of the beat / chord detection prepared each song (see PREP_ENGINE).
ALTER TABLE yt_songs ADD COLUMN engine INTEGER NOT NULL DEFAULT 1;
