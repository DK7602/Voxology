#pragma once

#include "PitchCorrector.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

/** Honey Tune: the note editor's engine (offline, the whole clip at once).

    analyse()  the pitch line of the clip (the live detector, every reading checked at the full rate up to 1600 Hz
               for the period it really repeats at, then cleaned with look-ahead: octave slips fixed against the
               surrounding notes, small gaps bridged, a median smooths jitter)
    findNotes()  splits it into notes: start / end, the note's pitch, how far off the nearest note it is
    edits      per note: target pitch, how much of its drift and vibrato to keep
    render()   the clip with the edits, using the live Pitch shifter driven by a plan (so it gets all of
               its clean-up work), latency removed. Untouched notes come out unchanged.
    cancel (analyse, render): set it from another thread to stop early (the plug-in closing); the result is then
    incomplete, to be thrown away. */
namespace vox::honey {

using Channels = std::vector<std::vector<float>>;   // a clip's channels (the same length)

struct Track
{
    double sampleRate = 48000.0;
    std::vector<double> time;      // input time of each reading (samples)
    std::vector<double> midi;      // fractional MIDI note; 0 = no note (breath, consonant, silence)
    std::vector<double> clarity;   // 0..1
    std::vector<char> filled;      // 1: a moment before / after what the detector heard, given the note's pitch (its attack
                                   // or release); findNotes decides the notes without these, then gives them to their note
    int aiChecked = 0, aiFixed = 0, aiFound = 0;   // AI check: moments checked, octave slips fixed, skipped notes found
    // The clip's waveform (the editor draws each note as its waveform): per kLevelHopSeconds from the clip's start, the
    // loudest sample (|x|) and the mean square; levelTop = how loud the singing gets (the 98th percentile of the peaks
    // where a pitch was heard: the notes' full height in the editor).
    static constexpr double kLevelHopSeconds = 0.001;
    std::vector<float> level, power;
    float levelTop = 0.0f;
};

/** The waveform's loudest sample between two clip moments (seconds; at least one step), 0 outside the clip. */
float levelOver (const Track& track, double fromSeconds, double toSeconds);
/** ... and its RMS there (the waveform's body, inside the peaks). */
float rmsOver (const Track& track, double fromSeconds, double toSeconds);

struct Note
{
    double start = 0.0, end = 0.0;   // samples
    double pitch = 0.0;              // the note as sung (median, MIDI)
    double target = 0.0;             // where it should sound (MIDI); = pitch when untouched
    double drift = 1.0;              // how much of its slow drift to keep (1 = all, 0 = straight)
    double vibrato = 1.0;            // how much of its vibrato to keep
    bool edited = false;
    int firstReading = 0, lastReading = 0;
    double outStart = -1.0, outEnd = -1.0;   // where it sounds in time (samples); < 0 = where it was sung
    double formant = 0.0;                    // semitones: + thinner / younger, - deeper (the note only)
    double gainDb = 0.0;                     // its level: louder (+) / quieter (-), dB
    bool muted = false;                      // silent (its consonant before it stays)
    double soundStart() const { return outStart >= 0.0 ? outStart : start; }
    double soundEnd() const { return outEnd >= 0.0 ? outEnd : end; }
    bool timeMoved() const { return std::abs (soundStart() - start) > 0.5 || std::abs (soundEnd() - end) > 0.5; }
};

/** Where every moment of the clip sounds after notes were moved / stretched: a piecewise-straight map
    between the input and output times of the notes' starts and ends (the gaps around a moved note
    stretch or squeeze to make room; the clip keeps its length). */
struct TimeMap
{
    std::vector<double> in, out;   // knots, both rising, from 0 to the clip length (samples)
    bool identity = true;
    double inAt (double tOut) const;    // the input moment heard at output time tOut
    double outAt (double tIn) const;
};
/** track: the sound next to the notes that move (a consonant before or after) goes with them whole; only the silence
    or breath between notes stretches / squeezes. */
TimeMap timeMap (const std::vector<Note>& notes, double clipSamples, double sampleRate, const Track* track = nullptr);

/** The clip re-timed by the map (pitch and tone kept: pitch-synchronous grains, a period repeated or
    skipped where it's stretched / squeezed), output samples [from, to) filled (rest 0). Every channel with the same
    grains (placed on their mix). */
Channels warp (const Channels& channels, double sampleRate, const Track& track, const TimeMap& map, size_t from, size_t to);
std::vector<float> warp (const std::vector<float>& mono, double sampleRate, const Track& track, const TimeMap& map,
                         size_t from, size_t to);

/** ai: also run the AI check (CREPE, see Crepe.h) every 30 ms where the voice is up: where most of its estimates
    over a stretch of the line hear another octave, or where it and the detector are an octave apart and it agrees
    with the moments around, its octave wins, if the waveform repeats there too; loud moments the detector called
    unvoiced (rasp) but the AI is sure about become notes. Offline only (a few seconds per minute of audio, on several
    threads). */
Track analyse (const std::vector<float>& mono, double sampleRate, bool ai = true, const std::atomic<bool>* cancel = nullptr);
/** The AI check on its own (analyse() runs it when ai is on). */
void aiCheck (Track& track, const std::vector<float>& mono, double sampleRate, const std::atomic<bool>* cancel = nullptr);
std::vector<Note> findNotes (const Track& track);

/** Pulls every note's target to the nearest note of the key / scale (amount 0..1 of the way). */
void snapToKey (std::vector<Note>& notes, int key, int scale, double amount = 1.0);
/** Cents from the nearest note of the key / scale (signed). */
double centsOff (const Note& n, int key, int scale);

/** A note's movement is two parts: its drift (the slow part, below about 2.5 Hz) and its vibrato (everything faster:
    vibrato, the wobble of a shaky voice). Note.drift / Note.vibrato say how much of each it keeps; keeping neither
    makes it a steady note. The split: a Gaussian average of the sung pitch, this wide (one standard deviation). */
inline constexpr double kDriftSigmaSeconds = 0.075;

/** Steadying (drift / vibrato kept under 100 %) eases in from a note's start and out to its end over this long, so its
    scoop in and fall off stay as sung (flattening them is what sounds robotic): a fifth of the note, 20 - 60 ms. */
inline double steadyEdgeSeconds (double noteSeconds) { return std::clamp (0.2 * noteSeconds, 0.02, 0.06); }

/** The note's pitch line as it will sound: one value per reading from note.firstReading to note.lastReading (MIDI; 0
    where no pitch was heard): the sung line moved to note.target, its drift and vibrato kept as the note says (the same
    as render(), which also smooths the moves between notes). The editor draws it. */
std::vector<double> soundingLine (const Track& track, const Note& note);
/** How much the note still moves as it will sound (cents, rms around its middle, attack and release left out). */
double movementCents (const Track& track, const Note& note);

/** How long a moved note takes to reach its new pitch (render(), renderPart()). */
inline constexpr double kTransitionMs = 25.0;

/** The clip with the edits (same length, aligned with the input). All channels through one shifter (a stereo image
    stays put); every changed note as loud as it was sung, then louder / quieter / muted as its gainDb / muted say
    (eased in and out over 10 ms around the note: centred on the middle of the gap when its neighbour is nearer). */
Channels render (const Channels& channels, double sampleRate, const Track& track, const std::vector<Note>& notes,
                 double transitionMs = kTransitionMs, const std::atomic<bool>* cancel = nullptr);
std::vector<float> render (const std::vector<float>& mono, double sampleRate, const Track& track,
                           const std::vector<Note>& notes, double transitionMs = kTransitionMs, const std::atomic<bool>* cancel = nullptr);

/** After an edit: the stretch to re-render (moved / stretched notes take their neighbours along: the
    gaps between them change), from the middle of the gap before the first changed note to
    the middle of the gap after the last (neighbours sung in one breath come along). Notes split or joined (the lists
    differ in length): around the notes that differ. false = the notes don't line up (re-analysed: render it all);
    from == to = nothing changed. Samples. */
bool changedRegion (const std::vector<Note>& before, const std::vector<Note>& after, double clipSamples, double sampleRate,
                    double& from, double& to);
/** Re-renders only [from, to) of `previous` (the clip's earlier render), crossfading 10 ms at each join. */
void renderPart (const Channels& channels, double sampleRate, const Track& track, const std::vector<Note>& notes,
                 Channels& previous, double from, double to, double transitionMs = kTransitionMs,
                 const std::atomic<bool>* cancel = nullptr);
void renderPart (const std::vector<float>& mono, double sampleRate, const Track& track, const std::vector<Note>& notes,
                 std::vector<float>& previous, double from, double to, double transitionMs = kTransitionMs,
                 const std::atomic<bool>* cancel = nullptr);

/** Note boundaries you set by hand, on top of findNotes(): a note cut in two at a moment (a split), two neighbours made
    one (a join: at the middle of the gap between them). Seconds from the clip's start: saved with the project, and kept
    when the clip is analysed again (a boundary that no longer finds its notes does nothing). */
struct Layout
{
    std::vector<double> splits, joins;
    bool empty() const { return splits.empty() && joins.empty(); }
    bool operator== (const Layout& o) const { return splits == o.splits && joins == o.joins; }
};
/** Each part of a split note is at least this long. */
inline constexpr double kMinPartSeconds = 0.03;
/** A boundary finds its notes again this near (seconds). */
inline constexpr double kLayoutReach = 0.03;
/** The boundary between two neighbouring notes (seconds): the middle of the gap between them. */
double boundarySeconds (const Track& track, const Note& a, const Note& b);
/** The reading a note is split at (the first one from this moment on), or -1 when the moment isn't inside the note far
    enough from its ends for both parts to last kMinPartSeconds. */
int splitReading (const Track& track, const Note& note, double seconds);
/** findNotes()'s notes with your boundaries (the joins first, then the splits). Each part's pitch is the middle of its
    own singing (a split note snaps each part on its own: that's what a split is for). */
std::vector<Note> reshape (const Track& track, std::vector<Note> notes, const Layout& layout);
/** Note `index` (of reshape()'s notes) cut at this moment: the split goes into the layout, or a join there comes out
    (the notes as they were found come back). false: not a place to split. */
bool addSplit (Layout& layout, const Track& track, const std::vector<Note>& notes, int index, double seconds);
/** Notes first .. last (reshape()'s, neighbours) made one: the joins go into the layout, or your splits there come out.
    false: nothing to join. */
bool addJoin (Layout& layout, const Track& track, const std::vector<Note>& notes, int first, int last);

} // namespace vox::honey
