#include "Signals.h"
#include "vox/LoudnessMeter.h"
#include "vox/VocalChain.h"

#include <limits>
#include <numbers>
#include <random>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

ChainParams busy()
{
    ChainParams p;
    p.pitch.amount = 100; p.pitch.speedMs = 20; p.pitch.scale = 2; p.pitch.key = 9;
    p.cleanup.lowCutHz = 90; p.cleanup.gateThrDb = -50; p.cleanup.gateRangeDb = 12;
    p.eq.gainDb = { -2, -3, -1.5, 3, 4 };
    p.deEsser.amount = 60;
    p.rider.rangeDb = 4;
    p.comp.peakThrDb = -14; p.comp.thrDb = -26; p.comp.ratio = 4; p.comp.makeupDb = 5;
    p.saturation.driveDb = 6; p.saturation.mix = 50;
    p.doubler.amount = 30;
    p.delay.mix = 15;
    p.reverb.mix = 15;
    p.outputDb = -2;
    return p;
}

size_t latency()
{
    VocalChain c;
    c.prepare (kSr, 2);
    return static_cast<size_t> (c.latencySamples());
}

const size_t kLat = latency();

std::vector<std::vector<double>> process (const ChainParams& p, const std::vector<float>& mono, int channels, int block)
{
    VocalChain c;
    c.setParams (p);
    c.prepare (kSr, channels);
    std::vector<std::vector<double>> x (static_cast<size_t> (channels), std::vector<double> (mono.begin(), mono.end()));
    for (size_t s = 0; s < mono.size(); s += static_cast<size_t> (block))
    {
        std::array<double*, 2> ptr {};
        for (int ch = 0; ch < channels; ++ch) ptr[static_cast<size_t> (ch)] = x[static_cast<size_t> (ch)].data() + s;
        c.process (ptr.data(), channels, static_cast<int> (std::min<size_t> (static_cast<size_t> (block), mono.size() - s)));
    }
    return x;
}
}

TEST_CASE ("Neutral chain = input delayed by the latency", "[chain]")
{
    ChainParams p;
    const auto x = testsig::vocal (kSr, 1.0);
    const auto y = process (p, x, 2, 480);
    double err = 0.0;
    for (size_t i = kLat; i < x.size(); ++i)
        err = std::max (err, std::abs (y[0][i] - x[i - kLat]));
    CHECK (err < 1.0e-9);
}

TEST_CASE ("Bypass and A (original) play the delayed input", "[chain]")
{
    auto p = busy();
    p.bypass = true;
    const auto x = testsig::vocal (kSr, 1.0);
    const auto y = process (p, x, 2, 333);
    double err = 0.0;
    for (size_t i = kLat; i < x.size(); ++i)
        err = std::max ({ err, std::abs (y[0][i] - x[i - kLat]), std::abs (y[1][i] - x[i - kLat]) });
    CHECK (err < 1.0e-9);

    p.bypass = false; p.listenOriginal = true; p.gainOriginalDb = -6.0;
    VocalChain c; c.setParams (p); c.prepare (kSr, 1);
    std::vector<double> z (x.begin(), x.end());
    double* ptr = z.data();
    c.process (&ptr, 1, static_cast<int> (z.size()));
    CHECK (z[30000] == Approx (x[30000 - kLat] * std::pow (10.0, -6.0 / 20.0)).margin (1e-9));
}

TEST_CASE ("Any block size gives the same result", "[chain]")
{
    const auto x = testsig::vocal (kSr, 1.5, -60.0, -3.0);
    const auto a = process (busy(), x, 2, 512);
    for (int block : { 1, 17, 64, 1000, 4096 })
    {
        const auto b = process (busy(), x, 2, block);
        double err = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < x.size(); ++i) err = std::max (err, std::abs (a[c][i] - b[c][i]));
        INFO ("block " << block);
        CHECK (err < 1.0e-9);
    }
}

TEST_CASE ("Busy chain stays finite on silence, a full-scale burst and a mono track", "[chain]")
{
    std::vector<float> x (48000, 0.0f);
    for (int i = 10000; i < 12000; ++i) x[static_cast<size_t> (i)] = (i % 2) ? 1.0f : -1.0f;
    for (int ch : { 1, 2 })
    {
        const auto y = process (busy(), x, ch, 256);
        for (const auto& c : y) for (double v : c) REQUIRE (std::isfinite (v));
    }
}

TEST_CASE ("In the chain the gate opens before a soft word start (look-ahead from Pitch's latency)", "[chain][cleanup]")
{
    // Room noise at -70 dB, then a word that swells from -60 to -12 dBFS over 60 ms and holds.
    std::vector<float> x (static_cast<size_t> (1.5 * kSr), 0.0f);
    std::mt19937 rng (3);
    std::normal_distribution<double> w (0.0, 1.0);
    for (auto& v : x) v = static_cast<float> (3.0e-4 * w (rng));
    const auto w0 = static_cast<size_t> (0.8 * kSr), ramp = static_cast<size_t> (0.06 * kSr);
    for (size_t i = w0; i < x.size(); ++i)
    {
        const double t = std::min (1.0, static_cast<double> (i - w0) / static_cast<double> (ramp));
        x[i] += static_cast<float> (std::pow (10.0, (-60.0 + 48.0 * t) / 20.0) * std::sin (2.0 * std::numbers::pi * 220.0 * static_cast<double> (i) / kSr));
    }
    ChainParams p;
    p.cleanup.gateThrDb = -30.0;
    p.cleanup.gateRangeDb = 18.0;
    const auto y = VocalChain::render ({ x }, kSr, p);
    // The swell crosses -30 dB peak at 37.5 ms; the 8 ms before that already pass at full level.
    const auto cross = w0 + static_cast<size_t> (0.0375 * kSr), a = cross - static_cast<size_t> (0.008 * kSr);
    double ein = 0.0, eout = 0.0;
    for (size_t i = a; i < cross; ++i) { ein += static_cast<double> (x[i]) * x[i]; eout += static_cast<double> (y[0][i]) * y[0][i]; }
    CHECK (10.0 * std::log10 (eout / ein) > -1.0);
    // The gaps are still turned down.
    double gin = 0.0, gout = 0.0;
    for (size_t i = static_cast<size_t> (0.3 * kSr); i < static_cast<size_t> (0.6 * kSr); ++i) { gin += static_cast<double> (x[i]) * x[i]; gout += static_cast<double> (y[0][i]) * y[0][i]; }
    CHECK (10.0 * std::log10 (gout / gin) == Approx (-18.0).margin (1.0));
}

TEST_CASE ("Bypass switched on while playing (the DAW's own too): the delayed input on both sides once glided", "[chain]")
{
    // The plug-in runs the DAW's bypass through the chain with bypass on: the same delayed dry vocal as Voxology's
    // own, so the DAW's latency compensation still lines it up. A mono vocal comes in on both channels.
    const auto x = testsig::vocal (kSr, 2.0);
    VocalChain c;
    auto p = busy();
    c.setParams (p);
    c.prepare (kSr, 2);
    std::vector<double> l (x.begin(), x.end()), r = l;
    const size_t on = static_cast<size_t> (0.7 * kSr);
    for (size_t s = 0; s < x.size(); s += 300)
    {
        if (s >= on) { p.bypass = true; c.setParams (p); }
        std::array<double*, 2> ptr { l.data() + s, r.data() + s };
        c.process (ptr.data(), 2, static_cast<int> (std::min<size_t> (300, x.size() - s)));
    }
    double err = 0.0;
    for (size_t i = on + static_cast<size_t> (0.3 * kSr); i < x.size(); ++i)
        err = std::max ({ err, std::abs (l[i] - x[i - kLat]), std::abs (r[i] - x[i - kLat]) });
    CHECK (err == 0.0);
    // ... and it glides there (no click): the step between two samples stays like the music's own (processed or dry).
    double step = 0.0, music = 0.0;
    for (size_t i = on; i < on + static_cast<size_t> (0.1 * kSr); ++i) step = std::max (step, std::abs (l[i] - l[i - 1]));
    for (size_t i = kLat + 1; i < on; ++i) music = std::max (music, std::abs (l[i] - l[i - 1]));
    for (size_t i = kLat + 1; i < x.size(); ++i) music = std::max (music, std::abs (static_cast<double> (x[i - kLat]) - x[i - kLat - 1]));
    CHECK (step < 1.5 * music);
}

TEST_CASE ("One NaN / inf input sample doesn't poison the output", "[chain]")
{
    // Before: Pitch, Tone EQ, the Compressor or Delay / Reverb kept playing NaN until the next prepare.
    const auto x = testsig::vocal (kSr, 3.0);
    for (const double bad : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() })
    {
        INFO ("bad sample " << bad);
        VocalChain c;
        c.setParams (busy());
        c.prepare (kSr, 2);
        std::vector<double> l (x.begin(), x.end()), r = l;
        l[48100] = r[48100] = bad;
        for (size_t s = 0; s < x.size(); s += 512)
        {
            std::array<double*, 2> ptr { l.data() + s, r.data() + s };
            c.process (ptr.data(), 2, static_cast<int> (std::min<size_t> (512, x.size() - s)));
        }
        for (size_t i = 0; i < x.size(); ++i) REQUIRE ((std::isfinite (l[i]) && std::isfinite (r[i])));
        double e = 0.0;
        for (size_t i = x.size() - 48000; i < x.size(); ++i) e += l[i] * l[i];
        CHECK (10.0 * std::log10 (e / 48000.0 + 1.0e-30) > -40.0);   // still the vocal, not silence
        CHECK (std::isfinite (c.takeMeters().pitch.correction));
    }
}

TEST_CASE ("Loudness meter: a NaN / inf sample is ignored (no stuck reading, no bin outside the histogram)", "[chain]")
{
    // An inf on the last sample of a 100 ms hop made the hop's energy inf: its histogram bin was INT_MIN.
    for (const float bad : { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
    {
        LoudnessMeter m;
        m.prepare (kSr, 1);
        auto x = testsig::sine (kSr, 1000.0, -20.0, 5.0);
        x[4799] = bad;
        const float* p[1] = { x.data() };
        m.process (p, 1, static_cast<int> (x.size()));
        CHECK (std::isfinite (m.shortTermLufs()));
        CHECK (m.integratedLufs() == Approx (-23.0).margin (0.5));   // a -20 dBFS 1 kHz sine reads about -23 LUFS
    }
}

TEST_CASE ("Beta 33: the movable inserts' order (from the Rack): helpers, state text", "[chain][order]")
{
    CHECK (validOrder (kDefaultOrder));
    CHECK (ChainParams {}.order == kDefaultOrder);
    CHECK (orderText (kDefaultOrder) == "eq,dyneq,deess,rider,comp,sat");
    CHECK (orderFromText ("eq,dyneq,deess,rider,comp,sat") == kDefaultOrder);
    const auto compFirst = movedTo (kDefaultOrder, static_cast<int> (Module::comp), 0);
    CHECK (orderText (compFirst) == "comp,eq,dyneq,deess,rider,sat");
    CHECK (orderFromText (orderText (compFirst)) == compFirst);
    CHECK (orderText (movedTo (kDefaultOrder, static_cast<int> (Module::eq), 99)) == "dyneq,deess,rider,comp,sat,eq");   // clamped: last
    CHECK (orderText (movedTo (kDefaultOrder, static_cast<int> (Module::deEsser), -3)) == "deess,eq,dyneq,rider,comp,sat");
    CHECK (movedTo (kDefaultOrder, static_cast<int> (Module::pitch), 0) == kDefaultOrder);   // Pitch stays first
    CHECK (movedTo (InsertOrder { 2, 2, 2, 2, 2, 2 }, static_cast<int> (Module::comp), 1) == movedTo (kDefaultOrder, static_cast<int> (Module::comp), 1));
    CHECK_FALSE (validOrder (InsertOrder { 2, 2, 4, 5, 6, 7 }));
    CHECK_FALSE (validOrder (InsertOrder { 0, 3, 4, 5, 6, 7 }));   // (Pitch isn't movable)
    for (const char* damaged : { "", "eq", "eq,dyneq,deess,rider,comp", "eq,dyneq,deess,rider,comp,sat,eq", "eq,eq,deess,rider,comp,sat", "EQ,dyneq,deess,rider,comp,sat" })
    {
        INFO (damaged);
        CHECK (orderFromText (damaged) == kDefaultOrder);
    }
    CHECK (isMovable (static_cast<int> (Module::rider)));
    CHECK_FALSE (isMovable (static_cast<int> (Module::reverb)));
}

TEST_CASE ("Beta 33: another order plays another sound; any block size the same; render plays it from the start", "[chain][order]")
{
    const auto x = testsig::vocal (kSr, 1.5, -60.0, -3.0);
    auto p = busy();
    p.eq.gainDb = { 0, 0, 0, 9, 9 };   // a big top boost: before or after the compressor and saturation sounds different
    const auto own = process (p, x, 2, 512);
    auto q = p;
    q.order = movedTo (p.order, static_cast<int> (Module::eq), kMovable - 1);   // the EQ last
    const auto moved = process (q, x, 2, 512);
    double diff = 0.0, level = 0.0;
    for (size_t i = kLat; i < x.size(); ++i)
    {
        diff = std::max (diff, std::abs (own[0][i] - moved[0][i]));
        level = std::max (level, std::abs (own[0][i]));
    }
    CHECK (diff > 0.02 * level);
    // block size doesn't matter (the order is taken at the start: no dip)
    const auto moved7 = process (q, x, 2, 7);
    double err = 0.0;
    for (size_t i = 0; i < x.size(); ++i) err = std::max (err, std::abs (moved[0][i] - moved7[0][i]));
    CHECK (err < 1.0e-9);
    // render() = the same chain from the start
    std::vector<std::vector<float>> in (2, x);
    const auto r = VocalChain::render (in, kSr, q);
    double rerr = 0.0;
    for (size_t i = 0; i + kLat < x.size(); ++i) rerr = std::max (rerr, std::abs (static_cast<double> (r[0][i]) - moved[0][i + kLat]));
    CHECK (rerr < 1.0e-5);
}

TEST_CASE ("Beta 33: moving an insert while it plays: a short dip, no click, then the new order", "[chain][order]")
{
    const auto x = testsig::vocal (kSr, 2.0, -60.0, -3.0);
    auto p = busy();
    p.eq.gainDb = { 0, 0, 0, 9, 9 };
    p.doubler.amount = 0; p.delay.mix = 0; p.reverb.mix = 0;   // (no tails: the comparison below is exact enough)
    auto q = p;
    q.order = movedTo (p.order, static_cast<int> (Module::comp), 0);
    VocalChain c;
    c.setParams (p);
    c.prepare (kSr, 2);
    std::vector<std::vector<double>> y (2, std::vector<double> (x.begin(), x.end()));
    const size_t at = 48000;   // the move, one second in
    for (size_t s = 0; s < x.size(); s += 480)
    {
        if (s == at) c.setParams (q);
        std::array<double*, 2> ptr { y[0].data() + s, y[1].data() + s };
        c.process (ptr.data(), 2, static_cast<int> (std::min<size_t> (480, x.size() - s)));
    }
    double jumpBefore = 0.0, jumpAround = 0.0;
    for (size_t i = 24000; i < at; ++i) jumpBefore = std::max (jumpBefore, std::abs (y[0][i] - y[0][i - 1]));
    for (size_t i = at; i < at + 2000; ++i) jumpAround = std::max (jumpAround, std::abs (y[0][i] - y[0][i - 1]));
    for (const auto& ch : y) for (double v : ch) REQUIRE (std::isfinite (v));
    CHECK (jumpAround <= jumpBefore * 1.25 + 1.0e-6);   // no step bigger than the vocal makes itself
    // Afterwards it plays the new order: much nearer the new order from the start than the old one (the Rider's and
    // the compressor's levels carry on from before, so not exactly).
    const auto fresh = process (q, x, 2, 480), old = process (p, x, 2, 480);
    double toNew = 0.0, toOld = 0.0;
    for (size_t i = at + 24000; i < at + 48000 && i < x.size(); ++i)
    {
        toNew += (y[0][i] - fresh[0][i]) * (y[0][i] - fresh[0][i]);
        toOld += (y[0][i] - old[0][i]) * (y[0][i] - old[0][i]);
    }
    INFO ("toNew " << toNew << " toOld " << toOld);
    CHECK (toNew < 0.25 * toOld);
}
