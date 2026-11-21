// Cleanup's AI mode (vox::AiCleanup): the network runs here exactly as the fine-tuned model does in PyTorch, it keeps dark held
// notes, takes noise out, passes the input bit for bit when off, and fits a live chain (constant latency, no allocation:
// test_realtime.cpp). Reference output: tools/ai_cleanup/make_ref.py.
#include "Signals.h"
#include "vox/AiCleanup.h"
#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {

/** Plays x (mono) through ai in blocks; returns the output with the latency removed (x's length). */
std::vector<double> play (AiCleanup& ai, const std::vector<double>& x, int block = 256)
{
    const int lat = ai.latencySamples();
    std::vector<double> buf (x.size() + static_cast<size_t> (lat), 0.0);
    std::copy (x.begin(), x.end(), buf.begin());
    for (size_t s = 0; s < buf.size(); s += static_cast<size_t> (block))
    {
        double* p = buf.data() + s;
        ai.process (&p, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), buf.size() - s)));
    }
    return { buf.begin() + lat, buf.end() };
}

std::vector<double> toDouble (const std::vector<float>& x) { return { x.begin(), x.end() }; }

/** A held sung note: harmonics up to `top` Hz only (a dark, clean take: nothing at all above), 5 Hz vibrato, RMS levelDb
    while held; silence (true zeros) for 0.5 s before and 0.3 s after. */
std::vector<double> heldNote (double sr, double f0, double top, double levelDb, double seconds = 3.0)
{
    const auto n = static_cast<size_t> (sr * seconds);
    std::vector<double> x (n, 0.0);
    double sumSq = 0.0;
    size_t held = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double env = std::clamp (std::min ((t - 0.5) / 0.05, (seconds - 0.3 - t) / 0.05), 0.0, 1.0);
        if (env <= 0.0) continue;
        const double ph = 2.0 * std::numbers::pi * f0 * t + 0.01 * f0 / 5.0 * std::sin (2.0 * std::numbers::pi * 5.0 * t);
        double v = 0.0;
        for (int k = 1; k * f0 < top; ++k) v += std::sin (k * ph) / std::pow (k, 1.2);
        x[i] = v * env;
        if (env >= 1.0) { sumSq += x[i] * x[i]; ++held; }
    }
    const double g = std::pow (10.0, levelDb / 20.0) / std::sqrt (sumSq / static_cast<double> (std::max<size_t> (held, 1)));
    for (auto& v : x) v *= g;
    return x;
}

/** Share of voiced 50 ms moments of `clean` that `out` turns down more than 6 dB (after matching the overall level). */
double notesLost (const std::vector<double>& clean, const std::vector<double>& out, double sr)
{
    const auto w = static_cast<size_t> (sr * 0.05);
    std::vector<double> ce, oe;
    for (size_t s = 0; s + w <= clean.size(); s += w)
    {
        double a = 0.0, b = 0.0;
        for (size_t i = s; i < s + w; ++i) { a += clean[i] * clean[i]; b += out[i] * out[i]; }
        ce.push_back (a); oe.push_back (b);
    }
    const double top = *std::max_element (ce.begin(), ce.end());
    double sc = 0.0, so = 0.0;
    for (size_t k = 0; k < ce.size(); ++k) if (ce[k] > top * 1e-3) { sc += ce[k]; so += oe[k]; }
    const double g = sc / std::max (so, 1e-30);
    int voiced = 0, lost = 0;
    for (size_t k = 0; k < ce.size(); ++k)
        if (ce[k] > top * 1e-3) { ++voiced; if (10.0 * std::log10 (oe[k] * g / ce[k] + 1e-20) < -6.0) ++lost; }
    return voiced > 0 ? static_cast<double> (lost) / voiced : 0.0;
}

double rmsDbOf (const std::vector<double>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += x[i] * x[i];
    return 10.0 * std::log10 (s / static_cast<double> (std::max<size_t> (to - from, 1)) + 1e-30);
}

} // namespace

TEST_CASE ("AI cleanup: the built-in network loads, and the FFT is exact at every frame size", "[ai]")
{
    AiCleanup ai;
    ai.prepare (48000.0, 1);
    REQUIRE (ai.netOk());

    for (const int n : { 640, 882, 960, 1764, 1920 })
    {
        ai::Fft f;
        f.prepare (n);
        std::vector<std::complex<double>> in (static_cast<size_t> (n)), out (static_cast<size_t> (n));
        testsig::Rng r (static_cast<unsigned> (n));
        for (auto& v : in) v = { r.normal(), r.normal() };
        f.forward (in.data(), out.data());
        double err = 0.0, mag = 0.0;
        for (int k = 0; k < n; k += 7)   // (every 7th bin against the plain sum)
        {
            std::complex<double> s {};
            for (int i = 0; i < n; ++i) s += in[static_cast<size_t> (i)] * std::polar (1.0, -2.0 * std::numbers::pi * static_cast<double> ((static_cast<long> (i) * k) % n) / n);
            err = std::max (err, std::abs (s - out[static_cast<size_t> (k)]));
            mag = std::max (mag, std::abs (s));
        }
        INFO ("n = " << n);
        CHECK (err < 1e-9 * mag);
    }
}

TEST_CASE ("AI cleanup: the same as the fine-tuned model in PyTorch, frame for frame", "[ai]")
{
    // tests/data/ai_ref_48k.f32 (tools/ai_cleanup/make_ref.py): the same network (float16 weights) in PyTorch on this input,
    // from its first frame, without the safety net's hiss.
    std::ifstream f (VOX_TEST_DATA "/ai_ref_48k.f32", std::ios::binary);
    REQUIRE (f.good());
    std::vector<float> ref (48000);
    f.read (reinterpret_cast<char*> (ref.data()), static_cast<std::streamsize> (ref.size() * sizeof (float)));
    REQUIRE (f.gcount() == static_cast<std::streamsize> (ref.size() * sizeof (float)));

    constexpr double sr = 48000.0;
    std::vector<double> x (static_cast<size_t> (sr * 1.2));
    std::uint32_t s = 12345;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double ph = 2.0 * std::numbers::pi * 220.0 * t + (0.02 * 220.0 / 5.0) * std::sin (2.0 * std::numbers::pi * 5.0 * t);
        const double env = std::clamp (std::min ((t - 0.1) / 0.02, (1.1 - t) / 0.02), 0.0, 1.0);
        double v = 0.0;
        for (int k = 1; k <= 20; ++k) v += 0.1 / k * std::sin (k * ph);
        s = s * 1664525u + 1013904223u;
        x[i] = v * env + 0.01 * (static_cast<double> (s) / 4294967296.0 * 2.0 - 1.0);
    }
    AiCleanup ai;
    ai.prepare (sr, 1);
    ai.setHissForTests (false);
    ai.setParams ({ true, AiCleanup::kFullDb });
    ai.startNowForTests();
    const auto y = play (ai, x);
    double err = 0.0, errSq = 0.0, sigSq = 0.0;
    for (size_t i = 0; i < ref.size(); ++i)
    {
        const double d = y[i] - ref[i];
        err = std::max (err, std::abs (d));
        errSq += d * d;
        sigSq += static_cast<double> (ref[i]) * ref[i];
    }
    INFO ("max |difference| " << err << ", " << 10.0 * std::log10 (errSq / sigSq) << " dB under the signal");
    CHECK (err < 2e-3);
    CHECK (10.0 * std::log10 (errSq / sigSq) < -60.0);
}

TEST_CASE ("AI cleanup: off, the input comes out bit for bit, 2 frames (40 ms) late, at any supported rate", "[ai]")
{
    for (const double sr : { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        INFO ("rate " << sr);
        REQUIRE (AiCleanup::supports (sr));
        AiCleanup ai;
        ai.prepare (sr, 2);
        CHECK (ai.latencySamples() == static_cast<int> (std::lround (sr * 0.04)));
        CHECK (ai.frameSize() == static_cast<int> (std::lround (sr / 50.0)));
        const auto x = toDouble (testsig::vocal (sr, 1.0, -50.0));
        for (const AiCleanupParams p : { AiCleanupParams { false, 24.0 }, AiCleanupParams { true, 0.0 } })
        {
            ai.reset();
            ai.setParams (p);
            const auto y = play (ai, x, 333);
            bool same = true;
            for (size_t i = 0; i < x.size(); ++i) same = same && y[i] == x[i];
            CHECK (same);
            CHECK (! ai.working());
        }
    }
    CHECK (! AiCleanup::supports (22050.0));   // (441 samples per 20 ms: odd)
    CHECK (! AiCleanup::supports (44056.0));
}

TEST_CASE ("AI cleanup: dark, clean held notes are kept (the safety net), at any level", "[ai]")
{
    // The failure the safety net fixes (docs/ai-cleanup/STEP2.md, "Root cause"): a clean take with nothing above 8 kHz
    // read as "noise only". Female and male notes, 2.5 - 8 kHz of harmonics, loud and quiet.
    for (const double sr : { 44100.0, 48000.0 })
        for (const auto& [f0, top] : { std::pair { 330.0, 4000.0 }, std::pair { 523.0, 6000.0 }, std::pair { 196.0, 2500.0 }, std::pair { 262.0, 8000.0 } })
            for (const double level : { -18.0, -36.0 })
            {
                INFO (sr << " Hz, note " << f0 << " Hz, harmonics to " << top << " Hz, " << level << " dBFS");
                const auto x = heldNote (sr, f0, top, level);
                AiCleanup ai;
                ai.prepare (sr, 1);
                ai.setParams ({ true, AiCleanup::kFullDb });
                const auto y = play (ai, x);
                CHECK (notesLost (x, y, sr) == 0.0);
                // (Within 2.5 dB: a test note is a perfectly even buzz, which the AI turns down a little; a voice isn't.)
                const auto a = static_cast<size_t> (sr * 1.0), b = static_cast<size_t> (sr * 2.5);
                CHECK (rmsDbOf (y, a, b) == Approx (rmsDbOf (x, a, b)).margin (2.5));
            }
}

TEST_CASE ("AI cleanup: takes steady noise out between and under the words, and keeps the voice", "[ai]")
{
    // A voice (words, a breath gap every 2.08 s) with steady hiss 32 dB under it, and the same voice without it.
    for (const double sr : { 44100.0, 48000.0 })
    {
        INFO (sr << " Hz");
        const auto clean = toDouble (testsig::vocal (sr, 4.5, -200.0));
        const auto x = toDouble (testsig::vocal (sr, 4.5, -50.0));
        AiCleanup ai;
        ai.prepare (sr, 1);
        ai.setParams ({ true, AiCleanup::kFullDb });
        const auto y = play (ai, x);
        const auto gapFrom = static_cast<size_t> (sr * 3.65), gapTo = static_cast<size_t> (sr * 4.05);   // the second breath gap
        INFO ("gap: " << rmsDbOf (x, gapFrom, gapTo) << " dB in, " << rmsDbOf (y, gapFrom, gapTo) << " dB out");
        CHECK (rmsDbOf (y, gapFrom, gapTo) < rmsDbOf (x, gapFrom, gapTo) - 15.0);
        CHECK (notesLost (clean, y, sr) < 0.02);
        const auto a = static_cast<size_t> (sr * 2.1), b = static_cast<size_t> (sr * 3.5);   // the second line's words
        CHECK (rmsDbOf (y, a, b) == Approx (rmsDbOf (clean, a, b)).margin (1.5));
        CHECK (ai.takeReductionDb() < -0.5);

        // Strength 6 dB: the untouched sound mixed in, so nothing goes more than 6 dB down (and the gap close to that).
        AiCleanup gentle;
        gentle.prepare (sr, 1);
        gentle.setParams ({ true, 6.0 });
        const auto z = play (gentle, x);
        CHECK (rmsDbOf (z, gapFrom, gapTo) >= rmsDbOf (x, gapFrom, gapTo) - 6.2);
        CHECK (rmsDbOf (z, gapFrom, gapTo) <= rmsDbOf (x, gapFrom, gapTo) - 4.5);
    }
}

TEST_CASE ("AI cleanup: any block size gives the same output; on and off glide; stereo is linked", "[ai]")
{
    constexpr double sr = 44100.0;
    const auto x = toDouble (testsig::vocal (sr, 3.0, -45.0));
    std::vector<std::vector<double>> outs;
    for (const int block : { 1, 17, 512, 4096 })
    {
        AiCleanup ai;
        ai.prepare (sr, 1);
        ai.setParams ({ true, 18.0 });
        outs.push_back (play (ai, x, block));
    }
    for (size_t k = 1; k < outs.size(); ++k) CHECK (outs[k] == outs[0]);

    // On, then off half way: no jump bigger than the signal's own, and back to the exact input (delayed) after the glide.
    AiCleanup ai;
    ai.prepare (sr, 1);
    ai.setParams ({ true, AiCleanup::kFullDb });
    const int lat = ai.latencySamples();
    std::vector<double> buf (x.size() + static_cast<size_t> (lat), 0.0);
    std::copy (x.begin(), x.end(), buf.begin());
    const size_t half = buf.size() / 2;
    double* p = buf.data();
    ai.process (&p, 1, static_cast<int> (half));
    CHECK (ai.working());
    ai.setParams ({ false, 0.0 });
    p = buf.data() + half;
    ai.process (&p, 1, static_cast<int> (buf.size() - half));
    CHECK (! ai.working());
    for (const double v : buf) REQUIRE (std::isfinite (v));
    const size_t tailFrom = half + static_cast<size_t> (sr * 0.5);   // well after the glide off
    bool exact = true;
    for (size_t i = tailFrom; i < buf.size(); ++i) exact = exact && buf[i] == x[i - static_cast<size_t> (lat)];
    CHECK (exact);

    // Stereo: the same vocal in both channels comes out the same in both, and as it does in mono.
    AiCleanup st, mono;
    st.prepare (sr, 2); mono.prepare (sr, 1);
    st.setParams ({ true, AiCleanup::kFullDb }); mono.setParams ({ true, AiCleanup::kFullDb });
    std::vector<double> l (x), r (x), m (x);
    double* lr[2] = { l.data(), r.data() };
    double* mm[1] = { m.data() };
    st.process (lr, 2, static_cast<int> (x.size()));
    mono.process (mm, 1, static_cast<int> (x.size()));
    double d = 0.0;
    for (size_t i = 0; i < x.size(); ++i) d = std::max ({ d, std::abs (l[i] - r[i]), std::abs (l[i] - m[i]) });
    CHECK (d < 1e-12);
}

TEST_CASE ("AI cleanup in the chain: first, constant latency on or off, out of Record mode, LISTEN hears what it takes out", "[ai][chain]")
{
    constexpr double sr = 48000.0;
    ChainParams off;   // (neutral by default)
    ChainParams on = off;
    on.cleanup.enabled = true;
    on.cleanup.aiMode = true;
    on.cleanup.noiseDb = AiCleanup::kFullDb;
    VocalChain a, b, rec;
    a.prepare (sr, 2); a.setParams (off);
    b.prepare (sr, 2); b.setParams (on);
    rec.prepare (sr, 2, true); rec.setParams (on);
    CHECK (a.latencySamples() == b.latencySamples());
    CHECK (a.latencySamples() - rec.latencySamples() >= 1920);   // Record mode leaves the AI (and the noise reducer) out

    // Rendered: noise between phrases goes down, the voice stays.
    const auto x = testsig::vocal (sr, 4.0, -45.0);
    const auto y = VocalChain::render ({ x }, sr, on)[0];
    const auto z = VocalChain::render ({ x }, sr, off)[0];
    CHECK (testsig::rmsDb (y) == Approx (testsig::rmsDb (z)).margin (2.0));
    CHECK (testsig::rmsDb (y) > -60.0);

    // The classic reducer is off in AI mode (Noise is the AI's strength there), and AI mode off means classic again.
    ChainParams classic = on;
    classic.cleanup.aiMode = false;
    classic.cleanup.noiseDb = 12.0;
    const auto c = VocalChain::render ({ x }, sr, classic)[0];
    double diff = 0.0;
    for (size_t i = 0; i < c.size(); ++i) diff = std::max (diff, static_cast<double> (std::abs (c[i] - y[i])));
    CHECK (diff > 1e-3);
}

TEST_CASE ("Auto-Edit: a noisy take gets the AI (Mode AI, full on Balanced), unless asked for the classic reducer", "[ai][autoedit]")
{
    constexpr double sr = 48000.0;
    const auto x = testsig::vocal (sr, 14.0, -50.0, -14.0);
    const auto r = autoEdit ({ x }, sr, { 0, 1, 120.0 });
    REQUIRE (r.ok);
    CHECK (r.params.cleanup.aiMode);
    CHECK (r.params.cleanup.noiseDb == AiCleanup::kFullDb);
    CHECK (r.params.cleanup.roomPct == 0.0);
    bool modeReason = false;
    for (const auto& reason : r.reasons) modeReason = modeReason || (reason.module == "cleanup" && reason.control == "Mode" && reason.value == "AI");
    CHECK (modeReason);

    AutoEditSettings classic { 0, 1, 120.0 };
    classic.allowAi = false;
    const auto c = autoEdit ({ x }, sr, classic);
    REQUIRE (c.ok);
    CHECK (! c.params.cleanup.aiMode);
    CHECK (c.params.cleanup.noiseDb > 0.0);
    CHECK (c.params.cleanup.noiseDb <= 18.0);

    // Light: a limit, not full.
    const auto light = autoEdit ({ x }, sr, { 0, 0, 120.0 });
    REQUIRE (light.ok);
    CHECK (light.params.cleanup.aiMode);
    CHECK (light.params.cleanup.noiseDb >= 12.0);
    CHECK (light.params.cleanup.noiseDb <= AiCleanup::kFullDb);

    // A studio-quiet take: no cleanup, so no AI either.
    const auto quiet = autoEdit ({ testsig::vocal (sr, 14.0, -95.0, -14.0) }, sr, { 0, 1, 120.0 });
    REQUIRE (quiet.ok);
    CHECK (! quiet.params.cleanup.aiMode);
}

TEST_CASE ("AI cleanup: cost per 10 ms frame", "[ai][!benchmark]")
{
    constexpr double sr = 48000.0;
    const auto x = toDouble (testsig::vocal (sr, 6.0, -45.0));
    AiCleanup ai;
    ai.prepare (sr, 1);
    ai.setParams ({ true, AiCleanup::kFullDb });
    const auto t0 = std::chrono::steady_clock::now();
    const auto y = play (ai, x);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("AI cleanup: %.2f ms per 10 ms frame (%.1f %% of one core)\n", secs / 600.0 * 1000.0, secs / 6.0 * 100.0);
    CHECK (y.size() == x.size());
}
