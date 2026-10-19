import type { UserSettings } from "../shared/types";

let media: MediaQueryList | null = null;
let listener: (() => void) | null = null;

/** Sets data-theme on <html>; "system" follows the device and keeps following it. */
export function applyTheme(theme: UserSettings["theme"] = "system") {
  const root = document.documentElement;
  if (media && listener) media.removeEventListener("change", listener);
  media = window.matchMedia("(prefers-color-scheme: dark)");
  const set = () => {
    const t = theme === "system" ? (media!.matches ? "dark" : "light") : theme;
    root.dataset.theme = t;
    document.querySelector('meta[name="theme-color"]')?.setAttribute("content", t === "dark" ? "#0a2016" : "#c9a13a");
  };
  set();
  if (theme === "system") {
    listener = set;
    media.addEventListener("change", listener);
  }
}

export function currentTheme(): "light" | "dark" {
  return document.documentElement.dataset.theme === "dark" ? "dark" : "light";
}
