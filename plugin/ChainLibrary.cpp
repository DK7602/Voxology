#include "ChainLibrary.h"

juce::File ChainLibrary::folder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Voxology").getChildFile ("Chains");
}

bool ChainLibrary::inChain (const juce::String& id)
{
    static const juce::StringArray session { "recMode", "bypass", "listenA", "levelMatch", "aeStyle", "aeIntensity", "mode", "umAmount", "umFocus" };
    return ! session.contains (id);
}

bool ChainLibrary::songSpecific (const juce::String& id)
{
    return id == "ptKey" || id == "ptScale" || id == "ptKeySrc" || id == "ptMidi" || id.startsWith ("ptRm");
}

const std::vector<ChainLibrary::Look>& ChainLibrary::looks()
{
    // Values in each parameter's own units (choices: the item's number). Only these are set.
    static const std::vector<Look> all {
        { "Hard Robot", "Pitch: Robot, instant. The hard, stepped trap tune. Set the song's key first.",
          { { "ptOn", 1 }, { "ptMode", 2 }, { "ptAmount", 100 }, { "ptSpeed", 0 }, { "ptHumanize", 0 }, { "ptVibrato", 0 } } },
        { "Smooth Tune", "Pitch: Natural and gentle. In tune, still your voice and your vibrato.",
          { { "ptOn", 1 }, { "ptMode", 0 }, { "ptAmount", 80 }, { "ptSpeed", 60 }, { "ptHumanize", 40 }, { "ptVibrato", 0 } } },
        { "Stacked Hook", "Voices: Unison both sides, Stack 6, Crew and the double. A big stacked hook, like your crew behind you.",
          { { "dbOn", 1 }, { "hvMidi", 0 }, { "hv1", 8 }, { "hv2", 8 }, { "hvStack", 3 }, { "hvLevel", 45 }, { "hvFormant", 0 }, { "hvCrew", 50 }, { "dbAmount", 40 }, { "dbWidth", 80 } } },
        { "Harmony 3rd + 5th", "Voices: a 3rd up and a 5th down, in the key set in Pitch.",
          { { "dbOn", 1 }, { "hvMidi", 0 }, { "hv1", 1 }, { "hv2", 6 }, { "hvStack", 0 }, { "hvLevel", 40 }, { "hvFormant", 0 } } },
        { "Deep Ad-lib", "Voices: an octave down with a deeper tone, under you. The classic ad-lib voice.",
          { { "dbOn", 1 }, { "hvMidi", 0 }, { "hv1", 7 }, { "hv2", 0 }, { "hvStack", 0 }, { "hvLevel", 60 }, { "hvFormant", -3 } } },
        { "Phone Voice", "Low Cut, Tone EQ and Saturation: thin, boxy and crunchy, like a phone or an old radio.",
          { { "clOn", 1 }, { "clLowCut", 400 }, { "eqOn", 1 }, { "eqGain1", -12 }, { "eqFreq1", 400 }, { "eqGain2", 0 },
            { "eqGain3", 6 }, { "eqFreq3", 1200 }, { "eqGain4", -8 }, { "eqFreq4", 6500 }, { "eqGain5", -12 }, { "eqFreq5", 6000 },
            { "saOn", 1 }, { "saMode", 2 }, { "saDrive", 10 }, { "saMix", 60 } } },
        { "Wide Chorus", "Dimension: a gentle, wide chorus. Widens a sung hook or a stack without sounding like an effect.",
          { { "dmOn", 1 }, { "dmMode", 0 }, { "dmAmount", 30 }, { "dmRate", 0.4f }, { "dmWidth", 90 } } },
        { "Ad-lib Echo", "Delay: dotted 1/8 ping-pong echoes that stay quiet while you sing.",
          { { "dlOn", 1 }, { "dlTime", 2 }, { "dlFeedback", 35 }, { "dlMix", 25 }, { "dlTone", 4000 }, { "dlDuck", 70 }, { "dlPing", 1 } } },
        { "Line Throws", "Delay and Reverb: an echo and a wash on the last word of each line (dotted 1/8), the rest stays dry.",
          { { "dlOn", 1 }, { "dlTime", 2 }, { "dlThrow", 50 }, { "rvOn", 1 }, { "rvThrow", 30 } } },
        { "Big Space", "Reverb: a long, wide plate that blooms in the gaps. For hooks and melodic parts.",
          { { "rvOn", 1 }, { "rvDecay", 2.6f }, { "rvPredelay", 40 }, { "rvMix", 20 }, { "rvTone", 6000 }, { "rvDuck", 50 } } },
    };
    return all;
}

juce::File ChainLibrary::fileFor (const juce::String& name)
{
    return folder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".voxchain");
}

juce::String ChainLibrary::listJson() const
{
    juce::Array<juce::var> yours, lookList;
    auto files = folder().findChildFiles (juce::File::findFiles, false, "*.voxchain");
    juce::StringArray names;
    for (const auto& f : files)
    {
        const auto json = juce::JSON::parse (f.loadFileAsString());
        const auto saved = json.getProperty ("name", {}).toString();
        if (json.getProperty ("values", {}).isObject())
            names.add (saved.isNotEmpty() && fileFor (saved) == f ? saved : f.getFileNameWithoutExtension());   // (a renamed file: its file name)
    }
    names.sortNatural();
    for (const auto& n : names)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", n);
        yours.add (juce::var (o));
    }
    for (const auto& l : looks())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (l.name));
        o->setProperty ("about", juce::String (l.about));
        lookList.add (juce::var (o));
    }
    auto* res = new juce::DynamicObject();
    res->setProperty ("yours", yours);
    res->setProperty ("looks", lookList);
    res->setProperty ("folder", folder().getFullPathName());
    return juce::JSON::toString (juce::var (res), true);
}

bool ChainLibrary::save (const juce::String& rawName)
{
    const auto name = rawName.trim().substring (0, 60);
    if (juce::File::createLegalFileName (name).isEmpty())
        return false;
    auto* values = new juce::DynamicObject();
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && inChain (rp->getParameterID()))
            values->setProperty (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
    auto* doc = new juce::DynamicObject();
    doc->setProperty ("voxology", juce::String (JucePlugin_VersionString));
    doc->setProperty ("name", name);
    doc->setProperty ("values", juce::var (values));
    const auto f = fileFor (name);
    return f.getParentDirectory().createDirectory().wasOk() && f.replaceWithText (juce::JSON::toString (juce::var (doc), false));
}

bool ChainLibrary::load (const juce::String& kind, const juce::String& name, bool keepKey)
{
    std::vector<std::pair<juce::RangedAudioParameter*, float>> todo;
    if (kind == "look")
    {
        for (const auto& l : looks())
            if (name == l.name)
                for (const auto& [id, v] : l.values)
                    if (auto* p = state.getParameter (id)) todo.emplace_back (p, v);
    }
    else if (kind == "yours" || kind == "fresh")
    {
        juce::var values;   // fresh: none, so every chain parameter goes to its default
        if (kind == "yours")
        {
            const auto f = fileFor (name);
            if (! f.existsAsFile()) return false;
            values = juce::JSON::parse (f.loadFileAsString()).getProperty ("values", {});
            if (! values.isObject()) return false;
        }
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto id = rp->getParameterID();
                if (! inChain (id) || (keepKey && songSpecific (id))) continue;
                const auto& v = values.getProperty (id, {});
                todo.emplace_back (rp, v.isVoid() ? rp->convertFrom0to1 (rp->getDefaultValue()) : static_cast<float> (v));
            }
    }
    if (todo.empty()) return false;
    if (beforeLoad) beforeLoad();
    for (auto& [p, v] : todo) set (*p, v);
    return true;
}

void ChainLibrary::remove (const juce::String& name)
{
    fileFor (name).deleteFile();
}

void ChainLibrary::set (juce::RangedAudioParameter& p, float value)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (value));
    p.endChangeGesture();
}
