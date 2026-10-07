// Side-chain key check: one real Voxology on a vocal (VOCAL mode, Key on Auto), the beat fed to its
// side-chain input, as Cubase does. Reads which key Pitch follows and from where (bkSource: 2 = the
// side-chain). Build: -DVOX_BUILD_LINKCHECK=ON; run: vox_sidecheck beat.f64 [vocal.f64] (mono 48 kHz doubles).
#include "PluginProcessor.h"

#include <cstdio>
#include <fstream>

namespace {
std::vector<double> load (const char* path)
{
    std::vector<double> x;
    std::ifstream f (path, std::ios::binary);
    double v;
    while (f.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (v);
    return x;
}
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: vox_sidecheck beat.f64 [vocal.f64]\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI juce;
    const double sr = 48000.0;
    const int block = 512;
    const auto beatAudio = load (argv[1]);
    const auto vocalAudio = argc > 2 ? load (argv[2]) : std::vector<double> {};

    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    auto* vox = dynamic_cast<VoxologyAudioProcessor*> (p.get());
    auto layout = vox->getBusesLayout();
    std::printf ("buses in: %d, side-chain default: %s\n", vox->getBusCount (true), layout.getChannelSet (true, 1).getDescription().toRawUTF8());
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    if (! vox->setBusesLayout (layout)) { std::printf ("FAIL: side-chain layout refused\n"); return 1; }
    vox->prepareToPlay (sr, block);
    const int chans = std::max (vox->getTotalNumInputChannels(), vox->getTotalNumOutputChannels());
    std::printf ("channels in %d (main %d), out %d\n", vox->getTotalNumInputChannels(), vox->getMainBusNumInputChannels(), vox->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (chans, block);
    juce::MidiBuffer midi;
    const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto& m = vox->meters;
    for (size_t s = 0, k = 0; s + block <= beatAudio.size(); s += block, ++k)
    {
        buf.clear();
        for (int i = 0; i < block; ++i)
        {
            buf.setSample (0, i, s + i < vocalAudio.size() ? static_cast<float> (vocalAudio[s + i]) : 0.0f);
            buf.setSample (1, i, static_cast<float> (beatAudio[s + i]));   // side-chain L
            buf.setSample (2, i, static_cast<float> (beatAudio[s + i]));   // side-chain R
        }
        vox->processBlock (buf, midi);
        if (k % static_cast<size_t> (5.0 * sr / block) == 0)
            std::printf ("%5.1f s  side-chain %d  state %d  source %d  key %s %s  conf %.2f  heard %.1f s  tune %+.0f c\n", static_cast<double> (s) / sr, m.scState.load(),
                         m.bkState.load(), m.bkSource.load(), N[m.bkKey.load()], vox::modeName (m.bkMode.load()), m.bkConf.load(), m.bkHeard.load(), m.bkTune.load());
    }
    return 0;
}
