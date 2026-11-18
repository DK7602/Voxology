// Sonicology Rack: what Voxology (vox/RackUnit.h) and Honey Tune (HoneyView.h) tell the Rack.

#include "vox/RackUnit.h"
#include "HoneyView.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using namespace vox;

namespace
{
    const rack::Module* find (const std::vector<rack::Module>& ms, const char* key)
    {
        for (const auto& m : ms)
            if (m.key == key) return &m;
        return nullptr;
    }
    const rack::Value* value (const rack::Module& m, const char* name)
    {
        for (const auto& v : m.values)
            if (v.name == name) return &v;
        return nullptr;
    }
    // A new Voxology: every module switched on, every amount at zero.
    rackunit::State fresh()
    {
        rackunit::State st;
        st.params.pitch.amount = 0.0;
        return st;
    }
}

TEST_CASE ("Rack unit (Voxology): the 13 cells in the window's order; a new Voxology is idle, not off", "[rack]")
{
    const auto ms = rackunit::modules (fresh(), true);
    const char* order[] = { "vox.pitch", "vox.cleanup", "vox.eq", "vox.dyneq", "vox.deess", "vox.rider", "vox.comp", "vox.sat",
                            "vox.voices", "vox.dimension", "vox.delay", "vox.reverb", "vox.output" };
    REQUIRE (ms.size() == 13);
    for (std::size_t i = 0; i < ms.size(); ++i)
    {
        CHECK (ms[i].key == order[i]);
        CHECK (ms[i].state == rack::State::idle);
        CHECK (ms[i].values.size() <= static_cast<std::size_t> (rack::kMaxValues));
    }
    CHECK (ms[0].summary == "Set to do nothing");
    CHECK (ms.back().summary == "0.0 dB");

    auto st = fresh();
    st.params.reverb.enabled = false;
    CHECK (find (rackunit::modules (st, true), "vox.reverb")->state == rack::State::off);
    CHECK (find (rackunit::modules (st, true), "vox.reverb")->summary == "Off");
}

TEST_CASE ("Rack unit (Voxology): Pitch, as the window writes it, and resting while Honey Tune tunes the track", "[rack]")
{
    auto st = fresh();
    st.params.pitch.amount = 85.0;
    st.params.pitch.mode = kPitchClassic;
    st.params.pitch.speedMs = 20.0;
    st.params.pitch.key = 5;     // F
    st.params.pitch.scale = 2;   // Minor
    st.keyAuto = true;
    st.keyState = 1;
    const auto ms = rackunit::modules (st, true);
    const auto* pt = find (ms, "vox.pitch");
    REQUIRE (pt != nullptr);
    CHECK (pt->state == rack::State::on);
    CHECK (pt->summary == "Classic \xC2\xB7 Amount 85 % \xC2\xB7 Auto: F Minor");
    CHECK (value (*pt, "Retune")->text == "20 ms");
    CHECK (value (*pt, "Vibrato")->text == "as sung");
    CHECK (value (*pt, "Vibrato")->bi);
    CHECK (value (*pt, "Formant")->text == "0 st");
    CHECK (value (*pt, "Retune")->norm == Catch::Approx (std::pow (20.0 / 400.0, std::log (0.5) / std::log (60.0 / 400.0))).margin (1e-4));

    st.keyState = 3;   // not sure yet
    CHECK (find (rackunit::modules (st, true), "vox.pitch")->summary == "Classic \xC2\xB7 Amount 85 % \xC2\xB7 Auto: listening");

    st.pitchRests = true;
    const auto rest = rackunit::modules (st, true);
    CHECK (find (rest, "vox.pitch")->state == rack::State::rest);
    CHECK (find (rest, "vox.pitch")->summary == "Rests: Honey Tune tunes this track");
    CHECK (rackunit::keyText (0, 0) == "Chromatic (any note)");
}

TEST_CASE ("Rack unit (Voxology): Tone EQ bands draw its curve, the Dynamic EQ its cuts as they happen", "[rack]")
{
    auto st = fresh();
    st.params.eq.gainDb = { 0.0, -3.0, 0.0, 2.5, 2.0 };
    st.params.dynEq.maxCutDb = { 0.0, 0.0, 0.0, 4.0 };
    st.params.dynEq.freqHz[3] = 3200.0;
    st.params.dynEq.smoothPct = 30.0;
    st.hasMeters = true;
    st.meters.dynEq[3] = -2.5f;
    const auto ms = rackunit::modules (st, true);

    const auto* eq = find (ms, "vox.eq");
    REQUIRE (eq != nullptr);
    CHECK (eq->state == rack::State::on);
    CHECK (eq->summary == "Mud \xE2\x88\x92" "3.0 dB \xC2\xB7 Presence +2.5 dB \xC2\xB7 Air +2.0 dB");
    REQUIRE (eq->bands.size() == 3);
    CHECK (eq->bands[0].type == rack::BandType::bell);
    CHECK (eq->bands[0].hz == Catch::Approx (300.0f));
    CHECK (eq->bands[2].type == rack::BandType::highShelf);
    CHECK (eq->values.size() == 5);
    CHECK (value (*eq, "Body")->text == "0.0 dB");
    CHECK (value (*eq, "Mud")->norm == Catch::Approx (9.0f / 24.0f));

    const auto* dq = find (ms, "vox.dyneq");
    REQUIRE (dq != nullptr);
    CHECK (dq->summary == "Harsh up to \xE2\x88\x92" "4.0 dB \xC2\xB7 Smooth 30 %");
    REQUIRE (dq->bands.size() == 1);
    CHECK (dq->bands[0].hz == Catch::Approx (3200.0f));
    CHECK (dq->bands[0].db == Catch::Approx (-2.5f));
    CHECK (dq->meterKind == rack::MeterKind::gr);
    CHECK (dq->meter == Catch::Approx (-2.5f));
    CHECK (value (*dq, "Boom")->text == "Off");
}

TEST_CASE ("Rack unit (Voxology): the compressor's gain reduction, the voices, delay and reverb with their throws", "[rack]")
{
    auto st = fresh();
    st.params.comp.thrDb = -18.0;
    st.params.comp.ratio = 3.0;
    st.params.comp.peakThrDb = -6.0;
    st.hasMeters = true;
    st.meters.peakGr = -1.0f;
    st.meters.levelGr = -3.0f;
    st.params.doubler.amount = 40.0;
    st.params.voices.interval = { 1, 2 };   // 3rd up, 5th up
    st.params.voices.level = 50.0;
    st.params.delay.mix = 10.0;
    st.params.throws.echo = 40.0;
    st.params.throws.wash = 0.0;
    st.params.reverb.mix = 12.0;
    st.params.reverb.decayS = 1.4;
    const auto ms = rackunit::modules (st, true);

    const auto* cp = find (ms, "vox.comp");
    CHECK (cp->summary == "Ratio 3.0:1 \xC2\xB7 Threshold \xE2\x88\x92" "18.0 dB \xC2\xB7 Peak \xE2\x88\x92" "6.0 dB");
    CHECK (cp->meter == Catch::Approx (-4.0f));
    CHECK (value (*cp, "Ratio")->text == "3.0:1");

    const auto* vc = find (ms, "vox.voices");
    CHECK (vc->state == rack::State::on);
    CHECK (vc->summary == "Double 40 % \xC2\xB7 3rd up + 5th up");
    CHECK (value (*vc, "Voice 1")->text == "3rd up");

    CHECK (find (ms, "vox.delay")->summary == "1/4 \xC2\xB7 Mix 10 % \xC2\xB7 Throw 40 %");
    CHECK (value (*find (ms, "vox.delay"), "Throw")->text == "40 %");
    CHECK (find (ms, "vox.reverb")->summary == "Plate 1.4 s \xC2\xB7 Mix 12 %");   // (the type first, since Beta 21)
    CHECK (find (ms, "vox.cleanup")->meter == 0.0f);   // idle: no meter

    st.params.comp.enabled = false;   // switched off: no meter, whatever the last block said
    CHECK (find (rackunit::modules (st, true), "vox.comp")->meter == 0.0f);
}

TEST_CASE ("Rack unit (Voxology): brief units carry keys, states and summaries only; Beat mode is one cell", "[rack]")
{
    auto st = fresh();
    st.params.eq.gainDb[1] = -3.0;
    for (const auto& m : rackunit::modules (st, false))
    {
        CHECK (m.values.empty());
        CHECK (m.bands.empty());
        CHECK_FALSE (m.summary.empty());
    }

    st.beat = true;
    st.beatState = 5;
    st.beatKey = 5;
    st.beatMode = 9;
    st.beatTuneCents = 12.0f;
    st.unmaskAmount = 50.0f;
    const auto beat = rackunit::modules (st, true);
    REQUIRE (beat.size() == 1);
    CHECK (beat[0].key == "vox.beat");
    CHECK (beat[0].summary == "Hears F Minor \xC2\xB7 Unmask 50 %");
    CHECK (value (beat[0], "Tuning")->text == "+12 cents");
    st.beatState = 4;
    CHECK (rackunit::modules (st, true)[0].summary == "Listening for the key \xC2\xB7 Unmask 50 %");
}

TEST_CASE ("Rack unit (Honey Tune): a track's clips as one cell", "[rack]")
{
    honeyui::Settings s;
    s.key = honeyui::kAutoKey;
    s.drift = 1.0;
    s.vibrato = 1.0;

    honeyui::Snapshot clip;
    clip.status = 2;
    clip.key = 5;
    clip.scale = 2;
    clip.keyFromBeat = true;
    for (int i = 0; i < 6; ++i) clip.notes.push_back ({});
    clip.notes[1].edit.moved = true;
    clip.notes[2].edit.drift = 0.0;
    clip.notes[2].edit.vibrato = 0.0;   // steadied by hand

    honeyui::RackTrack t;
    honeyui::addClip (t, clip, s);
    honeyui::Snapshot waiting;   // a second clip, still being listened to
    waiting.status = 1;
    honeyui::addClip (t, waiting, s);
    CHECK (t.clips == 2);
    CHECK (t.listening == 1);
    CHECK (t.notes == 6);

    const auto m = honeyui::rackModule (s, t, true);
    CHECK (m.key == "ht.tune");
    CHECK (m.state == rack::State::on);
    CHECK (m.summary == "2 notes edited \xC2\xB7 1 steadied \xC2\xB7 Auto: F Minor (from Voxology's beat)");
    REQUIRE (m.values.size() == 5);
    CHECK (m.values[0].text == "Auto: F");
    CHECK (m.values[1].text == "Minor");
    CHECK (m.values[3].name == "Keep Drift");
    CHECK (m.values[3].text == "100 %");
    CHECK (honeyui::rackModule (s, t, false).values.empty());

    s.drift = 0.0;
    s.vibrato = 0.0;   // KEEP DRIFT and KEEP VIBRATO at 0: every note steady
    honeyui::RackTrack all;
    honeyui::addClip (all, clip, s);
    CHECK (honeyui::rackModule (s, all, false).summary == "2 notes edited \xC2\xB7 all steady \xC2\xB7 Auto: F Minor (from Voxology's beat)");

    honeyui::RackTrack only;
    honeyui::addClip (only, waiting, s);
    CHECK (honeyui::rackModule (s, only, false).summary == "Listening to the clip");
}

TEST_CASE ("Rack unit: Honey Tune with Pitch resting on the same track reads as fine together", "[rack]")
{
    auto st = fresh();
    st.pitchRests = true;
    rack::Unit vox;
    vox.product = rack::Product::voxology;
    vox.role = rack::Role::vocal;
    vox.track = "Chorus Vox";
    vox.hash = suite::nameHash ("Chorus Vox");
    vox.version = suite::packVersion (0, 55, 0);
    vox.modules = rackunit::modules (st, false);

    honeyui::Settings s;
    honeyui::RackTrack t;
    rack::Unit honey;
    honey.product = rack::Product::honeytune;
    honey.role = rack::Role::vocal;
    honey.track = "Chorus Vox";
    honey.hash = vox.hash;
    honey.version = vox.version;
    honey.modules = { honeyui::rackModule (s, t, false) };

    auto mem = std::make_unique<unsigned char[]> (rack::Link::bytes() + 64);
    void* aligned = mem.get() + (64 - reinterpret_cast<std::uintptr_t> (mem.get()) % 64) % 64;
    rack::Link shared (aligned);
    const int a = shared.claim (1, 10), b = shared.claim (2, 10);
    REQUIRE (shared.publish (a, 1, vox, 10));
    REQUIRE (shared.publish (b, 2, honey, 10));
    bool rests = false, twoTuners = false;
    for (const auto& l : rack::together (shared.units (10)))
    {
        rests |= l.level == rack::Level::ok && l.text.find ("rests") != std::string::npos;
        twoTuners |= l.level == rack::Level::warn;
    }
    CHECK (rests);
    CHECK_FALSE (twoTuners);
}

TEST_CASE ("Rack unit (Voxology): every knob the Rack can turn names its parameter, and turns back to its value", "[rack]")
{
    auto st = fresh();
    st.params.pitch.amount = 80.0;
    st.params.pitch.speedMs = 120.0;
    st.params.pitch.transpose = -3;
    st.params.comp.thrDb = -18.0;
    st.params.comp.ratio = 4.0;
    st.params.rider.speed = 2;
    st.params.dynEq.maxCutDb[2] = 6.0;
    st.params.eq.gainDb[2] = 3.5;
    st.params.delay.division = 4;
    st.params.delay.pingPong = true;
    st.params.reverb.decayS = 2.4;
    st.params.dimension.rateHz = 1.2;

    // Every value is a knob the Rack can turn, except what is only shown (the key Pitch follows).
    const auto ms = rackunit::modules (st, true);
    int turnable = 0;
    for (const auto& m : ms)
    {
        CAPTURE (m.key);
        CHECK (m.switchable == (m.key != "vox.output"));
        if (m.switchable) CHECK (rackunit::switchOf (m.key) != nullptr);
        for (const auto& v : m.values)
        {
            CAPTURE (v.name);
            if (m.key == "vox.pitch" && v.name == "Key") { CHECK (v.key.empty()); continue; }
            REQUIRE_FALSE (v.key.empty());
            double back = 0.0;
            CHECK (rackunit::fromKnob (v.key, v.norm, back));
            ++turnable;
        }
    }
    CHECK (turnable > 55);
    CHECK (rackunit::switchOf ("vox.output") == nullptr);
    CHECK (std::string (rackunit::switchOf ("vox.comp")) == "cpOn");

    // Back from where the knob sits: linear, skewed (JUCE's centre), a choice, a band's own control.
    auto back = [&] (const char* module, const char* name) {
        const auto* v = value (*find (ms, module), name);
        REQUIRE (v != nullptr);
        double x = -999.0;
        REQUIRE (rackunit::fromKnob (v->key, v->norm, x));
        return x;
    };
    CHECK (back ("vox.comp", "Threshold") == Catch::Approx (-18.0).margin (1e-3));
    CHECK (back ("vox.comp", "Ratio") == Catch::Approx (4.0).margin (1e-3));
    CHECK (back ("vox.pitch", "Retune") == Catch::Approx (120.0).margin (1e-2));
    CHECK (back ("vox.pitch", "Amount") == Catch::Approx (80.0).margin (1e-3));
    CHECK (back ("vox.pitch", "Transpose") == Catch::Approx (-3.0).margin (1e-3));
    CHECK (back ("vox.rider", "Speed") == 2.0);
    CHECK (value (*find (ms, "vox.rider"), "Speed")->steps == 3);
    CHECK (back ("vox.delay", "Time") == 4.0);
    CHECK (back ("vox.delay", "Ping-Pong") == 1.0);
    CHECK (back ("vox.reverb", "Decay") == Catch::Approx (2.4).margin (1e-3));
    CHECK (back ("vox.dimension", "Speed") == Catch::Approx (1.2).margin (1e-3));
    CHECK (value (*find (ms, "vox.dyneq"), kDynBandInfo[2].name)->key == "dqCut3");
    CHECK (back ("vox.dyneq", kDynBandInfo[2].name) == Catch::Approx (6.0).margin (1e-3));
    CHECK (value (*find (ms, "vox.eq"), kEqBandInfo[2].name)->key == "eqGain3");

    // Choices land on whole options; past the ends stays in range; not a knob: refused.
    double x = 0.0;
    REQUIRE (rackunit::fromKnob ("rdSpeed", 0.4f, x));
    CHECK (x == 1.0);
    REQUIRE (rackunit::fromKnob ("cpThr", 3.0f, x));
    CHECK (x == 0.0);
    REQUIRE (rackunit::fromKnob ("cpThr", std::nanf (""), x));
    CHECK (x == -60.0);
    CHECK_FALSE (rackunit::fromKnob ("ptKey", 0.5f, x));

    // Beat mode: Unmask is the knob; what it hears is only shown.
    auto beat = fresh();
    beat.beat = true;
    beat.beatState = 5;
    beat.unmaskAmount = 40.0f;
    const auto b = rackunit::modules (beat, true);
    REQUIRE (b.size() == 1);
    CHECK_FALSE (b[0].switchable);
    for (const auto& v : b[0].values) CHECK (v.key.empty() == (v.name != "Unmask"));
}

TEST_CASE ("Rack unit (Voxology): the Rack's knobs are Voxology's parameters", "[rack]")
{
    // Every key a Rack knob or switch names is declared in plugin/Params.h (eqGain1 .. through n ("eqGain", b)).
    std::ifstream in (VOX_PARAMS_PATH);
    REQUIRE (in.good());
    const std::string params ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    auto st = fresh();
    st.params.pitch.transpose = 2;
    for (const auto& m : rackunit::modules (st, true))
    {
        if (const char* sw = rackunit::switchOf (m.key)) CHECK (params.find ("\"" + std::string (sw) + "\"") != std::string::npos);
        for (const auto& v : m.values)
        {
            if (v.key.empty()) continue;
            CAPTURE (v.key);
            std::string prefix = v.key;
            while (! prefix.empty() && std::isdigit (static_cast<unsigned char> (prefix.back()))) prefix.pop_back();
            const bool numbered = prefix.size() < v.key.size();
            const bool declared = params.find ("(\"" + v.key + "\"") != std::string::npos
                               || (numbered && params.find ("n (\"" + prefix + "\", b)") != std::string::npos);
            CHECK (declared);
        }
    }
    CHECK (params.find ("(\"umAmount\"") != std::string::npos);
}

TEST_CASE ("Rack unit (Honey Tune): the Rack turns Snap, Drift and Vibrato; the key stays Honey Tune's choice", "[rack]")
{
    honeyui::Settings s;
    s.snap = 0.8;
    s.drift = 0.25;
    s.vibrato = 0.5;
    honeyui::RackTrack t;
    const auto m = honeyui::rackModule (s, t, true);
    CHECK_FALSE (m.switchable);
    for (const auto& v : m.values)
    {
        CAPTURE (v.name);
        if (v.name == "Key" || v.name == "Scale") { CHECK (v.key.empty()); continue; }
        REQUIRE_FALSE (v.key.empty());
        honeyui::Settings back;
        REQUIRE (honeyui::rackSet (back, v.key, v.norm));
        if (v.key == "snap") CHECK (back.snap == Catch::Approx (0.8));
        if (v.key == "drift") CHECK (back.drift == Catch::Approx (0.25));
        if (v.key == "vibrato") CHECK (back.vibrato == Catch::Approx (0.5));
    }
    honeyui::Settings x;
    CHECK (honeyui::rackSet (x, "snap", 7.0f));
    CHECK (x.snap == 1.0);
    CHECK_FALSE (honeyui::rackSet (x, "key", 0.5f));
}

TEST_CASE ("Rack unit (Honey Tune): a double-click puts a knob back to Honey Tune's default", "[rack]")
{
    honeyui::Settings s;
    const honeyui::Settings def;
    s.snap = 0.1;
    s.drift = 0.9;
    REQUIRE (honeyui::rackReset (s, "snap"));
    CHECK (s.snap == def.snap);
    CHECK (s.drift == 0.9);
    REQUIRE (honeyui::rackReset (s, "drift"));
    CHECK (s.drift == def.drift);
    CHECK_FALSE (honeyui::rackReset (s, "key"));
}

TEST_CASE ("Rack unit (Voxology): Beta 33, the movable inserts in the order they play, marked movable / moved", "[rack]")
{
    auto key = [] (const rack::Module& m) { return m.key; };
    {
        const auto ms = rackunit::modules (fresh(), true);
        for (const auto& m : ms)
        {
            INFO (m.key);
            const bool movable = m.key == "vox.eq" || m.key == "vox.dyneq" || m.key == "vox.deess" || m.key == "vox.rider" || m.key == "vox.comp" || m.key == "vox.sat";
            CHECK (m.movable == movable);
            CHECK_FALSE (m.moved);
        }
    }
    auto st = fresh();
    st.params.comp.ratio = 3.0;   // (some settings: they move with their module)
    st.params.order = movedTo (kDefaultOrder, static_cast<int> (Module::comp), 0);
    const auto ms = rackunit::modules (st, true);
    REQUIRE (ms.size() == 13);
    const char* order[] = { "vox.pitch", "vox.cleanup", "vox.comp", "vox.eq", "vox.dyneq", "vox.deess", "vox.rider", "vox.sat",
                            "vox.voices", "vox.dimension", "vox.delay", "vox.reverb", "vox.output" };
    for (std::size_t i = 0; i < ms.size(); ++i) CHECK (key (ms[i]) == order[i]);
    CHECK (ms[2].moved);   // the compressor: first of the inserts now
    CHECK (ms[3].moved);   // the EQ: one down
    CHECK_FALSE (ms[7].moved);   // Saturation: still last
    const auto& comp = ms[2];
    CHECK_FALSE (comp.values.empty());
    CHECK (comp.switchable);
    // Beat mode: one cell, nothing to move
    st.beat = true;
    const auto beat = rackunit::modules (st, true);
    REQUIRE (beat.size() == 1);
    CHECK_FALSE (beat[0].movable);
}
