// How much later than the player's clock the words and chords are shown, on this device: the
// sound can reach the ear a little after the player says it plays (Bluetooth speakers and
// headphones, some phones), and only the listener can hear by how much.

const KEY = "sa-sync";
export const SYNC_MIN = -0.5;
export const SYNC_MAX = 1;

export function clampSync(s: number): number {
  return Math.round(Math.max(SYNC_MIN, Math.min(SYNC_MAX, s)) * 100) / 100;
}

/** "0.15 s later", for the controls. */
export function syncLabel(s: number): string {
  return Math.abs(s) < 0.005 ? "As the player says" : `${Math.abs(s).toFixed(2)} s ${s > 0 ? "later" : "earlier"}`;
}

export function savedSync(): number {
  try {
    const v = Number(localStorage.getItem(KEY) ?? 0);
    return Number.isFinite(v) ? clampSync(v) : 0;
  } catch {
    return 0;
  }
}

export function saveSync(s: number) {
  try {
    localStorage.setItem(KEY, String(s));
  } catch {
    // not remembered (private window): it still applies until the page closes
  }
}
