#include "vox/RackUnit.h"

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

        template <typename Array>
        const char* nameOf (const Array& names, int i) { return names[static_cast<std::size_t> (std::clamp (i, 0, static_cast<int> (names.size()) - 1))]; }

        const char* interval (int i) { return kHarmonyIntervals[static_cast<std::size_t> (std::clamp (i, 0, kHarmonies - 1))].name; }

        void add (rack::Module& m, const char* name, const std::string& text, float norm, bool bi = false)
        {
            if (m.values.size() < static_cast<std::size_t> (rack::kMaxValues)) m.values.push_back ({ name, text, norm, bi });
        }

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
                add (m, "Unmask", offOr (s.unmaskAmount, pct (s.unmaskAmount)), lin (s.unmaskAmount, 0.0, 100.0));
            }
            return m;
        }
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
            add (m, "Mode", nameOf (kPitchModeNames, pt.mode), choice (pt.mode, kPitchModes));
            add (m, "Amount", pct (pt.amount), lin (pt.amount, 0.0, 100.0));
            add (m, "Retune", ms (pt.speedMs), skew (pt.speedMs, 0.0, 400.0, 60.0));
            add (m, "Vibrato", vib (pt.vibrato), lin (pt.vibrato, -100.0, 100.0), true);
            add (m, "Humanize", pct (pt.humanize), lin (pt.humanize, 0.0, 100.0));
            add (m, "Formant", st (pt.formant), lin (pt.formant, -kMaxFormant, kMaxFormant), true);
            add (m, "Key", key, pt.scale <= 0 ? 0.0f : choice (pt.key, 12));
            if (pt.transpose != 0) add (m, "Transpose", st (pt.transpose), lin (pt.transpose, -12.0, 12.0), true);
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
            add (m, "Noise", cut (c.noiseDb), lin (c.noiseDb, 0.0, 24.0));
            add (m, "Room", offOr (c.roomPct, pct (c.roomPct)), lin (c.roomPct, 0.0, 100.0));
            add (m, "Low Cut", lowCut ? hz (c.lowCutHz) : "Off", skew (c.lowCutHz, 20.0, 400.0, 80.0));
            add (m, "Pops", offOr (c.popAmount, pct (c.popAmount)), lin (c.popAmount, 0.0, 100.0));
            add (m, "Clicks", offOr (c.clickAmount, pct (c.clickAmount)), lin (c.clickAmount, 0.0, 100.0));
            add (m, "Breaths", cut (c.breathDb), lin (c.breathDb, 0.0, 24.0));
            add (m, "Gate", db (c.gateThrDb), lin (c.gateThrDb, -80.0, -20.0));
            add (m, "Gate Range", offOr (c.gateRangeDb, db (c.gateRangeDb)), lin (c.gateRangeDb, 0.0, 30.0));
            if (c.noiseDb >= 0.05) join (m.summary, "Noise " + cut (c.noiseDb));
            if (c.roomPct >= 0.05) join (m.summary, "Room " + pct (c.roomPct));
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
                add (m, info.name, signedDb (g), lin (g, -kEqMaxDb, kEqMaxDb), true);
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
                add (m, info.name, cut (c), lin (c, 0.0, kDynMaxCutDb));
                if (c < 0.05) continue;
                join (m.summary, std::string (info.name) + " up to " + cut (c));
                // The cut it makes now (it moves: only while the band jumps out).
                const float now = s.hasMeters ? std::clamp (s.meters.dynEq[static_cast<std::size_t> (b)], static_cast<float> (-kDynMaxCutDb), 0.0f) : 0.0f;
                deepest = std::min (deepest, now);
                m.bands.push_back ({ rack::BandType::bell, static_cast<float> (f), now, static_cast<float> (info.q), info.name, true });
            }
            add (m, "Smooth", pct (d.smoothPct), lin (d.smoothPct, 0.0, 100.0));
            add (m, "Sensitivity", pct (d.sensitivity), lin (d.sensitivity, 0.0, 100.0));
            if (d.smoothPct >= 0.05) join (m.summary, "Smooth " + pct (d.smoothPct));
            gr (m, s, deepest);
            finish (m);
        }

        // 05 De-Esser
        {
            const auto& d = p.deEsser;
            auto m = cell ("vox.deess", d.enabled, d.amount < 0.05);
            add (m, "Amount", pct (d.amount), lin (d.amount, 0.0, 100.0));
            add (m, "Sensitivity", pct (d.sensitivity), lin (d.sensitivity, 0.0, 100.0));
            add (m, "Frequency", hz (d.freqHz), skew (d.freqHz, 3000.0, 12000.0, 6000.0));
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
            add (m, "Target", db (r.targetDb), lin (r.targetDb, -40.0, -6.0));
            add (m, "Range", range, lin (r.rangeDb, 0.0, 12.0));
            add (m, "Speed", kSpeeds[std::clamp (r.speed, 0, 2)], choice (r.speed, 3));
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
            add (m, "Peak", peak ? db (c.peakThrDb) : "Off", lin (c.peakThrDb, -40.0, 0.0));
            add (m, "Threshold", db (c.thrDb), lin (c.thrDb, -60.0, 0.0));
            add (m, "Ratio", digits (c.ratio, 1) + ":1", skew (c.ratio, 1.0, 10.0, 3.0));
            add (m, "Makeup", signedDb (c.makeupDb), lin (c.makeupDb, 0.0, 24.0));
            add (m, "Mix", pct (c.mix), lin (c.mix, 0.0, 100.0));
            if (level) join (m.summary, "Ratio " + digits (c.ratio, 1) + ":1" + kDot + "Threshold " + db (c.thrDb));
            if (peak) join (m.summary, "Peak " + db (c.peakThrDb));
            if (c.mix < 99.5) join (m.summary, "Mix " + pct (c.mix));
            if (m.summary.empty()) m.summary = "Makeup " + signedDb (c.makeupDb);
            gr (m, s, s.meters.peakGr + s.meters.levelGr);
            finish (m);
        }

        // 08 Saturation
        {
            const auto& c = p.saturation;
            static const char* const kModes[] = { "Tape", "Tube", "Clip" };
            const int mode = std::clamp (static_cast<int> (c.mode), 0, 2);
            auto m = cell ("vox.sat", c.enabled, c.driveDb < 0.05 || c.mix < 0.05);
            add (m, "Mode", kModes[mode], choice (mode, 3));
            add (m, "Drive", db (c.driveDb), lin (c.driveDb, 0.0, Saturation::kMaxDriveDb));
            add (m, "Mix", pct (c.mix), lin (c.mix, 0.0, 100.0));
            m.summary = kModes[mode];
            join (m.summary, "Drive " + db (c.driveDb));
            join (m.summary, "Mix " + pct (c.mix));
            finish (m);
        }

        // 09 Voices (the double and the harmonies: one switch)
        {
            const auto& d = p.doubler;
            const auto& v = p.voices;
            const bool doubleOn = d.amount >= 0.05, voicesOn = ! HarmonyVoices::isNeutral (v);
            auto m = cell ("vox.voices", d.enabled, ! doubleOn && ! voicesOn);
            add (m, "Double", offOr (d.amount, pct (d.amount)), lin (d.amount, 0.0, 100.0));
            add (m, "Width", pct (d.width), lin (d.width, 0.0, 100.0));
            if (v.midi)
                add (m, "Voices", "MIDI notes", 1.0f);
            else
            {
                add (m, "Voice 1", interval (v.interval[0]), choice (v.interval[0], kHarmonies));
                add (m, "Voice 2", interval (v.interval[1]), choice (v.interval[1], kHarmonies));
                add (m, "Stack", v.stack <= 0 ? "Off" : std::to_string (kStackSizes[static_cast<std::size_t> (std::clamp (v.stack, 0, 3))]), choice (v.stack, 4));
            }
            add (m, "Level", pct (v.level), lin (v.level, 0.0, 100.0));
            add (m, "Formant", st (v.formant), lin (v.formant, -kMaxFormant, kMaxFormant), true);
            add (m, "Crew", pct (v.crew), lin (v.crew, 0.0, 100.0));
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
            add (m, "Mode", nameOf (kDimensionNames, d.mode), choice (d.mode, kDimensionModes));
            add (m, "Amount", pct (d.amount), lin (d.amount, 0.0, 100.0));
            add (m, "Speed", rate (d.rateHz), skew (d.rateHz, Dimension::kMinRateHz, Dimension::kMaxRateHz, 0.6));
            add (m, "Width", pct (d.width), lin (d.width, 0.0, 100.0));
            m.summary = nameOf (kDimensionNames, d.mode);
            join (m.summary, "Amount " + pct (d.amount));
            finish (m);
        }

        // 11 Delay (its Throw: an echo on each line's last word)
        {
            const auto& d = p.delay;
            const double echo = p.throws.echo;
            auto m = cell ("vox.delay", d.enabled, d.mix < 0.05 && echo < 0.05);
            add (m, "Time", nameOf (kDelayNames, d.division), choice (d.division, kDelayDivisions));
            add (m, "Feedback", pct (d.feedback), lin (d.feedback, 0.0, 90.0));
            add (m, "Mix", pct (d.mix), lin (d.mix, 0.0, 100.0));
            add (m, "Tone", hz (d.toneHz), skew (d.toneHz, 1000.0, 16000.0, 4000.0));
            add (m, "Duck", pct (d.duck), lin (d.duck, 0.0, 100.0));
            add (m, "Throw", offOr (echo, pct (echo)), lin (echo, 0.0, 100.0));
            add (m, "Ping-Pong", d.pingPong ? "On" : "Off", d.pingPong ? 1.0f : 0.0f);
            m.summary = nameOf (kDelayNames, d.division);
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
            add (m, "Decay", digits (r.decayS, 1) + " s", skew (r.decayS, 0.3, 8.0, 1.5));
            add (m, "Pre-delay", ms (r.predelayMs), lin (r.predelayMs, 0.0, 200.0));
            add (m, "Mix", pct (r.mix), lin (r.mix, 0.0, 100.0));
            add (m, "Tone", hz (r.toneHz), skew (r.toneHz, 2000.0, 16000.0, 6000.0));
            add (m, "Duck", pct (r.duck), lin (r.duck, 0.0, 100.0));
            add (m, "Throw", offOr (wash, pct (wash)), lin (wash, 0.0, 100.0));
            m.summary = digits (r.decayS, 1) + " s";
            if (r.mix >= 0.05) join (m.summary, "Mix " + pct (r.mix));
            if (wash >= 0.05) join (m.summary, "Throw " + pct (wash));
            finish (m);
        }

        // 13 Output
        {
            auto m = cell ("vox.output", true, std::abs (p.outputDb) < 0.05);
            add (m, "Output", signedDb (p.outputDb), lin (p.outputDb, -24.0, 24.0), true);
            m.summary = signedDb (p.outputDb);
            finish (m);
            if (out.back().state == rack::State::idle) out.back().summary = "0.0 dB";
        }
        return out;
    }
}
