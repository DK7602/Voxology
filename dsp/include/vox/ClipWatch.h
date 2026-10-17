#pragma once

#include <array>
#include <vector>

/** Clip watch: spots a vocal that arrives clipped or too hot. Read-only (the audio is not touched), no latency.

    Clipped: 3 or more samples in a row at exactly the same value, at any level (above -40 dBFS): the signature of
    hard clipping or a brick-wall stage before Voxology. Smooth audio in float never does that, but a smooth peak in
    16-bit audio can sit on one value for a few samples (rounding), so a flat top only counts when
      - it sits at the recent peak (within kAtPeak of it), with lower samples on both sides (nothing gets past a ceiling),
      - it starts and ends with a jump of more than kJumpSteps of the audio's finest step (a 16-bit step in a 16-bit
        file): a clipper cuts off a waveform that was still moving, a rounded peak's neighbours are a step or two away,
      - another flat top sat at the same level (a clipper's ceiling is the same every time; then both count).
    Too hot: samples over full scale (|x| > 1, possible in a 32-bit float mix). */
namespace vox {

class ClipWatch
{
public:
    static constexpr int kMinRun = 3;
    static constexpr double kAtPeak = 0.9;      // within -0.9 dB of the recent peak
    static constexpr double kJumpSteps = 6.0;

    struct Counts
    {
        int runs = 0;    // clipped stretches
        int overs = 0;   // samples over full scale
    };

    void prepare (int numChannels);
    void reset() noexcept;
    void process (const double* const* ch, int nch, int n) noexcept;
    Counts take() noexcept { const Counts c = counts; counts = {}; return c; }   // since the last call

private:
    struct Top { double level = 0.0; bool counted = false; };
    struct Channel
    {
        double prev = 0.0, beforePrev = 0.0, peak = 0.0;
        double step = 1.0;                       // the finest step between samples lately (1 = none seen yet)
        double lastStep = 0.0;
        int run = 0;                             // equal pairs so far in the current flat stretch
        double stepIn = 0.0, before = 0.0;       // ... the step into it and the sample before it
        bool atPeak = false;                     // ... it reached the recent peak
        std::array<Top, 4> tops {};              // the last flat tops' levels
        size_t nextTop = 0;
    };
    void flatTop (Channel& s, double level) noexcept;

    std::vector<Channel> chans;
    Counts counts;
};

} // namespace vox
