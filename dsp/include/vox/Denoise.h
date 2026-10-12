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
    Allocates only in prepare(). */
namespace vox {

struct DenoiseParams
{
    double noiseDb = 0.0;   // how far steady noise is turned down, 0..kMaxNoiseDb; 0 = off
    double roomPct = 0.0;   // how much of the room's echo is taken out, 0..100 %; 0 = off
};

class Denoiser
{
public:
    static constexpr double kMaxNoiseDb = 24.0;
    static constexpr double kMaxRoomDb = 12.0;      // Room 100 % = the echo turned down by up to this
    static constexpr double kNoiseWindowSeconds = 1.5;
    static constexpr double kRoomDelaySeconds = 0.05;   // the echo predicted from the level this long before
    static constexpr double kRoomT60Seconds = 0.8;      // how long the echo it predicts takes to die away (only while the sound is fading)

    /** The frame size for a sample rate (about 21 ms: 1024 at 44.1 / 48 kHz, 2048 at 88.2 / 96 kHz ...). */
    static int frameSize (double sampleRate) noexcept;
    int latencySamples() const noexcept { return n; }

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const DenoiseParams& p) noexcept { params = p; }
    static bool isNeutral (const DenoiseParams& p) noexcept { return p.noiseDb < 0.05 && p.roomPct < 0.05; }

    void process (double* const* ch, int nch, int len) noexcept;

    /** How far it turned the sound down on average since the last call (dB, <= 0), and the deepest. */
    double takeReductionDb() noexcept;
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
