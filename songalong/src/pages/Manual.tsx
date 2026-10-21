import type { ReactNode } from "react";

// The user manual, at the top of Settings: one topic open at a time is plenty on a phone, so
// each is a <details> that opens on tap.

const TOPICS: { title: string; body: ReactNode }[] = [
  {
    title: "Find a song",
    body: (
      <>
        <p>Type a song or artist in the search box at the top and press Enter.</p>
        <ul>
          <li>
            <b>Ready to play</b>: songs already in SongAlong. Tap one to open it.
          </li>
          <li>
            <b>Songs</b>: videos from YouTube. Tap one and SongAlong works out the beat, the chords and the lyrics. The first time takes about half a
            minute; after that the song opens straight away, for everyone. Songs that are done show <b>✓ Ready</b>.
          </li>
        </ul>
        <p>You can also search by chords: “G C D” finds songs that use all three.</p>
      </>
    ),
  },
  {
    title: "The song screen",
    body: (
      <ul>
        <li>
          <b>Chord strip</b> (top): one box per bar. The red bar fills the chord you are on. Tap any box to jump there.
        </li>
        <li>
          <b>Lyrics</b>: the red marker follows the words as they are sung. Tap a line to jump to it. ♪ marks a part with no singing. If the credits say
          “timing estimated”, the words are placed roughly.
        </li>
        <li>
          <b>Guitar, Ukulele, Piano, Mandolin, Bass</b>: changes the chord diagrams to your instrument. On bass, each card shows the one note to play
          (the filled dot, named); the dashed rings are its fifth and octave, if you want to move between notes. <b>Cajon</b> shows the song’s drums
          as cajon strokes instead (see Cajon below).
        </li>
        <li>
          <b>Chord cards</b>: how to play each chord, in the order they come. The speaker plays it; the <b>1/8 …</b> button shows another way to play it
          (each tap, the next one), and the backing plays that shape too.
        </li>
        <li>
          <b>Animated / Summary</b>: Summary shows the whole song as a chord sheet, with the line you are on highlighted.
        </li>
        <li>
          <b>♥</b> saves the song to My Library; the share button sends a link.
        </li>
      </ul>
    ),
  },
  {
    title: "Playing along",
    body: (
      <ul>
        <li>
          <b>Play</b> starts after a count-in of clicks. Change it with the timer button next to Play (off, 1 bar or 2 bars), or in Settings below.
        </li>
        <li>
          <b>⏮</b> goes back to the start of the bar, or to the bar before if you are already at the start.
        </li>
        <li>
          <b>Speed</b>: slow the song down to learn it (YouTube videos offer the speeds YouTube allows). The pitch stays the same.
        </li>
        <li>
          <b>Loop</b>: turn it on to repeat. The arrow next to it picks what to repeat: the whole song, this section, this line or this bar. On a computer,
          shift-click two bars in the chord strip to loop them.
        </li>
        <li>
          <b>BPM</b> button: a metronome click, the chords played for you (handy without a recording), and the lead part played as a guide.
        </li>
      </ul>
    ),
  },
  {
    title: "Words and chords ahead of the music?",
    body: (
      <>
        <p>
          Bluetooth speakers and headphones, and some phones, play the sound a moment after the app shows it. Fix it once per device: tap the timer button
          next to Play (or use <b>Sync words &amp; chords to the sound</b> under Playing below) and press <b>+</b> until the red marker lands on each word
          as you hear it. Press <b>−</b> if they now come late.
        </p>
        <p>If one song’s chords or words are wrong while others are fine, that song needs fixing instead: see “Fix a chord or make your own song”.</p>
      </>
    ),
  },
  {
    title: "Capo, transpose and easier chords",
    body: (
      <ul>
        <li>
          <b>Capo</b>: pick a fret. <b>★ Easiest shapes</b> picks the capo with the easiest chords. “Show shapes” on: the diagrams show what you
          finger; off: the chords as they sound. For every choice side by side, open <b>Tools &amp; Features → Capo</b> on the song page.
        </li>
        <li>
          <b>Transpose</b>: moves the chords up or down to suit your voice. A recording or video stays in its own key.
        </li>
        <li>
          <b>Simplify Chords</b> (Tools &amp; Features): easier versions, like G for G7.
        </li>
      </ul>
    ),
  },
  {
    title: "Lead parts (riffs and solos)",
    body: (
      <>
        <p>
          Songs with a lead part have a <b>Rhythm / Lead</b> switch. Lead shows the notes of this bar and the next on the neck (or the keyboard for
          piano):
        </p>
        <ul>
          <li>
            <b>Gold</b>: the note to play now. <b>Green</b>: the rest of this bar. <b>Outlined</b>: the next bar.
          </li>
          <li>The small numbers give the order to play this bar’s notes; “1,3” means that spot is played first and third.</li>
          <li>
            Turn on <b>Play the lead (guide)</b> in the BPM button to hear it.
          </li>
        </ul>
        <p>Lead parts found from the recording can have the odd wrong or late note.</p>
      </>
    ),
  },
  {
    title: "Cajon",
    body: (
      <>
        <p>
          Pick <b>Cajon</b> with the instruments to play along on a cajon. The song’s drums become cajon strokes: the kick drum is the <b>Bass</b>{" "}
          (palm in the middle of the front), the snare the <b>Slap</b> (fingers at the top edge) and the hi-hat a light <b>tap</b>.
        </p>
        <ul>
          <li>
            <b>Easy / Groove</b>: Easy plays Bass and Slap on the beats; Groove plays the song’s beat on 8th notes, with taps. Songs open on Easy; the
            level you pick is remembered for the song.
          </li>
          <li>
            The <b>strip</b> at the top shows every stroke, bar by bar, and scrolls with the song. The <b>cajon</b> below lights where to hit:{" "}
            <b>gold</b> now, <b>green</b> the rest of this bar, <b>dashed</b> the next bar. R and L say which hand.
          </li>
          <li>
            The <b>groove cards</b> show each of the song’s grooves; the speaker plays one bar. Turn on <b>Play the cajon part</b> in the BPM button to hear
            it with the song.
          </li>
          <li>
            <b>Fix</b> on a groove card opens the groove to correct: tap a square to play that stroke there, tap a colored one to take it away.
            Bass and Slap in the same column make a <b>double hit</b> (both hands at once: the hand whose turn it is slaps, the other plays the
            bass). Every bar of that groove changes. Your fixes are kept for you on that song, Easy and Groove apart; <b>Back to the groove as found</b> undoes them.
          </li>
          <li>
            <b>One bar only</b>: hold a beat on the strip (or double-click it on a computer), or press <b>Fix this bar</b> on the cajon. The song pauses
            on that bar and it opens to fix: changes go to <b>just this bar</b> unless you pick every bar of its groove, and the arrows step to the bars
            either side. You can add strokes where the drums rest too. A bar you changed has a gold dot on the strip; <b>Back to groove A</b> puts it
            back.
          </li>
          <li>
            Where the drums stop, the strip says <b>no drums</b>: rest.
          </li>
        </ul>
        <p>
          The drums are found in the background after a song is prepared, which takes a few minutes. Until then, and for songs without drums, you get a
          suggested groove for the song’s tempo, and the cajon says so. Drums found from a recording can miss the odd snare or add one: that is what
          Fix is for.
        </p>
      </>
    ),
  },
  {
    title: "Fix a chord or make your own song",
    body: (
      <>
        <ul>
          <li>
            On a song that isn’t yours: <b>⋮ → Save an editable copy</b>. On your own: <b>⋮ → Edit chords, lyrics &amp; timing</b>.
          </li>
          <li>
            New song: <b>Tools → Add a song</b>. Play along with a backing track, your own recording (up to 100 MB; SongAlong finds the chords and beat
            for you) or a YouTube link.
          </li>
          <li>
            <b>Chords &amp; lyrics</b>: paste a chord sheet (chords over the words, or [G] in the words), then <b>Apply sheet</b>.
          </li>
          <li>
            <b>Timing</b>: press <b>Play</b> and tap the big <b>TAP</b> button (or the T or Enter key) as each line, or each chord change, starts.
            <b> Beat 1 is now</b> sets where the music starts; <b>Undo last tap</b> lets you redo one.
          </li>
          <li>
            Press <b>Save song</b>. Your songs are private unless you switch on <b>Public</b> (or later choose <b>⋮ → Make public</b>).
          </li>
        </ul>
      </>
    ),
  },
  {
    title: "Library and playlists",
    body: (
      <ul>
        <li>
          <b>My Library</b>: songs you saved with ♥ and songs you made. Press and hold a card to pick it up, then drag it to a new place
          (on a computer, just drag it). Let go without moving for its menu: <b>Remove from library</b> hides the song from your library
          (nothing is deleted; save it with ♥ to bring it back) or <b>Add to playlist</b>.
        </li>
        <li>
          <b>Playlists</b>: make one there, then add songs from a song’s <b>⋮ → Add to playlist</b> (or a long press on its card). In a
          playlist, drag a song by its <b>⠿</b> handle to change the order (the page scrolls when you reach the top or bottom); ✕ takes a song
          out.
        </li>
        <li>
          <b>Discover</b>: example songs and songs other players made public.
        </li>
        <li>
          <b>Long-press a song card</b> on Home or Discover (right-click on a computer) for more: take it off <b>Keep playing</b> or{" "}
          <b>Discover</b> (it's only hidden for you), save it to your library or remove it, or add it to a playlist. Changed your mind?
          Tap <b>Undo</b>.
        </li>
      </ul>
    ),
  },
  {
    title: "Print, export and share",
    body: (
      <ul>
        <li>
          <b>Print</b> makes a chord sheet with diagrams for your instrument (or save it as a PDF from there).
        </li>
        <li>
          <b>Export</b>: a PDF chord sheet, ChordPro, plain text or a MIDI file, with your capo and transpose.
        </li>
        <li>
          <b>Share</b> sends a link. A private song opens only for you until you make it public.
        </li>
      </ul>
    ),
  },
  {
    title: "Tuner and chord library",
    body: (
      <ul>
        <li>
          <b>Tuner</b> (Tools): press <b>Start tuner</b> and allow the microphone, then play one string. The stripes drift right when sharp and left when
          flat, and stop and turn green when in tune. Tap a string to hear its note.
        </li>
        <li>
          <b>Chord library</b> (Tools): any chord on any instrument, with several shapes to choose from.
        </li>
      </ul>
    ),
  },
  {
    title: "A song won’t prepare",
    body: (
      <p>
        YouTube sometimes refuses SongAlong’s servers. Then a small helper program on your own computer can prepare songs instead, and they work
        everywhere afterwards. Set it up under <b>Your PC helper</b> below. You can also try another video of the same song.
      </p>
    ),
  },
  {
    title: "Install SongAlong on your phone",
    body: (
      <p>
        Under <b>App</b> below, press <b>Install SongAlong</b> (Chrome on Android, Windows and Mac). On iPhone and iPad, open SongAlong in Safari, tap
        Share, then <b>Add to Home Screen</b>. It then opens full screen from its own icon.
      </p>
    ),
  },
  {
    title: "Keyboard shortcuts (computer)",
    body: (
      <ul>
        <li>
          <b>Space</b>: play or pause. <b>← / →</b>: a bar back or forward. <b>Home</b>: back to the start (or the loop’s start).
        </li>
        <li>
          While timing a song in the editor: <b>T</b> or <b>Enter</b> taps.
        </li>
      </ul>
    ),
  },
  {
    title: "Your account",
    body: (
      <p>
        Change your name or password under <b>Account</b> below. Forgot your password? On the log-in screen tap <b>Forgot password?</b>: the email link
        works for an hour (check your spam folder if it doesn’t arrive).
      </p>
    ),
  },
];

export function Manual() {
  return (
    <section className="settings-section manual" id="manual">
      <h2>Manual</h2>
      <p className="muted">How SongAlong works. Tap a topic to open it.</p>
      {TOPICS.map((t) => (
        <details key={t.title} className="manual-topic">
          <summary>{t.title}</summary>
          <div className="manual-body">{t.body}</div>
        </details>
      ))}
    </section>
  );
}
