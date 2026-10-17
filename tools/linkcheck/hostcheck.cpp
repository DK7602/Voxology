// Host check: one real Voxology driven the way a DAW drives it. The DAW's own bypass (processBlockBypassed) plays
// the dry vocal delayed by the reported latency on both sides; a NaN / inf input sample doesn't poison the output;
// Record mode switches without a step or a gap and the latency drops; a MIDI note let go in BEAT mode isn't held
// back in VOCAL. Build: -DVOX_BUILD_LINKCHECK=ON; run: vox_hostcheck.
#include "PluginProcessor.h"

#include <cstdio>

namespace {
int fails = 0;
void expect (bool ok, const char* what)
{
    std::printf ("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++fails;
}

/** A 220 Hz tone in 0.35 s phrases, 0.15 s gaps. */
double tone (int i) { return 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0) * (std::fmod (i / 48000.0, 0.5) < 0.35 ? 1.0 : 0.0); }

void set (VoxologyAudioProcessor& p, const char* id, float v)
{
    auto* q = p.parameters.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (v));
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;
    const int block = 512, total = static_cast<int> (2.0 * sr) / block * block;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = *dynamic_cast<VoxologyAudioProcessor*> (base.get());
    set (p, "cpThr", -30.0f); set (p, "cpRatio", 4.0f); set (p, "cpMakeup", 6.0f);   // something to hear
    p.prepareToPlay (sr, block);   // mono in, stereo out
    const int lat = p.getLatencySamples();
    juce::AudioBuffer<float> buf (juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), block);
    juce::MidiBuffer none;

    // The DAW's bypass: after the 20 ms glide, exactly the input delayed by the latency, left and right.
    double err = 0.0;
    for (int s = 0; s < total; s += block)
    {
        buf.clear();
        for (int i = 0; i < block; ++i) buf.setSample (0, i, static_cast<float> (tone (s + i)));
        p.processBlockBypassed (buf, none);
        for (int i = 0; i < block; ++i)
            if (s + i >= static_cast<int> (0.3 * sr))
                for (int c = 0; c < 2; ++c) err = std::max (err, static_cast<double> (std::abs (buf.getSample (c, i) - static_cast<float> (tone (s + i - lat)))));
    }
    std::printf ("latency %d samples; host bypass vs the delayed input: max error %g\n", lat, err);
    expect (err < 1.0e-6, "host bypass = dry vocal delayed by the latency, both sides");
    expect (p.isHostBypassed(), "isHostBypassed() after bypassed blocks");
    buf.clear();
    p.processBlock (buf, none);
    expect (! p.isHostBypassed(), "... and not after a normal block");

    // A NaN and an inf in the input: finite output, still sounding.
    bool finite = true;
    double energy = 0.0;
    for (int s = 0; s < total; s += block)
    {
        buf.clear();
        for (int i = 0; i < block; ++i) buf.setSample (0, i, static_cast<float> (tone (s + i)));
        if (s == 10 * block) buf.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
        if (s == 20 * block) buf.setSample (0, 7, std::numeric_limits<float>::infinity());
        p.processBlock (buf, none);
        for (int i = 0; i < block; ++i)
        {
            finite = finite && std::isfinite (buf.getSample (0, i)) && std::isfinite (buf.getSample (1, i));
            if (s > total / 2) energy += buf.getSample (0, i) * buf.getSample (0, i);
        }
    }
    expect (finite && energy > 1.0, "NaN / inf in: finite out, and the vocal keeps playing");

    // Record mode on: crossfaded (no step bigger than the music's own), no gap longer than the tone's own, less latency.
    set (p, "recMode", 1.0f);
    double maxStep = 0.0, prev = 0.0, toneStep = 0.0;
    int run = 0, longest = 0;
    for (int s = 0; s < total; s += block)
    {
        buf.clear();
        for (int i = 0; i < block; ++i) buf.setSample (0, i, static_cast<float> (tone (s + i)));
        p.processBlock (buf, none);
        for (int i = 0; i < block; ++i)
        {
            const double y = buf.getSample (0, i);
            maxStep = std::max (maxStep, std::abs (y - prev));
            prev = y;
            run = std::abs (y) < 1.0e-6 ? run + 1 : 0;
            longest = std::max (longest, run);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // (the latency change and the clean-up)
    }
    for (int i = 1; i < 48000; ++i) toneStep = std::max (toneStep, std::abs (tone (i) - tone (i - 1)));
    std::printf ("Record mode: latency %d -> %d; biggest step %.4f (the tone's %.4f); longest silence %d samples\n",
                 lat, p.getLatencySamples(), maxStep, toneStep, longest);
    expect (p.getLatencySamples() < lat, "Record mode: lower latency reported");
    expect (maxStep < 4.0 * toneStep, "Record mode: no click (the compressor's makeup doubles the tone)");
    expect (longest < static_cast<int> (0.16 * sr), "Record mode: no gap");

    // MIDI: held in VOCAL, let go in BEAT, not held when back in VOCAL.
    set (p, "recMode", 0.0f);
    juce::MidiBuffer on, off;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    buf.clear(); p.processBlock (buf, on);
    const int held = p.meters.heldNotes.load();
    set (p, "mode", 1.0f);
    buf.clear(); p.processBlock (buf, off);
    set (p, "mode", 0.0f);
    buf.clear(); p.processBlock (buf, none);
    expect (held == 1 && p.meters.heldNotes.load() == 0, "MIDI: a note let go in BEAT mode isn't held back in VOCAL");
    expect (p.getTailLengthSeconds() >= 60.0, "tail: 60 s");

    p.releaseResources();
    std::printf (fails == 0 ? "PASS\n" : "%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
