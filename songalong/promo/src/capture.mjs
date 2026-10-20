// Takes every screenshot the promo pages use, from a running dev server (npm run dev), in the
// dark and light themes: node promo/src/capture.mjs   (SITE=… to use another address)
import { chromium } from "playwright-core";
import { mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
const base = process.env.SITE || "http://127.0.0.1:5173";
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || undefined });
const errors = [];
for (const theme of ["dark", "light"]) {
  const out = fileURLToPath(new URL(`./shots/${theme}`, import.meta.url));
  mkdirSync(out, { recursive: true });
  const email = "promo" + theme + Date.now() + "@example.com";

  // phone
  {
    const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 3, colorScheme: theme, isMobile: true, hasTouch: true });
    const page = await ctx.newPage();
    page.on("pageerror", (e) => errors.push(e.message));
    await page.request.post(base + "/api/auth/register", { data: { email, password: "correct horse 1", name: "Dave King" } });
    const openSong = async (id, bar) => {
      await page.goto(base + "/song/" + id);
      await page.waitForSelector(".cell");
      await page.waitForTimeout(800);
      if (bar) await page.locator(`.cell[title^='Bar ${bar} ']`).first().tap();
      await page.waitForTimeout(600);
    };
    await openSong("demo-sunday-strum", 5);
    await page.locator(".play-btn").first().tap();
    await page.waitForTimeout(4200);
    await page.locator(".play-btn").first().tap();
    await page.waitForTimeout(400);
    await page.screenshot({ path: out + "/song.png" });
    await openSong("demo-iron-sparks", 3);
    await page.locator(".part-seg button", { hasText: "Lead" }).tap();
    await page.waitForTimeout(800);
    await page.locator(".play-btn").first().tap();
    await page.waitForTimeout(2600);
    await page.locator(".play-btn").first().tap();
    await page.waitForTimeout(400);
    await page.screenshot({ path: out + "/lead.png" });
    await openSong("demo-porch-light", 2);
    await page.locator(".t-drop", { hasText: "Speed" }).first().tap();
    await page.waitForTimeout(500);
    await page.screenshot({ path: out + "/speed.png" });
    await page.goto(base + "/tools/tuner");
    await page.waitForTimeout(1200);
    await page.screenshot({ path: out + "/tuner.png" });
    await ctx.close();
  }
  // desktop: cards, capo menu, lead neck
  {
    const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 3, colorScheme: theme });
    const page = await ctx.newPage();
    page.on("pageerror", (e) => errors.push(e.message));
    await page.request.post(base + "/api/auth/login", { data: { email, password: "correct horse 1" } });
    await page.goto(base + "/song/demo-sunday-strum");
    await page.waitForSelector(".diagram-card");
    await page.waitForTimeout(800);
    await page.locator(".t-drop", { hasText: "Capo" }).first().click();
    await page.waitForTimeout(500);
    await page.locator(".popover").first().screenshot({ path: out + "/capo-menu.png" });
    await page.keyboard.press("Escape");
    await page.mouse.click(5, 5);
    await page.waitForTimeout(300);
    await page.addStyleTag({ content: ".diagram-card{height:292px!important;align-content:space-between!important;transform:none!important;box-shadow:none!important}" });
    for (const inst of ["Guitar", "Ukulele", "Piano", "Mandolin", "Bass"]) {
      await page.locator(".inst-row button", { hasText: inst }).first().click();
      await page.waitForTimeout(600);
      await page.locator(".diagram-card").first().screenshot({ path: `${out}/c-${inst.toLowerCase()}.png` });
    }
    await page.goto(base + "/song/demo-iron-sparks");
    await page.waitForSelector(".cell");
    await page.locator(".part-seg button", { hasText: "Lead" }).click();
    await page.waitForTimeout(600);
    await page.locator(".cell[title^='Bar 13 ']").first().click();
    await page.waitForTimeout(300);
    await page.locator(".play-btn").first().click();
    await page.waitForTimeout(900);
    await page.locator(".play-btn").first().click();
    await page.waitForTimeout(400);
    await page.locator(".lead-neck").first().screenshot({ path: out + "/neck.png" });
    await page.locator(".strip-wrap").first().screenshot({ path: out + "/leadstrip.png" });
    await ctx.close();
  }
}
console.log(errors.join("\n") || "no errors");
await browser.close();
