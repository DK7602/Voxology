#pragma once

#include "Modules.h"

#include <array>
#include <complex>
#include <vector>

/** Noise and room reduction (part of the Cleanup module; in the chain it runs FIRST, before Pitch, so
    everything after it, the gate's look-ahead included, hears the cleaner vocal).

    Short-time spectra (about 21 ms frames, 75 % overlap, square-root Hann windows): per frequency it learns
    the noise from the quietest moments (the minimum of the smoothed level over the last 1.5 s, corrected
    for the minimum's bias), so steady noise (fans, AC, hiss, computer hum) is found without a profile.
    Room: the late echo of the room is predicted from the level a moment earlier, decaying at the room's
    rate (Lebart / Habets: an exponential decay model, T60 from the Room setting). The gain per frequency is
    the log-spectral MMSE estimate (Ephraim & Malah) with the decision-directed SNR, which keeps the
    "musical noise" twinkle low; it never goes below the Noise / Room setting.

    Constant latency (one frame) whether it's on or off, so the chain's timing never changes; when both
    are 0 the audio passes through exactly (bit for bit), delayed. Channels are linked (one gain).

    Smooth (the Dynamic EQ's resonance smoother, soothe-style) rides on the same spectra, so it adds no delay. It finds
    resonances in the voice's long-term spectrum (kSmoothLearnSeconds while singing: the vowels' formants move and
    average out, a room / mic / nose resonance stays put): the tops of the harmonics (the loudest bin within ~100 Hz)
    against their average 1/6 - 1/2 an octave either side. Where one sticks out more than a few dB it's turned down by
    the excess while it rings (that frequency at or over its usual level), at most kMaxSmoothDb, 800 Hz - 7 kHz, only
    where the sound is well above the noise and while you sing. Quick down (one frame), back up over ~60 ms. Measured:
    a +10 dB ringing peak (Q 8) 5.9 -> 2 dB at 80 %; three real dry vocals at 60 %: most third octaves within 0.5 dB.
    Allocates only in prepare(). */
namespace vox {

struct DenoiseParams
{
    double noiseDb = 0.0;   // how far steady noise is turned down, 0..kMaxNoiseDb; 0 = off
    double roomPct = 0.0;   // how much of the room's echo is taken out, 0..100 %; 0 = off
    double smoothPct = 0.0; // resonance smoothing (Dynamic EQ's Smooth), 0..100 %; 0 = off
};

class Denoiser
{
public:
    static constexpr double kMaxNoiseDb = 24.0;
    static constexpr double kMaxRoomDb = 12.0;      // Room 100 % = the echo turned down by up to this
    static constexpr double kNoiseWindowSeconds = 1.5;
    static constexpr double kRoomDelaySeconds = 0.05;   // the echo predicted from the level this long before
    static constexpr double kRoomT60Seconds = 0.8;      // how long the echo it predicts takes to die away (only while the sound is fading)
    static constexpr double kMaxSmoothDb = 9.0;         // Smooth 100 %: a resonance turned down by up to this
    static constexpr double kSmoothLearnSeconds = 2.0;
    static constexpr double kSmoothFromHz = 800.0, kSmoothToHz = 7000.0;   // (above: air and "s", the De-Esser's job)

    /** The frame size for a sample rate (about 21 ms: 1024 at 44.1 / 48 kHz, 2048 at 88.2 / 96 kHz ...). */
    static int frameSize (double sampleRate) noexcept;
    int latencySamples() const noexcept { return n; }

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const DenoiseParams& p) noexcept { params = p; }
    static bool isNeutral (const DenoiseParams& p) noexcept { return p.noiseDb < 0.05 && p.roomPct < 0.05 && p.smoothPct < 0.05; }

    void process (double* const* ch, int nch, int len) noexcept;

    /** How far it turned the sound down on average since the last call (dB, <= 0), and the deepest. */
    double takeReductionDb() noexcept;
    /** Smooth: the deepest resonance cut since the last call (dB, <= 0). */
    double takeSmoothDb() noexcept { const double d = smoothDeepest; smoothDeepest = 0.0; return d; }
    /** The noise it has learned, per frequency bin (power, the frame's scale), for the tests. */
    const std::vector<double>& noiseEstimate() const noexcept { return noise; }
    double binHz() const noexcept { return sr / n; }

private:
    void processFrame (int nch) noexcept;

    DenoiseParams params;
    double sr = 48000.0;
    int channels = 1, n = 1024, hop = 256, bins = 513;
    int count = 0;
    std::array<std::vector<double>, kMaxChannels> inBuf, outBuf, delay;   // delay: the neutral path (the input, one frame late)
    std::vector<double> window;
    std::array<std::vector<std::complex<double>>, kMaxChannels> spec;
    std::vector<double> power, smooth, noise, curMin, prevGain, prevPost, gain, gainSm;
    std::vector<double> longAvg, lvlLong, topsLong, resDb;   // Smooth: long-term spectrum, its level and tops (dB), the cut (dB)
    double smoothDeepest = 0.0, smoothRelease = 0.0, longCoeff = 0.0;
    std::vector<std::vector<double>> subMin;   // the minimum of each sub-window (minimum statistics)
    std::vector<std::vector<double>> history;  // the smoothed level of the last frames (room echo prediction)
    std::vector<double> levelHistory;          // the overall level of the last ~150 ms (is the sound dying away?)
    int levelPos = 0;
    int subFrames = 36, subIndex = 0, subCount = 0, histPos = 0, roomFrames = 9, delayPos = 0;
    std::array<double, 8> lvlSubMin {};   // the frame level's sub-window minima (the "quiet moments" test)
    double lvlCurMin = 0.0;
    int lvlSubFrames = 117, lvlIndex = 0, lvlCount = 0, lvlFilled = 0;
    size_t loBin = 3, hiBin = 170;
    bool warm = false;
    double sumIn = 0.0, sumOut = 0.0;
};

} // namespace vox
