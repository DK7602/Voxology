# SongAlong

A play-along web app in the spirit of Chordify. It works on PC, tablet and phone. Chords and lyrics move in time
with the music. You can play on guitar, ukulele, piano or mandolin, with capo and transpose. Diagrams show how to play
each chord and what type it is (single note, riff, power chord, triad, sus, 7th, extended). You can save songs and
playlists, and export chord sheets for print or PDF, plus MIDI, ChordPro and text. Sign-in is by email and password,
with a password-reset email.

## How it runs

One **Cloudflare Worker** serves both the React app (static assets) and the API under `/api`:

| Piece | What |
|---|---|
| `src/` | React app (Vite). `song/` = the play-along screen, `player/` = timing engine + synth, `analysis/` = chord & beat detection (runs in the browser, in a Web Worker) |
| `worker/` | API (Hono): auth, songs, favourites, playlists, practice settings, audio upload |
| `shared/` | Used by both: song document types, the music engine (chords, voicings, capo, transpose), ChordPro import / export, MIDI |
| `migrations/` | D1 database schema |
| D1 `songalong-db` | Accounts, sessions, songs, playlists |
| R2 `songalong-audio` | Recordings users upload (private to the uploader) |

Where songs come from:
- **Search any song.** Pick a result and SongAlong prepares it in about 20–45 seconds. It fetches the
  audio, finds the beat, key and chords, looks up timed lyrics (LRCLIB) and lines them up with the
  video using speech recognition. Then it plays inside SongAlong. Prepared songs are saved to a
  shared catalog, so they open instantly next time, for everyone.
  - The audio work runs in a **Cloudflare container** (`prep/`: yt-dlp + ffmpeg + the chord
    detector). YouTube sometimes refuses cloud servers; those songs go to the user's **PC helper**
    (Settings → Your PC helper), which runs the same code at home and reuses Sample Snagger's tools.
  - Only chords, timing and lyrics are kept; the audio is deleted after analysis.
- **Upload a recording** (MP3/WAV/M4A): the same detection runs in the browser.
- **Paste a chord sheet** (chords over lyrics or ChordPro), with or without a YouTube video.

## Develop

```
npm install
npm run db:migrate:local      # local D1 (once)
npm run dev                   # http://localhost:5173 (no container / AI: songs queue for the PC helper)
SONGALONG_FULL_DEV=1 npm run dev   # with the container (needs Docker) and Workers AI (needs `wrangler login`)
npm test                      # music engine, chord detection, API tests
```
In local dev, password-reset links are printed to the terminal instead of being emailed.

## Deploy

```
npm run db:migrate:remote     # apply migrations to the real D1 database
npm run deploy                # build + wrangler deploy
```
Or connect the repo in the Cloudflare dashboard (Workers → Create → Import a repository). Set **root directory**
`songalong`, **build command** `npm run build`, and **deploy command** `npx wrangler deploy`.

### Search any song
- Needs **Workers Paid** ($5/month) for the song-prep container and Workers AI (speech recognition).
  `npm run deploy` builds the container image with Docker (or deploy through Workers Builds).
- Optional: `npx wrangler secret put YOUTUBE_API_KEY` (free key from Google Cloud: YouTube Data
  API v3) makes search use YouTube's official search; without it the container searches.
- Lyrics: LRCLIB (free, unlicensed). Switch to Musixmatch (licensed) before opening the app to the
  public: replace `findLyrics()` in `worker/lyrics.ts`.

### Password-reset email (pick one)
1. **Cloudflare Email Service**. Needs Workers Paid and a domain on Cloudflare: onboard the domain under Email Service →
   Email Sending, uncomment `send_email` in `wrangler.jsonc`, and set `MAIL_FROM` to an address on that domain.
2. **Resend**: `npx wrangler secret put RESEND_API_KEY` and set `MAIL_FROM` to a sender verified in Resend.

Optional: set `APP_URL` (e.g. `https://songalong.example.com`) so reset links always use your domain.

## Look

Backgrounds are the smooth glass images in `public/bg/` (gold page; white / cream panels in light
mode, black in dark mode; green for active controls). Swap them there, or change the `--bg-image`,
`--panel`, `--tile` and `--green-glass` tokens in `src/styles/theme.css`. The search bar and the
chord / lyric timeline use plain colours (`--plain*`) on purpose.
