// The song document: everything needed to play along with, print and export a song.
// Stored as JSON in D1 (songs.data). Time is measured in beats; `offset` / `beatTimes`
// tie beats to seconds in the media.

export type Media =
  | { kind: "none" }
  | { kind: "youtube"; videoId: string }
  | { kind: "audio"; key: string; name: string; duration?: number };

/** A chord change. It lasts until the next chord (or the end of the song). */
export interface ChordEvent {
  /** start, in beats from beat 0 */
  beat: number;
  /** chord symbol as written: "Bbm7", "D/F#", "E5", "N.C.", "note:E", "notes:E G A" */
  name: string;
  /** character index in the lyric line this chord falls in (where the chord sits over the words).
   *  Missing = placed proportionally by time. */
  col?: number;
}

/** A lyric line. It lasts until the next line (or `end`). */
export interface LyricLine {
  beat: number;
  text: string;
  /** a section heading shown before this line: "Verse 1", "Chorus", "Intro" */
  section?: string;
  /** optional end beat (an instrumental gap follows) */
  end?: number;
}

export interface SongDoc {
  v: 1;
  title: string;
  artist: string;
  genre: string;
  /** key of the recording as written, e.g. "Gb" or "Ebm" */
  key: string;
  bpm: number;
  beatsPerBar: number;
  /** capo the arrangement is meant for (0 = none) */
  capo: number;
  tuning: string;
  media: Media;
  /** seconds into the media where beat 0 falls */
  offset: number;
  /** measured beat times in seconds (from audio analysis). When present they set the timing;
   *  otherwise beat n is at offset + n * 60 / bpm. */
  beatTimes?: number[];
  /** the rhythm part: chords (also power chords, single notes and riffs) */
  chords: ChordEvent[];
  /** optional lead part: melody notes, riffs and fills over time ("note:E4", "notes:E4 G4",
   *  "E5", "N.C." for a rest). Same format as `chords`; the player can show either part. */
  lead?: ChordEvent[];
  lines: LyricLine[];
  /** song length in beats */
  length: number;
  /** where the chords / lyrics came from, shown small under the title */
  credits?: string;
}

export type Instrument = "guitar" | "ukulele" | "piano" | "mandolin" | "bass";

export interface UserSettings {
  theme?: "light" | "dark" | "system";
  instrument?: Instrument;
  leftHanded?: boolean;
  /** show chord shapes relative to the capo (true) or the sounding chords (false) */
  capoShapes?: boolean;
  countIn?: number; // bars of count-in, 0 = off
}

/** What the player remembers per user and song. */
export interface PracticeSettings {
  part?: "rhythm" | "lead";
  transpose?: number;
  capo?: number;
  instrument?: Instrument;
  speed?: number; // 0.5 .. 1.5
  simplify?: boolean;
  loop?: { on: boolean; from?: number; to?: number }; // beats
}

export function emptySong(): SongDoc {
  return {
    v: 1,
    title: "",
    artist: "",
    genre: "",
    key: "C",
    bpm: 100,
    beatsPerBar: 4,
    capo: 0,
    tuning: "standard",
    media: { kind: "none" },
    offset: 0,
    chords: [],
    lines: [],
    length: 0,
  };
}
