// Renders the promo pages (after capture.mjs) to promo/*.png, each in the dark and light themes:
// node promo/src/render.mjs
import { chromium } from "playwright-core";
import { fileURLToPath } from "node:url";
const pages = ["1-play-along", "2-instruments", "3-lead", "4-practice"];
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || undefined });
const page = await browser.newPage({ viewport: { width: 1080, height: 1350 } });
for (const name of pages) {
  for (const theme of ["dark", "light"]) {
    await page.goto(new URL(`./${name}.html?t=${theme}`, import.meta.url).href);
    await page.evaluate(() => Promise.all([document.fonts.ready, ...[...document.images].map((i) => i.decode().catch(() => {}))]));
    await page.waitForTimeout(200);
    await page.screenshot({ path: fileURLToPath(new URL(`../songalong-${name}-${theme}.png`, import.meta.url)) });
    console.log("rendered", name, theme);
  }
}
await browser.close();
