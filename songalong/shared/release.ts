// Which release a page is: the name of the built script it loads changes with every release.

export function buildOf(html: string): string | null {
  return html.match(/\/assets\/index-[\w-]+\.js/)?.[0] ?? null;
}
