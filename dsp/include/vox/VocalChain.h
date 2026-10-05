#pragma once

#include "DynamicEq.h"
#include "Modules.h"
#include "PitchCorrector.h"
#include "Saturation.h"
#include "Space.h"

#include <array>
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
    DelayParams delay;
    ReverbParams reverb;
    double outputDb = 0.0;
    bool bypass = false;      // whole plug-in off (delayed dry, so the timing never jumps)
    bool listenOriginal = false;   // A/B: hear the untouched vocal (delayed to line up)
    double gainOriginalDb = 0.0, gainProcessedDb = 0.0;   // MATCH turn-downs (<= 0)
};

/** The modules in signal order, for the UI and the report. */
enum class Module { pitch = 0, cleanup, eq, dynEq, deEsser, rider, comp, saturation, doubler, delay, reverb, output, count };
inline constexpr int kModules = static_cast<int> (Module::count);

/** What the meters read since the last takeMeters() call. */
struct ChainMeters
{
    double gateDb = 0.0;        // deepest gate turn-down (<= 0)
    std::array<double, kDynBands> dynEqDb {};   // deepest Dynamic EQ cut per band (<= 0)
    double deEssDb = 0.0;       // deepest de-esser cut (<= 0)
    double riderDb = 0.0;       // rider gain now
    double peakGrDb = 0.0, levelGrDb = 0.0;   // compressor stages (<= 0)
    double satResidual = 0.0, satSignal = 0.0;   // saturation energies
    PitchCorrector::Reading pitch;                // what Pitch hears and does right now
};

/** Voxology's vocal chain:
      Pitch -> Cleanup -> Tone EQ -> Dynamic EQ -> De-Esser -> Rider -> Compressor -> Saturation      (inserts, linked)
      -> Doubler -> Delay -> Reverb (added to the vocal, stereo) -> Output gain
    Constant latency (Pitch look-ahead + Saturation oversampling). Framework-free: the plug-in, Auto-Edit and the
    tests all run this same class. prepare() allocates; process() never does (any block size). */
class VocalChain
{
public:
    static constexpr int kChunk = 256;
    /** Pitch's look-ahead + Saturation's oversampling; constant for a given sample rate. */
    int latencySamples() const noexcept { return pitch.latencySamples() + Saturation::kLatency; }

    void prepare (double sampleRate, int numChannels);
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
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < len; ++i)
                    work[static_cast<size_t> (c)][static_cast<size_t> (i)] = static_cast<double> (channels[c][start + i]);
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
    PitchCorrector pitch;
    Cleanup cleanup;
    VocalEQ eq;
    DynamicEq dynEq;
    DeEsser deEsser;
    Rider rider;
    VocalCompressor comp;
    Saturation saturation;
    Doubler doubler;
    EchoDelay delay;
    Reverb reverb;
    std::array<std::vector<double>, kMaxChannels> work;
    std::array<std::vector<double>, kMaxChannels> dryLine;   // latency-delayed input (bypass / A)
    int dryPos = 0;
    std::vector<double> mono;
    double outGain = 1.0, outGlide = 0.0;
    double mixB = 1.0, gainA = 1.0, gainB = 1.0, abGlide = 0.0;
    ChainMeters meters;
};

} // namespace vox
