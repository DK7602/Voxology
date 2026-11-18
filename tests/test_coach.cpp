// The Rack's Coach for a vocal (Beta 26): what stands out against Auto-Edit's aim, measured before and after the plan,
// and the plan as the Rack reads it (valid JSON, never longer than the Rack's report space).
#include "Signals.h"
#include "vox/CoachPlan.h"
#include "vox/VocalChain.h"

#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox;
using Catch::Approx;

namespace {
constexpr double kSr = 48000.0;

/** A small JSON checker: one value, nothing after it (objects, arrays, strings, numbers, true / false / null). */
struct Checker
{
    const std::string& s;
    size_t i = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) ++i; }
    bool lit (const char* w) { const std::string t (w); if (s.compare (i, t.size(), t) != 0) return false; i += t.size(); return true; }
    bool str()
    {
        if (s[i] != '"') return false;
        for (++i; i < s.size(); ++i)
        {
            if (s[i] == '\\') { ++i; continue; }
            if (static_cast<unsigned char> (s[i]) < 0x20) return false;
            if (s[i] == '"') { ++i; return true; }
        }
        return false;
    }
    bool number()
    {
        const size_t start = i;
        if (s[i] == '-') ++i;
        while (i < s.size() && (std::isdigit (static_cast<unsigned char> (s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        return i > start;
    }
    bool value()
    {
        ws();
        if (i >= s.size()) return false;
        bool ok = false;
        if (s[i] == '{')
        {
            ++i; ws();
            if (s[i] == '}') { ++i; ok = true; }
            else
                for (;;)
                {
                    ws();
                    if (! str()) return false;
                    ws();
                    if (s[i++] != ':' || ! value()) return false;
                    ws();
                    if (s[i] == ',') { ++i; continue; }
                    if (s[i] == '}') { ++i; ok = true; }
                    break;
                }
        }
        else if (s[i] == '[')
        {
            ++i; ws();
            if (s[i] == ']') { ++i; ok = true; }
            else
                for (;;)
                {
                    if (! value()) return false;
                    ws();
                    if (s[i] == ',') { ++i; continue; }
                    if (s[i] == ']') { ++i; ok = true; }
                    break;
                }
        }
        else if (s[i] == '"') ok = str();
        else if (s[i] == 't') ok = lit ("true");
        else if (s[i] == 'f') ok = lit ("false");
        else if (s[i] == 'n') ok = lit ("null");
        else ok = number();
        ws();
        return ok;
    }
};
bool validJson (const std::string& s) { Checker c { s }; return c.value() && c.i == s.size(); }

size_t count (const std::string& s, const std::string& what)
{
    size_t n = 0;
    for (size_t at = s.find (what); at != std::string::npos; at = s.find (what, at + 1)) ++n;
    return n;
}
}

TEST_CASE ("Coach: what stands out against the aim", "[coach]")
{
    const auto& bands = analysisBands();
    const auto target = styleTarget (0, 150.0);
    VocalAnalysis a;
    a.bandDb = target;
    for (size_t i = 0; i < bands.size(); ++i)
    {
        if (bands[i] >= 200.0 && bands[i] <= 400.0) a.bandDb[i] += 4.0;     // muddy
        if (bands[i] >= 10000.0) a.bandDb[i] -= 5.0;                        // dull on top
    }
    CHECK (coach::regionOffset (a.bandDb, target, 200.0, 400.0) == Approx (4.0));
    CHECK (coach::regionOffset (a.bandDb, target, 500.0, 800.0) == Approx (0.0));
    a.sibilanceDb = 2.0;
    a.sibilanceHz = 7000.0;
    a.rangeDb = 13.0;
    a.heardGaps = true;
    a.noiseFloorDb = -45.0;

    const auto found = coach::issues (a, target, -4.0, "Trap Lead's balance");
    auto has = [&] (const std::string& key) { for (const auto& i : found) if (i.key == key) return &i; return static_cast<const coach::Issue*> (nullptr); };
    REQUIRE (has ("mud") != nullptr);
    CHECK (has ("mud")->warn);
    CHECK (has ("mud")->db == Approx (4.0));
    CHECK (has ("mud")->text.find ("4.0 dB over Trap Lead's balance") != std::string::npos);
    REQUIRE (has ("air") != nullptr);
    CHECK (has ("air")->name == "Lack of air");
    CHECK_FALSE (has ("air")->warn);            // too little is taste, not a fault
    REQUIRE (has ("sibilance") != nullptr);
    CHECK (has ("sibilance")->db == Approx (6.0));
    CHECK (has ("swing") != nullptr);
    CHECK (has ("noise") != nullptr);
    CHECK (has ("boxy") == nullptr);
    CHECK (found.front().warn);                 // the red ones first

    // A take right on the aim: nothing to say.
    VocalAnalysis fine;
    fine.bandDb = target;
    fine.rangeDb = 6.0;
    CHECK (coach::issues (fine, target, -4.0, "x").empty());

    // Before -> after through a plan that took the mud down.
    VocalAnalysis after = a;
    for (size_t i = 0; i < bands.size(); ++i)
        if (bands[i] >= 200.0 && bands[i] <= 400.0) after.bandDb[i] -= 3.0;
    after.sibilanceDb = -4.5;
    after.rangeDb = 6.0;
    const auto res = coach::results (a, after, target, -4.0);
    auto got = [&] (const std::string& key) { for (const auto& r : res) if (r.key == key) return &r; return static_cast<const coach::Result*> (nullptr); };
    REQUIRE (got ("mud") != nullptr);
    CHECK (got ("mud")->before == Approx (4.0));
    CHECK (got ("mud")->after == Approx (1.0));
    CHECK (got ("mud")->better);
    REQUIRE (got ("sibilance") != nullptr);
    CHECK (got ("sibilance")->better);
    REQUIRE (got ("swing") != nullptr);
    CHECK (got ("swing")->after == Approx (6.0));
    CHECK (res.size() <= 6);
}

TEST_CASE ("Coach: a real take, Auto-Edit's plan, measured through it", "[coach]")
{
    const auto x = testsig::vocal (kSr, 14.0, -58.0, -1.0);
    AutoEditSettings s { 0, 1, 140.0 };
    const auto r = autoEdit ({ x }, kSr, s);
    REQUIRE (r.ok);
    const auto target = toneTarget (s.style, r.analysis.f0Median, nullptr);
    CHECK (target == styleTarget (s.style, r.analysis.f0Median));
    const double sibAim = sibilanceTargetDb (s.style, s.intensity, nullptr);
    CHECK (sibAim == Approx (-4.0));

    const auto rendered = VocalChain::render ({ x }, kSr, r.params);
    const auto after = analyseVocal (rendered, kSr);
    // The take is noisy and very sibilant: the plan takes the "s" down, measured on the take itself.
    REQUIRE (r.analysis.sibilanceDb > sibAim + 2.0);
    CHECK (after.sibilanceDb < r.analysis.sibilanceDb - 2.0);

    const auto json = coach::planJson (r, &after, target, sibAim, s, {}, 8191);
    INFO (json);
    CHECK (validJson (json));
    CHECK (json.size() <= 8191);
    CHECK (json.find ("\"ok\":true") != std::string::npos);
    CHECK (count (json, "\"caption\":") == 13);     // every module, in signal order
    CHECK (json.find ("\"key\":\"vox.pitch\"") < json.find ("\"key\":\"vox.output\""));
    CHECK (json.find ("\"key\":\"sibilance\"") != std::string::npos);
    CHECK (json.find ("\"after\":[") != std::string::npos);
    CHECK (json.find ("\"first\":[") != std::string::npos);

    // Squeezed: still valid, shorter texts, then fewer.
    for (const std::size_t room : { std::size_t { 5000 }, std::size_t { 3000 }, std::size_t { 1500 }, std::size_t { 200 } })
    {
        INFO (room);
        const auto small = coach::planJson (r, &after, target, sibAim, s, {}, room);
        CHECK (validJson (small));
        CHECK (small.size() <= std::max<std::size_t> (room, 140));
    }

    // A take too short to work with: why, and what to do.
    const auto shortTake = testsig::vocal (kSr, 1.0);
    const auto failed = autoEdit ({ shortTake }, kSr, s);
    REQUIRE_FALSE (failed.ok);
    const auto fj = coach::planJson (failed, nullptr, target, sibAim, s, {}, 8191);
    CHECK (validJson (fj));
    CHECK (fj.find ("\"ok\":false") != std::string::npos);
    CHECK (fj.find ("\"tip\"") != std::string::npos);
}
