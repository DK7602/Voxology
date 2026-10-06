#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vox {

/** Scales for pitch correction: which of the 12 notes (from the key's root) are allowed. */
inline constexpr int kScales = 10;
inline constexpr std::array<const char*, kScales> kScaleNames { "Chromatic", "Major", "Minor", "Harmonic Minor", "Minor Pentatonic", "Major Pentatonic",
                                                                "Dorian", "Phrygian", "Mixolydian", "Blues" };
inline constexpr std::array<std::array<bool, 12>, kScales> kScaleMasks {{
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 },
    { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 },
    { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1 },
    { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 },
    { 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 },
    { 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0 },
    { 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0 },
    { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0 },
    { 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0 },
}};
inline constexpr std::array<const char*, 12> kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** How the tune behaves. Natural: notes are pulled onto pitch at the Retune speed but real vibrato
    (and the start of a scoop) stays; Vibrato can calm or deepen it. Classic: the whole pitch line glides to the note at
    the Retune speed (fast = flat and tight, slow = only drift fixed). Robot: instant, flat, stepped
    notes whatever the knobs say - the hard trap / T-Pain effect. */
enum PitchMode : int { kPitchNatural = 0, kPitchClassic = 1, kPitchRobot = 2 };
inline constexpr int kPitchModes = 3;
inline constexpr std::array<const char*, kPitchModes> kPitchModeNames { "Natural", "Classic", "Robot" };

struct PitchParams
{
    bool enabled = true;
    double amount = 0.0;      // % of the way to the target note; 0 = off (delayed pass-through)
    int key = 0;              // 0 = C ... 11 = B
    int scale = 0;            // index into kScaleNames
    double speedMs = 50.0;    // retune speed: 0 = instant (the hard trap effect) ... 400 = slow, natural
    double humanize = 0.0;    // %: long notes get a slower retune so they keep their life
    double formant = 0.0;     // semitones (-kMaxFormant .. +kMaxFormant): + thinner / younger, - deeper; 0 = your own
    int harmony = 0;          // 0 = correct the voice; else a harmony voice at kHarmonies[harmony] (see below)
    int mode = kPitchClassic; // PitchMode
    double vibrato = 0.0;     // % (Natural): -100 = flat, 0 = as sung, +100 = twice as deep
    double tuneCents = 0.0;   // the notes' tuning vs A = 440 Hz (-50 .. +50; from the beat)
};

/** Harmony intervals: scale steps when a key / scale is set (they stay in key), semitones with Chromatic. */
struct HarmonyInterval { const char* name; int steps; int semis; };
inline constexpr int kHarmonies = 8;
inline constexpr std::array<HarmonyInterval, kHarmonies> kHarmonyIntervals {{
    { "Off", 0, 0 }, { "3rd up", 2, 4 }, { "5th up", 4, 7 }, { "Octave up", 7, 12 },
    { "3rd down", -2, -3 }, { "4th down", -3, -5 }, { "5th down", -4, -7 }, { "Octave down", -7, -12 },
}};
inline constexpr double kMaxFormant = 6.0;

/** Pitch correction ("auto-tune") for a single voice.

    Detection: YIN (cumulative mean normalised difference) on a 12 kHz copy every ~2.7 ms, the
    period refined at full rate by normalised cross-correlation, with a voicing decision (YIN
    aperiodicity + level), so breaths and "s" sounds are never pitched.
    Decision: the nearest note of the key / scale, with hysteresis so a note doesn't flicker at the
    boundary. The correction (in semitones) glides to its target with the Retune Speed time constant,
    so slow speeds keep vibrato and only fix drift; Humanize slows it further on sustained notes.
    Harmony (params.harmony): the voice is pulled fully onto the note a chosen interval from the note
    you sing (in key), with no scoop at note starts: a backing voice. Formant (params.formant): each
    grain is read faster or slower than it's laid down, which moves the voice's resonances (deeper /
    thinner) without changing the notes.
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
    static bool isNeutral (const PitchParams& p) noexcept
    {
        return ! p.enabled || (p.amount < 0.05 && std::abs (p.formant) < 0.01 && p.harmony == 0);
    }
    /** A harmony voice's note: `note` (a note of the key / scale) moved by the interval, in key. */
    static int harmonyNote (int note, int harmony, int key, int scale) noexcept;
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
        double period = 0.0;       // samples (0 = unvoiced)
        double clarity = 0.0;      // 0..1, how clear the note is
        double time = 0.0;         // the input time (samples) this reading describes
    };
    Reading reading() const noexcept { return last; }

    /** Offline (Honey Tune): a plan made from the whole clip replaces the live note decisions. For an
        input time (samples) it gives the period (0 = unvoiced) and the shift in semitones. */
    struct Guide
    {
        virtual ~Guide() = default;
        virtual void at (double time, double& period, double& shiftSemis) const = 0;
    };
    void setGuide (const Guide* g) noexcept { guide = g; }

private:
    void analyse() noexcept;
    void guideFrame() noexcept;
    void synthesiseUpTo (int64_t limit) noexcept;
    double alignMark (double prevMark, double candidate, double P) const noexcept;
    double grainEnergy (double centre, double P) const noexcept;

    PitchParams params;
    const Guide* guide = nullptr;
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
    double noteAge = 0.0;              // seconds since this note started (voiced, same note)
    int jumpRun = 0;                   // Natural: readings in a row far from the centre (a new note)
    double centreA = 0.0, centreB = 0.0;  // Natural: the note's centre (two one-poles over the sung pitch)
    Biquad vibBp;                      // Natural: the vibrato band of the sung pitch
    std::array<double, 3> rawP {};     // newest period readings (median of three)
    double lastP = 0.0;                // previous period reading (octave guard)
    double clarityS = 1.0;             // smoothed note clarity (how much of the correction to apply)
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

    // Two bands (correcting a single voice): the grains carry only the low band (the notes); the airy
    // high band (breath, rasp, "s" in a note) is read smoothly at the grains' moving offset instead of
    // being chopped into grains - chopped noise repeats at the voice's pitch: a buzz.
    static constexpr double kSplitHz = 2000.0;
    std::array<std::vector<double>, 2> loBand;   // low band ring (high = in - lo)
    std::array<std::array<Biquad, 2>, 2> xover {};
    struct Mark { double pos = 0.0, off = 0.0, preOff = 0.0; bool join = false, split = false; };
    static constexpr int kMarks = 256;
    std::array<Mark, kMarks> marks {};
    int64_t markCount = 0, markRead = 0;
    double highAt (int c, double t) const noexcept;
    double highBand (int c, int64_t t) noexcept;
    static double cubicAt (const std::vector<double>& buf, int mask, double src) noexcept;

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
struct KeyGuess
{
    int key = 0; bool minor = true; double confidence = 0.0; double offCents = 0.0;
    /** A neighbouring key (a note apart) fits about as well: the voice alone can't tell them apart. */
    bool ambiguous = false; int altKey = 0; bool altMinor = false;
};
KeyGuess detectKey (const std::vector<double>& midiNotes);
/** The same from a pitch-class histogram (weights for C .. B); offCents is left 0. */
KeyGuess keyFromHistogram (const std::array<double, 12>& hist);

} // namespace vox
