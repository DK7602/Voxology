// Turning a YouTube video's title / channel into "artist" and "title" for a lyrics lookup.

export interface SongName {
  artist: string;
  title: string;
}

// Words that mark a bracketed / piped part of a title as decoration rather than the song's name.
const NOISE =
  /\b(official|video|audio|lyrics?|visuali[sz]er|music video|mv|hd|hq|4k|remaster(ed)?|explicit|clean|full song|with lyrics|lyric video|official audio|official video|official music video|color coded|cover|acoustic|live|karaoke|instrumental|unplugged|session|tutorial|lesson|chords|tabs?|playthrough|backing track)\b/i;

/** Removes "(Official Video)", "[HD]", "| Lyrics" and similar decorations. */
export function cleanTitle(s: string): string {
  let t = s;
  // bracketed decorations
  t = t.replace(/[([{【]([^)\]}】]*)[)\]}】]/g, (m, inner: string) => (NOISE.test(inner) ? "" : m));
  // trailing " | Official Video", " || ACOUSTIC" (repeatedly: "| Ed Sheeran | Guitar Cover" keeps the name)
  for (let k = 0; k < 3; k++) t = t.replace(/\s*[|•]+\s*[^|•]*$/, (m) => (NOISE.test(m) ? "" : m));
  t = t.replace(/\s+-\s+(official|lyrics?|audio|video)[^-]*$/i, "");
  return t.replace(/\s{2,}/g, " ").trim();
}

/** "Artist - Topic", "ArtistVEVO", "Artist Official" -> "Artist" */
export function cleanChannel(s: string): string {
  return s
    .replace(/\s*-\s*Topic$/i, "")
    .replace(/VEVO$/i, "")
    .replace(/\s+(official|music|records|tv)$/i, "")
    .trim();
}

/** Drops "feat. X" / "ft. X" / "(with X)" for matching. */
export function withoutFeatures(s: string): string {
  return s
    .replace(/\s*[([]\s*(feat\.?|ft\.?|featuring|with)\s[^)\]]*[)\]]/gi, "")
    .replace(/\s+(feat\.?|ft\.?|featuring)\s.+$/i, "")
    .trim();
}

export function guessSongName(meta: { title: string; channel: string; artist?: string; track?: string }): SongName {
  if (meta.artist && meta.track) {
    return { artist: withoutFeatures(meta.artist.split(/,|&| x /i)[0].trim()), title: withoutFeatures(meta.track) };
  }
  const title = cleanTitle(meta.title);
  const dash = title.split(/\s+[-–—]\s+/);
  if (dash.length >= 2) {
    return { artist: withoutFeatures(dash[0].trim()), title: withoutFeatures(cleanTitle(dash.slice(1).join(" - ")).replace(/^["“]|["”]$/g, "")) };
  }
  const quoted = /^(.*?)\s*["“](.+?)["”]/.exec(title);
  if (quoted && quoted[1]) return { artist: withoutFeatures(quoted[1]), title: withoutFeatures(quoted[2]) };
  return { artist: cleanChannel(meta.channel), title: withoutFeatures(title) };
}

/** Lyrics-site names are typed in by people: "Oasis - Wonderwall" as a track name, for example. */
export function tidyTitle(title: string, artist: string): string {
  const t = title.trim();
  if (!artist.trim()) return t;
  const escaped = artist.trim().replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return t.replace(new RegExp(`^${escaped}\\s*[-–—:]\\s*`, "i"), "").trim() || t;
}

/**
 * The names worth looking a video's lyrics up under, most likely first. Uploads write
 * "Artist - Song", but covers often write "Song - Artist" (or "Song | Artist"), so both orders
 * are tried; yt-dlp's own artist / track tags come first when present.
 */
export function nameCandidates(meta: { title: string; channel: string; artist?: string; track?: string }): SongName[] {
  const out: SongName[] = [];
  const add = (n: SongName) => {
    const artist = n.artist.trim();
    const title = n.title.trim();
    if (title && !out.some((o) => o.artist.toLowerCase() === artist.toLowerCase() && o.title.toLowerCase() === title.toLowerCase())) {
      out.push({ artist, title });
    }
  };
  if (meta.artist && meta.track) add(guessSongName(meta));
  const title = cleanTitle(meta.title);
  const parts = title
    .split(/\s+[-–—|]+\s+/)
    .map((p) => withoutFeatures(cleanTitle(p)).replace(/^["“]|["”]$/g, ""))
    .filter(Boolean);
  if (parts.length >= 2) {
    add({ artist: parts[0], title: parts.slice(1).join(" ") });
    add({ artist: parts[parts.length - 1], title: parts.slice(0, -1).join(" ") });
  }
  add(guessSongName({ title: meta.title, channel: meta.channel }));
  return out.slice(0, 3);
}
