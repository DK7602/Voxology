// Unmask end-to-end check: two real Voxology instances in one process, as a host runs them.
// One on a vocal (VOCAL mode), one on a beat (BEAT mode); the beat is processed BEFORE the vocal in
// every block (the worst order). Checks that the beat's middle dips while the vocal sings and comes
// back in the gaps, lined up by song position. Build: -DVOX_BUILD_LINKCHECK=ON; run: vox_linkcheck.
#include "PluginProcessor.h"
#include "../../tests/Signals.h"

#include <cstdio>
#include <random>

namespace {
struct PlayHead final : juce::AudioPlayHead
{
    juce::int64 pos = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSamples (pos);
        p.setIsPlaying (true);
        p.setBpm (140.0);
        return p;
    }
};

double bandDb (const std::vector<double>& x, size_t a, size_t b, double hz, double sr)
{
    vox::Biquad f;
    vox::design::apply (f, vox::design::bandPass (hz, 2.0, sr));
    double e = 0.0;
    for (size_t i = a > 9600 ? a - 9600 : 0; i < b; ++i) { const double y = f.process (x[i]); if (i >= a) e += y * y; }
    return 10.0 * std::log10 (e / static_cast<double> (b - a) + 1.0e-30);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    const double sr = 48000.0;
    const int block = 512;
    const double seconds = 6.0;
    const int n = static_cast<int> (seconds * sr) / block * block;   // whole blocks

    std::unique_ptr<juce::AudioProcessor> vocalP (createPluginFilter()), beatP (createPluginFilter());
    auto* vocal = dynamic_cast<VoxologyAudioProcessor*> (vocalP.get());
    auto* beat = dynamic_cast<VoxologyAudioProcessor*> (beatP.get());
    auto stereo = beat->getBusesLayout();
    stereo.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! beat->setBusesLayout (stereo)) { std::printf ("FAIL: stereo layout\n"); return 1; }
    if (auto* m = beat->parameters.getParameter ("mode")) m->setValueNotifyingHost (1.0f);
    if (auto* a = beat->parameters.getParameter ("umAmount")) a->setValueNotifyingHost (1.0f);   // 100 %
    if (auto* f = beat->parameters.getParameter ("umFocus")) f->setValueNotifyingHost (1.0f);    // Full: measure on L
    PlayHead ph;
    vocal->setPlayHead (&ph);
    beat->setPlayHead (&ph);
    vocal->prepareToPlay (sr, block);
    beat->prepareToPlay (sr, block);
    const int lat = beat->getLatencySamples();
    if (lat != vocal->getLatencySamples()) { std::printf ("FAIL: latencies differ (%d vs %d)\n", lat, vocal->getLatencySamples()); return 1; }

    // Vocal: sung in the first half of every second. Beat: noise in both channels.
    auto v = testsig::vocal (sr, seconds, -120.0, -30.0, 3, 200.0);
    for (int i = 0; i < n; ++i) if (std::fmod (i / sr, 1.0) >= 0.5) v[static_cast<size_t> (i)] = 0.0f;
    std::mt19937 rng (2);
    std::normal_distribution<double> w (0.0, 0.05);
    std::vector<double> beatIn (static_cast<size_t> (n)), beatOut (static_cast<size_t> (n));
    for (auto& x : beatIn) x = w (rng);

    juce::MidiBuffer midi;
    for (int s = 0; s < n; s += block)
    {
        ph.pos = s;
        juce::AudioBuffer<float> b (2, block), vb (2, block);
        for (int i = 0; i < block; ++i)
        {
            const float x = static_cast<float> (beatIn[static_cast<size_t> (s + i)]);
            b.setSample (0, i, x); b.setSample (1, i, x);
            vb.setSample (0, i, v[static_cast<size_t> (s + i)]); vb.setSample (1, i, 0.0f);
        }
        beat->processBlock (b, midi);    // the beat first: this block's vocal frames aren't published yet
        vocal->processBlock (vb, midi);
        for (int i = 0; i < block; ++i) beatOut[static_cast<size_t> (s + i)] = b.getSample (0, i);
    }

    // The beat comes out `lat` samples late (constant latency, both modes): line it up again.
    std::vector<double> aligned (static_cast<size_t> (n), 0.0);
    for (int i = 0; i + lat < n; ++i) aligned[static_cast<size_t> (i)] = beatOut[static_cast<size_t> (i + lat)];
    bool ok = true;
    for (int sec = 1; sec < 5; ++sec)
    {
        const auto s0 = static_cast<size_t> ((sec + 0.15) * sr), s1 = static_cast<size_t> ((sec + 0.45) * sr);
        const auto g0 = static_cast<size_t> ((sec + 0.75) * sr), g1 = static_cast<size_t> ((sec + 0.97) * sr);
        const double sing = bandDb (beatIn, s0, s1, 1600.0, sr) - bandDb (aligned, s0, s1, 1600.0, sr);
        const double gap = bandDb (beatIn, g0, g1, 1600.0, sr) - bandDb (aligned, g0, g1, 1600.0, sr);
        std::printf ("second %d: beat at 1.6 kHz dips %.2f dB while singing, %.2f dB in the gap\n", sec, sing, gap);
        ok = ok && sing > 2.0 && gap < 0.5;
    }
    std::printf ("latency %d samples (both modes). %s\n", lat, ok ? "PASS" : "FAIL");
    vocal->releaseResources(); beat->releaseResources();
    return ok ? 0 : 1;
}
