#pragma once

#include "Space.h"

#include <array>
#include <cstdint>
#include <vector>

/** Throws: an echo (and a reverb wash) on the last word of each line, the way engineers "throw" it by hand.

    It watches the vocal (mono, after the inserts) for a line ending: singing for at least kMinLineSeconds, then the
    level 24 dB under the line's loud part for kHoldSeconds. The last word (back to the last dip in level, at most
    kMaxWordSeconds) then goes into the throw echo as if it had been sent while it was sung: every part of it lands
    where its echo was due (the Delay's Time at the song's tempo), so the echo is on the beat even though the line's end
    is only known afterwards (an echo already due by then is skipped; the next repeat comes in at its level).
    Repeats: kFeedback, darker each time (kToneHz), bouncing left / right; quieter (-8 dB) if you start singing again.
    The echo also feeds a reverb wash (its own plate, kWashSeconds), for the "space" throw.
    Echo = its level (100 % = as loud as the word), Wash = the reverb's. Off = untouched, bit for bit. */
namespace vox {

struct ThrowParams
{
    double echo = 0.0;         // %, 0 = off
    double wash = 0.0;         // %, 0 = off
    int division = 0;          // index into kDelayBeats (the Delay's Time)
    double bpm = 120.0;
};

class Throws
{
public:
    static constexpr double kMinLineSeconds = 0.8, kHoldSeconds = 0.25, kMaxWordSeconds = 0.6, kQuietDb = 24.0;
    static constexpr double kFeedback = 0.45, kToneHz = 4500.0, kWashSeconds = 2.8;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setParams (const ThrowParams& p) noexcept { params = p; }
    static bool isNeutral (const ThrowParams& p) noexcept { return p.echo < 0.05 && p.wash < 0.05; }
    /** mono: the vocal; out: 1 or 2 channels, the throws are added. */
    void process (const double* mono, double* const* out, int nch, int n) noexcept;
    int takeThrows() noexcept { const int t = throws; throws = 0; return t; }
    /** Tests: reset, with the sample clock started at `sample` (as if that much had already played). */
    void startClockAt (int64_t sample) noexcept;

private:
    void block (double levelDb) noexcept;   // every 10 ms
    void throwWord (int64_t from, int64_t to) noexcept;

    ThrowParams params;
    double sr = 48000.0;
    int blockLen = 480;
    int64_t now = 0;                          // samples since reset (64-bit: a long is 32 on Windows, 12 h at 48 kHz)
    std::vector<double> hist;                 // the vocal, the last ~2 s
    std::vector<double> lineL, lineR;         // the echo (ping-pong), random access for the late "send"
    int pos = 0;                              // write position (time `now`)
    double delay = 0.0;                       // samples
    Biquad hp, lpL, lpR;
    Reverb wash;
    std::vector<double> washIn;
    // Line detector (10 ms levels)
    std::vector<double> levels;               // ring of block levels (dB), ~2 s
    int64_t blocks = 0;
    double blockE = 0.0;
    int blockFill = 0;
    bool inLine = false;
    int64_t lineStart = 0, lastLoud = 0;
    double linePeak = -120.0;
    int quiet = 0;
    // Output
    double echoGain = 0.0, washGain = 0.0, duck = 1.0, glide = 0.0;
    int64_t offFor = 0;                       // samples since switched off (the wash's tail rings out first)
    int throws = 0;
    bool running = false;
};

} // namespace vox
