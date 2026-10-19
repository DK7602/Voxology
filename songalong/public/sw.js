// SongAlong service worker: keeps the app's own files so it opens fast (and opens at all when
// offline). The API is never cached: songs, sign-in and uploads always come from the server.

const CACHE = "songalong-v1";
const SHELL = ["/", "/manifest.webmanifest", "/favicon.png", "/brand/emblem.webp", "/brand/wordmark.webp"];
const MAX_ASSETS = 80;

/** Saves the page and the built files it names (scripts, styles), so it can open offline. */
async function saveShell(cache, html) {
  const files = new Set(html.match(/\/assets\/[^"'\s)]+/g) ?? []);
  const missing = [];
  for (const f of files) if (!(await cache.match(f))) missing.push(f);
  await cache.addAll(missing);
}

self.addEventListener("install", (event) => {
  event.waitUntil(
    (async () => {
      const cache = await caches.open(CACHE);
      await cache.addAll(SHELL);
      const page = await cache.match("/");
      if (page) await saveShell(cache, await page.text());
      await self.skipWaiting();
    })(),
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

/** Keeps the cache from growing forever: built files change name with every release. */
async function trim(cache) {
  const keys = (await cache.keys()).filter((r) => new URL(r.url).pathname.startsWith("/assets/"));
  for (let i = 0; i < keys.length - MAX_ASSETS; i++) await cache.delete(keys[i]);
}

self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.method !== "GET") return;
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;
  // never: the API, the PC helper download, the service worker itself
  if (url.pathname.startsWith("/api/") || url.pathname.startsWith("/helper/") || url.pathname === "/sw.js") return;

  // pages: the network first (new releases show at once), the saved copy when offline
  if (req.mode === "navigate") {
    event.respondWith(
      fetch(req)
        .then((res) => {
          if (res.ok) {
            const copy = res.clone();
            // keep the newest page, and the built files of a new release, for next time
            event.waitUntil(
              caches.open(CACHE).then(async (cache) => {
                const html = await copy.clone().text();
                await cache.put("/", copy);
                await saveShell(cache, html);
              }),
            );
          }
          return res;
        })
        .catch(() => caches.match("/").then((hit) => hit || Response.error())),
    );
    return;
  }

  // built files carry a content hash in their name, so a saved copy is always right
  if (url.pathname.startsWith("/assets/")) {
    event.respondWith(
      caches.match(req).then(
        (hit) =>
          hit ||
          fetch(req).then((res) => {
            if (res.ok) {
              const copy = res.clone();
              void caches.open(CACHE).then((cache) => cache.put(req, copy).then(() => trim(cache)));
            }
            return res;
          }),
      ),
    );
    return;
  }

  // images, icons and the like: the saved copy at once, refreshed in the background
  event.respondWith(
    caches.open(CACHE).then((cache) =>
      cache.match(req).then((hit) => {
        const fresh = fetch(req)
          .then((res) => {
            if (res.ok) void cache.put(req, res.clone());
            return res;
          })
          .catch(() => hit || Response.error());
        return hit || fresh;
      }),
    ),
  );
});
