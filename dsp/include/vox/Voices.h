#pragma once

#include "Modules.h"
#include "PitchCorrector.h"

#include <array>
#include <vector>

/** Harmony voices: up to kMaxVoices backing voices sung from the lead.

    Which voices: Voice 1 and Voice 2, each a chosen interval away in the Pitch module's key / scale (Chromatic =
    plain intervals) or Unison; plus Stack, 2 / 4 / 6 more unison singers spread around the lead (the stacked-hook
    sound). Or, with MIDI on, the notes you hold on a keyboard (up to kMaxVoices), whatever you sing: a note you keep
    holding stays on its voice; a chord change slides each released voice to the nearest new note (up to kMaxGlide
    semitones); any other new note starts a voice from silence (it warms up quietly for its latency, then fades in);
    a released voice fades out (no clicks).

    Timing: they're made from the chain's INPUT (before Pitch), which is Pitch's latency ahead of the
    lead; each voice's own pitch shifter has that same latency, so they land in time with the lead.
    A short delay (Saturation's latency) lines them up exactly with the finished lead.
    Unison (voice 1 / 2 and the stack): the lead's own notes, tuned onto them, as more singers: each a few cents off
    the lead and a little later (like a double), so they widen the lead instead of combing with it.
    Sound: each voice is a PitchCorrector in harmony mode (full shift, no scoop, formant control),
    then the lead's own clean-up low cut, Tone EQ and De-Esser, then its level follows the finished
    lead's (so the voices get the lead's compression and never jump out), times Level. With more than two voices
    each is a little quieter (sqrt (2 / voices)), so a stack sounds bigger, not just louder. Panned across Width.
    Crew: each voice its own singer, by the amount: its own formant (deeper / brighter voice), tone (a high shelf at
    2.5 kHz), timing (later) and tuning (cents off). 0 = as without it, bit for bit. A timing change cross-fades
    between the old and new delay (30 ms), so moving it never clicks or bends the pitch. */
namespace vox {

inline constexpr int kMaxVoices = 8;
inline constexpr std::array<int, 4> kStackSizes { 0, 2, 4, 6 };

struct VoicesParams
{
    std::array<int, 2> interval { 0, 0 };   // Voice 1 / 2: indices into kHarmonyIntervals (0 = off)
    double level = 50.0;                    // % of the lead's level
    double formant = 0.0;                   // semitones, the voices' own formant (- = deeper, darker backing)
    double width = 70.0;                    // % how far left / right
    int stack = 0;                          // index into kStackSizes: 0 / 2 / 4 / 6 more unison singers
    bool midi = false;                      // the voices sing the MIDI notes you hold (instead of Voice 1 / 2 / Stack)
    double crew = 0.0;                      // %: each voice its own singer (formant, tone, timing, tuning)
    std::array<int, kMaxVoices> notes { -1, -1, -1, -1, -1, -1, -1, -1 };   // MIDI: the held notes, lowest first (-1 = none)
};

class HarmonyVoices
{
public:
    static constexpr double kFollowSeconds = 0.030;
    static constexpr double kMaxFollowDb = 24.0;
    static constexpr std::array<double, 2> kUnisonCents { 9.0, -9.0 };
    static constexpr std::array<double, 2> kUnisonMs { 19.0, 27.0 };
    /** The stack's singers: cents off the lead, how much later (ms), and where (-1 left .. +1 right, times Width). */
    static constexpr std::array<double, 6> kStackCents { 14.0, -14.0, 6.0, -6.0, 20.0, -20.0 };
    static constexpr std::array<double, 6> kStackMs { 23.0, 31.0, 15.0, 35.0, 11.0, 39.0 };
    static constexpr std::array<double, 6> kStackPan { -0.6, 0.6, -0.9, 0.9, -0.3, 0.3 };
    /** MIDI voices: a little looser than a keyboard chord (each slightly off and later, like separate singers). */
    static constexpr std::array<double, kMaxVoices> kMidiCents { 3.0, -3.0, 5.0, -5.0, 2.0, -2.0, 4.0, -4.0 };
    static constexpr std::array<double, kMaxVoices> kMidiMs { 0.0, 7.0, 13.0, 4.0, 10.0, 2.0, 15.0, 9.0 };
    /** Crew at 100 %, per voice: formant (semitones; - deeper), tone (dB above 2.5 kHz), later (ms), tuning (cents).
        Deeper voices are a little darker, like real ones. */
    static constexpr std::array<double, kMaxVoices> kCrewFormant { -2.5, 2.0, -1.5, 3.0, -3.0, 1.5, -1.0, 2.5 };
    static constexpr std::array<double, kMaxVoices> kCrewToneDb { -3.0, 2.0, -2.0, 3.0, -4.0, 1.0, -1.0, 2.0 };
    static constexpr std::array<double, kMaxVoices> kCrewMs { 12.0, 4.0, 20.0, 8.0, 16.0, 2.0, 24.0, 10.0 };
    static constexpr std::array<double, kMaxVoices> kCrewCents { 6.0, -5.0, -8.0, 4.0, 9.0, -3.0, -6.0, 7.0 };
    static constexpr double kCrewToneHz = 2500.0;
    static constexpr double kMaxLateMs = 70.0;
    static constexpr double kFadeSeconds = 0.020;   // a voice starting / stopping (a MIDI note on / off)
    static constexpr int kMaxGlide = 5;             // semitones: a released voice slides to a new note this close

    void prepare (double sampleRate);
    void reset() noexcept;
    /** The lead's settings the voices share: key / scale and retune speed (Pitch), clean-up, EQ, de-esser. */
    void setParams (const VoicesParams& v, const PitchParams& lead, double lowCutHz, const EqParams& eq, const DeEsserParams& ds) noexcept;
    static int activeVoices (const VoicesParams& v) noexcept;
    static bool isNeutral (const VoicesParams& v) noexcept { return activeVoices (v) == 0 || v.level < 0.05; }
    int latencySamples() const noexcept { return voice[0].latencySamples(); }

    /** side: the chain's mono input (Pitch's latency ahead of `ch`). ch: the finished lead (1 or 2
        channels), the voices are added to it; mono gets them too (for the delay / reverb). */
    void process (const double* side, double* const* ch, int nch, double* mono, int n) noexcept;

    /** The note each voice is singing right now (MIDI, -1 = none), for the UI. */
    std::array<int, kMaxVoices> currentNotes() const noexcept;

private:
    /** What each voice slot does this block (from the params). */
    struct Slot
    {
        bool on = false;
        int harmony = 0, fixedNote = -1;
        double cents = 0.0, lateMs = 0.0, pan = 0.0;
        bool operator== (const Slot&) const = default;
    };
    static std::array<Slot, kMaxVoices> slotsFor (const VoicesParams& v) noexcept;   // Voice 1 / 2 + Stack
    std::array<Slot, kMaxVoices> midiSlots (const VoicesParams& v) const noexcept;    // the held notes, on stable voices

    VoicesParams params;
    std::array<Slot, kMaxVoices> slots {};       // what each voice should do now
    std::array<Slot, kMaxVoices> sing {};        // what each voice sings: its slot, kept while it fades out
    std::array<double, kMaxVoices> slotGain {};  // each voice's fade in / out (0 = silent)
    std::array<int, kMaxVoices> warm {};         // a voice started from silence: samples until it's heard
    std::array<int, kMaxVoices> hold {};         // a voice let go of: samples until its fade (its latency: on time)
    double fadeStep = 0.0, smoothCoeff = 0.0;
    double sr = 48000.0;
    std::array<PitchCorrector, kMaxVoices> voice;
    std::array<std::vector<double>, kMaxVoices> buf;          // a block of each voice
    std::array<std::array<Biquad, 2>, kMaxVoices> lowCut {};  // per voice, two stages
    double designedLowCut = -1.0, lowCutHz = 20.0;
    VocalEQ eq;
    DeEsser deEsser;
    std::vector<double> align;                       // Saturation's latency, so they match the finished lead
    int alignPos = 0;
    std::array<double, kMaxVoices> voiceMs {};
    std::array<std::vector<double>, kMaxVoices> late;        // each voice a little after the lead (unison, Crew)
    std::array<int, kMaxVoices> latePos {}, lateA {}, lateB {}, lateTarget {};   // delays (samples): fading from A to B
    std::array<double, kMaxVoices> lateXf {};                // 1 = on B
    double lateXfStep = 0.0;
    std::array<Biquad, kMaxVoices> tone {};                  // Crew: each voice's tone
    std::array<double, kMaxVoices> toneDb {};                // (as designed; 0 = not filtered)
    double leadMs = 0.0, leadPeak = 0.0, peakFall = 0.0, follow = 0.0, followCoeff = 0.0, gain = 0.0, gainCoeff = 0.0;
    std::array<double, kMaxVoices> panL {}, panR {}, panLTarget {}, panRTarget {};   // (smoothed: a MIDI voice can move)
    double voiceWeight = 1.0, weightTarget = 1.0;     // sqrt (2 / voices): more voices, each a little quieter
    bool running = false;
    std::vector<double> scratchL, scratchR;
};

} // namespace vox
