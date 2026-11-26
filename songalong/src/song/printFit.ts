// Fitting the printed words and chords onto as few pages as reads well: the sheet is measured at
// the printed page's size, and its text made smaller until it fits.

/** The printable area of a page, in CSS px: the smaller of Letter and A4 inside the sheet's
 *  @page margins (14 mm at the sides and top, 16 mm at the bottom). */
export const PAGE = { width: 688, height: 940 };
/** Space between the two columns, in CSS px. */
export const COLUMN_GAP = 32;
/** Text sizes tried for the words and chords, largest first (1 = the usual 17px). */
export const SCALES = [1, 0.94, 0.88, 0.82, 0.76, 0.7];
/** The smallest size worth squeezing everything onto one page for: below it, two pages read better. */
export const MIN_SCALE_ONE_PAGE = 0.82;

/**
 * How many pages blocks of these heights fill, flowing down `columns` columns a page. A block
 * moves whole to the next column when it doesn't fit (as break-inside: avoid does), unless it is
 * taller than a whole column. On the first page the columns start below a header `head` px tall.
 */
export function pagesFor(blocks: readonly number[], columns: number, head: number, pageHeight = PAGE.height): number {
  const room = (col: number) => (col < columns ? pageHeight - head : pageHeight);
  let col = 0;
  let used = 0;
  for (const h of blocks) {
    if (used + h <= room(col)) {
      used += h;
      continue;
    }
    if (used > 0) col++;
    let left = h;
    while (left > room(col)) {
      left -= room(col);
      col++;
    }
    used = left;
  }
  return Math.floor(col / columns) + 1;
}

/**
 * The text size for the words and chords: the fewest pages they fit on (one page only at
 * MIN_SCALE_ONE_PAGE or larger), at the largest size that still fits on that many.
 */
export function chooseFit(pagesAt: (scale: number) => number): { scale: number; pages: number } {
  const tried = SCALES.map((scale) => ({ scale, pages: pagesAt(scale) })).filter((t) => t.pages > 1 || t.scale >= MIN_SCALE_ONE_PAGE);
  const fewest = Math.min(...tried.map((t) => t.pages));
  return tried.find((t) => t.pages === fewest)!;
}
