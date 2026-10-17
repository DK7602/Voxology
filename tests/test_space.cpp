#include "Signals.h"
#include "vox/Space.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

template <typename FX, typename Params>
std::array<std::vector<double>, 2> wet (FX& fx, const Params& p, const std::vector<double>& in)
{
    fx.prepare (kSr);
    fx.setParams (p);
    std::array<std::vector<double>, 2> out { std::vector<double> (in.size(), 0.0), std::vector<double> (in.size(), 0.0) };
    for (size_t s = 0; s < in.size(); s += 256)
    {
        const int len = static_cast<int> (std::min<size_t> (256, in.size() - s));
        double* o[2] = { out[0].data() + s, out[1].data() + s };
        fx.process (in.data() + s, o, 2, len);
    }
    return out;
}
}

TEST_CASE ("Delay repeats land on the beat grid", "[space][delay]")
{
    std::vector<double> x (static_cast<size_t> (kSr * 2.0), 0.0);
    x[100] = 1.0;
    DelayParams p; p.mix = 100.0; p.feedback = 50.0; p.duck = 0.0; p.toneHz = 16000.0; p.bpm = 120.0; p.division = 0;
    EchoDelay d;
    const auto y = wet (d, p, x);
    // Biggest output sample after the impulse: 0.5 s later (a quarter note at 120 BPM).
    size_t at = 0; double best = 0.0;
    for (size_t i = 200; i < 40000; ++i) if (std::abs (y[0][i]) > best) { best = std::abs (y[0][i]); at = i; }
    CHECK (std::abs (static_cast<double> (at) - (100 + 24000)) < 40.0);
    CHECK (EchoDelay::delaySeconds (p) == Approx (0.5));
    p.division = 2;   // 1/8 dotted
    CHECK (EchoDelay::delaySeconds (p) == Approx (0.375));
}

TEST_CASE ("Delay ducks while the vocal is loud", "[space][delay]")
{
    CHECK (duckGain (std::pow (10.0, -20.0 / 10.0) / 2.0, 100.0) == Approx (std::pow (10.0, -18.0 / 20.0)).margin (1e-6));
    CHECK (duckGain (1.0e-9, 100.0) == Approx (1.0));
    CHECK (duckGain (0.1, 0.0) == 1.0);
}

TEST_CASE ("Reverb tail decays at about its Decay time and stays stable", "[space][reverb]")
{
    std::vector<double> x (static_cast<size_t> (kSr * 4.0), 0.0);
    x[10] = 1.0;
    ReverbParams p; p.mix = 100.0; p.decayS = 1.5; p.duck = 0.0; p.toneHz = 16000.0; p.predelayMs = 0.0;
    Reverb r;
    const auto y = wet (r, p, x);
    auto energyAt = [&] (double t) { double e = 0; for (size_t i = static_cast<size_t> (t * kSr); i < static_cast<size_t> ((t + 0.1) * kSr); ++i) e += y[0][i] * y[0][i] + y[1][i] * y[1][i]; return 10 * std::log10 (e + 1e-30); };
    const double drop = energyAt (0.3) - energyAt (1.05);   // 0.75 s apart: ~30 dB for RT60 1.5 s
    CHECK (drop > 20.0);
    CHECK (drop < 42.0);
    for (double v : y[0]) REQUIRE (std::isfinite (v));

    ReverbParams longP = p; longP.decayS = 10.0;
    Reverb r2;
    const auto z = wet (r2, longP, x);
    double peakLate = 0.0;
    for (size_t i = static_cast<size_t> (3.0 * kSr); i < z[0].size(); ++i) peakLate = std::max (peakLate, std::abs (z[0][i]));
    CHECK (peakLate < 0.5);
}

TEST_CASE ("Space effects add nothing when off", "[space]")
{
    const auto s = testsig::sine (kSr, 440.0, -12.0, 0.5);
    std::vector<double> x (s.begin(), s.end());
    { Doubler d; const auto y = wet (d, DoublerParams {}, x); for (double v : y[0]) REQUIRE (v == 0.0); }
    { EchoDelay d; const auto y = wet (d, DelayParams {}, x); for (double v : y[0]) REQUIRE (v == 0.0); }
    { Reverb d; const auto y = wet (d, ReverbParams {}, x); for (double v : y[1]) REQUIRE (v == 0.0); }
}

TEST_CASE ("Doubler adds two copies, wider with Width", "[space][doubler]")
{
    const auto s = testsig::vocal (kSr, 2.0, -80.0, -10.0);
    std::vector<double> x (s.begin(), s.end());
    DoublerParams p; p.amount = 100.0; p.width = 100.0;
    Doubler d;
    const auto y = wet (d, p, x);
    double el = 0, er = 0, elr = 0;
    for (size_t i = 0; i < x.size(); ++i) { el += y[0][i] * y[0][i]; er += y[1][i] * y[1][i]; elr += y[0][i] * y[1][i]; }
    const double corr = elr / std::sqrt (el * er);
    CHECK (corr < 0.6);   // two different takes, left and right
    CHECK (10 * std::log10 (el / x.size()) < testsig::rmsDb (s));   // quieter than the lead
}

TEST_CASE ("Doubler, Delay and Reverb fade out when switched off (no click), then add nothing", "[space]")
{
    // Cut in one sample, a loud tail clicked. Now it's still nearly all there 1 ms after the switch, and gone
    // (exactly) a moment later.
    const auto s = testsig::vocal (kSr, 3.0, -80.0, -10.0);
    const std::vector<double> x (s.begin(), s.end());
    auto check = [&x] (auto& on, auto& sw, const auto& params, const auto& offParams)
    {
        const auto ref = wet (on, params, x);
        // Switch at the loudest moment between 1.5 and 2.5 s (on a 16-sample block edge).
        size_t at = static_cast<size_t> (1.5 * kSr);
        for (size_t i = at; i < static_cast<size_t> (2.5 * kSr); ++i) if (std::abs (ref[0][i]) > std::abs (ref[0][at])) at = i;
        at -= at % 16;
        sw.prepare (kSr);
        sw.setParams (params);
        std::array<std::vector<double>, 2> y { std::vector<double> (x.size(), 0.0), std::vector<double> (x.size(), 0.0) };
        for (size_t b = 0; b < x.size(); b += 16)
        {
            if (b == at) sw.setParams (offParams);
            double* o[2] = { y[0].data() + b, y[1].data() + b };
            sw.process (x.data() + b, o, 2, static_cast<int> (std::min<size_t> (16, x.size() - b)));
        }
        double tail = 0.0, err = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = at; i < at + 48; ++i)
            {
                tail = std::max (tail, std::abs (ref[c][i]));
                err = std::max (err, std::abs (y[c][i] - ref[c][i]));
            }
        CHECK (tail > 1.0e-3);
        CHECK (err < 0.1 * tail);
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = at + static_cast<size_t> (0.5 * kSr); i < x.size(); ++i) REQUIRE (y[c][i] == 0.0);
    };
    {
        INFO ("Doubler");
        DoublerParams p; p.amount = 100.0;
        DoublerParams off = p; off.enabled = false;
        Doubler a, b;
        check (a, b, p, off);
    }
    {
        INFO ("Delay");
        DelayParams p; p.mix = 50.0; p.feedback = 60.0; p.duck = 0.0;
        DelayParams off = p; off.mix = 0.0;
        EchoDelay a, b;
        check (a, b, p, off);
    }
    {
        INFO ("Reverb");
        ReverbParams p; p.mix = 50.0; p.decayS = 2.5; p.duck = 0.0;
        ReverbParams off = p; off.enabled = false;
        Reverb a, b;
        check (a, b, p, off);
    }
}
