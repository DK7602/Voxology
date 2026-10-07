// End-to-end side-chain check through the real VST3 (the wrapper a host like Cubase talks to): loads
// Voxology.vst3, turns its side-chain on, feeds a beat there and a steady F4 "vocal" on the main input
// with Pitch at 100 % and Key on Auto. A beat in B minor (D major notes) has no F: if the side-chain is
// heard, the F is pulled to F# (or E); if not (Chromatic), it stays F. (Not C: the Gallas beat barely
// plays C or C#, so both are allowed.)
// Run: vox_vst3sidecheck path/to/Voxology.vst3 beat.f64 (mono 48 kHz doubles)
#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>
#include <fstream>

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: vox_vst3sidecheck Voxology.vst3 beat.f64\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI juce;
    std::vector<double> beat;
    { std::ifstream f (argv[2], std::ios::binary); double v; while (f.read (reinterpret_cast<char*> (&v), sizeof v)) beat.push_back (v); }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, argv[1]);
    if (found.isEmpty()) { std::printf ("FAIL: no plug-in in %s\n", argv[1]); return 1; }
    juce::String error;
    const double sr = 48000.0;
    const int block = 512;
    auto plugin = format.createInstanceFromDescription (*found[0], sr, block, error);
    if (plugin == nullptr) { std::printf ("FAIL: %s\n", error.toRawUTF8()); return 1; }
    std::printf ("input buses: %d\n", plugin->getBusCount (true));
    for (int b = 0; b < plugin->getBusCount (true); ++b)
        std::printf ("  in %d: %s, %s\n", b, plugin->getBus (true, b)->getName().toRawUTF8(), plugin->getBus (true, b)->getCurrentLayout().getDescription().toRawUTF8());
    auto layout = plugin->getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    std::printf ("side-chain on: %s\n", plugin->setBusesLayout (layout) ? "ok" : "REFUSED");
    for (auto* p : plugin->getParameters())
    {
        const auto name = p->getName (64);
        if (name == "Pitch Amount") p->setValue (1.0f);
        if (name == "Pitch Key Source") p->setValue (0.0f);
        if (name == "Pitch Scale") p->setValue (0.0f);   // Chromatic: following gives the beat's own key
        if (std::getenv ("MANUAL_BMINOR") != nullptr)
        {
            // Control: Key set by hand to B minor (no side-chain needed): the F must move.
            if (name == "Pitch Key Source") p->setValue (1.0f);
            if (name == "Pitch Key") p->setValue (1.0f);               // B (last of 12)
            if (name == "Pitch Scale") p->setValue (2.0f / 9.0f);        // Minor (2 of 0 .. 9)
        }
        if (name.startsWith ("Pitch")) std::printf ("  %s = %s\n", name.toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
    }
    plugin->prepareToPlay (sr, block);
    const int chans = std::max (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    std::printf ("channels: in %d, out %d\n", plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (chans, block);
    juce::MidiBuffer midi;
    std::vector<float> out;
    double ph = 0.0;
    const double f0 = 349.23;   // F4
    for (size_t s = 0; s + block <= beat.size(); s += block)
    {
        buf.clear();
        for (int i = 0; i < block; ++i)
        {
            ph += f0 / sr;
            double v = 0.0;
            for (int h = 1; h <= 8; ++h) v += std::sin (2.0 * juce::MathConstants<double>::pi * h * ph) / h;
            buf.setSample (0, i, static_cast<float> (0.2 * v));
            for (int c = 1; c < plugin->getTotalNumInputChannels(); ++c) buf.setSample (c, i, static_cast<float> (beat[s + i]));
        }
        plugin->processBlock (buf, midi);
        for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
    }
    plugin->releaseResources();
    // Pitch of the last 2 s (autocorrelation over 80 - 600 Hz).
    const size_t n = 96000, a = out.size() - n;
    int bestLag = 0; double best = -1.0;
    for (int lag = 100; lag <= 300; ++lag)   // 160 - 480 Hz
    {
        double r = 0.0, e1 = 0.0, e2 = 0.0;
        for (size_t i = a; i + static_cast<size_t> (lag) < out.size(); i += 3) { r += out[i] * out[i + lag]; e1 += out[i] * out[i]; e2 += out[i + lag] * out[i + lag]; }
        const double c = r / std::sqrt (e1 * e2 + 1e-30);
        if (c > best) { best = c; bestLag = lag; }
    }
    const double hz = sr / bestLag, outMidi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    std::printf ("vocal in: F4 (65.0); out: %.1f Hz = MIDI %.2f -> %s\n", hz, outMidi,
                 std::abs (std::remainder (outMidi - 65.0, 12.0)) < 0.3 ? "stayed F: key NOT followed" : "moved: key followed");
    return 0;
}
