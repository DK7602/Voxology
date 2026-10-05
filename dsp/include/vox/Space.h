#pragma once

#include "Modules.h"

#include <array>
#include <vector>

/** The space effects: Doubler, Delay and Reverb. Each takes the (mono) vocal and ADDS its wet
    signal to the output channels (2 = stereo; 1 = folded to mono). Allocation only in prepare(). */
namespace vox {

/** A delay line read at a fractional position (linear interpolation). */
class FracDelay
{
public:
    void allocate (int maxSamples) { buf.assign (static_cast<size_t> (maxSamples + 4), 0.0); pos = 0; }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    void push (double x) noexcept { pos = (pos + 1) % static_cast<int> (buf.size()); buf[static_cast<size_t> (pos)] = x; }
    /** Sample written `delay` samples ago (delay >= 0, < size - 2). */
    double read (double delay) const noexcept
    {
        const int size = static_cast<int> (buf.size());
        const int d = static_cast<int> (delay);
        const double f = delay - d;
        int i0 = pos - d; if (i0 < 0) i0 += size;
        int i1 = i0 - 1; if (i1 < 0) i1 += size;
        return buf[static_cast<size_t> (i0)] * (1.0 - f) + buf[static_cast<size_t> (i1)] * f;
    }
    double readInt (int delay) const noexcept
    {
        const int size = static_cast<int> (buf.size());
        int i = pos - delay; if (i < 0) i += size;
        return buf[static_cast<size_t> (i)];
    }

private:
    std::vector<double> buf;
    int pos = 0;
};

// ------------------------------------------------------------------------------------------------
/** Doubler: two slightly late, slowly drifting copies of the vocal (like a second take), one
    each side. Amount = how loud the copies are (100 % = 3 dB under the lead); Width = how far apart. */
struct DoublerParams
{
    bool enabled = true;
    double amount = 0.0;   // %; 0 = off
    double width = 70.0;   // %
};

class Doubler
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const DoublerParams& p) noexcept { params = p; }
    static bool isNeutral (const DoublerParams& p) noexcept { return ! p.enabled || p.amount < 0.05; }
    void process (const double* mono, double* const* out, int nch, int n) noexcept;

private:
    DoublerParams params;
    double sr = 48000.0;
    FracDelay line;
    std::array<double, 2> phase {}, phase2 {};
    Biquad hpL, hpR, lpL, lpR;
    double level = 0.0, glide = 0.0;
};

// ------------------------------------------------------------------------------------------------
/** Delay (echo) locked to the song tempo. Feedback = how many repeats; Tone = how dark they get;
    Duck = turns the echoes down while you're singing so they fill the gaps instead of blurring the
    words; Ping-Pong bounces them left / right. */
inline constexpr int kDelayDivisions = 6;
inline constexpr std::array<double, kDelayDivisions> kDelayBeats { 1.0, 0.5, 0.75, 1.5, 0.25, 2.0 };
inline constexpr std::array<const char*, kDelayDivisions> kDelayNames { "1/4", "1/8", "1/8 dot", "1/4 dot", "1/16", "1/2" };

struct DelayParams
{
    bool enabled = true;
    int division = 0;          // index into kDelayBeats
    double feedback = 25.0;    // %
    double mix = 0.0;          // % wet level; 0 = off
    double toneHz = 6000.0;    // low-pass on the repeats
    double duck = 50.0;        // %
    bool pingPong = false;
    double bpm = 120.0;        // from the host
};

class EchoDelay
{
public:
    static constexpr double kMaxSeconds = 4.0;
    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const DelayParams& p) noexcept { params = p; }
    static bool isNeutral (const DelayParams& p) noexcept { return ! p.enabled || p.mix < 0.05; }
    static double delaySeconds (const DelayParams& p) noexcept;
    void process (const double* mono, double* const* out, int nch, int n) noexcept;

private:
    DelayParams params;
    double sr = 48000.0;
    FracDelay lineL, lineR;
    double curDelay = -1.0, delayGlide = 0.0;
    Biquad inHp, lpL, lpR;
    double designedTone = -1.0;
    double duckEnv = 0.0, duckAtk = 0.0, duckRel = 0.0, wet = 0.0, glide = 0.0;
    bool silent = true;
};

// ------------------------------------------------------------------------------------------------
/** Reverb: a smooth vocal plate (8-line feedback delay network after four diffusers).
    Decay = how long the tail lasts (RT60); Pre-delay keeps the words clear of the tail; Tone =
    how bright the tail is; Duck = tail drops while you sing and blooms in the gaps. The input is
    high-passed at 200 Hz so the reverb never muddies the low end. */
struct ReverbParams
{
    bool enabled = true;
    double decayS = 1.6;
    double predelayMs = 20.0;
    double mix = 0.0;          // %; 0 = off
    double toneHz = 7000.0;
    double duck = 30.0;        // %
};

class Reverb
{
public:
    static constexpr int kLines = 8;
    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const ReverbParams& p) noexcept { params = p; }
    static bool isNeutral (const ReverbParams& p) noexcept { return ! p.enabled || p.mix < 0.05; }
    void process (const double* mono, double* const* out, int nch, int n) noexcept;

private:
    void design() noexcept;

    struct Allpass { std::vector<double> buf; int pos = 0; double g = 0.6;
        double process (double x) noexcept
        {
            const double d = buf[static_cast<size_t> (pos)];
            const double y = -g * x + d;
            buf[static_cast<size_t> (pos)] = x + g * y;
            if (++pos >= static_cast<int> (buf.size())) pos = 0;
            return y;
        } };

    ReverbParams params;
    double sr = 48000.0;
    FracDelay pre;
    std::array<Allpass, 4> diffusers;
    std::array<std::vector<double>, kLines> lines;
    std::array<int, kLines> linePos {}, lineLen {};
    std::array<double, kLines> fbGain {}, lp {};
    double lpCoeff = 0.0;
    double designedDecay = -1.0, designedTone = -1.0;
    Biquad inHp;
    double duckEnv = 0.0, duckAtk = 0.0, duckRel = 0.0, wet = 0.0, glide = 0.0;
    double modPhase = 0.0;
    bool silent = true;
};

/** Gain (0..1) the delay / reverb use for ducking at a given dry envelope (mean square). */
double duckGain (double envMs, double duckPercent) noexcept;

} // namespace vox
