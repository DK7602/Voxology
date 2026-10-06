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
    fades slowly (~40 s), so a whole song section decides and a key change is followed in time. The
    key comes from the same major / minor profiles as the voice (keyFromHistogram).

    Audio thread: process() never allocates. Results are read from the same thread. */
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
        KeyGuess key;
        double tuneCents = 0.0;   // vs A = 440 Hz (-50 .. +50)
        double heardSeconds = 0.0;
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
    Result res;
};

/** Pitch follows the beat: the beat gives the key's notes, your Scale choice keeps its flavour. A
    minor-type scale (Minor, Harmonic Minor, Minor Pentatonic, Dorian, Phrygian, Blues) sits on the
    beat's minor tonic, a major-type one (Major, Major Pentatonic, Mixolydian) on its major tonic
    (A minor and C major are the same notes); Chromatic becomes the beat's own Major / Minor. */
inline constexpr std::array<bool, kScales> kMinorTypeScale { false, false, true, true, true, false, true, true, false, true };
inline void followBeatKey (const KeyGuess& beat, int userScale, int& key, int& scale) noexcept
{
    const int minorTonic = beat.minor ? beat.key : (beat.key + 9) % 12;
    const int majorTonic = beat.minor ? (beat.key + 3) % 12 : beat.key;
    if (userScale <= 0 || userScale >= kScales) { key = beat.key; scale = beat.minor ? 2 : 1; return; }
    scale = userScale;
    key = kMinorTypeScale[static_cast<size_t> (userScale)] ? minorTonic : majorTonic;
}

} // namespace vox
