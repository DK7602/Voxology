#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

/** My Chains and Quick Looks.

    My Chains: your whole Voxology setup saved under a name, as a small file (Documents/Voxology/Chains/<name>.voxchain:
    JSON, every chain parameter's value in its own units), so it's there in every project. Loading one sets every
    chain parameter (one the file doesn't have, from an older Voxology, goes to its default: new features start off).
    "Keep this song's key" (on by default) leaves the song's key, scale, key source, MIDI mode and removed notes alone.

    Not part of a chain: REC, bypass, A/B, Match, Auto-Edit's style / intensity, VOCAL / BEAT and Unmask (they belong
    to the session or to Beat mode, not to a sound).

    Quick Looks: built-in looks that set only their own module(s) (Hard Robot sets Pitch, Stacked Hook sets Voices ...),
    on top of whatever you have. "Start fresh" sets every chain parameter back to its default (neutral).

    Every load is one step back with UNDO (beforeLoad takes the snapshot). Message thread only. */
class ChainLibrary
{
public:
    explicit ChainLibrary (juce::AudioProcessorValueTreeState& s) : state (s) {}

    static juce::File folder();
    static bool inChain (const juce::String& id);
    static bool songSpecific (const juce::String& id);

    /** { yours: [{name}], looks: [{name, about}], folder } */
    juce::String listJson() const;
    /** false: no usable name, or the file couldn't be written. A name you already have is replaced. */
    bool save (const juce::String& name);
    /** kind: "yours", "look" or "fresh". false: not found. */
    bool load (const juce::String& kind, const juce::String& name, bool keepKey);
    void remove (const juce::String& name);

    std::function<void()> beforeLoad;

private:
    struct Look
    {
        const char* name;
        const char* about;
        std::vector<std::pair<const char*, float>> values;
    };
    static const std::vector<Look>& looks();
    static juce::File fileFor (const juce::String& name);
    void set (juce::RangedAudioParameter& p, float value);

    juce::AudioProcessorValueTreeState& state;
};
