// Noise and room reduction (Cleanup's Noise / Room, v0.35): first in the chain, a constant one-frame delay.
#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/Denoise.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>
#include <random>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

std::vector<double> run (Denoiser& d, std::vector<double> x, int block = 300)
{
    for (size_t s = 0; s < x.size(); s += static_cast<size_t> (block))
    {
        double* p = x.data() + s;
        d.process (&p, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), x.size() - s)));
    }
    return x;
}

std::vector<double> whiteNoise (double seconds, double rms, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0.0, rms);
    std::vector<double> x (static_cast<size_t> (seconds * kSr));
    for (auto& v : x) v = g (rng);
    return x;
}

double energy (const std::vector<double>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i) e += x[i] * x[i];
    return e;
}

/** A held, sung note (2 s, harmonics, a little vibrato) with silence around it. */
std::vector<double> heldNote()
{
    std::vector<double> x (static_cast<size_t> (4.0 * kSr), 0.0);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        if (t < 1.0 || t > 3.0) continue;
        double s = 0.0;
        for (int h = 1; h < 20; ++h) s += std::sin (2.0 * std::numbers::pi * h * 220.0 * t * (1.0 + 0.003 * std::sin (2.0 * std::numbers::pi * 5.0 * t))) / h;
        x[i] = 0.1 * s;
    }
    return x;
}
}

TEST_CASE ("Denoise: off = the input exactly, one frame late, whatever the blocks", "[denoise]")
{
    Denoiser d;
    d.prepare (kSr, 1);
    REQUIRE (d.latencySamples() == Denoiser::frameSize (kSr));
    CHECK (Denoiser::frameSize (44100.0) == 1024);
    CHECK (Denoiser::frameSize (96000.0) == 2048);
    const auto x = whiteNoise (1.0, 0.1, 1);
    const auto y = run (d, x, 77);
    const auto n = static_cast<size_t> (d.latencySamples());
    for (size_t i = n; i < x.size(); ++i) REQUIRE (y[i] == x[i - n]);

    // On, the result doesn't depend on how the host cuts the blocks.
    Denoiser a, b;
    a.prepare (kSr, 1);
    b.prepare (kSr, 1);
    a.setParams ({ 12.0, 50.0 });
    b.setParams ({ 12.0, 50.0 });
    const auto v = testsig::vocal (kSr, 4.0, -50.0);
    const std::vector<double> vd (v.begin(), v.end());
    const auto ya = run (a, vd, 64), yb = run (b, vd, 1000);
    for (size_t i = 0; i < ya.size(); ++i) REQUIRE (ya[i] == Approx (yb[i]).margin (1.0e-12));
}

TEST_CASE ("Denoise: learns steady noise by itself and takes it down by the setting", "[denoise]")
{
    for (const double setting : { 6.0, 12.0, 18.0 })
    {
        INFO ("setting " << setting);
        Denoiser d;
        d.prepare (kSr, 1);
        d.setParams ({ setting, 0.0 });
        const auto x = whiteNoise (6.0, 0.003, 2);
        const auto y = run (d, x);
        const double got = 10.0 * std::log10 (energy (y, 3 * 48000, x.size()) / energy (x, 3 * 48000, x.size()));
        CHECK (got <= -(setting - 1.5));
        CHECK (got >= -(setting + 1.0));
    }
    // The learned noise matches the real noise (white noise: its power per bin = rms^2 x the window's sum).
    Denoiser d;
    d.prepare (kSr, 1);
    d.setParams ({ 12.0, 0.0 });
    run (d, whiteNoise (4.0, 0.01, 3));
    const double expected = 0.01 * 0.01 * Denoiser::frameSize (kSr) / 2.0;
    double sum = 0.0;
    int count = 0;
    for (size_t k = 20; k + 20 < d.noiseEstimate().size(); ++k) { sum += d.noiseEstimate()[k]; ++count; }
    CHECK (10.0 * std::log10 (sum / count / expected) == Approx (0.0).margin (1.0));   // on average within 1 dB
}

TEST_CASE ("Denoise: noise under a vocal goes, the words stay", "[denoise]")
{
    const auto v = testsig::vocal (kSr, 14.0, -120.0, -14.0);
    const std::vector<double> clean (v.begin(), v.end());
    auto x = clean;
    const auto noise = whiteNoise (14.0, 0.003, 4);   // about -50 dBFS, ~32 dB under the voice
    for (size_t i = 0; i < x.size(); ++i) x[i] += noise[i];
    Denoiser d;
    d.prepare (kSr, 1);
    d.setParams ({ 12.0, 0.0 });
    const auto y = run (d, x);
    const auto n = static_cast<size_t> (d.latencySamples());
    double gapIn = 0.0, gapOut = 0.0, wordIn = 0.0, wordOut = 0.0;
    for (size_t s = 4 * 48000; s + 480 + n <= x.size(); s += 480)
    {
        const double ec = energy (clean, s, s + 480);
        double eo = 0.0, ex = 0.0;
        for (size_t i = s; i < s + 480; ++i) { eo += y[i + n] * y[i + n]; ex += x[i] * x[i]; }
        if (ec < 1.0e-9) { gapIn += ex; gapOut += eo; }
        else if (10.0 * std::log10 (ec / 480.0) > -30.0) { wordIn += ex; wordOut += eo; }
    }
    CHECK (10.0 * std::log10 (gapOut / gapIn) < -10.0);
    CHECK (10.0 * std::log10 (wordOut / wordIn) == Approx (0.0).margin (0.2));
}

TEST_CASE ("Denoise: a clean vocal and a long held note are left alone", "[denoise]")
{
    const auto v = testsig::vocal (kSr, 10.0, -90.0, -14.0);
    for (const auto& clean : { std::vector<double> (v.begin(), v.end()), heldNote() })
        for (const auto prm : { DenoiseParams { 12.0, 0.0 }, DenoiseParams { 0.0, 100.0 } })
        {
            INFO ("noise " << prm.noiseDb << " room " << prm.roomPct);
            Denoiser d;
            d.prepare (kSr, 1);
            d.setParams (prm);
            const auto y = run (d, clean);
            const auto n = static_cast<size_t> (d.latencySamples());
            double err = 0.0, e = 0.0, eo = 0.0;
            for (size_t i = 24000; i + n < y.size(); ++i) { err += std::pow (y[i + n] - clean[i], 2.0); e += clean[i] * clean[i]; eo += y[i + n] * y[i + n]; }
            CHECK (10.0 * std::log10 (eo / e) == Approx (0.0).margin (prm.roomPct > 0.0 ? 0.4 : 0.1));
            if (clean.size() == heldNote().size())
                CHECK (10.0 * std::log10 (err / e) < -60.0);   // a held note: untouched
        }
}

TEST_CASE ("Denoise: Room shortens a room's echo after words", "[denoise]")
{
    std::mt19937 rng (5);
    std::normal_distribution<double> g (0.0, 1.0);
    const auto v = testsig::vocal (kSr, 10.0, -120.0, -14.0);
    const std::vector<double> dry (v.begin(), v.end());
    // A room: decaying noise, T60 0.45 s, 10 ms pre-delay (taps every 8 samples, enough for the test).
    const int len = static_cast<int> (0.9 * kSr);
    std::vector<double> ir (static_cast<size_t> (len), 0.0);
    double e = 0.0;
    for (int i = static_cast<int> (0.01 * kSr); i < len; i += 8) { ir[static_cast<size_t> (i)] = g (rng) * std::exp (-6.9078 * i / (0.45 * kSr)); e += ir[static_cast<size_t> (i)] * ir[static_cast<size_t> (i)]; }
    for (auto& h : ir) h *= std::pow (10.0, -6.0 / 20.0) / std::sqrt (e);
    std::vector<double> x = dry;
    for (int j = 0; j < len; j += 8)
        if (ir[static_cast<size_t> (j)] != 0.0)
            for (size_t i = static_cast<size_t> (j); i < dry.size(); ++i) x[i] += ir[static_cast<size_t> (j)] * dry[i - static_cast<size_t> (j)];

    auto measure = [&] (double room)
    {
        Denoiser d;
        d.prepare (kSr, 1);
        d.setParams ({ 0.0, room });
        const auto y = run (d, x);
        const auto n = static_cast<size_t> (d.latencySamples());
        double tIn = 0.0, tOut = 0.0, wIn = 0.0, wOut = 0.0;
        for (size_t s = 3 * 48000; s + 480 + n <= x.size(); s += 480)
        {
            const double ed = energy (dry, s, s + 480);
            double ex = 0.0, ey = 0.0;
            for (size_t i = s; i < s + 480; ++i) { ex += x[i] * x[i]; ey += y[i + n] * y[i + n]; }
            if (ed < 1.0e-9 && ex > 1.0e-9) { tIn += ex; tOut += ey; }
            else if (10.0 * std::log10 (ed / 480.0) > -30.0) { wIn += ex; wOut += ey; }
        }
        return std::make_pair (10.0 * std::log10 (tOut / tIn), 10.0 * std::log10 (wOut / wIn));
    };
    const auto [tail0, words0] = measure (0.0);
    const auto [tail100, words100] = measure (100.0);
    INFO ("echo in the gaps " << tail100 << " dB, words " << words100 << " dB");
    CHECK (tail0 == Approx (0.0).margin (0.01));
    CHECK (tail100 < -3.0);
    CHECK (words100 > -0.5);
}

TEST_CASE ("Denoise in the chain: first, constant latency, not in Record mode; stereo stays linked", "[denoise]")
{
    VocalChain chain, rec;
    chain.prepare (kSr, 2);
    rec.prepare (kSr, 2, true);
    PitchCorrector p, pr;
    p.prepare (kSr, 2);
    pr.prepare (kSr, 2, true);
    CHECK (chain.latencySamples() == Denoiser::frameSize (kSr) + p.latencySamples() + Saturation::kLatency);
    CHECK (rec.latencySamples() == pr.latencySamples() + Saturation::kLatency);

    Denoiser d;
    d.prepare (kSr, 2);
    d.setParams ({ 12.0, 40.0 });
    const auto v = testsig::vocal (kSr, 6.0, -50.0);
    std::vector<double> l (v.begin(), v.end()), r = l;
    for (auto& s : r) s *= 0.5;   // the same sound, quieter on the right
    for (size_t s = 0; s < l.size(); s += 256)
    {
        std::array<double*, 2> ch { l.data() + s, r.data() + s };
        d.process (ch.data(), 2, static_cast<int> (std::min<size_t> (256, l.size() - s)));
    }
    for (size_t i = 0; i < l.size(); ++i) REQUIRE (r[i] == Approx (0.5 * l[i]).margin (1.0e-12));   // one gain for both sides
}

TEST_CASE ("Auto-Edit: noise reduction on a noisy take, off on a clean one; Room off on a dry one", "[denoise][autoedit]")
{
    auto reasonOf = [] (const AutoEditResult& r, const char* control) -> std::string
    {
        for (const auto& x : r.reasons) if (x.module == "cleanup" && x.control == control) return x.value;
        return "?";
    };
    const auto noisy = testsig::vocal (kSr, 14.0, -50.0, -14.0), clean = testsig::vocal (kSr, 14.0, -90.0, -14.0);
    const auto rn = autoEdit ({ noisy }, kSr, { 0, 1, 140.0 }), rc = autoEdit ({ clean }, kSr, { 0, 1, 140.0 });
    REQUIRE (rn.ok);
    REQUIRE (rc.ok);
    INFO ("noisy: noise " << rn.params.cleanup.noiseDb << ", room " << rn.params.cleanup.roomPct);
    CHECK (rn.params.cleanup.noiseDb >= 6.0);
    CHECK (rn.params.cleanup.noiseDb <= 15.0);
    CHECK (rc.params.cleanup.noiseDb == 0.0);
    CHECK (reasonOf (rc, "Noise") == "Off");
    CHECK (rc.params.cleanup.roomPct == 0.0);   // a dry synthetic take: no room
    CHECK (rn.params.cleanup.roomPct == 0.0);
    CHECK (reasonOf (rn, "Room") == "Off");
}
