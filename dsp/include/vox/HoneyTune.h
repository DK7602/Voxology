#pragma once

#include "PitchCorrector.h"

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
};

Track analyse (const std::vector<float>& mono, double sampleRate);
std::vector<Note> findNotes (const Track& track);

/** Pulls every note's target to the nearest note of the key / scale (amount 0..1 of the way). */
void snapToKey (std::vector<Note>& notes, int key, int scale, double amount = 1.0);
/** Cents from the nearest note of the key / scale (signed). */
double centsOff (const Note& n, int key, int scale);

/** The clip with the edits (same length, aligned with the input). */
std::vector<float> render (const std::vector<float>& mono, double sampleRate, const Track& track,
                           const std::vector<Note>& notes, double transitionMs = 25.0);

} // namespace vox::honey
