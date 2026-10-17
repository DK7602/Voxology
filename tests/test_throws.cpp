// Throws (v0.42): an echo / reverb wash on the last word of each line.
#include "Signals.h"
#include "vox/Fft.h"
#include "vox/Throws.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <random>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;

/** Sung lines: 1.6 s of "words" (a 220 Hz vowel, 0.25 s words with 40 ms dips between them), then 0.9 s of silence. */
std::vector<double> lines (double seconds)
{
    std::vector<double> x (static_cast<size_t> (seconds * kSr));
    std::mt19937 rng (5);
    std::normal_distribution<double> w (0.0, 1.0e-4);
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = std::fmod (static_cast<double> (i) / kSr, 2.5);
        double env = 0.0;
        if (t < 1.6)
        {
            const double inWord = std::fmod (t, 0.29);
            env = inWord < 0.25 ? std::min ({ 1.0, inWord / 0.01, (0.25 - inWord) / 0.02 }) : 0.0;
        }
        ph += 220.0 / kSr;
        double s = 0.0;
        for (int h = 1; h < 30; ++h) s += std::sin (2.0 * std::numbers::pi * h * ph) / h;
        x[i] = 0.1 * env * s + w (rng);
    }
    return x;
}

struct Run { std::array<std::vector<double>, 2> out; int throws = 0; };

Run run (const std::vector<double>& x, ThrowParams p, int64_t clock = 0)
{
    Throws t;
    t.setParams (p);
    t.prepare (kSr);
    if (clock > 0) t.startClockAt (clock);
    Run r;
    for (auto& o : r.out) o.assign (x.size(), 0.0);
    for (size_t s = 0; s < x.size(); s += 256)
    {
        const int n = static_cast<int> (std::min<size_t> (256, x.size() - s));
        std::array<double*, 2> o { r.out[0].data() + s, r.out[1].data() + s };
        t.process (x.data() + s, o.data(), 2, n);
        r.throws += t.takeThrows();
    }
    return r;
}

double rmsDb (const std::vector<double>& x, double a, double b)
{
    double e = 0.0;
    const auto i0 = static_cast<size_t> (a * kSr), i1 = static_cast<size_t> (b * kSr);
    for (size_t i = i0; i < i1; ++i) e += x[i] * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (i1 - i0) + 1.0e-30);
}
}

TEST_CASE ("Throws: off = silent; each line's last word echoes in the gap after it, on time", "[throws]")
{
    const auto x = lines (10.0);   // lines end at 1.6 (last word 1.45 - 1.6), 4.1, 6.6, 9.1 s
    ThrowParams off;
    const auto none = run (x, off);
    for (const auto& o : none.out) for (double v : o) REQUIRE (v == 0.0);

    ThrowParams p;
    p.echo = 60.0;
    p.division = 0;    // 1/4
    p.bpm = 120.0;     // 0.5 s
    const auto r = run (x, p);
    CHECK (r.throws == 4);
    for (int line = 0; line < 3; ++line)
    {
        const double end = 1.6 + 2.5 * line;
        INFO ("line " << line);
        // The last word (1.45 - 1.6) echoes at 1.95 - 2.1, then bounces right (2.45 - 2.6).
        const double word = rmsDb (x, end - 0.15, end - 0.02);
        const double e1 = std::max (rmsDb (r.out[0], end + 0.36, end + 0.48), rmsDb (r.out[1], end + 0.36, end + 0.48));
        // Nothing thrown mid-line: before the first line ends, silence; later, only the last line's repeats (ducked).
        if (line == 0) CHECK (rmsDb (r.out[0], end - 1.5, end) < -90.0);
        else CHECK (rmsDb (r.out[0], end - 1.0, end) < e1 - 15.0);
        INFO ("word " << word << " dB, first echo " << e1 << " dB");
        CHECK (e1 > word - 18.0);   // (high-passed at 250 Hz: the 220 Hz note's fundamental isn't in the echo)
        CHECK (e1 < word);
        CHECK (rmsDb (r.out[1], end + 0.86, end + 0.98) > e1 - 20.0);    // the next repeat, on the other side (ducked: the next line)
        CHECK (rmsDb (r.out[0], end + 0.02, end + 0.3) < e1 - 20.0);     // not before its time
    }
}

TEST_CASE ("Throws: Wash (reverb) after the line; switching off rings out, then exact", "[throws]")
{
    const auto x = lines (12.0);
    ThrowParams p;
    p.wash = 60.0;
    const auto r = run (x, p);
    CHECK (r.throws >= 3);
    CHECK (rmsDb (r.out[0], 2.0, 2.4) > -60.0);   // a wash after the first line
    // Switched off at 5 s: the wash rings out, then not a sample.
    Throws t;
    t.setParams (p);
    t.prepare (kSr);
    std::vector<double> o0 (x.size(), 0.0), o1 (x.size(), 0.0);
    for (size_t s = 0; s < x.size(); s += 256)
    {
        if (s >= static_cast<size_t> (5.0 * kSr)) t.setParams (ThrowParams {});
        const int n = static_cast<int> (std::min<size_t> (256, x.size() - s));
        std::array<double*, 2> o { o0.data() + s, o1.data() + s };
        t.process (x.data() + s, o.data(), 2, n);
    }
    for (size_t i = static_cast<size_t> (9.5 * kSr); i < x.size(); ++i) REQUIRE (o0[i] == 0.0);
}

TEST_CASE ("Throws: the same after hours of playing (64-bit clock past 2^31 samples, 12 h at 48 kHz)", "[throws]")
{
    // A 32-bit sample count (a long on Windows) went negative here and wrote outside the history ring.
    const auto x = lines (8.0);
    ThrowParams p;
    p.echo = 60.0;
    p.wash = 40.0;
    const auto fresh = run (x, p);
    const auto late = run (x, p, (int64_t { 1 } << 31) - static_cast<int64_t> (3.0 * kSr));
    CHECK (late.throws == fresh.throws);
    for (size_t c = 0; c < 2; ++c)
        for (size_t i = 0; i < x.size(); ++i) REQUIRE (late.out[c][i] == fresh.out[c][i]);
}

TEST_CASE ("Throws in the chain: off = untouched; Delay / Reverb off switch their throws off", "[throws]")
{
    const auto v = lines (8.0);
    const std::vector<float> xf (v.begin(), v.end());
    ChainParams p;
    const auto plain = VocalChain::render ({ xf }, kSr, p);
    p.throws.echo = 60.0;
    p.delay.enabled = false;
    const auto noDelay = VocalChain::render ({ xf }, kSr, p);
    for (size_t i = 0; i < plain[0].size(); ++i) REQUIRE (noDelay[0][i] == plain[0][i]);
    p.delay.enabled = true;
    const auto on = VocalChain::render ({ xf }, kSr, p);
    double diff = 0.0;
    for (size_t i = 0; i < plain[0].size(); ++i) diff = std::max (diff, std::abs (static_cast<double> (on[0][i] - plain[0][i])));
    CHECK (diff > 0.001);
}
