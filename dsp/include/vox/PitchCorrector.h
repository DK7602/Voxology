#pragma once

#include "Biquad.h"

#include <array>
#include <cstdint>
#include <vector>

namespace vox {

/** Scales for pitch correction: which of the 12 notes (from the key's root) are allowed. */
inline constexpr int kScales = 6;
inline constexpr std::array<const char*, kScales> kScaleNames { "Chromatic", "Major", "Minor", "Harmonic Minor", "Minor Pentatonic", "Major Pentatonic" };
inline constexpr std::array<std::array<bool, 12>, kScales> kScaleMasks {{
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 },
    { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 },
    { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1 },
    { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 },
    { 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 },
}};
inline constexpr std::array<const char*, 12> kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

struct PitchParams
{
    bool enabled = true;
    double amount = 0.0;      // % of the way to the target note; 0 = off (delayed pass-through)
    int key = 0;              // 0 = C ... 11 = B
    int scale = 0;            // index into kScaleNames
    double speedMs = 50.0;    // retune speed: 0 = instant (the hard trap effect) ... 400 = slow, natural
    double humanize = 0.0;    // %: long notes get a slower retune so they keep their life
};

/** Pitch correction ("auto-tune") for a single voice.

    Detection: YIN (cumulative mean normalised difference) on a 12 kHz copy every ~2.7 ms, the
    period refined at full rate by normalised cross-correlation, with a voicing decision (YIN
    aperiodicity + level), so breaths and "s" sounds are never pitched.
    Decision: the nearest note of the key / scale, with hysteresis so a note doesn't flicker at the
    boundary. The correction (in semitones) glides to its target with the Retune Speed time constant,
    so slow speeds keep vibrato and only fix drift; Humanize slows it further on sustained notes.
    Shifting: TD-PSOLA. Two-period Hann grains are taken one input period apart and laid down one
    output period apart (period / shift ratio), then normalised by the window sum. The voice's
    formants stay put (no chipmunk), and with no correction the grains line up exactly, so the
    output equals the input (to rounding). Unvoiced sound passes through with fixed small grains.

    Constant latency (latencySamples(), ~32 ms) in every state; exact delayed pass-through when off or
    at Amount 0. prepare() allocates; process() never does. Up to 2 channels share one detector. */
class PitchCorrector
{
public:
    static constexpr double kMinHz = 75.0, kMaxHz = 1000.0;
    static constexpr double kLatencySeconds = 0.032;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const PitchParams& p) noexcept { params = p; }
    static bool isNeutral (const PitchParams& p) noexcept { return ! p.enabled || p.amount < 0.05; }
    int latencySamples() const noexcept { return latency; }

    void process (double* const* ch, int nch, int n) noexcept;

    /** The note to aim for: nearest allowed note to `midi` (fractional), keeping `current` while the
        sung pitch stays within its +/- 0.5 semitone plus a little hysteresis. */
    static int targetNote (double midi, int key, int scale, int current) noexcept;

    struct Reading
    {
        bool voiced = false;
        double sungMidi = 0.0;     // what you sing (fractional MIDI note)
        int targetMidi = -1;       // the note it's pulled to
        double correction = 0.0;   // semitones applied right now
    };
    Reading reading() const noexcept { return last; }

private:
    void analyse() noexcept;
    void synthesiseUpTo (int64_t limit) noexcept;
    double alignMark (double prevMark, double candidate, double P) const noexcept;
    double grainEnergy (double centre, double P) const noexcept;

    PitchParams params;
    double sr = 48000.0;
    int channels = 2;
    int latency = 1536;

    // Rings (power-of-two size), indexed by absolute sample time.
    int ringSize = 0, mask = 0;
    std::array<std::vector<double>, 2> in, acc;
    std::vector<double> wsum;
    std::vector<double> mono;          // mono input for detection
    int64_t now = 0;                   // samples written so far

    // Detection (12 kHz copy of the mono input).
    int dec = 4;
    double decAcc = 0.0; int decCount = 0;
    std::vector<double> dbuf;          // ring of decimated samples
    int dmask = 0; int64_t dnow = 0;
    Biquad aa1, aa2;                   // anti-alias before decimation
    int hop = 128, hopCount = 0;
    std::vector<double> diff, cmnd;
    double period = 0.0;               // current period (samples at full rate), 0 = unvoiced
    double levelMs = 0.0, levelCoeff = 0.0;

    // Decision.
    int note = -1;
    double corr = 0.0;                 // semitones, smoothed
    double sustain = 0.0;              // seconds on the current note
    std::array<double, 3> rawP {};
    double lastP = 0.0;
    double clarityS = 1.0;             // smoothed note clarity (how much of the correction to apply)                // previous period reading (octave guard)     // newest period readings (median of three)
    int voicedRun = 0;                 // + consecutive voiced readings, - consecutive unvoiced
    Reading last;

    // Every analysis, stamped with the input time it describes, so each grain uses the reading for
    // its own moment (the pitch is measured ~10 ms after the audio the grain is cut from).
    struct Frame { double time = 0.0, period = 0.0, corr = 0.0; };
    static constexpr int kFrames = 64;
    std::array<Frame, kFrames> frames {};
    int frameCount = 0;
    Frame frameAt (double t) const noexcept;
    void pushFrame (double time, double p, double c) noexcept;

    // Synthesis.
    double synthPos = 0.0;             // next output grain centre (input time)
    double anaPos = 0.0;               // analysis mark (input time)
    double noteEnergy = 0.0;           // recent grain energy (join timing)
    double drift = 0.0;                // anaPos - synthPos, eased back to 0 in breaths
    double gPeriod = 0.0, gRatio = 1.0;
    bool wasNeutral = true;
};

/** Detects the key of a vocal from its sung pitches (fractional MIDI notes): pitch-class histogram
    vs the Krumhansl-Kessler major / minor profiles. confidence 0..1 (correlation margin). */
struct KeyGuess { int key = 0; bool minor = true; double confidence = 0.0; double offCents = 0.0; };
KeyGuess detectKey (const std::vector<double>& midiNotes);

} // namespace vox
