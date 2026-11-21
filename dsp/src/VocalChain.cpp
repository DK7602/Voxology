#include "vox/VocalChain.h"
#include "vox/Pace.h"

#include <algorithm>
#include <cmath>

namespace vox {

// ---- Beta 33: the movable inserts' order -----------------------------------------------------------------------------
bool isMovable (int module) noexcept
{
    return std::find (kDefaultOrder.begin(), kDefaultOrder.end(), module) != kDefaultOrder.end();
}

bool validOrder (const InsertOrder& o) noexcept
{
    for (const int m : kDefaultOrder)
        if (std::count (o.begin(), o.end(), m) != 1) return false;
    return true;
}

InsertOrder movedTo (InsertOrder o, int module, int to) noexcept
{
    if (! validOrder (o)) o = kDefaultOrder;
    const auto at = std::find (o.begin(), o.end(), module);
    if (at == o.end()) return o;
    std::array<int, kMovable - 1> rest {};
    std::copy (o.begin(), at, rest.begin());
    std::copy (at + 1, o.end(), rest.begin() + (at - o.begin()));
    const int place = std::clamp (to, 0, kMovable - 1);
    InsertOrder out {};
    for (int i = 0, r = 0; i < kMovable; ++i)
        out[static_cast<size_t> (i)] = i == place ? module : rest[static_cast<size_t> (r++)];
    return out;
}

const char* insertKey (int module) noexcept
{
    switch (static_cast<Module> (module))
    {
        case Module::eq: return "eq";
        case Module::dynEq: return "dyneq";
        case Module::deEsser: return "deess";
        case Module::rider: return "rider";
        case Module::comp: return "comp";
        case Module::saturation: return "sat";
        default: return "";
    }
}

std::uint32_t packOrder (const InsertOrder& o) noexcept
{
    std::uint32_t v = 0;
    for (int i = 0; i < kMovable; ++i) v |= static_cast<std::uint32_t> (o[static_cast<size_t> (i)] & 15) << (4 * i);
    return v;
}

InsertOrder unpackOrder (std::uint32_t v) noexcept
{
    InsertOrder o {};
    for (int i = 0; i < kMovable; ++i) o[static_cast<size_t> (i)] = static_cast<int> ((v >> (4 * i)) & 15u);
    return validOrder (o) ? o : kDefaultOrder;
}

std::string orderText (const InsertOrder& o)
{
    std::string s;
    for (const int m : o)
    {
        if (! s.empty()) s += ',';
        s += insertKey (m);
    }
    return s;
}

InsertOrder orderFromText (const std::string& text)
{
    InsertOrder o = kDefaultOrder;
    std::size_t start = 0;
    for (int i = 0; i < kMovable; ++i)
    {
        const auto comma = text.find (',', start);
        const auto part = text.substr (start, comma == std::string::npos ? std::string::npos : comma - start);
        int found = -1;
        for (const int m : kDefaultOrder)
            if (part == insertKey (m)) found = m;
        if (found < 0) return kDefaultOrder;
        o[static_cast<size_t> (i)] = found;
        if (comma == std::string::npos) { if (i != kMovable - 1) return kDefaultOrder; break; }
        start = comma + 1;
    }
    return validOrder (o) ? o : kDefaultOrder;
}

void VocalChain::prepare (double sampleRate, int numChannels, bool lowLatency)
{
    sr = sampleRate;
    chanCount = std::clamp (numChannels, 1, kMaxChannels);
    clipWatch.prepare (chanCount);
    ai.prepare (sr, chanCount);
    denoise.prepare (sr, chanCount);
    pitch.prepare (sr, chanCount, lowLatency);
    // The gate, pops and breaths listen to the chain's input, which is Pitch's latency ahead of them: free look-ahead.
    cleanup.prepare (sr, chanCount, pitch.latencySamples(), kChunk);
    pops.prepare (sr, chanCount, pitch.latencySamples());
    declick.prepare (sr, chanCount, pitch.latencySamples());   // (Record mode: too little look-ahead, it stays off)
    breaths.prepare (sr, chanCount, pitch.latencySamples());
    side.assign (kChunk, 0.0);
    eq.prepare (sr, chanCount);
    dynEq.prepare (sr, chanCount);
    deEsser.prepare (sr, chanCount);
    rider.prepare (sr, chanCount);
    comp.prepare (sr, chanCount);
    saturation.prepare (sr, chanCount);
    voices.prepare (sr);
    doubler.prepare (sr);
    dimension.prepare (sr);
    throws.prepare (sr);
    delay.prepare (sr);
    reverb.prepare (sr);
    for (auto& w : work) w.assign (kChunk, 0.0);
    for (auto& d : dryLine) d.assign (static_cast<size_t> (latencySamples() + 1), 0.0);
    mono.assign (kChunk, 0.0);
    listenMix.prepare (sr);
    peaks.prepare (sr);
    for (auto& l : earlyLine) l.assign (static_cast<size_t> (pitch.latencySamples() + 1), 0.0);
    for (auto& l : lateLine) l.assign (static_cast<size_t> (Saturation::kLatency + 1), 0.0);
    for (auto& l : aiLine) l.assign (static_cast<size_t> (denoise.latencySamples() + 1), 0.0);
    outGlide = design::onePole (0.020, sr);
    abGlide = design::onePole (0.020, sr);
    setParams (params);
    reset();
}

void VocalChain::reset() noexcept
{
    clipWatch.reset();
    ai.reset();
    denoise.reset();
    pitch.reset();
    cleanup.reset();
    pops.reset();
    declick.reset();
    breaths.reset();
    eq.reset();
    dynEq.reset();
    deEsser.reset();
    rider.reset();
    comp.reset();
    saturation.reset();
    voices.reset();
    doubler.reset();
    dimension.reset();
    throws.reset();
    delay.reset();
    reverb.reset();
    for (auto& d : dryLine) std::fill (d.begin(), d.end(), 0.0);
    dryPos = 0;
    listenMix.reset();
    peaks.reset();
    clearListenLines();
    denoise.setListen (0);
    outGain = fromDb (params.outputDb);
    mixB = (params.bypass || params.listenOriginal) ? 0.0 : 1.0;
    gainA = fromDb (params.gainOriginalDb);
    gainB = fromDb (params.gainProcessedDb);
    activeOrder = validOrder (params.order) ? params.order : kDefaultOrder;   // (a fresh start plays the order at once)
    orderGain = 1.0;
    meters = {};
}

void VocalChain::setParams (const ChainParams& p) noexcept
{
    params = p;
    pitch.setParams (p.pitch);
    cleanup.setParams (p.cleanup);
    {
        // Noise / Room: the AI (AI mode: Noise = how far it may turn the sound down) or the classic reducer.
        const bool aiMode = p.cleanup.enabled && p.cleanup.aiMode;
        ai.setParams ({ aiMode, aiMode ? p.cleanup.noiseDb : 0.0 });
        DenoiseParams d;
        if (p.cleanup.enabled && ! aiMode) { d.noiseDb = p.cleanup.noiseDb; d.roomPct = p.cleanup.roomPct; }
        if (p.dynEq.enabled) d.smoothPct = p.dynEq.smoothPct;   // (the Dynamic EQ's Smooth rides on the same spectra)
        denoise.setParams (d);
    }
    pops.setParams ({ p.cleanup.enabled ? p.cleanup.popAmount : 0.0 });
    declick.setParams ({ p.cleanup.enabled ? p.cleanup.clickAmount : 0.0 });
    breaths.setParams ({ p.cleanup.enabled ? p.cleanup.breathDb : 0.0 });
    eq.setParams (p.eq);
    dynEq.setParams (p.dynEq);
    peaks.setParams (static_cast<PeakMode> (std::clamp (p.peakMode, 0, kPeakModes - 1)), p.ceilingDb);
    deEsser.setParams (p.deEsser);
    rider.setParams (p.rider);
    comp.setParams (p.comp);
    saturation.setParams (p.saturation);
    {
        VoicesParams v = p.voices;
        if (! p.doubler.enabled) { v.interval = { 0, 0 }; v.stack = 0; v.midi = false; }
        voices.setParams (v, p.pitch, p.cleanup.enabled ? p.cleanup.lowCutHz : 20.0, p.eq, p.deEsser);
    }
    doubler.setParams (p.doubler);
    dimension.setParams (p.dimension);
    {
        ThrowParams t = p.throws;
        t.division = p.delay.division;
        t.bpm = p.delay.bpm;
        if (! p.delay.enabled) t.echo = 0.0;
        if (! p.reverb.enabled) t.wash = 0.0;
        throws.setParams (t);
    }
    delay.setParams (p.delay);
    reverb.setParams (p.reverb);
}

void VocalChain::clearListenLines() noexcept
{
    for (auto& l : earlyLine) std::fill (l.begin(), l.end(), 0.0);
    for (auto& l : lateLine) std::fill (l.begin(), l.end(), 0.0);
    for (auto& l : aiLine) std::fill (l.begin(), l.end(), 0.0);
    earlyPos = latePos = aiPos = 0;
}

ChainMeters VocalChain::takeMeters() noexcept
{
    ChainMeters m = meters;
    m.gateDb = cleanup.takeGateDb();
    m.noiseDb = std::min (denoise.takeReductionDb(), ai.takeReductionDb());   // (one of them works: classic or AI)
    m.smoothDb = denoise.takeSmoothDb();
    m.popDb = pops.takeCutDb();
    m.clickDb = declick.takeCutDb();
    m.clicks = declick.takeClicks();
    m.throws = throws.takeThrows();
    m.breathDb = breaths.takeGainDb();
    m.dynEqDb = dynEq.takeCutDb();
    m.deEssDb = deEsser.takeCutDb();
    m.riderDb = Rider::isNeutral (params.rider) ? 0.0 : rider.currentGainDb();
    m.peakGrDb = comp.takePeakGrDb();
    m.levelGrDb = comp.takeLevelGrDb();
    saturation.takeEnergies (m.satResidual, m.satSignal);
    m.pitch = pitch.reading();
    m.voiceNotes = voices.currentNotes();
    m.input = clipWatch.take();
    m.listenPlaying = listenMix.audible();
    m.listenGainDb = m.listenPlaying != listen::off ? listenMix.appliedGainDb() : 0.0;
    m.listenUnderDb = m.listenPlaying != listen::off ? listenMix.levelUnderVocalDb() : -100.0;
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

    // The incoming vocal: clipped or too hot? (read-only, for the tips)
    clipWatch.process (ch.data(), nch, len);

    // LISTEN (monitoring only): which module's take-out is formed this chunk. Only while you hear B (the processed
    // vocal); A and bypass fade it out. It is complete once the whole chain's latency has passed through.
    const bool hearB = ! (params.bypass || params.listenOriginal);
    listenMix.request (hearB ? params.listenMode : static_cast<int> (listen::off), latencySamples() + kChunk);
    const int lm = listenMix.beginBlock();
    if (listenMix.startedArming()) clearListenLines();
    denoise.setListen (lm == listen::cleanup ? 1 : lm == listen::dynEq ? 2 : 0);
    std::array<std::array<double, kChunk>, kMaxChannels> early {}, late {}, before {};
    std::array<double*, kMaxChannels> earlyPtr {};
    for (int c = 0; c < nch; ++c) earlyPtr[static_cast<size_t> (c)] = early[static_cast<size_t> (c)].data();
    const auto keep = [&] { for (int c = 0; c < nch; ++c) std::copy_n (ch[static_cast<size_t> (c)], len, before[static_cast<size_t> (c)].data()); };
    const auto takenOut = [&]
    {
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < len; ++i)
                late[static_cast<size_t> (c)][static_cast<size_t> (i)] += before[static_cast<size_t> (c)][static_cast<size_t> (i)] - ch[static_cast<size_t> (c)][i];
    };

    // Noise / room reduction first (Cleanup's, the AI or the classic reducer): Pitch, the side-chain below and everything
    // else hear the cleaner vocal. Both always run (a constant delay, and no stale frame when B comes back; the AI costs
    // almost nothing while it's off); not in Record mode.
    if (! pitch.isLowLatency())
    {
        std::array<std::array<double, kChunk>, kMaxChannels> aiOut {};
        std::array<double*, kMaxChannels> aiPtr {};
        for (int c = 0; c < nch; ++c) aiPtr[static_cast<size_t> (c)] = aiOut[static_cast<size_t> (c)].data();
        const bool aiListen = lm == listen::cleanup && ai.working();
        ai.process (ch.data(), nch, len, aiListen ? aiPtr.data() : nullptr);
        denoise.process (ch.data(), nch, len, lm != listen::off ? earlyPtr.data() : nullptr);
        if (aiListen)   // what the AI took out, a noise-reducer frame later (in line with what that took out)
        {
            const int aLen = static_cast<int> (aiLine[0].size());
            for (int i = 0; i < len; ++i)
            {
                for (int c = 0; c < nch; ++c)
                {
                    const auto cs = static_cast<size_t> (c);
                    auto& al = aiLine[cs];
                    al[static_cast<size_t> (aiPos)] = aiOut[cs][static_cast<size_t> (i)];
                    early[cs][static_cast<size_t> (i)] += al[static_cast<size_t> ((aiPos + 1) % aLen)];
                }
                aiPos = (aiPos + 1) % aLen;
            }
        }
    }

    // The input, before Pitch delays it: the gate / pops / breaths side-chain.
    cleanup.listen (ch.data(), nch, len);
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
        if (lm == listen::cleanup) keep();
        cleanup.process (ch.data(), nch, len);
        pops.process (ch.data(), nch, len, side.data());
        declick.process (ch.data(), nch, len, side.data());
        breaths.process (ch.data(), nch, len, side.data());
        if (lm == listen::cleanup) takenOut();
        // Beta 33: the movable inserts, in the order they play now (Voxology's own unless you moved one in the Rack).
        for (const int m : activeOrder)
        {
            switch (static_cast<Module> (m))
            {
                case Module::eq: eq.process (ch.data(), nch, len); break;
                case Module::dynEq:
                    if (lm == listen::dynEq) keep();
                    dynEq.process (ch.data(), nch, len);
                    if (lm == listen::dynEq) takenOut();
                    break;
                case Module::deEsser:
                    if (lm == listen::deEsser) keep();
                    deEsser.process (ch.data(), nch, len);
                    if (lm == listen::deEsser) takenOut();
                    break;
                case Module::rider: rider.process (ch.data(), nch, len); break;
                case Module::comp: comp.process (ch.data(), nch, len); break;
                case Module::saturation: saturation.process (ch.data(), nch, len); break;
                default: break;
            }
        }
        // A new order: this chunk fades the inserts' output out, they switch while silent, the next chunk fades in.
        {
            const bool change = validOrder (params.order) && params.order != activeOrder;
            const double g0 = orderGain, g1 = change ? 0.0 : 1.0;
            if (g0 != 1.0 || g1 != 1.0)
                for (int i = 0; i < len; ++i)
                {
                    const double t = static_cast<double> (i + 1) / len;
                    const double g = g0 + (g1 - g0) * (0.5 - 0.5 * std::cos (3.14159265358979323846 * t));
                    for (int c = 0; c < nch; ++c) ch[static_cast<size_t> (c)][i] *= g;
                }
            orderGain = g1;
            if (change) activeOrder = params.order;
        }

        // Space: everything hears the mono vocal after the inserts.
        for (int i = 0; i < len; ++i)
        {
            double s = 0.0;
            for (int c = 0; c < nch; ++c) s += ch[static_cast<size_t> (c)][i];
            mono[static_cast<size_t> (i)] = s / nch;
        }
        // Harmonies: from the input (ahead by Pitch's latency, which they add back), into the lead and the
        // mono feed, so the doubler, delay and reverb carry them too.
        if (! pitch.isLowLatency())   // (Record mode: the harmonies need the normal look-ahead)
            voices.process (side.data(), ch.data(), nch, mono.data(), len);
        doubler.process (mono.data(), ch.data(), nch, len);
        dimension.process (ch.data(), nch, len);   // (the delay and reverb still hear the plain vocal)
        delay.process (mono.data(), ch.data(), nch, len);
        reverb.process (mono.data(), ch.data(), nch, len);
        throws.process (mono.data(), ch.data(), nch, len);
    }

    // LISTEN: the take-out lined up with the vocal: the spectral stage's waits Pitch's latency, then all of it
    // Saturation's.
    if (lm != listen::off)
    {
        const int eLen = static_cast<int> (earlyLine[0].size()), lLen = static_cast<int> (lateLine[0].size());
        for (int i = 0; i < len; ++i)
        {
            for (int c = 0; c < nch; ++c)
            {
                const auto cs = static_cast<size_t> (c), is = static_cast<size_t> (i);
                auto& el = earlyLine[cs];
                el[static_cast<size_t> (earlyPos)] = early[cs][is];
                const double e = el[static_cast<size_t> ((earlyPos + 1) % eLen)];   // eLen - 1 samples ago
                auto& ll = lateLine[cs];
                ll[static_cast<size_t> (latePos)] = e + late[cs][is];
                late[cs][is] = ll[static_cast<size_t> ((latePos + 1) % lLen)];
            }
            earlyPos = (earlyPos + 1) % eLen;
            latePos = (latePos + 1) % lLen;
        }
    }

    const double outTarget = fromDb (std::clamp (params.outputDb, -24.0, 24.0));
    const double gATarget = fromDb (std::min (0.0, params.listenOriginal && ! params.bypass ? params.gainOriginalDb : 0.0));
    const double gBTarget = fromDb (std::min (0.0, params.gainProcessedDb));
    const double mixTarget = wantB ? 1.0 : 0.0;
    double check = 0.0;   // stays 0 unless an output sample is NaN / inf (x * 0 is NaN for those)
    for (int i = 0; i < len; ++i)
    {
        outGain += (outTarget - outGain) * outGlide;
        mixB += (mixTarget - mixB) * abGlide;
        if (std::abs (mixB - mixTarget) < 1.0e-6) mixB = mixTarget;
        gainA += (gATarget - gainA) * abGlide;
        gainB += (gBTarget - gainB) * abGlide;
        double processed[kMaxChannels] {};
        if (mixB > 0.0)
        {
            // The Output level, then its PEAKS (Clip / Limit at the Ceiling): what leaves Voxology, before MATCH's
            // turn-down for a fair A / B.
            for (int c = 0; c < nch; ++c) processed[c] = ch[static_cast<size_t> (c)][i] * outGain;
            peaks.process (processed, nch);
        }
        for (int c = 0; c < nch; ++c)
        {
            double& y = ch[static_cast<size_t> (c)][i];
            const double a = dry[static_cast<size_t> (c)][static_cast<size_t> (i)];
            if (mixB == 1.0)       y = processed[c] * gainB;
            else if (mixB == 0.0)  y = params.bypass ? a : a * gainA;
            else                   y = mixB * processed[c] * gainB + (1.0 - mixB) * a * gainA;
            check += y * 0.0;
        }
        if (listenMix.isActive())
        {
            // LISTEN: the take-out (at the vocal's own output level) plays in the vocal's place.
            double v[2] { ch[0][i], ch[static_cast<size_t> (nch - 1)][i] }, t[2] {};
            for (int c = 0; c < nch; ++c) t[c] = late[static_cast<size_t> (c)][static_cast<size_t> (i)] * outGain * gainB;
            listenMix.process (v, t, nch);
            for (int c = 0; c < nch; ++c) ch[static_cast<size_t> (c)][i] = v[c];
        }
    }
    if (! std::isfinite (check))
    {
        // A module went bad anyway (the input is already cleaned, so this should never happen): start every module
        // over and play silence for this moment, instead of NaN until the next prepare.
        reset();
        for (int c = 0; c < nch; ++c) std::fill_n (ch[static_cast<size_t> (c)], len, 0.0);
    }
}

std::vector<std::vector<float>> VocalChain::render (const std::vector<std::vector<float>>& in, double sampleRate, const ChainParams& p,
                                                    const std::atomic<bool>* cancel)
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
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
            break;
        pace::tick();
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
