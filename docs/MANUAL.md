# Voxology & Honey Tune Manual

Manual for version 0.35.0. It's built into both plug-ins (the ? button) and updated with every release.

## Install and first run

Voxology goes on your vocal track as an insert; Honey Tune is a separate note editor that sits on the vocal clip (ARA). Both come from the same download.

1. Open the repo's **Actions** page on GitHub, click the latest green **Build & Test** run, and download the artifact **Voxology-Windows**.
2. In Chrome, press **Ctrl+J** and click **Keep** if Chrome warns about the download.
3. Unzip it. Copy **Voxology.vst3** (and **HoneyTune.vst3** if it is in the zip) to `C:\Program Files\Common Files\VST3`. Replace the old ones if Windows asks.
4. Close Cubase if it is open, then start it. Open **Studio > VST Plug-in Manager** and click **Rescan** if the new version does not show.
5. Check the version: Voxology shows it at the bottom right (for example **VOXOLOGY v0.35.0**).

Where each one goes in Cubase:

| Plug-in | Where | What it does |
| --- | --- | --- |
| Voxology | Insert slot on the vocal track (first slot) | The whole vocal chain: pitch, cleanup, EQ, de-ess, compression, space, output |
| Honey Tune | On the vocal clip as an ARA extension (Audio > Extensions > Honey Tune) | Edit each note by hand: pitch, timing, length, formant |
| Voxology (BEAT mode) | Optional: an insert on the beat / instrumental track | Hears the beat to find its key and make room for the vocal |

## Quick start: a finished vocal in 5 minutes

Auto-Edit does the whole chain for you: pick a style, press one button, play the song.

1. Put **Voxology** in the first insert slot of your vocal track. Leave the switch at the top left of SIGNAL CHAIN on **VOCAL**.
2. Pick a **STYLE** with the arrows (Trap Lead, Rap, Melodic, Robot, R&B, Pop, Folk, Natural Singer) and an **INTENSITY** (Light, Balanced, Strong).
3. Optional: click **+ REFERENCE** and pick a built-in pro vocal, or add a vocal you love (see References).
4. Press **AUTO-EDIT**, then play a part of the song where you are rapping or singing.
5. After about 12 seconds of voice it sets every module. The **LEARN** panel switches to the **AUTO-EDIT REPORT** and explains each choice.
6. Turn on **MATCH**, then click **A** (your original) and **B** (Voxology) to compare at the same loudness.
7. Don't like it? Press **UNDO** to get your old settings back.

Tip: run Auto-Edit on the busiest, loudest part of the song (the hook), not a quiet intro.

## Voxology screen: the top bar and the meters

The top bar holds everything you need for a quick mix; the panels below are for fine-tuning.

| Control | What it does | When to use it |
| --- | --- | --- |
| Logo | Click it: every blue part turns red. Click again: blue. Saved with your project | Just for fun |
| STYLE | What the finished vocal should sound like. Auto-Edit aims for it | Pick before Auto-Edit |
| INTENSITY | Light, Balanced or Strong: how far Auto-Edit pushes | Light for a good recording, Strong for a rough one |
| + REFERENCE | Aim at a pro vocal's sound instead of the style's target | When you want a specific sound |
| AUTO-EDIT | Listens to about 12 s of voice, then sets every module | Start of every mix |
| UNDO | Puts back the settings from before Auto-Edit | If you don't like the result |
| A / B | A = your original vocal, B = through Voxology | Checking it really is better |
| MATCH | A and B play at the same loudness | Always on while comparing (louder always sounds better) |
| REC | Record mode: about 6 ms delay instead of about 55 ms, so you hear yourself tuned while recording. A little rougher; Voices, Formant, Noise and Room are off | On while recording, off for mixing |
| Power button | Bypasses all of Voxology | Quick before / after |

The round **?** next to LEARN opens this manual (Esc or CLOSE to go back). The bottom bar shows **IN** and **OUT** loudness (LUFS) and peak level, plus the version number. The middle card, **VOCAL SPECTRUM**, shows your vocal (grey-blue), the result (gold), the Tone EQ curve (blue line, drag its 5 points) and Dynamic EQ cuts (orange).

## The 12 modules

The honeycomb on the left is the chain, run top to bottom: click a cell to open it, click its dot to switch it on or off.

Every knob:

- Drag up / down, or use the mouse wheel for small steps (hold **Shift** for finer).
- **Double-click the knob**: back to its default.
- **Double-click the number** under it: type an exact value, then press **Enter** (**Esc** cancels, clicking away keeps it). Units are optional: `-3`, `120` (Hz), `1.2k` (1.2 kHz), `4` (4:1), `250 ms`, `off`. A knob that only goes one way doesn't need the minus (Threshold `18` = −18 dB). Values past the end stop at the end. With a knob selected, **Enter** opens the box too. Transpose on the Pitch page works the same way.

| # | Module | What it does | Main controls |
| --- | --- | --- | --- |
| 01 | Pitch | Auto-tune, from natural to the hard robot effect | Mode (Natural, Classic, Robot), Amount, Retune (speed), Vibrato, Humanize (long notes stay live), Formant, Key (Auto / Manual + note), Scale, Notes (MIDI off / notes / learn), Transpose |
| 02 | Cleanup | Cleans the recording | Noise (steady hiss, fan, AC or hum turned down, even under the words), Room (the echo of your room after words, shortened), Low Cut (rumble), Pops (p and b thumps), Breaths (turned down), Gate + Gate Range (quiets the gaps). Warns "clipped in!" if the recording itself is distorted |
| 03 | Tone EQ | Shapes the tone: body, mud, nasal, presence, air | 5 bands, each Gain + Frequency. Or drag the 5 points on the spectrum (double-click a point = flat) |
| 04 | Dynamic EQ | Cuts boom, mud, honk and harshness only when they jump out | 4 bands, each Max Cut + Frequency, Sensitivity |
| 05 | De-Esser | Tames sharp s, t, sh and ch | Amount, Sensitivity, Frequency |
| 06 | Rider | Rides the level word by word, like a hand on the fader | Target, Range, Speed (Slow, Med, Fast) |
| 07 | Compressor | Evens the performance | Peak (catches spikes), Threshold + Ratio (smooth leveler), Makeup, Mix (parallel blend) |
| 08 | Saturation | Adds harmonics: warmth, density, presence | Mode (Tape, Tube, Clip), Drive, Mix |
| 09 | Voices | Doubles, extra voices and harmonies in key | Double, Width, Voice 1 (left) + Voice 2 (right): **Unison** (a second singer on your own notes, same octave) or a harmony: 3rd, 5th or octave, up or down, Level, Formant |
| 10 | Delay | Echoes locked to the song's tempo | Time (note value), Feedback, Mix, Tone, Duck (quiet while you sing), Ping-Pong |
| 11 | Reverb | A vocal plate: a space for the voice | Decay, Pre-delay (keeps words clear), Mix, Tone, Duck (blooms in the gaps) |
| 12 | Output | Final level into your mix | Output gain |

**Noise and Room (Cleanup)** work before anything else in the chain, so Pitch and the gate already hear the cleaner vocal.

- **Noise** learns your background noise by itself from the quiet moments (it needs about a second of the song), then turns it down, also under the words where the gate can't reach. 6 - 12 dB sounds natural; past 18 dB the quiet ends of words can turn watery. Measured on steady noise: 6 dB takes out 6, 12 takes out 12, 18 about 17, 24 about 22. A clean recording and long held notes are left exactly as they are.
- **Room** shortens the echo of a bare room after your words (by up to about 5 dB), and leaves held notes alone. It isn't a full de-reverb (that needs an AI model), so a very echoey take still sounds roomy: record in a smaller space with soft things around you.
- Auto-Edit sets both: Noise when the noise in your gaps is less than about 42 dB under your voice (dry pro vocals: about 50 dB), so it takes out at least 6 dB, Room when more than -34 dB rings on after your phrases (dry pro vocals: -37 dB or less). If your vocal already has reverb on it, Room shortens that too: turn Room off if it's on purpose.
- They add about 21 ms to Voxology's delay, on or off, so the timing never changes (Cubase makes up for it automatically). Record mode leaves them out.

Each cell shows a short status under its name (for example "idle", "flat", "cut 80 Hz"). Cleanup's status reads "clipped in!" or "too hot in!" when the recording itself is distorted: fix that at the recording stage.

## Learn panel: explanations, warnings and the report

The right-hand panel tells you what each module does, why Auto-Edit chose its settings, and what to fix. It has two tabs:

- **THIS MODULE**: what the open module does, how to use each control, and live warnings about it.
- **AUTO-EDIT REPORT**: after Auto-Edit, one line per setting it chose and why. "CHECKED · KEPT" means it looked at a module and left it alone on purpose. With the beat on the side-chain, a **VOCAL FIT · YOUR BEAT** section comes first (see Vocal Fit).

How to read a warning card:

- **Red card** = worth your attention (for example CLIPPED RECORDING).
- **Blue card** = calm: optional or just information. In red mode (logo clicked) these turn red too.
- Every card answers **"Do I need to fix it?"** and then gives numbered steps. If the answer is "No" or "Optional", you can ignore it.

## References: aim at a pro sound

A reference makes Auto-Edit aim at the tone of a real finished vocal instead of the style's target. Click **+ REFERENCE** (it shows the reference's name once one is loaded) to open the menu:

- **BUILT-IN**: 15 measured groups of finished pro vocals, in three sections: MALE, FEMALE and MIXED. Only measurements are stored, never audio. Projects saved with an older version keep their reference (renamed to today's name).
- **YOURS**: every vocal file you've added. Click ✕ to remove one.
- **+ Add a vocal file…**: pick a WAV, AIFF, FLAC, MP3 or OGG file. It's measured once and saved in Documents\Voxology\References, so it's there in every project.
- **No reference**: back to the style's own target.

| Section | Built-in | Pitch | Measured from |
| --- | --- | --- | --- |
| Male | Male pop singer | *Natural* | 15 released pop songs (MoisesDB) |
| Male | Male R&B singer | *Natural* | 7 R&B lead vocals (Cymatics sample packs, mostly dry stems) |
| Male | Male trap, melodic | *Classic* | 7 hard-tuned melodic trap vocals, effects baked in |
| Male | Male singer, rock / indie | *Natural* | 18 finished mixes (MUSDB18) |
| Male | Male rap, released songs | *Off* | 17 released rap songs (MoisesDB) |
| Male | Male rap, rhythmic | *Off* | 12 finished mixes (MUSDB18) |
| Male | Male trap rap | *Off* | 22 trap rap vocals, effects baked in |
| Female | Female pop singer | *Natural* | 26 released pop songs (MoisesDB) |
| Female | Female R&B singer | *Natural* | 4 R&B acapellas (Cymatics sample packs) |
| Female | Female singer, bright | *Natural* | 26 finished mixes, the brighter half (MUSDB18) |
| Female | Female singer, warm | *Natural* | 25 finished mixes, the warmer half (MUSDB18) |
| Female | Female electronic singer | *Natural* | 13 released electronic / dance songs (MoisesDB) |
| Female | Female rap, rhythmic | *Off* | 35 finished mixes (MUSDB18) |
| Mixed | Sung hooks, wet | *Natural* | 11 finished hooks, male and female, effects baked in (Cymatics) |
| Mixed | All voices, pro average | *Natural* | all 143 MUSDB18 vocals |

The *Pitch* tag after each name (in italics in the menu) says how tuned that reference sounds, as one of Voxology's Pitch modes, so you can pick one that fits your mix. *Natural* = notes centred on pitch with the voice's own movement kept; *Off* = rapped, no audible tuning; *Classic* = the hard auto-tune sound: notes snapped close to pitch with little movement left inside them (Male trap, melodic). Measured on the vocals themselves; the MoisesDB groups are labelled by kind. No built-in sounds fully like Robot: for that sound, set Pitch's Mode (or pick the Robot style). The tag is a guide only: Pitch still follows your STYLE.

After picking a reference, press **AUTO-EDIT** again so it re-aims.

Best files to add: the **acapella or vocal stem** of a song you love, dry or lightly processed. A full song (with the beat) only works roughly; the Learn panel warns "FULL SONG?". Most online acapellas are **wet** (reverb or echo baked in): Voxology notices, takes only their tone and "s" brightness, and leaves space and compression to your STYLE.

## The beat: key detection, Vocal Fit and Unmask

Let Voxology hear your beat and it gets the key right, checks how your vocal sits against it, and makes room for the vocal. The beat is the surest way to find the key; the voice alone is often unsure, especially on rap.

**Pitch key, Auto (the default)** uses the surest of these, in this order:

1. **Side-chain**: the beat sent into the vocal's Voxology. Cubase: click the side-chain button at the top of the Voxology window, then on the beat track add a Send to "Voxology - Side-Chain".
2. **A second Voxology on the beat** in BEAT mode (below).
3. **Your voice**: needs about 6 seconds of held notes. If it stays unsure, Pitch snaps to the nearest note (Chromatic) until you pick the key under **MANUAL**.

The Pitch page shows where the key came from ("beat: Dm", "voice: Dm", "voice: listening…").

**Vocal Fit**: with the beat on the side-chain (step 1 above), Auto-Edit records the beat while it listens to your vocal and sets the chain for that beat. It nudges two modules the way pro vocals sit in their own beats:

- **Tone EQ**: pro vocals follow their beat's tone a little. Over a beat with more presence (2 - 5 kHz) the vocal gets a bit more too, so the words aren't buried; the same for low-mids and air. The target moves at most 1.5 dB, and a band only moves when your vocal is outside the normal range.
- **Compressor**: over a punchy beat (space between the hits) your words keep up to 1 dB more punch; over a steady, wall-of-sound beat the vocal is smoothed a little more. If the compressor can't get there on your take, it leaves it as the style set it and says so.

The report's **VOCAL FIT · YOUR BEAT** section (and a "Vocal Fit" line in those two modules) shows:

| Line | What it tells you |
| --- | --- |
| Beat heard | How many seconds of beat played under your voice (it needs 4 s; with less, the chain is set as if there were no beat) |
| Vocal vs beat | Your finished vocal's loudness against the beat's. Pro mixes put the vocal about 2 dB under the beat (most between 0.6 and 4 dB under) |
| FIT LEVEL button | Appears when your vocal is 1 dB or more off that. It shows the move first ("FIT LEVEL: OUTPUT TO −2.5 dB"); click it to set Voxology's Output there, click again to put it back. It moves Output at most 6 dB (the rest is the fader's). Nothing changes unless you click |
| Top end (hi-hats) | How bright the beat is (dark, typical, bright) |
| Low end (808 / kick) | How heavy the beat's low end is (light, typical, heavy) |
| Movement | Steady (a wall of sound) or punchy (space between the hits) |
| Tone for this beat | How far the Tone EQ target moved for this beat |
| Compressor for this beat | How much more (+) or less (−) punch your words keep |
| Covers your voice | Where the beat covers your words more than in pro mixes, if anywhere. Then a **BEAT COVERS YOUR VOICE** card gives the steps to make room there with Unmask (below), or, if a Voxology in BEAT mode is already in the project, how to make it dip deeper |

The level reading and FIT LEVEL are only right with the **vocal fader at 0 dB** and the beat's side-chain send at **0 dB, post-fader**: Voxology can't see or move Cubase's faders (no plug-in can). Rap often sits a little louder than the pro middle: skip FIT LEVEL if you like your vocal up front. If the report says **BEAT SILENT**, the side-chain is on but no beat reached it: switch the beat track's send on (0 dB) and run Auto-Edit again while the beat plays.

Send the **beat only**. If what reaches the side-chain has your own vocal in it (a full mix, a bounce with vocals, or a bus with the vocal on it), the report says **YOUR VOCAL IS IN THE BEAT** and Vocal Fit stays out: it would be fitting your voice to itself. Send the instrumental (or a group with just the music) and run Auto-Edit again. Voxology can tell your own vocal by how the sound follows your words; it can't reliably tell a different singer's vocal (a sampled hook) in a beat, so it doesn't warn about that.

Without a beat, Auto-Edit works exactly as before. The pro numbers come from 143 released songs (MUSDB18, mostly rock and pop; trap beats often read brighter and punchier).

**Unmask (BEAT mode)**: put a second Voxology on the beat track and flip its switch to **BEAT**. While you sing, it dips the beat only in the frequency bands where your voice sits, so the vocal cuts through without turning it up. In the gaps the beat comes back full.

- **Amount**: how deep the dips go (default 50 %).
- **Focus**: Centre (dips only the middle of the beat; keeps its width) or Full.
- **Make room for**: all vocals, or one vocal track by name.
- Both Voxologys must be in the same project. If the Learn panel says NO VOCAL HEARD, check the vocal's Voxology is on and playing.

## Honey Tune: edit every note

Honey Tune shows each sung note as a honeycomb cell on a piano roll, so you can fix single notes by hand. It needs a DAW with ARA (Cubase Artist or Pro, Studio One, Logic, Reaper, Cakewalk). Cubase Elements has no ARA.

**Open it in Cubase**: select the vocal clip, then **Audio > Extensions > Honey Tune**. It listens to the clip first (a few seconds per minute of audio), including an AI pitch check that fixes octave slips and finds missed notes. The status line reports what it found. The **?** button at the top right opens this manual.

**Window size**: drag the window's corner to resize it. It opens 1280 px wide with every control on one row. In a narrower window (down to 900 px) the KEEP sliders and the Undo / Snap / Reset buttons move to a second row, so nothing gets cut off.

**Reading the notes**

| Look | Meaning |
| --- | --- |
| Blue marble cell | The note plays as sung, in key |
| Gold cell | Honey Tune pulled it onto the key |
| Red glow | It will play off-key (also when you drag a note onto an off-key note) |
| Dashed ghost cell | Where the note was sung, after you move it |
| Line inside a cell | The real pitch: drift and vibrato |

**Top bar**

- **KEY / SCALE**: Auto finds the key in this order: the key you pick, the project's key (Cubase key signature), Voxology's beat key (when a Voxology hears the beat in the same project), then the voice. If it says **KEY UNSURE** in red, pick your song's key.
- **SNAP TO NOTE**: how hard every note is pulled onto the key (100 % = all of them).
- **KEEP DRIFT / KEEP VIBRATO**: how much of the natural movement inside each note stays.
- **Original / Tuned**: hear and see the clip as recorded or with your edits.
- **Fit**: zoom to show the whole clip. **Undo / Redo**.

**Editing a note** (click it to select):

- Drag up / down: move by whole notes. Hold **Alt** for free, cent-by-cent moves.
- Drag the middle sideways: move it in time. Drag an end: make it longer or shorter.
- Double-click: snap it to the key. **Delete**: reset it. Arrow keys: up / down = one semitone, left / right = next note.
- Bottom bar for the selected note: **NOTE DRIFT**, **NOTE VIBRATO**, **NOTE FORMANT** (+ thinner, − deeper; pitch stays), **Snap note to key**, **Reset note**, **Reset all notes**.
- Every slider (top and bottom bar): click the number box next to it to type an exact value, then press **Enter**.
- **Ctrl+Z** undo, **Ctrl+Y** redo. Click the ruler to move the playhead. Mouse wheel scrolls; Ctrl+wheel or + / − zooms.

Your edits are saved with the Cubase project.

## Using both together

Use Honey Tune for the tuning and Voxology for everything else, and switch Voxology's Pitch off on that track. Voxology processes whatever Honey Tune plays, so leaving Pitch on tunes the vocal twice.

1. Record the vocal (REC on in Voxology if you want to hear yourself tuned while recording; switch it off after).
2. Open the clip in **Honey Tune**. Let it snap the notes, then fix the few that sound wrong by hand.
3. In Voxology on the same track: open **01 Pitch** and click its dot off (or set Amount to 0).
4. Run **AUTO-EDIT** in Voxology for the rest of the chain. Afterwards check Pitch is still off, since Auto-Edit sets every module.
5. Optional: a second Voxology on the beat in **BEAT** mode. It gives Honey Tune the beat's key and makes room for the vocal.

Quick rule: a fast trap or robot effect = Voxology's Pitch alone (Robot or Classic mode). A natural, polished sung vocal = Honey Tune + Voxology with Pitch off.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| Chrome says the download is dangerous | Ctrl+J, then Keep. The plug-ins aren't code-signed yet |
| Voxology or Honey Tune doesn't show in Cubase | Check the .vst3 files are in C:\Program Files\Common Files\VST3, then Studio > VST Plug-in Manager > Rescan. Voxology is in the Vocals folder |
| Voxology shows plain sliders instead of the honeycomb | WebView2 is missing on Windows. Install Microsoft Edge WebView2 Runtime, then reopen |
| Auto-Edit says TRY AGAIN | It didn't hear enough voice. Play a part with singing or rapping for at least 12 seconds |
| Crackle or harsh distortion | Look at Cleanup: "clipped in!" means the recording itself is clipped. Record again with the mic gain lower |
| Pitch sounds off-key | Check the key on the Pitch page. Send the beat to the side-chain, or pick the key under MANUAL |
| A sample-pack vocal or beat sounds off-key | Pack file names are often wrong about the key (in one tested pack, 24 of 54 were a semitone off). Leave Key on AUTO, or check the key by ear or with a key app |
| Honey Tune says KEY UNSURE | Pick the song's key in KEY, or put a Voxology on the beat (BEAT mode) and play the song for about 6 s |
| Honey Tune isn't in Audio > Extensions | Your Cubase edition needs ARA (Artist or Pro). Elements doesn't have it |
| The vocal sounds tuned twice or warbly | Voxology's Pitch is on after Honey Tune. Switch Pitch off on that track |
| Auto-Edit report has no VOCAL FIT section | The beat isn't on the side-chain. Click the side-chain button at the top of Voxology, add a Send to "Voxology - Side-Chain" on the beat track, play the beat with the vocal and run Auto-Edit again |
| Report says YOUR VOCAL IS IN THE BEAT | The side-chain gets a mix with your vocal in it. Send only the instrumental track (or a music-only group) to "Voxology - Side-Chain", then run Auto-Edit again |
| Unmask doesn't dip the beat | Both Voxologys must be in the same project, and the vocal must be playing. The beat's Learn panel says NO VOCAL HEARD if the link is missing |
| Hearing yourself late while recording | Turn on REC in Voxology's top bar (about 6 ms). Turn it off for mixing |
| B always sounds better than A | Turn on MATCH so both play at the same loudness |
