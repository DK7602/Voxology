import { importLead } from "./lead";
import type { ChordEvent, LyricLine, SongDoc } from "./types";

/** Built-in example songs (original compositions written for SongAlong), visible to every
 *  signed-in user. They have no recording: the player's backing track plays them. */
export interface DemoSong {
  id: string; // always starts with "demo-"
  doc: SongDoc;
}

/**
 * Builds a song from compact sheet lines. Each chord is written where it falls in the words,
 * with its length in beats: "[F#:8]Left the porch light [C#:8]burning". A line starting with
 * "#" names a section. A line of only chords is instrumental.
 */
function sheet(meta: Omit<SongDoc, "v" | "chords" | "lines" | "length" | "media" | "offset" | "tuning">, text: string): SongDoc {
  const chords: ChordEvent[] = [];
  const lines: LyricLine[] = [];
  let beat = 0;
  let section: string | undefined;
  for (const raw of text.trim().split("\n")) {
    const line = raw.trim();
    if (!line) continue;
    if (line.startsWith("#")) {
      section = line.slice(1).trim();
      continue;
    }
    let lyric = "";
    const start = beat;
    const re = /\[([^\]]+):(\d+(?:\.\d+)?)\]/g;
    let last = 0;
    let m: RegExpExecArray | null;
    const placed: ChordEvent[] = [];
    while ((m = re.exec(line))) {
      lyric += line.slice(last, m.index);
      placed.push({ beat, name: m[1], col: lyric.length });
      beat += parseFloat(m[2]);
      last = m.index + m[0].length;
    }
    lyric += line.slice(last);
    lyric = lyric.trimEnd();
    for (const p of placed) {
      if (!lyric) delete p.col;
      chords.push(p);
    }
    const l: LyricLine = { beat: start, text: lyric };
    if (section) l.section = section;
    lines.push(l);
    section = undefined;
  }
  return { v: 1, ...meta, media: { kind: "none" }, offset: 0, tuning: "standard", chords, lines, length: beat };
}

const PORCH_CHORUS = `
#Chorus
[F#:4]So come on home when the [C#:4]evening's gold
[D#m:4]Back to the dirt road and the [G#m:4]stories told
[B:4]I'll be the light at the [F#:4]end of the drive
[C#:4]Waiting on the porch till [F#:4]you arrive`;

const porchLight = sheet(
  { title: "Porch Light Letters", artist: "The Demo Pines", genre: "Country", key: "F#", bpm: 111, beatsPerBar: 4, capo: 2 },
  `
#Intro
[F#:4][C#:4][F#:4][C#:4]
#Verse 1
[F#:4]Left the porch light burning [C#:4]by the old screen door
[D#m:4]Folded every letter in the [D#m7:4]cedar drawer
[G#m:4]Summer took the long way [B:4]down the county line
[F#:4]Still I keep a candle in the [C#:4]window, just in time
${PORCH_CHORUS}
#Verse 2
[F#:4]Radio is humming all the [C#:4]songs we knew
[D#m:4]Coffee on the stove is still [D#m7:4]enough for two
[G#m:4]Fireflies are writing what I [B:4]couldn't say
[F#:4]Every little spark is pointing [C#:4]out your way
${PORCH_CHORUS}
#Outro
[B:4][C#:4][F#:8]`,
);

const IRON_CHORUS = `
#Chorus
[E5:4]Iron sparks, [D5:4]light the dark
[C5:4]Strike it loud and [A5:4]make your mark
[E5:4]Iron sparks, [D5:4]burning bright
[C5:4]We own the [B5:4]street tonight`;

const ironSparks = sheet(
  { title: "Iron Sparks", artist: "Steel Avenue", genre: "Rock", key: "Em", bpm: 132, beatsPerBar: 4, capo: 0 },
  `
#Intro riff
[notes:E G A:4][notes:E G A:4][notes:B A G:4][E5:4]
#Verse 1
[E5:4]Sparks are flying off the [G5:4]factory wall
[A5:4]Night shift hammer, hear the [E5:4]city call
[C5:4]Engines roaring on the [D5:4]edge of town
[A5:4]We were built to never [B5:4]settle down
${IRON_CHORUS}
#Verse 2
[E5:4]Neon humming on the [G5:4]wet black street
[A5:4]Every heartbeat keeping [E5:4]double time
[C5:4]Turn the amp up till the [D5:4]windows shake
[A5:4]Nothing left to lose and [B5:4]all to take
${IRON_CHORUS}
#Outro riff
[notes:E G A:4][notes:B A G:4][note:E:4][E5:4]`,
);

const rainWaltz = sheet(
  { title: "Midnight Rain Waltz", artist: "Lena Hart", genre: "Jazz", key: "G", bpm: 84, beatsPerBar: 3, capo: 0 },
  `
#Intro
[Gmaj7:3][Em7:3][Am7:3][D7:3]
#Verse 1
[Gmaj7:3]Rain on the window, the [Em7:3]streetlights are low
[Am7:3]Two cups of coffee and [D7:3]nowhere to go
[Cmaj7:3]Your coat on the chair and a [B7:3]song on the air
[Em7:3]We dance in the [Em/D:3]kitchen, slow
#Chorus
[Cmaj7:3]One, two, three, round the [D7:3]room we [G/B:3]sway
[Am7:3]Let the midnight [F#m7b5:3]rain wash the [B7:3]world away
[Em7:3]Hold me close till the [A7:3]morning gray
[Am7:3]Just one more waltz, then [D7:3]we're on our [Gmaj7:3]way
#Verse 2
[Gmaj7:3]Records are spinning the [Em7:3]tunes that we keep
[Am7:3]Somewhere a city is [D7:3]falling asleep
[Cmaj7:3]The clock on the wall can wait [B7:3]after all
[Em7:3]Love doesn't [Em/D:3]count too deep
#Ending
[Cmaj7:3][Dsus4:3][D7:3][Gmaj7:6]`,
);

const SUNDAY_CHORUS = `
#Chorus
[F:4]La la la, the [C:4]sun is up
[F:4]Pour a little gold in my [G:4]coffee cup
[Am:4]La la la, the [Em:4]day is new
[F:4]Got a song to sing and it's [G7:2]all for [C:2]you`;

const sundayStrum = sheet(
  { title: "Sunday Morning Strum", artist: "Maple & Pine", genre: "Folk", key: "C", bpm: 92, beatsPerBar: 4, capo: 0 },
  `
#Intro
[Cadd9:4][C:4][Fmaj7:4][G:4]
#Verse 1
[C:4]Sunday morning, [G:4]open the blinds
[Am:4]Leave all the Monday [F:4]noise behind
[C:4]Toast is burning, [Em:4]kettle sings
[F:4]Small and simple, the [Gsus4:2]best of [G:2]things
${SUNDAY_CHORUS}
#Verse 2
[C:4]Socks on the floor and a [G:4]cat on the chair
[Am:4]Nobody's hurrying [F:4]anywhere
[C:4]Windows open, the [Em:4]neighbors wave
[F:4]Easy is the only [Gsus4:2]plan we [G:2]made
${SUNDAY_CHORUS}
#Outro
[Fmaj7:4][G:4][Cadd9:8]`,
);

// Lead parts (original melodies and fills) for two of the examples.
porchLight.lead = importLead(
  `bar 1: A#4 C#5 A#4 G#4 | F4:2 G#4:2 | F#4 G#4 A#4 C#5 | G#4:3 r
bar 13: A#4 A#4 G#4 F#4 | G#4:2 F4:2 | C#5 C#5 A#4 G#4 | A#4:2 F#4:2
D#4 F#4 G#4 A#4 | A#4:2 F#4:2 | F4 F#4 G#4:2 | F#4:4`,
  4,
).events;

ironSparks.lead = importLead(
  `bar 1: E3:0.5 E3:0.5 G3:0.5 E3:0.5 A3 G3 | E3:0.5 E3:0.5 G3:0.5 E3:0.5 A3 G3 | B3:0.5 A3:0.5 G3:0.5 E3:0.5 D3 E3 | [E5]:4
bar 13: E4:0.5 G4:0.5 A4:0.5 B4:0.5 D5 B4 | A4:2 F#4:2 | G4:0.5 A4:0.5 G4:0.5 E4:0.5 G4:2 | A4:4
B4:0.5 D5:0.5 E5 D5:0.5 B4:0.5 A4 | F#4:2 A4:2 | G4 E4 G4 A4 | B4:4`,
  4,
).events;

export const DEMO_SONGS: DemoSong[] = [
  { id: "demo-porch-light", doc: porchLight },
  { id: "demo-iron-sparks", doc: ironSparks },
  { id: "demo-rain-waltz", doc: rainWaltz },
  { id: "demo-sunday-strum", doc: sundayStrum },
];

export function findDemo(id: string): DemoSong | undefined {
  return DEMO_SONGS.find((d) => d.id === id);
}
