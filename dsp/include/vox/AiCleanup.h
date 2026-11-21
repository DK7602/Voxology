#pragma once

#include "Modules.h"

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

/** AI noise and room cleanup: Cleanup's AI mode. DeepFilterNet3 (Schroeter, Rosenkranz, Escalante-B., Maier 2023;
    github.com/Rikorose/DeepFilterNet, MIT / Apache-2.0, see dsp/third_party/DeepFilterNet-LICENSE.txt), fine-tuned for
    sung and rapped vocals (docs/ai-cleanup/STEP2.md, round 3), run here without any framework. It takes out noise and
    the room's echo together.

    Every 10 ms a 20 ms frame (Vorbis window, 50 Hz bins at any sample rate: 960 points at 48 kHz, 882 at 44.1 kHz)
    gives the network 32 band levels (ERB bands, dB, minus their 1 s running average) and the lowest 96 bins
    (0 - 4.8 kHz, divided by their running level). It returns a gain per band (0..1) and a 5-tap complex filter per
    low bin that reaches 2 frames ahead; so the output is 2 frames late, plus the hop over which the network's work is
    spread (6 pieces, so no audio callback carries a whole frame of it): 50 ms in all (latencySamples()).
    Channels are linked: the network hears the mid, its gains and filter go on each channel.

    Safety net (STEP2.md "Root cause"): the network judges a faint hiss above 8 kHz added to what it hears (never
    to the sound), 28 dB under the voice's running level. Without it, dark clean held notes (nothing above 8 kHz)
    read as "noise only" and were turned down.

    Strength: how far it may turn the sound down (the processed and the untouched sound mixed so nothing goes more
    than that far down); kFullDb and up = no limit (how it was tested). Turning it on, off or changing it glides
    over about 0.1 s. Off: the input passes exactly (bit for bit), delayed, and costs almost nothing; so does digital
    silence while it's on (the network waits).
    Allocates only in prepare() (the network's weights once per process). */
namespace vox::ai {

/** Mixed-radix complex FFT (sizes made of small factors: 640, 882, 960, 1764, 1920 ...), unscaled, e^-i.
    Allocates only in prepare(). */
class Fft
{
public:
    void prepare (int size);
    int size() const noexcept { return n; }
    /** out = DFT (in); in and out must not overlap. */
    void forward (const std::complex<double>* in, std::complex<double>* out) noexcept;

private:
    void work (std::complex<double>* out, const std::complex<double>* in, size_t fstride, size_t stage) noexcept;
    int n = 0;
    std::vector<int> radix, span;                // per stage: the radix p and the length m left after it
    std::vector<std::complex<double>> tw, scratch;
};

/** The network (about 2.1 M weights, 3.5 M multiply-adds per frame), one 10 ms frame at a time. */
class Net
{
public:
    static constexpr int kErb = 32, kDf = 96, kOrder = 5, kLookahead = 2;

    Net();
    /** False if the built-in weights don't match the layout this code expects (then it isn't used). */
    bool ok() const noexcept;
    void reset() noexcept;
    /** erb: kErb features; spec: kDf (re, im) pairs -> mask: kErb gains (0..1); coefs: kDf x kOrder (re, im). */
    void step (const float* erb, const float* spec, float* mask, float* coefs) noexcept;
    /** The same in pieces, so one frame's work spreads over the next 10 ms of audio callbacks (none takes it all):
        begin() then stage (0 .. kStages - 1) in order; stage 3 writes mask, the last stage coefs. */
    static constexpr int kStages = 6;
    void begin (const float* erb, const float* spec) noexcept;
    void stage (int k, float* mask, float* coefs) noexcept;

private:
    // History of the inputs the convolutions look back over (frame rings), and the five GRU layers' states.
    std::vector<float> erbHist, specHist, c0Hist;
    int erbPos = 0, specPos = 0, c0Pos = 0, erbAt = 0, specAt = 0, c0At = 0;
    std::array<std::vector<float>, 5> h;
    // Work buffers.
    std::vector<float> e0, e1, e2, e3, c0, c1, tmp, tmp2, flat, emb, x256, gi, gh, y512, t3, t2, t1, cp, dfo;
};

} // namespace vox::ai

namespace vox {

struct AiCleanupParams
{
    bool on = false;
    double strengthDb = 0.0;   // how far it may turn the sound down; 0 = off, >= AiCleanup::kFullDb = no limit
};

class AiCleanup
{
public:
    static constexpr double kFullDb = 24.0;            // (the Noise knob's top in AI mode: no limit)
    static constexpr double kHissUnderVoiceDb = 28.0;  // the safety net's hiss, under the voice's running level
    static constexpr double kHissFromHz = 8000.0;

    /** The AI works at sample rates with a whole, even number of samples per 20 ms (32, 44.1, 48, 88.2, 96 kHz ...). */
    static bool supports (double sampleRate) noexcept;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;
    void setParams (const AiCleanupParams& p) noexcept { params = p; }
    /** Constant (2 frames + a hop, 50 ms), on or off, so the chain's timing never changes. */
    int latencySamples() const noexcept { return latency; }

    /** removed (optional): what it takes out (the input, latencySamples() late, minus the output). */
    void process (double* const* ch, int nch, int len, double* const* removed = nullptr) noexcept;

    /** How far it turned the sound down on average since the last call (dB, <= 0). */
    double takeReductionDb() noexcept;
    /** Working (on, or still gliding off)? */
    bool working() const noexcept { return state != State::idle; }
    /** Fully on at the strength asked for (or not asked to work): the chain hands over from the classic reducer then. */
    bool settled() const noexcept { return ! (params.on && params.strengthDb >= 0.05 && supported) || (state == State::running && amount >= goalAmount - 1e-9 && goalAmount > 0.0); }
    /** It can work here (a supported sample rate, the weights loaded). */
    bool available() const noexcept { return supported; }

    // For the tests.
    bool netOk() const noexcept { return net.ok(); }
    double hissLevelDb() const noexcept;
    void setHissForTests (bool on) noexcept { hissOn = on; }
    /** Start the network at once, full strength, as the Python reference does (no warm-up, no glide). */
    void startNowForTests() noexcept;
    int frameSize() const noexcept { return n; }
    long framesJudged() const noexcept { return judged; }   // network steps since reset

private:
    enum class State { idle, warming, running, closing };
    void frame() noexcept;
    void features() noexcept;
    void startNet() noexcept;
    double nextGauss() noexcept;

    AiCleanupParams params;
    double sr = 48000.0;
    int channels = 1, n = 960, hop = 480, bins = 481, latency = 1920;
    bool supported = true;
    State state = State::idle;
    int stateFrames = 0, netWarm = 0;
    double amount = 0.0;                                            // 1 - the strength limit's gain, gliding

    ai::Fft fft;
    ai::Net net;
    std::vector<double> window;
    std::array<std::vector<double>, kMaxChannels> frameIn, hopIn, ola, outFifo, dry;
    std::vector<double> hissFrame, hopMid;
    std::array<std::vector<std::complex<double>>, kMaxChannels> spec;   // the last kOrder spectra, ring
    std::vector<std::complex<double>> cin, cout, featSpec;
    int specPos = 0, pos = 0, dryPos = 0, channelsNow = 1, lastChannels = 1, passFrames = 0, nextStage = 0;
    int silentRun = 0;                         // exact zeros in a row on every channel (digital silence)
    long judged = 0;
    bool haveCoefs = false, stepping = false;   // stepping: the network's frame is being worked on over this hop
    double goalAmount = 0.0;

    // Features (the running averages start where DeepFilterNet's do) and the network's in / out.
    std::vector<double> erbEma, unitEma;
    std::vector<int> bandOf, bandWidth;
    std::vector<float> featErb, featDf, mask, coefs;

    // The safety net's hiss (Gaussian, twice through a 6th-order Butterworth high-pass) and the voice's level.
    bool hissOn = true;
    std::uint64_t rng = 0;
    double spare = 0.0;
    bool haveSpare = false;
    std::array<Biquad, 6> hissHp {};
    double hissNorm = 1.0, voicePow = 0.0, peakDb = -120.0;

    double sumIn = 0.0, sumOut = 0.0;   // the meter: dB summed, frames
};

} // namespace vox
