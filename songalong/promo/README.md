# SongAlong promotional images

Four designs, each in the app's dark and light looks, in three shapes:

- **Post**, 1080 × 1350 (Instagram and Facebook feed): `songalong-<design>-<dark|light>.png`
- **Square**, 1080 × 1080: `songalong-<design>-<dark|light>-square.png`
- **Story**, 1080 × 1920 (Instagram and Facebook stories, Reels covers): `songalong-<design>-<dark|light>-story.png`.
  The top and bottom 250 pixels or so hold nothing important, since the apps put their own
  controls there.

| Design | |
| --- | --- |
| `1-play-along` | Play along with any song |
| `2-instruments` | Your instrument. Your key. |
| `3-lead` | Learn the riff, note by note |
| `4-practice` | Practice your way |

Everything shown is a real screenshot of the app, using only the demo songs (original
compositions), so there are no copyrighted lyrics or album art.

## Making them again

The pages in `src/` lay the screenshots out; the fonts and backgrounds come from the app itself.

1. `npm install --no-save playwright-core` (once) and `npm run dev` in another terminal.
2. `node promo/src/capture.mjs` takes the screenshots (into `promo/src/shots/`, not committed).
3. `node promo/src/render.mjs` writes the PNGs here (or `node promo/src/render.mjs 3-lead` for one design).

Set `CHROME_PATH` if Playwright has no browser of its own, and `SITE` to capture from somewhere
other than `http://127.0.0.1:5173`.
