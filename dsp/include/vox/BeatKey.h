#pragma once

#include "Biquad.h"
#include "PitchCorrector.h"

#include <array>
#include <complex>
#include <vector>

namespace vox {

/** The key and tuning of a beat, heard live (Voxology in BEAT mode).

    Every quarter second it looks at the last ~0.7 s between 50 Hz and 2 kHz (a 6 kHz copy, 4096-point
    FFT): the spectral peaks that stand out from their neighbourhood (notes - the 808, chords, melody;
    drum hits are broad and mostly don't make peaks) are added to a pitch-class histogram, weighted by
    level, and their offsets from the nearest A = 440 Hz note give the beat's tuning. The histogram
    fades slowly (~40 s), so a whole song section decides.

    What tuning needs is the beat's NOTES: the 7-note (major-scale) set that holds most of the
    histogram (a new set must beat the current one clearly, so it doesn't flip). The home note
    (tonic) is the set's note the beat leans on most (its level plus a quarter of its fifth's): that
    names the key (B minor, E mixolydian ...), but never changes the notes.

    Audio thread: process() never allocates. */
class BeatKey
{
public:
    static constexpr int kFft = 4096;
    static constexpr double kRate = 6000.0;        // analysis rate (about: a whole decimation of the host rate)
    static constexpr double kMinSeconds = 6.0;     // of tonal music before a key is reported

    void prepare (double sampleRate);
    void reset() noexcept;
    void process (const double* mono, int n) noexcept;

    struct Result
    {
        bool ready = false;       // heard enough music
        int setRoot = 0;          // the notes: those of setRoot's major scale (0 = C ... 11 = B)
        int tonicOffset = 0;      // the home note, semitones above setRoot: 0 major, 2 dorian, 4 phrygian,
                                  // 5 lydian, 7 mixolydian, 9 minor, 11 locrian
        double confidence = 0.0;  // 0..1: how much of what's played fits those 7 notes
        bool unclear = false;     // the one note telling it from a neighbouring set is barely played
        double tuneCents = 0.0;   // vs A = 440 Hz (-50 .. +50)
        double heardSeconds = 0.0;
        int tonic() const noexcept { return (setRoot + tonicOffset) % 12; }
    };
    Result result() const noexcept { return res; }

private:
    void analyse() noexcept;

    double sr = 48000.0, fs = 6000.0;
    int dec = 8, decCount = 0;
    double decAcc = 0.0;
    Biquad aa1, aa2;
    std::vector<double> ring;          // decimated input, kFft long
    int pos = 0, sinceLast = 0, hop = 1500;
    std::vector<std::complex<double>> buf;
    std::vector<double> window, mag;
    std::array<double, 12> chroma {};
    double tuneRe = 0.0, tuneIm = 0.0; // tuning as a weighted circular mean (cents on a 100-cent circle)
    double fade = 1.0;                 // per analysis
    int currentSet = -1;
    Result res;
};

/** Pitch follows the beat: the beat gives the notes, your Scale choice picks where home is in them.
    A 7-note scale keeps exactly the beat's notes (Minor on its minor home, Dorian on its dorian home,
    ...); pentatonics are those notes' major / minor pentatonic; Harmonic Minor and Blues sit on the
    minor home; Chromatic becomes the beat's own key (its tonic and mode). */
inline constexpr std::array<int, kScales> kScaleHomeOffset { -1, 0, 9, 9, 9, 0, 2, 4, 7, 9 };
inline void followBeatKey (const BeatKey::Result& beat, int userScale, int& key, int& scale) noexcept
{
    if (userScale > 0 && userScale < kScales)
    {
        scale = userScale;
        key = (beat.setRoot + kScaleHomeOffset[static_cast<size_t> (userScale)]) % 12;
        return;
    }
    // Chromatic: the beat's own key. Lydian / locrian have no scale of their own here: the major set.
    switch (beat.tonicOffset)
    {
        case 9: scale = 2; break;   // minor
        case 2: scale = 6; break;   // dorian
        case 4: scale = 7; break;   // phrygian
        case 7: scale = 8; break;   // mixolydian
        default: scale = 1; key = beat.setRoot; return;
    }
    key = beat.tonic();
}

/** "B minor", "E mixolydian" ... */
inline const char* modeName (int tonicOffset) noexcept
{
    switch (tonicOffset)
    {
        case 0: return "major"; case 2: return "dorian"; case 4: return "phrygian"; case 5: return "lydian";
        case 7: return "mixolydian"; case 9: return "minor"; default: return "locrian";
    }
}

} // namespace vox
