// Mouth de-click (Cleanup's Clicks, v0.36): short bursts of top end in the side-chain, fixed before they arrive.
#include "Signals.h"
#include "vox/AutoEdit.h"
#include "vox/DeClick.h"
#include "vox/VocalChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace vox;

namespace {
constexpr double kSr = 48000.0;
constexpr int kLook = 1616;   // Pitch's latency at 48 kHz: how far the side-chain runs ahead in the chain

struct Clicky
{
    std::vector<double> clean, clicks, ts, x;
    std::vector<std::pair<size_t, size_t>> clickWin, tWin;
};

/** A vocal with 1 ms mouth clicks (top end, -20 dBFS peaks) every 0.53 s and 12 ms "t" bursts every 0.71 s. */
Clicky clicky (double seconds = 12.0)
{
    Clicky c;
    const auto v = testsig::vocal (kSr, seconds, -70.0, -14.0, 3);
    c.clean.assign (v.begin(), v.end());
    c.clicks.assign (c.clean.size(), 0.0);
    c.ts.assign (c.clean.size(), 0.0);
    std::mt19937 rng (3);
    std::normal_distribution<double> g (0.0, 1.0);
    Biquad h1, h2;
    design::apply (h1, design::butterworth (true, 1500.0, kSr));
    design::apply (h2, design::butterworth (true, 1500.0, kSr));
    for (double t0 = 1.03; t0 < seconds - 0.5; t0 += 0.53)
    {
        const auto s = static_cast<size_t> (t0 * kSr);
        for (size_t i = 0; i < 96; ++i) c.clicks[s + i] = 0.15 * g (rng) * std::exp (-static_cast<double> (i) / 12.0);
        c.clickWin.push_back ({ s, s + 96 });
    }
    for (auto& v2 : c.clicks) v2 = h2.process (h1.process (v2));
    for (double t0 = 1.31; t0 < seconds - 0.5; t0 += 0.71)
    {
        const auto s = static_cast<size_t> (t0 * kSr);
        for (size_t i = 0; i < 576; ++i) c.ts[s + i] = 0.05 * g (rng) * std::min (1.0, static_cast<double> (i) / 144.0) * std::exp (-static_cast<double> (i) / 300.0);
        c.tWin.push_back ({ s, s + 576 });
    }
    c.x.resize (c.clean.size());
    for (size_t i = 0; i < c.x.size(); ++i) c.x[i] = c.clean[i] + c.clicks[i] + c.ts[i];
    return c;
}

/** Runs the de-clicker as in the chain: the audio kLook behind the side-chain. Returns the audio, lined up with x. */
std::vector<double> declick (const std::vector<double>& x, double amount, int look = kLook, int* found = nullptr, int64_t clock = 0)
{
    std::vector<double> audio (x.size() + static_cast<size_t> (look), 0.0), side = x;
    std::copy (x.begin(), x.end(), audio.begin() + look);
    side.resize (audio.size(), 0.0);
    DeClick d;
    d.prepare (kSr, 1, look);
    d.setParams ({ amount });
    if (clock > 0) d.startClockAt (clock);
    for (size_t s = 0; s < audio.size(); s += 256)
    {
        double* p = audio.data() + s;
        d.process (&p, 1, static_cast<int> (std::min<size_t> (256, audio.size() - s)), side.data() + s);
    }
    if (found != nullptr) *found = d.takeClicks();
    return std::vector<double> (audio.begin() + look, audio.end());
}
}

TEST_CASE ("De-click: clicks in the gaps go, by the setting; t, k, word edges and the voice stay", "[declick]")
{
    const auto c = clicky();
    std::vector<std::pair<size_t, size_t>> gapClicks;
    for (const auto& w : c.clickWin)
    {
        double e = 0.0;
        for (size_t i = w.first; i < w.second; ++i) e += c.clean[i] * c.clean[i];
        if (10.0 * std::log10 (e / 96.0 + 1.0e-30) < -50.0) gapClicks.push_back (w);
    }
    REQUIRE (gapClicks.size() >= 6);
    for (const double amount : { 30.0, 60.0 })
    {
        INFO ("amount " << amount);
        int found = 0;
        const auto y = declick (c.x, amount, kLook, &found);
        double in = 0.0, out = 0.0;
        for (const auto& [s, e] : gapClicks)
            for (size_t i = s; i < e; ++i) { in += c.clicks[i] * c.clicks[i]; out += std::pow (y[i] - c.clean[i] - c.ts[i], 2.0); }
        CHECK (10.0 * std::log10 (out / in) < -0.8 * amount * DeClick::kMaxCutDb / 100.0);
        CHECK (found >= static_cast<int> (gapClicks.size()));
        CHECK (found <= static_cast<int> (c.clickWin.size()) + 3);
        double tIn = 0.0, tOut = 0.0;
        for (const auto& [s, e] : c.tWin)
            for (size_t i = s; i < e; ++i) { tIn += c.x[i] * c.x[i]; tOut += y[i] * y[i]; }
        CHECK (std::abs (10.0 * std::log10 (tOut / tIn)) < 0.05);   // "t" bursts are consonants: left alone
    }
    // Away from the clicks nothing changes (30 %: not a sample).
    const auto y = declick (c.x, 30.0);
    std::vector<bool> near (c.x.size(), false);
    for (const auto& [s, e] : c.clickWin)
        for (size_t i = s > 300 ? s - 300 : 0; i < e + 300 && i < c.x.size(); ++i) near[i] = true;
    for (size_t i = 0; i < c.x.size(); ++i)
        if (! near[i]) REQUIRE (y[i] == c.x[i]);
}

TEST_CASE ("De-click: off = untouched; too little look-ahead (Record mode) = off", "[declick]")
{
    const auto c = clicky (6.0);
    const auto off = declick (c.x, 0.0);
    for (size_t i = 0; i < c.x.size(); ++i) REQUIRE (off[i] == c.x[i]);
    DeClick d;
    d.prepare (kSr, 1, 240);   // Record mode's 5 ms
    CHECK (! d.isAvailable());
    const auto rec = declick (c.x, 100.0, 240);
    for (size_t i = 0; i < c.x.size(); ++i) REQUIRE (rec[i] == c.x[i]);

    // A clean vocal (hard word starts and ends included) isn't touched at any setting.
    const auto v = testsig::vocal (kSr, 14.0, -70.0, -14.0, 3);
    const std::vector<double> clean (v.begin(), v.end());
    for (const double amount : { 30.0, 60.0, 100.0 })
    {
        int found = -1;
        const auto y = declick (clean, amount, kLook, &found);
        CHECK (found == 0);
        for (size_t i = 0; i < clean.size(); ++i) REQUIRE (y[i] == clean[i]);
    }
}

TEST_CASE ("De-click: the same after hours of playing (64-bit clock past 2^31 samples, 12 h at 48 kHz)", "[declick]")
{
    auto c = clicky (6.0);
    c.x.insert (c.x.begin(), static_cast<size_t> (0.1 * kSr), 0.0);   // (starts in silence: no history either way)
    int foundFresh = 0, foundLate = 0;
    const auto fresh = declick (c.x, 60.0, kLook, &foundFresh);
    const auto late = declick (c.x, 60.0, kLook, &foundLate, (int64_t { 1 } << 31) - static_cast<int64_t> (2.0 * kSr));
    CHECK (foundFresh > 0);
    CHECK (foundLate == foundFresh);
    for (size_t i = 0; i < fresh.size(); ++i) REQUIRE (late[i] == fresh[i]);
}

TEST_CASE ("De-click in the chain, and Auto-Edit turns it on for a clicky take only", "[declick][autoedit]")
{
    const auto c = clicky (14.0);
    const std::vector<float> xf (c.x.begin(), c.x.end());
    ChainParams p;
    p.cleanup.clickAmount = 60.0;
    const auto on = VocalChain::render ({ xf }, kSr, p);
    p.cleanup.clickAmount = 0.0;
    const auto off = VocalChain::render ({ xf }, kSr, p);
    double eOn = 0.0, eOff = 0.0;
    for (const auto& [s, e] : c.clickWin)
    {
        double ec = 0.0;
        for (size_t i = s; i < e; ++i) ec += c.clean[i] * c.clean[i];
        if (10.0 * std::log10 (ec / 96.0 + 1.0e-30) >= -50.0) continue;   // the clicks in the gaps
        for (size_t i = s; i < e; ++i) { eOn += on[0][i] * on[0][i]; eOff += off[0][i] * off[0][i]; }
    }
    CHECK (10.0 * std::log10 (eOn / eOff) < -10.0);

    const auto clean = testsig::vocal (kSr, 14.0, -70.0, -14.0, 3);
    const auto rc = autoEdit ({ clean }, kSr, { 0, 1, 140.0 }), rk = autoEdit ({ xf }, kSr, { 0, 1, 140.0 });
    REQUIRE (rc.ok);
    REQUIRE (rk.ok);
    CHECK (rc.params.cleanup.clickAmount == 0.0);
    CHECK (rk.params.cleanup.clickAmount == 60.0);
}
