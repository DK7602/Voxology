# SongAlong promotional images

Four designs, each in the app's dark and light looks, at 1080 × 1350 (the portrait size for
Instagram and Facebook posts):

| Design | Dark | Light |
| --- | --- | --- |
| Play along with any song | `songalong-1-play-along-dark.png` | `songalong-1-play-along-light.png` |
| Your instrument. Your key. | `songalong-2-instruments-dark.png` | `songalong-2-instruments-light.png` |
| Learn the riff, note by note | `songalong-3-lead-dark.png` | `songalong-3-lead-light.png` |
| Practice your way | `songalong-4-practice-dark.png` | `songalong-4-practice-light.png` |

Everything shown is a real screenshot of the app, using only the demo songs (original
compositions), so there are no copyrighted lyrics or album art.

## Making them again

The pages in `src/` lay the screenshots out; the fonts and backgrounds come from the app itself.

1. `npm install --no-save playwright-core` (once) and `npm run dev` in another terminal.
2. `node promo/src/capture.mjs` takes the screenshots (into `promo/src/shots/`, not committed).
3. `node promo/src/render.mjs` writes the PNGs here.

Set `CHROME_PATH` if Playwright has no browser of its own, and `SITE` to capture from somewhere
other than `http://127.0.0.1:5173`.
