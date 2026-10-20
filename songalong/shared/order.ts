// Custom orders that can be changed from a filtered view (the Library's Saved or Made by me tabs).

/**
 * `full` with the songs in `moved` put in that order, each taking one of the places those songs
 * already held, so reordering a filtered view leaves everything else where it was. Ids that are not
 * in `full` are ignored.
 */
export function mergeOrder(full: readonly string[], moved: readonly string[]): string[] {
  const inFull = new Set(full);
  const seq = [...new Set(moved)].filter((id) => inFull.has(id));
  const isMoved = new Set(seq);
  let k = 0;
  return full.map((id) => (isMoved.has(id) ? seq[k++] : id));
}

/** `list` in a saved order: anything not in it yet (just added or saved) first, as it came. */
export function inOrder<T extends { id: string }>(list: readonly T[], order: readonly string[]): T[] {
  const pos = new Map(order.map((id, i) => [id, i]));
  const fresh = list.filter((s) => !pos.has(s.id));
  const placed = list.filter((s) => pos.has(s.id)).sort((a, b) => pos.get(a.id)! - pos.get(b.id)!);
  return [...fresh, ...placed];
}
