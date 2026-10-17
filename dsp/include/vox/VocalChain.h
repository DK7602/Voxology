#pragma once

#include "ClipWatch.h"
#include "DeClick.h"
#include "Dimension.h"
#include "Denoise.h"
#include "DynamicEq.h"
#include "Modules.h"
#include "PopBreath.h"
#include "PitchCorrector.h"
#include "Saturation.h"
#include "Space.h"
#include "Throws.h"
#include "Voices.h"

#include <array>
#include <cmath>
#include <vector>

namespace vox {

/** Every setting of the chain (the plug-in fills this from its parameters each block). */
struct ChainParams
{
    PitchParams pitch;
    CleanupParams cleanup;
    EqParams eq;
    DynEqParams dynEq;
    DeEsserParams deEsser;
    RiderParams rider;
    CompParams comp;
    SaturationParams saturation;
    DoublerParams doubler;
    VoicesParams voices;      // harmonies (the VOICES module = doubler + harmonies; doubler.enabled switches both)
    DimensionParams dimension;
    ThrowParams throws;       // echo: Delay's Throw, wash: Reverb's Throw (each off with its module; time = the Delay's)
    DelayParams delay;
    ReverbParams reverb;
    double outputDb = 0.0;
    bool bypass = false;      // whole plug-in off (delayed dry, so the timing never jumps)
    bool listenOriginal = false;   // A/B: hear the untouched vocal (delayed to line up)
    double gainOriginalDb = 0.0, gainProcessedDb = 0.0;   // MATCH turn-downs (<= 0)
};

/** The modules in signal order, for the UI and the report. */
// (Dimension sits between Voices and Delay in the chain, but is numbered last here so reports saved before it keep
// their meaning.)
enum class Module { pitch = 0, cleanup, eq, dynEq, deEsser, rider, comp, saturation, doubler, delay, reverb, output, dimension, count };
inline constexpr int kModules = static_cast<int> (Module::count);

/** What the meters read since the last takeMeters() call. */
struct ChainMeters
{
    double gateDb = 0.0;        // deepest gate turn-down (<= 0)
    double noiseDb = 0.0;       // noise / room reduction: how far the sound was turned down on average (<= 0)
    double smoothDb = 0.0;      // Smooth: the deepest resonance cut (<= 0)
    double popDb = 0.0;         // deepest plosive cut (<= 0)
    double clickDb = 0.0;       // deepest mouth-click cut (<= 0)
    int clicks = 0;             // mouth clicks fixed
    int throws = 0;             // lines thrown (Throws)
    double breathDb = 0.0;      // deepest breath turn-down (<= 0)
    std::array<double, kDynBands> dynEqDb {};   // deepest Dynamic EQ cut per band (<= 0)
    double deEssDb = 0.0;       // deepest de-esser cut (<= 0)
    double riderDb = 0.0;       // rider gain now
    double peakGrDb = 0.0, levelGrDb = 0.0;   // compressor stages (<= 0)
    double satResidual = 0.0, satSignal = 0.0;   // saturation energies
    PitchCorrector::Reading pitch;                // what Pitch hears and does right now
    std::array<int, kMaxVoices> voiceNotes { -1, -1, -1, -1, -1, -1, -1, -1 };   // the harmony voices' notes (MIDI, -1 = none)
    ClipWatch::Counts input;                      // the incoming vocal: clipped stretches, samples over full scale
};

/** Voxology's vocal chain:
      Noise / Room reduction (Cleanup's, first so everything after hears the cleaner vocal) ->
      Pitch -> Cleanup (low cut, gate, pops, mouth clicks, breaths) -> Tone EQ -> Dynamic EQ -> De-Esser -> Rider -> Compressor -> Saturation      (inserts, linked)
      -> Doubler -> Delay -> Reverb (added to the vocal, stereo) -> Output gain
    Constant latency (the noise reducer's frame + Pitch look-ahead + Saturation oversampling; Record mode: no noise reducer). Framework-free: the plug-in, Auto-Edit and the
    tests all run this same class. prepare() allocates; process() never does (any block size). */
class VocalChain
{
public:
    static constexpr int kChunk = 256;
    /** The noise reducer's frame + Pitch's look-ahead + Saturation's oversampling; constant for a given sample rate. */
    int latencySamples() const noexcept { return (pitch.isLowLatency() ? 0 : denoise.latencySamples()) + pitch.latencySamples() + Saturation::kLatency; }

    /** lowLatency: Record mode (~6 ms instead of ~33): Pitch's low-latency shifter; harmony voices and
        Formant are off (they need the normal look-ahead). */
    void prepare (double sampleRate, int numChannels, bool lowLatency = false);
    void reset() noexcept;
    void setParams (const ChainParams& p) noexcept;
    const ChainParams& getParams() const noexcept { return params; }

    /** In place: channels hold the vocal in (mono or stereo) and the processed vocal out. */
    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        const int nch = std::min (numChannels, chanCount);
        for (int start = 0; start < numSamples; start += kChunk)
        {
            const int len = std::min (kChunk, numSamples - start);
            // A bad sample (NaN / inf) comes in as silence: in a filter or a delay line it would stay forever.
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < len; ++i)
                {
                    const auto x = static_cast<double> (channels[c][start + i]);
                    work[static_cast<size_t> (c)][static_cast<size_t> (i)] = std::isfinite (x) ? x : 0.0;
                }
            processChunk (nch, len);
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < len; ++i)
                    channels[c][start + i] = static_cast<Sample> (work[static_cast<size_t> (c)][static_cast<size_t> (i)]);
        }
    }

    ChainMeters takeMeters() noexcept;
    double getSampleRate() const noexcept { return sr; }

    /** Renders a whole clip offline (latency removed), for Auto-Edit and the tests. */
    static std::vector<std::vector<float>> render (const std::vector<std::vector<float>>& in, double sampleRate, const ChainParams& p);

private:
    void processChunk (int nch, int len) noexcept;

    ChainParams params;
    double sr = 48000.0;
    int chanCount = 2;
    ClipWatch clipWatch;   // watches the input (read-only)
    Denoiser denoise;
    PitchCorrector pitch;
    Cleanup cleanup;
    PopRemover pops;
    DeClick declick;
    BreathControl breaths;
    VocalEQ eq;
    DynamicEq dynEq;
    DeEsser deEsser;
    Rider rider;
    VocalCompressor comp;
    Saturation saturation;
    HarmonyVoices voices;
    Doubler doubler;
    Dimension dimension;
    Throws throws;
    EchoDelay delay;
    Reverb reverb;
    std::array<std::vector<double>, kMaxChannels> work;
    std::array<std::vector<double>, kMaxChannels> dryLine;   // latency-delayed input (bypass / A)
    int dryPos = 0;
    std::vector<double> mono, side;
    double outGain = 1.0, outGlide = 0.0;
    double mixB = 1.0, gainA = 1.0, gainB = 1.0, abGlide = 0.0;
    ChainMeters meters;
};

} // namespace vox
