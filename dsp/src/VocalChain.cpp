#include "vox/VocalChain.h"

#include <algorithm>

namespace vox {

void VocalChain::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    chanCount = std::clamp (numChannels, 1, kMaxChannels);
    pitch.prepare (sr, chanCount);
    cleanup.prepare (sr, chanCount);
    // Pops and breaths listen to the chain's input, which is Pitch's latency ahead of them: free look-ahead.
    pops.prepare (sr, chanCount, pitch.latencySamples());
    breaths.prepare (sr, chanCount, pitch.latencySamples());
    side.assign (kChunk, 0.0);
    eq.prepare (sr, chanCount);
    dynEq.prepare (sr, chanCount);
    deEsser.prepare (sr, chanCount);
    rider.prepare (sr, chanCount);
    comp.prepare (sr, chanCount);
    saturation.prepare (sr, chanCount);
    doubler.prepare (sr);
    delay.prepare (sr);
    reverb.prepare (sr);
    for (auto& w : work) w.assign (kChunk, 0.0);
    for (auto& d : dryLine) d.assign (static_cast<size_t> (latencySamples() + 1), 0.0);
    mono.assign (kChunk, 0.0);
    outGlide = design::onePole (0.020, sr);
    abGlide = design::onePole (0.020, sr);
    setParams (params);
    reset();
}

void VocalChain::reset() noexcept
{
    pitch.reset();
    cleanup.reset();
    pops.reset();
    breaths.reset();
    eq.reset();
    dynEq.reset();
    deEsser.reset();
    rider.reset();
    comp.reset();
    saturation.reset();
    doubler.reset();
    delay.reset();
    reverb.reset();
    for (auto& d : dryLine) std::fill (d.begin(), d.end(), 0.0);
    dryPos = 0;
    outGain = fromDb (params.outputDb);
    mixB = (params.bypass || params.listenOriginal) ? 0.0 : 1.0;
    gainA = fromDb (params.gainOriginalDb);
    gainB = fromDb (params.gainProcessedDb);
    meters = {};
}

void VocalChain::setParams (const ChainParams& p) noexcept
{
    params = p;
    pitch.setParams (p.pitch);
    cleanup.setParams (p.cleanup);
    pops.setParams ({ p.cleanup.enabled ? p.cleanup.popAmount : 0.0 });
    breaths.setParams ({ p.cleanup.enabled ? p.cleanup.breathDb : 0.0 });
    eq.setParams (p.eq);
    dynEq.setParams (p.dynEq);
    deEsser.setParams (p.deEsser);
    rider.setParams (p.rider);
    comp.setParams (p.comp);
    saturation.setParams (p.saturation);
    doubler.setParams (p.doubler);
    delay.setParams (p.delay);
    reverb.setParams (p.reverb);
}

ChainMeters VocalChain::takeMeters() noexcept
{
    ChainMeters m = meters;
    m.gateDb = cleanup.takeGateDb();
    m.popDb = pops.takeCutDb();
    m.breathDb = breaths.takeGainDb();
    m.dynEqDb = dynEq.takeCutDb();
    m.deEssDb = deEsser.takeCutDb();
    m.riderDb = Rider::isNeutral (params.rider) ? 0.0 : rider.currentGainDb();
    m.peakGrDb = comp.takePeakGrDb();
    m.levelGrDb = comp.takeLevelGrDb();
    saturation.takeEnergies (m.satResidual, m.satSignal);
    m.pitch = pitch.reading();
    meters = {};
    return m;
}

void VocalChain::processChunk (int nch, int len) noexcept
{
    std::array<double*, kMaxChannels> ch {};
    for (int c = 0; c < nch; ++c) ch[static_cast<size_t> (c)] = work[static_cast<size_t> (c)].data();

    // Keep the untouched input, delayed by the chain's latency (bypass and A of A/B).
    std::array<std::array<double, kChunk>, kMaxChannels> dry;
    const int lat = latencySamples();
    const int dlen = lat + 1;
    for (int i = 0; i < len; ++i)
    {
        for (int c = 0; c < nch; ++c)
        {
            auto& line = dryLine[static_cast<size_t> (c)];
            line[static_cast<size_t> (dryPos)] = ch[static_cast<size_t> (c)][i];
            int r = dryPos - lat; if (r < 0) r += dlen;
            dry[static_cast<size_t> (c)][static_cast<size_t> (i)] = line[static_cast<size_t> (r)];
        }
        if (++dryPos >= dlen) dryPos = 0;
    }

    // The mono input, before Pitch delays it: the pops / breaths side-chain.
    for (int i = 0; i < len; ++i)
    {
        double m = 0.0;
        for (int c = 0; c < nch; ++c) m += ch[static_cast<size_t> (c)][i];
        side[static_cast<size_t> (i)] = m / nch;
    }

    const bool wantB = ! (params.bypass || params.listenOriginal);
    const bool fullyA = ! wantB && mixB == 0.0;
    if (! fullyA || ! params.bypass)
    {
        // Inserts (the chain keeps running while you listen to A, so B comes back without a jump).
        pitch.process (ch.data(), nch, len);
        cleanup.process (ch.data(), nch, len);
        pops.process (ch.data(), nch, len, side.data());
        breaths.process (ch.data(), nch, len, side.data());
        eq.process (ch.data(), nch, len);
        dynEq.process (ch.data(), nch, len);
        deEsser.process (ch.data(), nch, len);
        rider.process (ch.data(), nch, len);
        comp.process (ch.data(), nch, len);
        saturation.process (ch.data(), nch, len);

        // Space: everything hears the mono vocal after the inserts.
        for (int i = 0; i < len; ++i)
        {
            double s = 0.0;
            for (int c = 0; c < nch; ++c) s += ch[static_cast<size_t> (c)][i];
            mono[static_cast<size_t> (i)] = s / nch;
        }
        doubler.process (mono.data(), ch.data(), nch, len);
        delay.process (mono.data(), ch.data(), nch, len);
        reverb.process (mono.data(), ch.data(), nch, len);
    }

    const double outTarget = fromDb (std::clamp (params.outputDb, -24.0, 24.0));
    const double gATarget = fromDb (std::min (0.0, params.listenOriginal && ! params.bypass ? params.gainOriginalDb : 0.0));
    const double gBTarget = fromDb (std::min (0.0, params.gainProcessedDb));
    const double mixTarget = wantB ? 1.0 : 0.0;
    for (int i = 0; i < len; ++i)
    {
        outGain += (outTarget - outGain) * outGlide;
        mixB += (mixTarget - mixB) * abGlide;
        if (std::abs (mixB - mixTarget) < 1.0e-6) mixB = mixTarget;
        gainA += (gATarget - gainA) * abGlide;
        gainB += (gBTarget - gainB) * abGlide;
        for (int c = 0; c < nch; ++c)
        {
            double& y = ch[static_cast<size_t> (c)][i];
            const double a = dry[static_cast<size_t> (c)][static_cast<size_t> (i)];
            if (mixB == 1.0)       y = y * outGain * gainB;
            else if (mixB == 0.0)  y = params.bypass ? a : a * gainA;
            else                   y = mixB * y * outGain * gainB + (1.0 - mixB) * a * gainA;
        }
    }
}

std::vector<std::vector<float>> VocalChain::render (const std::vector<std::vector<float>>& in, double sampleRate, const ChainParams& p)
{
    if (in.empty()) return {};
    const int nch = std::min (static_cast<int> (in.size()), kMaxChannels);
    const int n = static_cast<int> (in.front().size());
    VocalChain chain;
    chain.setParams (p);
    chain.prepare (sampleRate, nch);

    std::vector<std::vector<float>> out (static_cast<size_t> (nch), std::vector<float> (static_cast<size_t> (n), 0.0f));
    std::vector<std::vector<double>> buf (static_cast<size_t> (nch), std::vector<double> (static_cast<size_t> (kChunk)));
    std::array<double*, kMaxChannels> ptr {};
    for (int c = 0; c < nch; ++c) ptr[static_cast<size_t> (c)] = buf[static_cast<size_t> (c)].data();

    const int lat = chain.latencySamples();
    const int total = n + lat;
    for (int start = 0; start < total; start += kChunk)
    {
        const int len = std::min (kChunk, total - start);
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < len; ++i)
            {
                const int s = start + i;
                buf[static_cast<size_t> (c)][static_cast<size_t> (i)] = s < n ? in[static_cast<size_t> (c)][static_cast<size_t> (s)] : 0.0;
            }
        chain.process (ptr.data(), nch, len);
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < len; ++i)
            {
                const int o = start + i - lat;
                if (o >= 0 && o < n)
                    out[static_cast<size_t> (c)][static_cast<size_t> (o)] = static_cast<float> (buf[static_cast<size_t> (c)][static_cast<size_t> (i)]);
            }
    }
    return out;
}

} // namespace vox
