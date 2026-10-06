#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "vox/AutoEdit.h"
#include "vox/VocalChain.h"

#include <array>

/** Every Voxology parameter: IDs, layout, and a lock-free reader that turns the current values
    into the engine's ChainParams. Shared by the processor, Auto-Edit and the web editor.
    Defaults are neutral: inserting Voxology changes nothing until you (or Auto-Edit) turn things up. */
namespace VoxParams
{
    inline juce::String n (const char* prefix, int b) { return juce::String (prefix) + juce::String (b + 1); }

    inline juce::StringArray sliderIds()
    {
        juce::StringArray ids { "ptAmount", "ptSpeed", "ptHumanize", "clLowCut", "clGateThr", "clGateRange", "clPops", "clBreath" };
        for (int b = 0; b < vox::kEqBands; ++b) { ids.add (n ("eqGain", b)); ids.add (n ("eqFreq", b)); }
        ids.add ("dqSens");
        for (int b = 0; b < vox::kDynBands; ++b) { ids.add (n ("dqCut", b)); ids.add (n ("dqFreq", b)); }
        ids.addArray ({ "dsAmount", "dsSens", "dsFreq", "rdTarget", "rdRange",
                        "cpPeak", "cpThr", "cpRatio", "cpMakeup", "cpMix", "saDrive", "saMix",
                        "dbAmount", "dbWidth", "dlFeedback", "dlMix", "dlTone", "dlDuck",
                        "rvDecay", "rvPredelay", "rvMix", "rvTone", "rvDuck", "outGain" });
        return ids;
    }
    inline juce::StringArray toggleIds()
    {
        return { "bypass", "listenA", "levelMatch", "ptOn", "clOn", "eqOn", "dqOn", "dsOn", "rdOn", "cpOn", "saOn", "dbOn", "dlOn", "dlPing", "rvOn" };
    }
    inline juce::StringArray comboIds() { return { "aeStyle", "aeIntensity", "ptKey", "ptScale", "rdSpeed", "saMode", "dlTime" }; }

    inline void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        using namespace juce;
        auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
        auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };
        auto msText = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
        auto hzText = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
        auto skewed = [] (float lo, float hi, float step, float centre) { NormalisableRange<float> r (lo, hi, step); r.setSkewForCentre (centre); return r; };
        auto toggle = [&] (const char* id, const char* name, bool def) { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
        auto slider = [&] (const char* id, const String& name, NormalisableRange<float> r, float def, const char* unit, auto text)
        {
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                               AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (text)));
        };
        auto choice = [&] (const char* id, const char* name, const StringArray& items, int def)
        { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

        toggle ("bypass", "Bypass", false);
        toggle ("listenA", "Listen to Original (A)", false);
        toggle ("levelMatch", "Match Loudness", false);
        StringArray styles, intensities, delays;
        for (auto* s : vox::kStyleNames) styles.add (s);
        for (auto* s : vox::kIntensityNames) intensities.add (s);
        for (auto* s : vox::kDelayNames) delays.add (s);
        choice ("aeStyle", "Auto-Edit Style", styles, 0);
        choice ("aeIntensity", "Auto-Edit Intensity", intensities, 1);

        // Pitch (first in the chain). Amount 0 = off.
        toggle ("ptOn", "Pitch On", true);
        slider ("ptAmount", "Pitch Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%", pctText);
        StringArray keys, scales;
        for (auto* k : vox::kNoteNames) keys.add (k);
        for (auto* sc : vox::kScaleNames) scales.add (sc);
        choice ("ptKey", "Pitch Key", keys, 0);
        choice ("ptScale", "Pitch Scale", scales, 0);
        slider ("ptSpeed", "Pitch Retune Speed", skewed (0.0f, 400.0f, 1.0f, 60.0f), 50.0f, "ms", msText);
        slider ("ptHumanize", "Pitch Humanize", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%", pctText);

        toggle ("clOn", "Cleanup On", true);
        slider ("clLowCut", "Low Cut", skewed (20.0f, 400.0f, 1.0f, 80.0f), 20.0f, "Hz",
                [] (float v, int) { return v <= static_cast<float> (vox::Cleanup::kLowCutOffHz) ? String ("Off") : String (roundToInt (v)) + " Hz"; });
        slider ("clGateThr", "Gate Threshold", NormalisableRange<float> (-80.0f, -20.0f, 0.1f), -60.0f, "dB", dbText);
        slider ("clGateRange", "Gate Range", NormalisableRange<float> (0.0f, 30.0f, 0.1f), 0.0f, "dB", dbText);
        slider ("clPops", "Plosive Remover", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%",
                [] (float v, int) { return v < 0.05f ? String ("Off") : String (roundToInt (v)) + " %"; });
        slider ("clBreath", "Breath Reduction", NormalisableRange<float> (0.0f, static_cast<float> (vox::BreathControl::kMaxReductionDb), 0.1f), 0.0f, "dB",
                [] (float v, int) { return v < 0.05f ? String ("Off") : String (-v, 1) + " dB"; });

        toggle ("eqOn", "Tone EQ On", true);
        for (int b = 0; b < vox::kEqBands; ++b)
        {
            const auto& info = vox::kEqBandInfo[static_cast<size_t> (b)];
            slider (n ("eqGain", b).toRawUTF8(), String ("EQ ") + info.name + " Gain",
                    NormalisableRange<float> (static_cast<float> (-vox::kEqMaxDb), static_cast<float> (vox::kEqMaxDb), 0.1f), 0.0f, "dB", dbText);
            slider (n ("eqFreq", b).toRawUTF8(), String ("EQ ") + info.name + " Freq",
                    skewed (static_cast<float> (info.lo), static_cast<float> (info.hi), 1.0f, static_cast<float> (std::sqrt (info.lo * info.hi))),
                    static_cast<float> (info.def), "Hz", hzText);
        }

        // Dynamic EQ: Max Cut 0 = band off.
        toggle ("dqOn", "Dynamic EQ On", true);
        slider ("dqSens", "Dynamic EQ Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, "%", pctText);
        for (int b = 0; b < vox::kDynBands; ++b)
        {
            const auto& info = vox::kDynBandInfo[static_cast<size_t> (b)];
            slider (n ("dqCut", b).toRawUTF8(), String ("Dynamic EQ ") + info.name + " Max Cut",
                    NormalisableRange<float> (0.0f, static_cast<float> (vox::kDynMaxCutDb), 0.1f), 0.0f, "dB",
                    [] (float v, int) { return v < 0.05f ? String ("Off") : String (-v, 1) + " dB"; });
            slider (n ("dqFreq", b).toRawUTF8(), String ("Dynamic EQ ") + info.name + " Freq",
                    skewed (static_cast<float> (info.lo), static_cast<float> (info.hi), 1.0f, static_cast<float> (std::sqrt (info.lo * info.hi))),
                    static_cast<float> (info.def), "Hz", hzText);
        }

        toggle ("dsOn", "De-Esser On", true);
        slider ("dsAmount", "De-Esser Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%", pctText);
        slider ("dsSens", "De-Esser Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, "%", pctText);
        slider ("dsFreq", "De-Esser Frequency", skewed (3000.0f, 12000.0f, 10.0f, 6000.0f), 6000.0f, "Hz", hzText);

        toggle ("rdOn", "Rider On", true);
        slider ("rdTarget", "Rider Target", NormalisableRange<float> (-40.0f, -6.0f, 0.1f), -20.0f, "dB", dbText);
        slider ("rdRange", "Rider Range", NormalisableRange<float> (0.0f, 12.0f, 0.1f), 0.0f, "dB", dbText);
        choice ("rdSpeed", "Rider Speed", { "Slow", "Medium", "Fast" }, 1);

        toggle ("cpOn", "Compressor On", true);
        slider ("cpPeak", "Compressor Peak", NormalisableRange<float> (-40.0f, 0.0f, 0.1f), 0.0f, "dB",
                [] (float v, int) { return v > -0.05f ? String ("Off") : String (v, 1) + " dB"; });
        slider ("cpThr", "Compressor Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), 0.0f, "dB", dbText);
        slider ("cpRatio", "Compressor Ratio", skewed (1.0f, 10.0f, 0.01f, 3.0f), 1.0f, "",
                [] (float v, int) { return String (v, 1) + ":1"; });
        slider ("cpMakeup", "Compressor Makeup", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, "dB", dbText);
        slider ("cpMix", "Compressor Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, "%", pctText);

        toggle ("saOn", "Saturation On", true);
        choice ("saMode", "Saturation Mode", { "Tape", "Tube", "Clip" }, 0);
        slider ("saDrive", "Saturation Drive", NormalisableRange<float> (0.0f, static_cast<float> (vox::Saturation::kMaxDriveDb), 0.1f), 0.0f, "dB", dbText);
        slider ("saMix", "Saturation Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, "%", pctText);

        toggle ("dbOn", "Doubler On", true);
        slider ("dbAmount", "Doubler Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%", pctText);
        slider ("dbWidth", "Doubler Width", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f, "%", pctText);

        toggle ("dlOn", "Delay On", true);
        choice ("dlTime", "Delay Time", delays, 0);
        slider ("dlFeedback", "Delay Feedback", NormalisableRange<float> (0.0f, 90.0f, 0.1f), 25.0f, "%", pctText);
        slider ("dlMix", "Delay Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%", pctText);
        slider ("dlTone", "Delay Tone", skewed (1000.0f, 16000.0f, 10.0f, 4000.0f), 6000.0f, "Hz", hzText);
        slider ("dlDuck", "Delay Duck", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, "%", pctText);
        toggle ("dlPing", "Delay Ping-Pong", false);

        toggle ("rvOn", "Reverb On", true);
        slider ("rvDecay", "Reverb Decay", skewed (0.3f, 8.0f, 0.01f, 1.5f), 1.6f, "s", [] (float v, int) { return String (v, 1) + " s"; });
        slider ("rvPredelay", "Reverb Pre-delay", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 20.0f, "ms", msText);
        slider ("rvMix", "Reverb Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, "%", pctText);
        slider ("rvTone", "Reverb Tone", skewed (2000.0f, 16000.0f, 10.0f, 6000.0f), 7000.0f, "Hz", hzText);
        slider ("rvDuck", "Reverb Duck", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, "%", pctText);

        slider ("outGain", "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB", dbText);
    }

    /** Writes every chain setting as parameter values (Auto-Edit's apply). */
    template <typename Set>
    void forEachValue (const vox::ChainParams& p, Set&& set)
    {
        set ("ptOn", 1.0f);
        set ("ptAmount", static_cast<float> (p.pitch.amount));
        set ("ptKey", static_cast<float> (p.pitch.key));
        set ("ptScale", static_cast<float> (p.pitch.scale));
        set ("ptSpeed", static_cast<float> (p.pitch.speedMs));
        set ("ptHumanize", static_cast<float> (p.pitch.humanize));
        set ("clOn", 1.0f);
        set ("clLowCut", static_cast<float> (p.cleanup.lowCutHz));
        set ("clGateThr", static_cast<float> (p.cleanup.gateThrDb));
        set ("clGateRange", static_cast<float> (p.cleanup.gateRangeDb));
        set ("clPops", static_cast<float> (p.cleanup.popAmount));
        set ("clBreath", static_cast<float> (p.cleanup.breathDb));
        set ("eqOn", 1.0f);
        for (int b = 0; b < vox::kEqBands; ++b)
        {
            set (n ("eqGain", b), static_cast<float> (p.eq.gainDb[static_cast<size_t> (b)]));
            set (n ("eqFreq", b), static_cast<float> (p.eq.freqHz[static_cast<size_t> (b)]));
        }
        set ("dqOn", 1.0f);
        set ("dqSens", static_cast<float> (p.dynEq.sensitivity));
        for (int b = 0; b < vox::kDynBands; ++b)
        {
            set (n ("dqCut", b), static_cast<float> (p.dynEq.maxCutDb[static_cast<size_t> (b)]));
            set (n ("dqFreq", b), static_cast<float> (p.dynEq.freqHz[static_cast<size_t> (b)]));
        }
        set ("dsOn", 1.0f);
        set ("dsAmount", static_cast<float> (p.deEsser.amount));
        set ("dsSens", static_cast<float> (p.deEsser.sensitivity));
        set ("dsFreq", static_cast<float> (p.deEsser.freqHz));
        set ("rdOn", 1.0f);
        set ("rdTarget", static_cast<float> (p.rider.targetDb));
        set ("rdRange", static_cast<float> (p.rider.rangeDb));
        set ("rdSpeed", static_cast<float> (p.rider.speed));
        set ("cpOn", 1.0f);
        set ("cpPeak", static_cast<float> (p.comp.peakThrDb));
        set ("cpThr", static_cast<float> (p.comp.thrDb));
        set ("cpRatio", static_cast<float> (p.comp.ratio));
        set ("cpMakeup", static_cast<float> (p.comp.makeupDb));
        set ("cpMix", static_cast<float> (p.comp.mix));
        set ("saOn", 1.0f);
        set ("saMode", static_cast<float> (static_cast<int> (p.saturation.mode)));
        set ("saDrive", static_cast<float> (p.saturation.driveDb));
        set ("saMix", static_cast<float> (p.saturation.mix));
        set ("dbOn", 1.0f);
        set ("dbAmount", static_cast<float> (p.doubler.amount));
        set ("dbWidth", static_cast<float> (p.doubler.width));
        set ("dlOn", 1.0f);
        set ("dlTime", static_cast<float> (p.delay.division));
        set ("dlFeedback", static_cast<float> (p.delay.feedback));
        set ("dlMix", static_cast<float> (p.delay.mix));
        set ("dlTone", static_cast<float> (p.delay.toneHz));
        set ("dlDuck", static_cast<float> (p.delay.duck));
        set ("dlPing", p.delay.pingPong ? 1.0f : 0.0f);
        set ("rvOn", 1.0f);
        set ("rvDecay", static_cast<float> (p.reverb.decayS));
        set ("rvPredelay", static_cast<float> (p.reverb.predelayMs));
        set ("rvMix", static_cast<float> (p.reverb.mix));
        set ("rvTone", static_cast<float> (p.reverb.toneHz));
        set ("rvDuck", static_cast<float> (p.reverb.duck));
        set ("outGain", static_cast<float> (p.outputDb));
    }

    /** Reads the current values (lock-free; safe on the audio thread once attached). */
    class Reader
    {
    public:
        void attach (juce::AudioProcessorValueTreeState& s)
        {
            for (const auto& id : sliderIds()) add (s, id);
            for (const auto& id : toggleIds()) add (s, id);
            for (const auto& id : comboIds()) add (s, id);
        }

        void read (vox::ChainParams& p, double bpm) const noexcept
        {
            auto on = [this] (const char* id) { return v (id) > 0.5f; };
            auto d = [this] (const char* id) { return static_cast<double> (v (id)); };
            auto idx = [this] (const char* id, int hi) { return juce::jlimit (0, hi, juce::roundToInt (v (id))); };

            p.bypass = on ("bypass");
            p.listenOriginal = on ("listenA");
            p.pitch = { on ("ptOn"), d ("ptAmount"), idx ("ptKey", 11), idx ("ptScale", vox::kScales - 1), d ("ptSpeed"), d ("ptHumanize") };
            p.cleanup = { on ("clOn"), d ("clLowCut"), d ("clGateThr"), d ("clGateRange"), d ("clPops"), d ("clBreath") };
            p.eq.enabled = on ("eqOn");
            for (size_t b = 0; b < static_cast<size_t> (vox::kEqBands); ++b)
            {
                p.eq.gainDb[b] = eqGain[b]->load();
                p.eq.freqHz[b] = eqFreq[b]->load();
            }
            p.dynEq.enabled = on ("dqOn");
            p.dynEq.sensitivity = d ("dqSens");
            for (size_t b = 0; b < static_cast<size_t> (vox::kDynBands); ++b)
            {
                p.dynEq.maxCutDb[b] = dqCut[b]->load();
                p.dynEq.freqHz[b] = dqFreq[b]->load();
            }
            p.deEsser = { on ("dsOn"), d ("dsAmount"), d ("dsSens"), d ("dsFreq") };
            p.rider = { on ("rdOn"), d ("rdTarget"), d ("rdRange"), idx ("rdSpeed", 2) };
            p.comp = { on ("cpOn"), d ("cpPeak"), d ("cpThr"), d ("cpRatio"), d ("cpMakeup"), d ("cpMix") };
            p.saturation.enabled = on ("saOn");
            p.saturation.mode = static_cast<vox::SaturationMode> (idx ("saMode", 2));
            p.saturation.driveDb = d ("saDrive");
            p.saturation.mix = d ("saMix");
            p.doubler = { on ("dbOn"), d ("dbAmount"), d ("dbWidth") };
            p.delay = { on ("dlOn"), idx ("dlTime", vox::kDelayDivisions - 1), d ("dlFeedback"), d ("dlMix"), d ("dlTone"), d ("dlDuck"), on ("dlPing"), bpm };
            p.reverb = { on ("rvOn"), d ("rvDecay"), d ("rvPredelay"), d ("rvMix"), d ("rvTone"), d ("rvDuck") };
            p.outputDb = d ("outGain");
        }

    private:
        void add (juce::AudioProcessorValueTreeState& s, const juce::String& id)
        {
            auto* a = s.getRawParameterValue (id);
            jassert (a != nullptr);
            values.emplace_back (id, a);
            for (int b = 0; b < vox::kEqBands; ++b)
            {
                if (id == n ("eqGain", b)) eqGain[static_cast<size_t> (b)] = a;
                if (id == n ("eqFreq", b)) eqFreq[static_cast<size_t> (b)] = a;
            }
            for (int b = 0; b < vox::kDynBands; ++b)
            {
                if (id == n ("dqCut", b)) dqCut[static_cast<size_t> (b)] = a;
                if (id == n ("dqFreq", b)) dqFreq[static_cast<size_t> (b)] = a;
            }
        }
        float v (const char* id) const noexcept
        {
            // Linear search comparing against a const char*: no allocation on the audio thread.
            for (const auto& [name, a] : values)
                if (name == id) return a->load();
            jassertfalse;
            return 0.0f;
        }

        std::vector<std::pair<juce::String, std::atomic<float>*>> values;
        std::array<std::atomic<float>*, vox::kEqBands> eqGain {}, eqFreq {};
        std::array<std::atomic<float>*, vox::kDynBands> dqCut {}, dqFreq {};
    };
}
