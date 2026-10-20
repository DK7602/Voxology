import type { UserSettings } from "../shared/types";

let media: MediaQueryList | null = null;
let listener: (() => void) | null = null;

/** Light or dark (or follow the device), and the color scheme, as stored in settings. */
export const MODES = ["light", "dark", "system"] as const;
export const PALETTES = ["gold", "usyay", "sunday"] as const;
export type Mode = (typeof MODES)[number];
export type Palette = (typeof PALETTES)[number];
export interface Look {
  mode: Mode;
  palette: Palette;
}

/** The look stored settings ask for. The theme "usyay" is the older way of saying USyay in light. */
export function lookOf(theme: string | null | undefined, palette: string | null | undefined): Look {
  const legacy = theme === "usyay";
  return {
    mode: legacy ? "light" : MODES.includes(theme as Mode) ? (theme as Mode) : "system",
    palette: PALETTES.includes(palette as Palette) ? (palette as Palette) : legacy ? "usyay" : "gold",
  };
}

/** the browser bar's color for each scheme, light and dark */
const BAR_COLOR: Record<Palette, { light: string; dark: string }> = {
  gold: { light: "#c9a13a", dark: "#0a2016" },
  usyay: { light: "#1a5fd0", dark: "#081433" },
  sunday: { light: "#6b1a2a", dark: "#2a0b12" },
};

/** Sets data-theme (light / dark) and data-palette on <html>; "system" follows the device and
 *  keeps following it. */
export function applyTheme({ mode, palette }: Look = { mode: "system", palette: "gold" }) {
  const root = document.documentElement;
  if (media && listener) media.removeEventListener("change", listener);
  media = window.matchMedia("(prefers-color-scheme: dark)");
  const set = () => {
    const t = mode === "system" ? (media!.matches ? "dark" : "light") : mode;
    root.dataset.theme = t;
    root.dataset.palette = palette;
    document.querySelector('meta[name="theme-color"]')?.setAttribute("content", BAR_COLOR[palette][t]);
  };
  set();
  if (mode === "system") {
    listener = set;
    media.addEventListener("change", listener);
  }
}

export function currentTheme(): "light" | "dark" {
  return document.documentElement.dataset.theme === "dark" ? "dark" : "light";
}

export function currentPalette(): Palette {
  return lookOf(null, document.documentElement.dataset.palette).palette;
}

/** The look saved on this device, for before the account's settings load. */
export function localLook(): Look {
  try {
    return lookOf(localStorage.getItem("sa-theme"), localStorage.getItem("sa-palette"));
  } catch {
    return lookOf(null, null);
  }
}

export function saveLocalLook(patch: Pick<UserSettings, "theme" | "palette">) {
  try {
    if (patch.theme) localStorage.setItem("sa-theme", patch.theme);
    if (patch.palette) localStorage.setItem("sa-palette", patch.palette);
  } catch {
    /* storage unavailable */
  }
}
