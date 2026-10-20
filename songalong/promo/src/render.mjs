// Renders the promo pages (after capture.mjs) to promo/*.png: each design in the dark and light
// themes, as a post (1080 x 1350), a square (1080 x 1080) and a story (1080 x 1920).
// node promo/src/render.mjs [design ...]   (all four when none are named)
import { chromium } from "playwright-core";
import { fileURLToPath } from "node:url";
const all = ["1-play-along", "2-instruments", "3-lead", "4-practice"];
const pages = process.argv.length > 2 ? process.argv.slice(2) : all;
const formats = { post: 1350, square: 1080, story: 1920 };
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || undefined });
for (const [format, height] of Object.entries(formats)) {
  const page = await browser.newPage({ viewport: { width: 1080, height } });
  for (const name of pages) {
    for (const theme of ["dark", "light"]) {
      await page.goto(new URL(`./${name}.html?t=${theme}&f=${format}`, import.meta.url).href);
      await page.evaluate(() => Promise.all([document.fonts.ready, ...[...document.images].map((i) => i.decode().catch(() => {}))]));
      await page.waitForTimeout(200);
      const suffix = format === "post" ? "" : `-${format}`;
      await page.screenshot({ path: fileURLToPath(new URL(`../songalong-${name}-${theme}${suffix}.png`, import.meta.url)) });
      console.log("rendered", name, theme, format);
    }
  }
  await page.close();
}
await browser.close();
