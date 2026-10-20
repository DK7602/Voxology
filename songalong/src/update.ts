// Telling a running SongAlong that a new version is out: an open page (or the installed app,
// brought back from the background) keeps running the code it started with, so it looks at the
// live page now and then and offers a reload when that names other built files.

import { useEffect, useState } from "react";
import { buildOf } from "../shared/release";

function runningBuild(): string | null {
  const s = document.querySelector<HTMLScriptElement>('script[type="module"][src*="/assets/index-"]');
  return s ? buildOf(new URL(s.src).pathname) : null;
}

const EVERY_MS = 10 * 60_000;
/** not more often than this, however often the app comes back to the front */
const AT_MOST_MS = 60_000;

/** True once a newer version of SongAlong is live than the one running here. */
export function useNewVersion(): boolean {
  const [ready, setReady] = useState(false);
  useEffect(() => {
    if (!import.meta.env.PROD) return;
    const mine = runningBuild();
    if (!mine) return;
    let last = Date.now(); // the page was just loaded
    const check = async () => {
      if (document.visibilityState !== "visible" || Date.now() - last < AT_MOST_MS) return;
      last = Date.now();
      try {
        // no-store: past the browser's cache and the service worker's saved page
        const res = await fetch("/", { cache: "no-store" });
        const live = res.ok ? buildOf(await res.text()) : null;
        if (live && live !== mine) setReady(true);
      } catch {
        // offline: look again later
      }
    };
    const timer = setInterval(check, EVERY_MS);
    document.addEventListener("visibilitychange", check);
    return () => {
      clearInterval(timer);
      document.removeEventListener("visibilitychange", check);
    };
  }, []);
  return ready;
}
