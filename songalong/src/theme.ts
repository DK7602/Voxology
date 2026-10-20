import type { UserSettings } from "../shared/types";

let media: MediaQueryList | null = null;
let listener: (() => void) | null = null;

/** The colour schemes, as stored in settings. */
export const THEMES = ["light", "dark", "usyay", "system"] as const;
type Theme = (typeof THEMES)[number];

/** A stored theme name, if it is one (else "system"). */
export function themeOf(raw: string | null | undefined): Theme {
  return THEMES.includes(raw as Theme) ? (raw as Theme) : "system";
}

/** the browser bar's colour for each scheme */
const BAR_COLOR: Record<string, string> = { light: "#c9a13a", dark: "#0a2016", usyay: "#1a5fd0" };

/** Sets data-theme on <html>; "system" follows the device and keeps following it. */
export function applyTheme(theme: UserSettings["theme"] = "system") {
  const root = document.documentElement;
  if (media && listener) media.removeEventListener("change", listener);
  media = window.matchMedia("(prefers-color-scheme: dark)");
  const set = () => {
    const t = theme === "system" ? (media!.matches ? "dark" : "light") : theme;
    root.dataset.theme = t;
    document.querySelector('meta[name="theme-color"]')?.setAttribute("content", BAR_COLOR[t]);
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
