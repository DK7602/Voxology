// Installing SongAlong as an app. Android (and desktop Chrome / Edge) offer a one-tap install
// prompt; iPhone and iPad install from the Share menu (Add to Home Screen). The service worker
// keeps the app's files so the installed app opens quickly.

import { useSyncExternalStore } from "react";

interface BeforeInstallPromptEvent extends Event {
  prompt(): Promise<void>;
  userChoice: Promise<{ outcome: "accepted" | "dismissed" }>;
}

let deferred: BeforeInstallPromptEvent | null = null;
let installedNow = false;
const listeners = new Set<() => void>();
const emit = () => listeners.forEach((l) => l());

/** Call once at start-up, before React renders (the browser may offer the prompt early). */
export function startInstallSupport() {
  window.addEventListener("beforeinstallprompt", (e) => {
    e.preventDefault(); // keep it for our own Install button
    deferred = e as BeforeInstallPromptEvent;
    emit();
  });
  window.addEventListener("appinstalled", () => {
    installedNow = true;
    deferred = null;
    emit();
  });
  if (import.meta.env.PROD && "serviceWorker" in navigator) {
    window.addEventListener("load", () => {
      navigator.serviceWorker.register("/sw.js").catch(() => {});
    });
  }
}

function standalone(): boolean {
  return window.matchMedia("(display-mode: standalone)").matches || (navigator as { standalone?: boolean }).standalone === true;
}

/** iPhone, iPod, or an iPad (which reports itself as a Mac with a touch screen). */
function apple(): boolean {
  return /iPad|iPhone|iPod/.test(navigator.userAgent) || (/Macintosh/.test(navigator.userAgent) && navigator.maxTouchPoints > 1);
}

/** installed: running as the app; prompt: one tap installs; ios: show the Share-menu steps;
 *  none: this browser can't install (or already offered and was turned down). */
export type InstallState = "installed" | "prompt" | "ios" | "none";

function snapshot(): InstallState {
  if (installedNow || standalone()) return "installed";
  if (deferred) return "prompt";
  if (apple()) return "ios";
  return "none";
}

function subscribe(fn: () => void) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

export function useInstall(): { state: InstallState; install: () => Promise<boolean> } {
  const state = useSyncExternalStore(subscribe, snapshot);
  const install = async () => {
    const d = deferred;
    if (!d) return false;
    await d.prompt();
    const choice = await d.userChoice;
    deferred = null; // a prompt can only be used once
    emit();
    return choice.outcome === "accepted";
  };
  return { state, install };
}
