// Sonicology Rack: what Voxology (vox/RackUnit.h) and Honey Tune (HoneyView.h) tell the Rack.

#include "vox/RackUnit.h"
#include "HoneyView.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

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
    CHECK (find (ms, "vox.reverb")->summary == "1.4 s \xC2\xB7 Mix 12 %");
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
