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
- **Upload a recording** (MP3/WAV/M4A). The browser finds the beat, key and chords automatically.
- **YouTube video**. The video plays along with a chord sheet you paste, and you tap-sync the timing.
- **Backing track only**. Paste a chord sheet and SongAlong strums the chords for you.

Lyrics come from what you paste or type. Nothing is fetched from lyric sites, because song lyrics are licensed.

## Develop

```
npm install
npm run db:migrate:local      # local D1 (once)
npm run dev                   # http://localhost:5173
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

### Password-reset email (pick one)
1. **Cloudflare Email Service**. Needs Workers Paid and a domain on Cloudflare: onboard the domain under Email Service →
   Email Sending, uncomment `send_email` in `wrangler.jsonc`, and set `MAIL_FROM` to an address on that domain.
2. **Resend**: `npx wrangler secret put RESEND_API_KEY` and set `MAIL_FROM` to a sender verified in Resend.

Optional: set `APP_URL` (e.g. `https://songalong.example.com`) so reset links always use your domain.

## Textures

`scripts/make-textures.py` generates the liquid-gold and frosted-glass art in `public/textures/`
(`python3 scripts/make-textures.py`).
