#include "vox/RackUnit.h"
#include "vox/AutoEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vox::rackunit
{
    namespace
    {
        const char* const kMinus = "\xE2\x88\x92";   // −
        const char* const kDot = " \xC2\xB7 ";       // " · "

        // Fixed decimals without printf (whose decimal point follows the host's locale); no sign.
        std::string digits (double v, int decimals)
        {
            if (! std::isfinite (v)) v = 0.0;
            long long scale = 1;
            for (int i = 0; i < decimals; ++i) scale *= 10;
            const long long n = std::llround (std::abs (v) * static_cast<double> (scale));
            std::string s = std::to_string (n / scale);
            if (decimals > 0)
            {
                const std::string frac = std::to_string (n % scale);
                s += "." + std::string (static_cast<std::size_t> (decimals) - std::min (frac.size(), static_cast<std::size_t> (decimals)), '0') + frac;
            }
            return s;
        }

        // Written the way Voxology's window writes them (plugin/ui/app.js: fmtNum, fmtDb, fmtSigned, fmtHz ...).
        std::string num (double v, int d = 1) { return (v < 0.0 && std::abs (v) >= 0.5 * std::pow (10.0, -d) ? kMinus : "") + digits (v, d); }
        std::string db (double v) { return num (v) + " dB"; }
        std::string signedDb (double v) { return (v >= 0.05 ? "+" : "") + num (v) + " dB"; }
        std::string hz (double f) { return f >= 1000.0 ? digits (f / 1000.0, 1) + " kHz" : digits (f, 0) + " Hz"; }
        std::string pct (double v) { return digits (v, 0) + " %"; }
        std::string ms (double v) { return digits (v, 0) + " ms"; }
        std::string rate (double v) { return digits (v, v < 1.0 ? 2 : 1) + " Hz"; }
        std::string st (double v) { return std::abs (v) < 0.05 ? "0 st" : std::string (v > 0.0 ? "+" : kMinus) + digits (v, 1) + " st"; }
        std::string cut (double v) { return v < 0.05 ? "Off" : std::string (kMinus) + digits (v, 1) + " dB"; }
        std::string offOr (double v, const std::string& text) { return v < 0.05 ? "Off" : text; }
        std::string vib (double v)
        {
            if (std::abs (v) < 0.5) return "as sung";
            if (v <= -99.5) return "flat";
            return std::string (v > 0.0 ? "+" : kMinus) + digits (std::abs (v), 0) + " %";
        }

        // Where a value sits on its knob (0..1), as the parameter's range (JUCE's skew for a centre value).
        float lin (double v, double lo, double hi) { return hi > lo ? static_cast<float> (std::clamp ((v - lo) / (hi - lo), 0.0, 1.0)) : 0.5f; }
        float skew (double v, double lo, double hi, double centre)
        {
            const double x = lin (v, lo, hi);
            const double k = std::log (0.5) / std::log ((centre - lo) / (hi - lo));
            const double n = std::pow (x, k);
            return std::isfinite (n) ? static_cast<float> (std::clamp (n, 0.0, 1.0)) : 0.5f;
        }
        float choice (int i, int count) { return count > 1 ? static_cast<float> (std::clamp (i, 0, count - 1)) / static_cast<float> (count - 1) : 0.0f; }

        // Every knob the Rack can turn: its parameter (plugin/Params.h: the same range; centre: JUCE's skew for a centre
        // value, 0 = linear; steps: a choice of that many).
        struct Knob
        {
            const char* key;
            double lo, hi, centre;
            int steps;
        };
        const Knob kKnobs[] = {
            { "ptMode", 0, kPitchModes - 1, 0, kPitchModes }, { "ptAmount", 0, 100, 0, 0 }, { "ptSpeed", 0, 400, 60, 0 },
            { "ptVibrato", -100, 100, 0, 0 }, { "ptHumanize", 0, 100, 0, 0 }, { "ptFormant", -kMaxFormant, kMaxFormant, 0, 0 },
            { "ptTranspose", -12, 12, 0, 0 },
            { "clNoise", 0, Denoiser::kMaxNoiseDb, 0, 0 }, { "clRoom", 0, 100, 0, 0 }, { "clLowCut", 20, 400, 80, 0 },
            { "clPops", 0, 100, 0, 0 }, { "clClicks", 0, 100, 0, 0 }, { "clBreath", 0, BreathControl::kMaxReductionDb, 0, 0 },
            { "clGateThr", -80, -20, 0, 0 }, { "clGateRange", 0, 30, 0, 0 }, { "clMode", 0, 1, 0, 2 },
            { "eqGain1", -kEqMaxDb, kEqMaxDb, 0, 0 }, { "eqGain2", -kEqMaxDb, kEqMaxDb, 0, 0 }, { "eqGain3", -kEqMaxDb, kEqMaxDb, 0, 0 },
            { "eqGain4", -kEqMaxDb, kEqMaxDb, 0, 0 }, { "eqGain5", -kEqMaxDb, kEqMaxDb, 0, 0 },
            { "dqCut1", 0, kDynMaxCutDb, 0, 0 }, { "dqCut2", 0, kDynMaxCutDb, 0, 0 }, { "dqCut3", 0, kDynMaxCutDb, 0, 0 },
            { "dqCut4", 0, kDynMaxCutDb, 0, 0 }, { "dqSmooth", 0, 100, 0, 0 }, { "dqSens", 0, 100, 0, 0 },
            { "dsAmount", 0, 100, 0, 0 }, { "dsSens", 0, 100, 0, 0 }, { "dsFreq", 3000, 12000, 6000, 0 },
            { "rdTarget", -40, -6, 0, 0 }, { "rdRange", 0, 12, 0, 0 }, { "rdSpeed", 0, 2, 0, 3 },
            { "cpPeak", -40, 0, 0, 0 }, { "cpThr", -60, 0, 0, 0 }, { "cpRatio", 1, 10, 3, 0 }, { "cpMakeup", 0, 24, 0, 0 },
            { "cpMix", 0, 100, 0, 0 }, { "cpStyle", 0, kCompStyles - 1, 0, kCompStyles },
            { "saMode", 0, 2, 0, 3 }, { "saDrive", 0, Saturation::kMaxDriveDb, 0, 0 }, { "saMix", 0, 100, 0, 0 }, { "saAir", 0, 100, 0, 0 },
            { "dbAmount", 0, 100, 0, 0 }, { "dbWidth", 0, 100, 0, 0 }, { "hv1", 0, kHarmonies - 1, 0, kHarmonies },
            { "hv2", 0, kHarmonies - 1, 0, kHarmonies }, { "hvStack", 0, 3, 0, 4 }, { "hvLevel", 0, 100, 0, 0 },
            { "hvFormant", -kMaxFormant, kMaxFormant, 0, 0 }, { "hvCrew", 0, 100, 0, 0 },
            { "dmMode", 0, kDimensionModes - 1, 0, kDimensionModes }, { "dmAmount", 0, 100, 0, 0 },
            { "dmRate", Dimension::kMinRateHz, Dimension::kMaxRateHz, 0.6, 0 }, { "dmWidth", 0, 100, 0, 0 },
            { "dlTime", 0, kDelayDivisions - 1, 0, kDelayDivisions }, { "dlFeedback", 0, 90, 0, 0 }, { "dlMix", 0, 100, 0, 0 },
            { "dlTone", 1000, 16000, 4000, 0 }, { "dlDuck", 0, 100, 0, 0 }, { "dlThrow", 0, 100, 0, 0 }, { "dlPing", 0, 1, 0, 2 },
            { "dlChar", 0, kDelayCharacters - 1, 0, kDelayCharacters }, { "rvType", 0, kReverbTypes - 1, 0, kReverbTypes },
            { "rvDecay", 0.3, 8, 1.5, 0 }, { "rvPredelay", 0, 200, 0, 0 }, { "rvMix", 0, 100, 0, 0 }, { "rvTone", 2000, 16000, 6000, 0 },
            { "rvDuck", 0, 100, 0, 0 }, { "rvThrow", 0, 100, 0, 0 },
            { "outGain", -24, 24, 0, 0 }, { "outPeaks", 0, kPeakModes - 1, 0, kPeakModes },
            { "outCeil", OutputPeaks::kMinCeilingDb, OutputPeaks::kMaxCeilingDb, 0, 0 },
            { "umAmount", 0, 100, 0, 0 },
            { "aeStyle", 0, kStyles - 1, 0, kStyles }, { "aeIntensity", 0, kIntensities - 1, 0, kIntensities },   // the Coach's setup
        };

        const Knob* knobSpec (const std::string& key)
        {
            for (const auto& k : kKnobs)
                if (key == k.key) return &k;
            return nullptr;
        }

        float knobNorm (const Knob& k, double value)
        {
            if (k.steps >= 2) return choice (static_cast<int> (std::lround (value - k.lo)), k.steps);
            return k.centre > 0.0 ? skew (value, k.lo, k.hi, k.centre) : lin (value, k.lo, k.hi);
        }

        template <typename Array>
        const char* nameOf (const Array& names, int i) { return names[static_cast<std::size_t> (std::clamp (i, 0, static_cast<int> (names.size()) - 1))]; }

        const char* interval (int i) { return kHarmonyIntervals[static_cast<std::size_t> (std::clamp (i, 0, kHarmonies - 1))].name; }

        // Shown only (not a control of its own): the key Pitch follows, what the Beat Listener hears.
        void add (rack::Module& m, const char* name, const std::string& text, float norm, bool bi = false)
        {
            if (m.values.size() < static_cast<std::size_t> (rack::kMaxValues)) m.values.push_back ({ name, text, norm, bi, {}, 0 });
        }

        // A knob the Rack can turn: its parameter (kKnobs) places it.
        void add (rack::Module& m, const char* name, const std::string& text, const std::string& key, double value, bool bi = false)
        {
            const auto* k = knobSpec (key);
            if (k == nullptr || m.values.size() >= static_cast<std::size_t> (rack::kMaxValues)) return;
            m.values.push_back ({ name, text, knobNorm (*k, value), bi, key, k->steps });
        }

        std::string numbered (const char* prefix, int b) { return prefix + std::to_string (b + 1); }

        void join (std::string& s, const std::string& part, std::size_t limit = 64)
        {
            if (part.empty() || s.size() >= limit) return;
            s += (s.empty() ? "" : kDot) + part;
        }

        void gr (rack::Module& m, const State& state, float db)
        {
            m.meterKind = rack::MeterKind::gr;
            m.meter = state.hasMeters && m.state == rack::State::on && std::isfinite (db) ? db : 0.0f;
        }

        // 0 = major ... the beat's mode by where its home note sits in the major scale (vox::BeatKey's tonicOffset).
        const char* modeName (int offset)
        {
            switch (offset)
            {
                case 2: return "Dorian";
                case 4: return "Phrygian";
                case 5: return "Lydian";
                case 7: return "Mixolydian";
                case 9: return "Minor";
                case 11: return "Locrian";
                default: return "Major";
            }
        }

        bool cleanupIdle (const CleanupParams& c)
        {
            return c.lowCutHz <= Cleanup::kLowCutOffHz && c.gateRangeDb < 0.05 && c.popAmount < 0.05 && c.breathDb < 0.05
                && c.noiseDb < 0.05 && c.roomPct < 0.05 && c.clickAmount < 0.05;
        }

        rack::Module beatListener (const State& s, bool full)
        {
            rack::Module m;
            m.key = "vox.beat";
            m.state = rack::State::on;
            const bool has = s.beatState == 5;
            const std::string key = std::string (nameOf (kNoteNames, s.beatKey)) + " " + modeName (s.beatMode);
            m.summary = has ? "Hears " + key : "Listening for the key";
            if (s.unmaskAmount >= 0.05f) join (m.summary, "Unmask " + pct (s.unmaskAmount));
            if (full)
            {
                add (m, "Key", has ? nameOf (kNoteNames, s.beatKey) : "\xE2\x80\x94", has ? choice (s.beatKey, 12) : 0.0f);
                add (m, "Scale", has ? modeName (s.beatMode) : "\xE2\x80\x94", has ? choice (s.beatMode, 12) : 0.0f);
                const double c = std::clamp (static_cast<double> (s.beatTuneCents), -50.0, 50.0);
                add (m, "Tuning", has ? std::string (c >= 0.0 ? "+" : kMinus) + digits (c, 0) + " cents" : "\xE2\x80\x94", has ? lin (c, -50.0, 50.0) : 0.5f, true);
                add (m, "Unmask", offOr (s.unmaskAmount, pct (s.unmaskAmount)), "umAmount", s.unmaskAmount);
            }
            return m;
        }
    }

    const char* switchOf (const std::string& moduleKey)
    {
        static const std::pair<const char*, const char*> kSwitches[] = {
            { "vox.pitch", "ptOn" }, { "vox.cleanup", "clOn" }, { "vox.eq", "eqOn" }, { "vox.dyneq", "dqOn" }, { "vox.deess", "dsOn" },
            { "vox.rider", "rdOn" }, { "vox.comp", "cpOn" }, { "vox.sat", "saOn" }, { "vox.voices", "dbOn" }, { "vox.dimension", "dmOn" },
            { "vox.delay", "dlOn" }, { "vox.reverb", "rvOn" },
        };
        for (const auto& [module, param] : kSwitches)
            if (moduleKey == module) return param;
        return nullptr;
    }

    bool fromKnob (const std::string& key, float knob, double& value)
    {
        const auto* k = knobSpec (key);
        if (k == nullptr) return false;
        const double n = std::clamp (std::isfinite (knob) ? static_cast<double> (knob) : 0.0, 0.0, 1.0);
        if (k->steps >= 2)
            value = k->lo + std::round (n * (k->steps - 1));
        else if (k->centre > 0.0)
        {
            // JUCE's skew for a centre value: n = x^s, s = log 0.5 / log ((centre - lo) / (hi - lo)).
            const double s = std::log (0.5) / std::log ((k->centre - k->lo) / (k->hi - k->lo));
            value = k->lo + std::pow (n, 1.0 / s) * (k->hi - k->lo);
        }
        else
            value = k->lo + n * (k->hi - k->lo);
        value = std::clamp (value, k->lo, k->hi);
        return true;
    }

    std::string keyText (int key, int scale)
    {
        if (scale <= 0) return "Chromatic (any note)";
        return std::string (nameOf (kNoteNames, key)) + " " + nameOf (kScaleNames, scale);
    }

    std::vector<rack::Module> modules (const State& s, bool full)
    {
        if (s.beat) return { beatListener (s, full) };

        const auto& p = s.params;
        std::vector<rack::Module> out;
        auto cell = [&] (const char* key, bool enabled, bool idle) {
            rack::Module m;
            m.key = key;
            m.state = ! enabled ? rack::State::off : idle ? rack::State::idle : rack::State::on;
            return m;
        };
        auto finish = [&] (rack::Module& m) {
            m.switchable = switchOf (m.key) != nullptr;
            if (m.state == rack::State::off) m.summary = "Off";
            else if (m.state == rack::State::idle) m.summary = "Set to do nothing";
            if (! full)
            {
                m.values.clear();
                m.bands.clear();
            }
            out.push_back (std::move (m));
        };

        // 01 Pitch
        {
            const auto& pt = p.pitch;
            auto m = cell ("vox.pitch", pt.enabled || s.pitchRests, PitchCorrector::isNeutral (pt));
            std::string key = keyText (pt.key, pt.scale);
            if (s.keyAuto) key = (s.keyState == 2 || s.keyState == 3) ? "Auto: listening" : "Auto: " + key;
            add (m, "Mode", nameOf (kPitchModeNames, pt.mode), "ptMode", pt.mode);
            add (m, "Amount", pct (pt.amount), "ptAmount", pt.amount);
            add (m, "Retune", ms (pt.speedMs), "ptSpeed", pt.speedMs);
            add (m, "Vibrato", vib (pt.vibrato), "ptVibrato", pt.vibrato, true);
            add (m, "Humanize", pct (pt.humanize), "ptHumanize", pt.humanize);
            add (m, "Formant", st (pt.formant), "ptFormant", pt.formant, true);
            add (m, "Key", key, pt.scale <= 0 ? 0.0f : choice (pt.key, 12));
            if (pt.transpose != 0) add (m, "Transpose", st (pt.transpose), "ptTranspose", pt.transpose, true);
            m.summary = nameOf (kPitchModeNames, pt.mode);
            join (m.summary, "Amount " + pct (pt.amount));
            join (m.summary, key);
            if (pt.transpose != 0) join (m.summary, st (pt.transpose));
            if (s.pitchRests)
            {
                m.state = rack::State::rest;
                m.summary = "Rests: Honey Tune tunes this track";
            }
            finish (m);
        }

        // 02 Cleanup
        {
            const auto& c = p.cleanup;
            auto m = cell ("vox.cleanup", c.enabled, cleanupIdle (c));
            const bool lowCut = c.lowCutHz > Cleanup::kLowCutOffHz;
            // AI mode: Noise is how far the AI may turn the sound down (the top: no limit); it takes out the room too.
            const bool aiFull = c.aiMode && c.noiseDb >= AiCleanup::kFullDb - 0.05;
            add (m, "Noise", c.aiMode && c.noiseDb >= 0.05 ? (aiFull ? std::string ("AI full") : "AI " + cut (c.noiseDb)) : cut (c.noiseDb), "clNoise", c.noiseDb);
            add (m, "Room", c.aiMode ? std::string ("by the AI") : offOr (c.roomPct, pct (c.roomPct)), "clRoom", c.roomPct);
            add (m, "Low Cut", lowCut ? hz (c.lowCutHz) : "Off", "clLowCut", c.lowCutHz);
            add (m, "Pops", offOr (c.popAmount, pct (c.popAmount)), "clPops", c.popAmount);
            add (m, "Clicks", offOr (c.clickAmount, pct (c.clickAmount)), "clClicks", c.clickAmount);
            add (m, "Breaths", cut (c.breathDb), "clBreath", c.breathDb);
            // (8 values at most: the gate as one, its range on the knob and its threshold in the text; then Mode.)
            add (m, "Gate", c.gateRangeDb >= 0.05 ? db (-c.gateRangeDb) + " under " + db (c.gateThrDb) : std::string ("Off"), "clGateRange", c.gateRangeDb);
            add (m, "Mode", c.aiMode ? "AI" : "Classic", "clMode", c.aiMode ? 1.0 : 0.0);
            if (c.aiMode)
            {
                if (c.noiseDb >= 0.05) join (m.summary, aiFull ? std::string ("AI noise + room") : "AI noise + room " + cut (c.noiseDb));
            }
            else
            {
                if (c.noiseDb >= 0.05) join (m.summary, "Noise " + cut (c.noiseDb));
                if (c.roomPct >= 0.05) join (m.summary, "Room " + pct (c.roomPct));
            }
            if (lowCut) join (m.summary, "Low cut " + hz (c.lowCutHz));
            if (c.popAmount >= 0.05) join (m.summary, "Pops " + pct (c.popAmount));
            if (c.clickAmount >= 0.05) join (m.summary, "Clicks " + pct (c.clickAmount));
            if (c.breathDb >= 0.05) join (m.summary, "Breaths " + cut (c.breathDb));
            if (c.gateRangeDb >= 0.05) join (m.summary, "Gate");
            gr (m, s, s.meters.gate);
            finish (m);
        }

        // 03 Tone EQ
        {
            const auto& e = p.eq;
            bool flat = true;
            for (const double g : e.gainDb) flat &= std::abs (g) < 0.05;
            auto m = cell ("vox.eq", e.enabled, flat);
            for (int b = 0; b < kEqBands; ++b)
            {
                const auto& info = kEqBandInfo[static_cast<std::size_t> (b)];
                const double g = e.gainDb[static_cast<std::size_t> (b)], f = e.freqHz[static_cast<std::size_t> (b)];
                add (m, info.name, signedDb (g), numbered ("eqGain", b), g, true);
                if (std::abs (g) < 0.05) continue;
                join (m.summary, std::string (info.name) + " " + signedDb (g));
                const auto type = info.type == 0 ? rack::BandType::lowShelf : info.type == 2 ? rack::BandType::highShelf : rack::BandType::bell;
                m.bands.push_back ({ type, static_cast<float> (f), static_cast<float> (g), static_cast<float> (info.q), info.name, true });
            }
            finish (m);
        }

        // 04 Dynamic EQ (with Smooth)
        {
            const auto& d = p.dynEq;
            bool idle = d.smoothPct < 0.05;
            for (const double c : d.maxCutDb) idle &= c < 0.05;
            auto m = cell ("vox.dyneq", d.enabled, idle);
            float deepest = 0.0f;
            for (int b = 0; b < kDynBands; ++b)
            {
                const auto& info = kDynBandInfo[static_cast<std::size_t> (b)];
                const double c = d.maxCutDb[static_cast<std::size_t> (b)], f = d.freqHz[static_cast<std::size_t> (b)];
                add (m, info.name, cut (c), numbered ("dqCut", b), c);
                if (c < 0.05) continue;
                join (m.summary, std::string (info.name) + " up to " + cut (c));
                // The cut it makes now (it moves: only while the band jumps out).
                const float now = s.hasMeters ? std::clamp (s.meters.dynEq[static_cast<std::size_t> (b)], static_cast<float> (-kDynMaxCutDb), 0.0f) : 0.0f;
                deepest = std::min (deepest, now);
                m.bands.push_back ({ rack::BandType::bell, static_cast<float> (f), now, static_cast<float> (info.q), info.name, true });
            }
            add (m, "Smooth", pct (d.smoothPct), "dqSmooth", d.smoothPct);
            add (m, "Sensitivity", pct (d.sensitivity), "dqSens", d.sensitivity);
            if (d.smoothPct >= 0.05) join (m.summary, "Smooth " + pct (d.smoothPct));
            gr (m, s, deepest);
            finish (m);
        }

        // 05 De-Esser
        {
            const auto& d = p.deEsser;
            auto m = cell ("vox.deess", d.enabled, d.amount < 0.05);
            add (m, "Amount", pct (d.amount), "dsAmount", d.amount);
            add (m, "Sensitivity", pct (d.sensitivity), "dsSens", d.sensitivity);
            add (m, "Frequency", hz (d.freqHz), "dsFreq", d.freqHz);
            m.summary = "Amount " + pct (d.amount);
            join (m.summary, "above " + hz (d.freqHz));
            gr (m, s, s.meters.deEss);
            finish (m);
        }

        // 06 Rider
        {
            const auto& r = p.rider;
            static const char* const kSpeeds[] = { "Slow", "Medium", "Fast" };
            auto m = cell ("vox.rider", r.enabled, r.rangeDb < 0.05);
            const std::string range = r.rangeDb < 0.05 ? "Off" : "\xC2\xB1" + num (r.rangeDb) + " dB";
            add (m, "Target", db (r.targetDb), "rdTarget", r.targetDb);
            add (m, "Range", range, "rdRange", r.rangeDb);
            add (m, "Speed", kSpeeds[std::clamp (r.speed, 0, 2)], "rdSpeed", r.speed);
            m.summary = "Target " + db (r.targetDb);
            join (m.summary, range);
            gr (m, s, s.meters.rider);   // the gain it rides now (+ lifts a quiet word)
            finish (m);
        }

        // 07 Compressor
        {
            const auto& c = p.comp;
            auto m = cell ("vox.comp", c.enabled, VocalCompressor::isNeutral (c));
            const bool peak = c.peakThrDb <= -0.05, level = c.ratio >= 1.005;
            const int style = std::clamp (c.style, 0, kCompStyles - 1);
            add (m, "Style", kCompStyleNames[static_cast<std::size_t> (style)], "cpStyle", style);
            add (m, "Peak", peak ? db (c.peakThrDb) : "Off", "cpPeak", c.peakThrDb);
            add (m, "Threshold", db (c.thrDb), "cpThr", c.thrDb);
            add (m, "Ratio", digits (c.ratio, 1) + ":1", "cpRatio", c.ratio);
            add (m, "Makeup", signedDb (c.makeupDb), "cpMakeup", c.makeupDb);
            add (m, "Mix", pct (c.mix), "cpMix", c.mix);
            if (level) join (m.summary, "Ratio " + digits (c.ratio, 1) + ":1" + kDot + "Threshold " + db (c.thrDb));
            if (peak) join (m.summary, "Peak " + db (c.peakThrDb));
            if (c.mix < 99.5) join (m.summary, "Mix " + pct (c.mix));
            if (m.summary.empty()) m.summary = "Makeup " + signedDb (c.makeupDb);
            if (style != 0) m.summary = std::string (kCompStyleNames[static_cast<std::size_t> (style)]) + kDot + m.summary;
            gr (m, s, s.meters.peakGr + s.meters.levelGr);
            finish (m);
        }

        // 08 Saturation
        {
            const auto& c = p.saturation;
            static const char* const kModes[] = { "Tape", "Tube", "Clip" };
            const int mode = std::clamp (static_cast<int> (c.mode), 0, 2);
            const bool air = c.air >= 0.05;
            auto m = cell ("vox.sat", c.enabled, (c.driveDb < 0.05 || c.mix < 0.05) && ! air);
            add (m, "Mode", kModes[mode], "saMode", mode);
            add (m, "Drive", db (c.driveDb), "saDrive", c.driveDb);
            add (m, "Mix", pct (c.mix), "saMix", c.mix);
            add (m, "Air", air ? pct (c.air) : "Off", "saAir", c.air);
            if (c.driveDb >= 0.05 && c.mix >= 0.05)
            {
                m.summary = kModes[mode];
                join (m.summary, "Drive " + db (c.driveDb));
                join (m.summary, "Mix " + pct (c.mix));
            }
            if (air) join (m.summary, "Air " + pct (c.air));
            if (m.summary.empty()) m.summary = kModes[mode];
            finish (m);
        }

        // 09 Voices (the double and the harmonies: one switch)
        {
            const auto& d = p.doubler;
            const auto& v = p.voices;
            const bool doubleOn = d.amount >= 0.05, voicesOn = ! HarmonyVoices::isNeutral (v);
            auto m = cell ("vox.voices", d.enabled, ! doubleOn && ! voicesOn);
            add (m, "Double", offOr (d.amount, pct (d.amount)), "dbAmount", d.amount);
            add (m, "Width", pct (d.width), "dbWidth", d.width);
            if (v.midi)
                add (m, "Voices", "MIDI notes", 1.0f);
            else
            {
                add (m, "Voice 1", interval (v.interval[0]), "hv1", v.interval[0]);
                add (m, "Voice 2", interval (v.interval[1]), "hv2", v.interval[1]);
                add (m, "Stack", v.stack <= 0 ? "Off" : std::to_string (kStackSizes[static_cast<std::size_t> (std::clamp (v.stack, 0, 3))]), "hvStack", v.stack);
            }
            add (m, "Level", pct (v.level), "hvLevel", v.level);
            add (m, "Formant", st (v.formant), "hvFormant", v.formant, true);
            add (m, "Crew", pct (v.crew), "hvCrew", v.crew);
            if (doubleOn) join (m.summary, "Double " + pct (d.amount));
            if (voicesOn)
            {
                std::string harmony;
                if (v.midi) harmony = "MIDI notes";
                else
                    for (const int i : v.interval)
                        if (i > 0) harmony += (harmony.empty() ? "" : " + ") + std::string (interval (i));
                if (! v.midi && v.stack > 0) harmony += (harmony.empty() ? "Stack " : " + stack ") + std::to_string (kStackSizes[static_cast<std::size_t> (std::clamp (v.stack, 0, 3))]);
                join (m.summary, harmony);
            }
            finish (m);
        }

        // 10 Dimension
        {
            const auto& d = p.dimension;
            auto m = cell ("vox.dimension", d.enabled, d.amount < 0.05);
            add (m, "Mode", nameOf (kDimensionNames, d.mode), "dmMode", d.mode);
            add (m, "Amount", pct (d.amount), "dmAmount", d.amount);
            add (m, "Speed", rate (d.rateHz), "dmRate", d.rateHz);
            add (m, "Width", pct (d.width), "dmWidth", d.width);
            m.summary = nameOf (kDimensionNames, d.mode);
            join (m.summary, "Amount " + pct (d.amount));
            finish (m);
        }

        // 11 Delay (its Throw: an echo on each line's last word)
        {
            const auto& d = p.delay;
            const double echo = p.throws.echo;
            auto m = cell ("vox.delay", d.enabled, d.mix < 0.05 && echo < 0.05);
            const int character = std::clamp (d.character, 0, kDelayCharacters - 1);
            add (m, "Character", nameOf (kDelayCharacterNames, character), "dlChar", character);
            add (m, "Time", nameOf (kDelayNames, d.division), "dlTime", d.division);
            add (m, "Feedback", pct (d.feedback), "dlFeedback", d.feedback);
            add (m, "Mix", pct (d.mix), "dlMix", d.mix);
            add (m, "Tone", hz (d.toneHz), "dlTone", d.toneHz);
            add (m, "Duck", pct (d.duck), "dlDuck", d.duck);
            add (m, "Throw", offOr (echo, pct (echo)), "dlThrow", echo);
            add (m, "Ping-Pong", d.pingPong ? "On" : "Off", "dlPing", d.pingPong ? 1.0 : 0.0);
            m.summary = nameOf (kDelayNames, d.division);
            if (character != 0) join (m.summary, nameOf (kDelayCharacterNames, character));
            if (d.mix >= 0.05) join (m.summary, "Mix " + pct (d.mix));
            if (echo >= 0.05) join (m.summary, "Throw " + pct (echo));
            if (d.pingPong) join (m.summary, "Ping-pong");
            finish (m);
        }

        // 12 Reverb (its Throw: a wash after each line)
        {
            const auto& r = p.reverb;
            const double wash = p.throws.wash;
            auto m = cell ("vox.reverb", r.enabled, r.mix < 0.05 && wash < 0.05);
            const int type = std::clamp (r.type, 0, kReverbTypes - 1);
            add (m, "Type", nameOf (kReverbTypeNames, type), "rvType", type);
            add (m, "Decay", digits (r.decayS, 1) + " s", "rvDecay", r.decayS);
            add (m, "Pre-delay", ms (r.predelayMs), "rvPredelay", r.predelayMs);
            add (m, "Mix", pct (r.mix), "rvMix", r.mix);
            add (m, "Tone", hz (r.toneHz), "rvTone", r.toneHz);
            add (m, "Duck", pct (r.duck), "rvDuck", r.duck);
            add (m, "Throw", offOr (wash, pct (wash)), "rvThrow", wash);
            m.summary = std::string (nameOf (kReverbTypeNames, type)) + " " + digits (r.decayS, 1) + " s";
            if (r.mix >= 0.05) join (m.summary, "Mix " + pct (r.mix));
            if (wash >= 0.05) join (m.summary, "Throw " + pct (wash));
            finish (m);
        }

        // 13 Output
        {
            const int peakMode = std::clamp (p.peakMode, 0, kPeakModes - 1);
            auto m = cell ("vox.output", true, std::abs (p.outputDb) < 0.05 && peakMode == 0);
            add (m, "Output", signedDb (p.outputDb), "outGain", p.outputDb, true);
            add (m, "Peaks", nameOf (kPeakModeNames, peakMode), "outPeaks", peakMode);
            add (m, "Ceiling", signedDb (p.ceilingDb), "outCeil", p.ceilingDb);
            m.summary = signedDb (p.outputDb);
            if (peakMode != 0) join (m.summary, std::string (nameOf (kPeakModeNames, peakMode)) + " at " + signedDb (p.ceilingDb));
            finish (m);
            if (out.back().state == rack::State::idle) out.back().summary = "0.0 dB";
        }

        // Beta 33: the movable inserts (Tone EQ ... Saturation) in the order they play, marked so the Rack can move them.
        {
            const auto order = validOrder (p.order) ? p.order : kDefaultOrder;
            auto keyOf = [] (int module) { return std::string ("vox.") + insertKey (module); };
            std::vector<std::size_t> slots;
            for (std::size_t i = 0; i < out.size(); ++i)
                for (const int m : kDefaultOrder)
                    if (out[i].key == keyOf (m)) slots.push_back (i);
            if (slots.size() == static_cast<std::size_t> (kMovable))
            {
                std::vector<rack::Module> inOrder;
                for (const int m : order)
                    for (const auto i : slots)
                        if (out[i].key == keyOf (m)) inOrder.push_back (out[i]);
                for (std::size_t j = 0; j < slots.size() && j < inOrder.size(); ++j)
                {
                    auto& m = out[slots[j]];
                    m = std::move (inOrder[j]);
                    m.movable = true;
                    m.moved = order[j] != kDefaultOrder[j];
                }
            }
        }
        return out;
    }
}
