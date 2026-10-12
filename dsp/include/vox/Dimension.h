#pragma once

#include "Space.h"

#include <array>

/** Dimension: movement and width on the finished vocal (after the voices and the double, before the delay / reverb).

    Chorus: a copy of the voice on a slowly moving short delay (about 12 ms, +-1.5 ms), left and right moving
    opposite ways (Width): thicker and wider, like a second singer close by.
    Flanger: a very short delay (0.5 - 4.5 ms) swept up and down with feedback: the jet "whoosh".
    Phaser: six all-pass stages swept 300 Hz - 3 kHz with feedback: the swirling, watery sound.

    Amount: how much of the effect (0 = off, bit for bit). Speed: how fast it sweeps. Width: left and right apart.
    The level stays the same at any Amount (the mix is power-normalised). Switching on / off or changing mode fades
    (no clicks). No latency. */
namespace vox {

inline constexpr int kDimensionModes = 3;
inline constexpr std::array<const char*, kDimensionModes> kDimensionNames { "Chorus", "Flanger", "Phaser" };

struct DimensionParams
{
    bool enabled = true;
    int mode = 0;              // index into kDimensionNames
    double amount = 0.0;       // %, 0 = off
    double rateHz = 0.5;       // sweep speed
    double width = 70.0;       // %
};

class Dimension
{
public:
    static constexpr double kMinRateHz = 0.05, kMaxRateHz = 5.0;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const DimensionParams& p) noexcept { params = p; }
    static bool isNeutral (const DimensionParams& p) noexcept { return ! p.enabled || p.amount < 0.05; }
    /** ch: the vocal, 1 or 2 channels, changed in place. */
    void process (double* const* ch, int nch, int n) noexcept;

private:
    double wetSample (int c, double x, double lfo) noexcept;

    DimensionParams params;
    double sr = 48000.0;
    std::array<FracDelay, 2> line;
    std::array<std::array<double, 6>, 2> ap {};   // phaser all-pass states
    std::array<double, 2> fb {};                   // flanger / phaser feedback (last wet)
    double phase = 0.0;                            // LFO, 0 .. 1
    double mix = 0.0, mixCoeff = 0.0;              // the effect's level, gliding (fades)
    int mode = 0;                                  // the mode running now (a change fades out, switches, fades in)
    bool running = false;
};

} // namespace vox
