#pragma once

#include "PitchCorrector.h"

#include <cmath>
#include <string>
#include <vector>

/** Honey Tune: the note editor's engine (offline, the whole clip at once).

    analyse()  the pitch line of the clip (the live detector, then cleaned with look-ahead: octave slips
               fixed against the surrounding notes, small gaps bridged, a median smooths jitter)
    findNotes()  splits it into notes: start / end, the note's pitch, how far off the nearest note it is
    edits      per note: target pitch, how much of its drift and vibrato to keep
    render()   the clip with the edits, using the live Pitch shifter driven by a plan (so it gets all of
               its clean-up work), latency removed. Untouched notes come out unchanged. */
namespace vox::honey {

struct Track
{
    double sampleRate = 48000.0;
    std::vector<double> time;      // input time of each reading (samples)
    std::vector<double> midi;      // fractional MIDI note; 0 = no note (breath, consonant, silence)
    std::vector<double> clarity;   // 0..1
    int aiChecked = 0, aiFixed = 0, aiFound = 0;   // AI check: moments checked, octave slips fixed, skipped notes found
};

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
TimeMap timeMap (const std::vector<Note>& notes, double clipSamples, double sampleRate);

/** The clip re-timed by the map (pitch and tone kept: pitch-synchronous grains, a period repeated or
    skipped where it's stretched / squeezed), output samples [from, to) filled (rest 0). */
std::vector<float> warp (const std::vector<float>& mono, double sampleRate, const Track& track, const TimeMap& map,
                         size_t from, size_t to);

/** ai: also run the AI check (CREPE, see Crepe.h) every 30 ms where the voice is up: where it and the
    detector are an octave apart, the one that agrees with the moments around it wins; loud moments the
    detector called unvoiced (rasp) but the AI is sure about become notes. Offline only (a few seconds per
    minute of audio, on several threads). */
Track analyse (const std::vector<float>& mono, double sampleRate, bool ai = true);
/** The AI check on its own (analyse() runs it when ai is on). */
void aiCheck (Track& track, const std::vector<float>& mono, double sampleRate);
std::vector<Note> findNotes (const Track& track);

/** Pulls every note's target to the nearest note of the key / scale (amount 0..1 of the way). */
void snapToKey (std::vector<Note>& notes, int key, int scale, double amount = 1.0);
/** Cents from the nearest note of the key / scale (signed). */
double centsOff (const Note& n, int key, int scale);

/** The clip with the edits (same length, aligned with the input). */
std::vector<float> render (const std::vector<float>& mono, double sampleRate, const Track& track,
                           const std::vector<Note>& notes, double transitionMs = 25.0);

/** After an edit: the stretch to re-render (moved / stretched notes take their neighbours along: the
    gaps between them change), from the middle of the gap before the first changed note to
    the middle of the gap after the last (neighbours sung in one breath come along). false = the notes
    don't line up (re-analysed: render it all); from == to = nothing changed. Samples. */
bool changedRegion (const std::vector<Note>& before, const std::vector<Note>& after, double clipSamples, double sampleRate,
                    double& from, double& to);
/** Re-renders only [from, to) of `previous` (the clip's earlier render), crossfading 10 ms at each join. */
void renderPart (const std::vector<float>& mono, double sampleRate, const Track& track, const std::vector<Note>& notes,
                 std::vector<float>& previous, double from, double to, double transitionMs = 25.0);

} // namespace vox::honey
