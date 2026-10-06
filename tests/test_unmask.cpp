#include "Signals.h"
#include "vox/Unmask.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;
constexpr int kHop = 128;

/** A stereo "beat": pink-ish noise in the middle plus a wide pad only on the sides (L = -R). */
std::array<std::vector<double>, 2> beat (double seconds)
{
    std::mt19937 rng (4);
    std::normal_distribution<double> w (0.0, 1.0);
    const size_t n = static_cast<size_t> (seconds * kSr);
    std::array<std::vector<double>, 2> b { std::vector<double> (n), std::vector<double> (n) };
    double p = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        p = 0.98 * p + 0.2 * w (rng);   // darker noise
        const double mid = 0.05 * p + 0.02 * w (rng);
        const double side = 0.05 * std::sin (2.0 * std::numbers::pi * 1600.0 * static_cast<double> (i) / kSr);
        b[0][i] = mid + side;
        b[1][i] = mid - side;
    }
    return b;
}

/** The vocal's band levels over time: singing (a 1.6 kHz-heavy voice) in the first half of every second. */
std::vector<std::array<float, kUnmaskBands>> vocalFrames (double seconds)
{
    BandAnalyser a;
    a.prepare (kSr);
    const auto x = testsig::vocal (kSr, seconds, -120.0, -40.0, 3, 200.0);
    std::vector<std::array<float, kUnmaskBands>> frames;
    for (size_t s = 0; s < x.size(); s += kHop)
    {
        for (size_t i = s; i < std::min (x.size(), s + kHop); ++i)
        {
            const double t = std::fmod (static_cast<double> (i) / kSr, 1.0);
            a.process (t < 0.5 ? x[i] * 4.0 : 0.0);
        }
        frames.push_back (a.levelsDb());
    }
    return frames;
}

double bandDb (const std::vector<double>& x, size_t a, size_t b, double hz)
{
    Biquad f;
    design::apply (f, design::bandPass (hz, 2.0, kSr));
    double e = 0.0;
    for (size_t i = a > 9600 ? a - 9600 : 0; i < b; ++i) { const double y = f.process (x[i]); if (i >= a) e += y * y; }
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1.0e-30);
}
}

TEST_CASE ("Unmask link: publish by song position, read the matching frame", "[unmask]")
{
    auto& link = UnmaskLink::instance();
    link.resetForTests();
    const int slot = link.claim();
    REQUIRE (slot >= 0);
    link.setName (slot, "Lead Vox");
    std::array<float, kUnmaskBands> db {};
    for (int k = 0; k < 100; ++k)
    {
        db.fill (static_cast<float> (k));
        link.publish (slot, static_cast<int64_t> (k) * kHop, db);
    }
    std::array<float, kUnmaskBands> got {};
    REQUIRE (link.read (slot, 50 * kHop + 10, 4 * kHop, got));
    CHECK (got[0] == 50.0f);                        // the frame at or just before the position
    REQUIRE (link.read (slot, UnmaskLink::kNoPosition, 0, got));
    CHECK (got[0] == 99.0f);                        // no position: the newest
    // Far past the newest frame (the vocal stopped being processed, e.g. a silent track Cubase suspended):
    // nothing recent enough, so no dip is held on stale data.
    CHECK_FALSE (link.read (slot, 1000 * kHop, 4 * kHop, got));
    const auto src = link.sources();
    REQUIRE (src.size() == 1);
    CHECK (src[0].name == "Lead Vox");
    CHECK (src[0].live);
    link.release (slot);
    CHECK_FALSE (link.read (slot, 50 * kHop, 4 * kHop, got));
    CHECK (link.sources().empty());
}

TEST_CASE ("Unmask: off = untouched; no vocal = no dip", "[unmask]")
{
    auto b = beat (2.0);
    const auto orig = b;
    Unmask u;
    u.prepare (kSr, 2);
    u.setParams ({ 0.0, true });
    std::array<double*, 2> ch { b[0].data(), b[1].data() };
    u.process (ch.data(), 2, static_cast<int> (b[0].size()), nullptr);
    for (size_t i = 0; i < b[0].size(); ++i) REQUIRE (b[0][i] == orig[0][i]);

    u.setParams ({ 100.0, true });
    for (size_t s = 0; s < b[0].size(); s += kHop)
    {
        std::array<double*, 2> p { b[0].data() + s, b[1].data() + s };
        u.process (p.data(), 2, kHop, nullptr);
    }
    for (int k = 0; k < kUnmaskBands; ++k) CHECK (u.currentDipDb (k) == 0.0);
    for (size_t i = 0; i < b[0].size(); ++i) REQUIRE (b[0][i] == Approx (orig[0][i]).margin (1.0e-12));
}

TEST_CASE ("Unmask: the beat's middle dips where and while the vocal sings; the sides stay", "[unmask]")
{
    const double seconds = 4.0;
    auto b = beat (seconds);
    const auto orig = b;
    const auto frames = vocalFrames (seconds);
    Unmask u;
    u.prepare (kSr, 2);
    u.setParams ({ 100.0, true });
    for (size_t k = 0; k < frames.size(); ++k)
    {
        const size_t s = k * kHop;
        const int len = static_cast<int> (std::min<size_t> (kHop, b[0].size() - s));
        std::array<double*, 2> p { b[0].data() + s, b[1].data() + s };
        u.process (p.data(), 2, len, frames[k].data());
    }
    std::vector<double> midIn (b[0].size()), midOut (b[0].size()), sideIn (b[0].size()), sideOut (b[0].size());
    for (size_t i = 0; i < b[0].size(); ++i)
    {
        midIn[i] = 0.5 * (orig[0][i] + orig[1][i]); midOut[i] = 0.5 * (b[0][i] + b[1][i]);
        sideIn[i] = 0.5 * (orig[0][i] - orig[1][i]); sideOut[i] = 0.5 * (b[0][i] - b[1][i]);
    }
    for (int sec = 1; sec < 4; ++sec)
    {
        const auto sing0 = static_cast<size_t> ((sec + 0.15) * kSr), sing1 = static_cast<size_t> ((sec + 0.45) * kSr);
        const auto gap0 = static_cast<size_t> ((sec + 0.8) * kSr), gap1 = static_cast<size_t> ((sec + 0.98) * kSr);
        INFO ("second " << sec);
        const double singDip = bandDb (midIn, sing0, sing1, 1600.0) - bandDb (midOut, sing0, sing1, 1600.0);
        const double gapDip = bandDb (midIn, gap0, gap1, 1600.0) - bandDb (midOut, gap0, gap1, 1600.0);
        CHECK (singDip > 2.0);    // the middle steps back while the vocal sings ...
        CHECK (singDip < 6.6);    // ... by about Amount x 6 dB at most (neighbouring dips add a little)
        CHECK (gapDip < 0.5);     // ... and comes back in the gaps
        CHECK (bandDb (sideOut, sing0, sing1, 1600.0) == Approx (bandDb (sideIn, sing0, sing1, 1600.0)).margin (0.05));
    }
}

TEST_CASE ("Unmask through the link, with the vocal published a block late (host processing order)", "[unmask]")
{
    auto& link = UnmaskLink::instance();
    link.resetForTests();
    const int slot = link.claim();
    const double seconds = 3.0;
    auto b = beat (seconds);
    const auto orig = b;
    const auto frames = vocalFrames (seconds);
    Unmask u;
    u.prepare (kSr, 2);
    u.setParams ({ 100.0, false });
    const int block = 512;
    std::array<float, kUnmaskBands> db {};
    for (size_t s = 0; s + block <= b[0].size(); s += block)
    {
        // The beat runs before the vocal this cycle: the vocal's frames for this block aren't there yet.
        for (size_t h = s; h < s + block; h += kHop)
        {
            const bool have = link.read (slot, static_cast<int64_t> (h), 4 * block, db);
            std::array<double*, 2> p { b[0].data() + h, b[1].data() + h };
            u.process (p.data(), 2, kHop, have ? db.data() : nullptr);
        }
        for (size_t h = s; h < s + block; h += kHop)
            link.publish (slot, static_cast<int64_t> (h), frames[h / kHop]);
    }
    std::vector<double> l (b[0].begin(), b[0].end()), lo (orig[0].begin(), orig[0].end());
    const auto a0 = static_cast<size_t> (1.15 * kSr), a1 = static_cast<size_t> (1.45 * kSr);
    const auto g0 = static_cast<size_t> (1.8 * kSr), g1 = static_cast<size_t> (1.98 * kSr);
    CHECK (bandDb (lo, a0, a1, 1600.0) - bandDb (l, a0, a1, 1600.0) > 2.0);
    CHECK (bandDb (lo, g0, g1, 1600.0) - bandDb (l, g0, g1, 1600.0) < 0.5);
    link.release (slot);
}
