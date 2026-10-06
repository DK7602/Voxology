#include "vox/Voices.h"

#include "vox/Saturation.h"

#include <algorithm>

namespace vox {

using design::onePole;

static constexpr int kBlock = 256;   // VocalChain::kChunk: the chain never sends more at once

void HarmonyVoices::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& v : voice) v.prepare (sr, 1);
    for (auto& b : buf) b.assign (kBlock, 0.0);
    eq.prepare (sr, 2);
    deEsser.prepare (sr, 2);
    align.assign (static_cast<size_t> (Saturation::kLatency + 1) * 2, 0.0);
    followCoeff = onePole (kFollowSeconds, sr);
    gainCoeff = onePole (0.020, sr);
    peakFall = onePole (2.0, sr);
    scratchL.assign (kBlock, 0.0);
    scratchR.assign (kBlock, 0.0);
    reset();
}

void HarmonyVoices::reset() noexcept
{
    for (auto& v : voice) v.reset();
    for (auto& v : lowCut) for (auto& f : v) f.reset();
    designedLowCut = -1.0;
    eq.reset();
    deEsser.reset();
    std::fill (align.begin(), align.end(), 0.0);
    alignPos = 0;
    voiceMs.fill (0.0);
    leadMs = leadPeak = 0.0;
    follow = 0.0;
    gain = 0.0;
    running = false;
}

void HarmonyVoices::setParams (const VoicesParams& v, const PitchParams& lead, double lowCut_, const EqParams& eqp, const DeEsserParams& ds) noexcept
{
    params = v;
    for (size_t k = 0; k < voice.size(); ++k)
    {
        PitchParams p;
        p.enabled = true;
        p.amount = 100.0;
        p.key = lead.key;
        p.scale = lead.scale;
        p.speedMs = std::min (lead.speedMs, 40.0);   // backing voices hold their notes; a slow lead retune would smear them
        p.humanize = lead.humanize;
        p.formant = std::clamp (v.formant, -kMaxFormant, kMaxFormant);
        p.harmony = std::clamp (v.interval[k], 0, kHarmonies - 1);
        voice[k].setParams (p);
    }
    lowCutHz = lowCut_;
    eq.setParams (eqp);
    deEsser.setParams (ds);
    // Voice 1 left, voice 2 right (equal-power); one voice alone sits in the middle.
    const bool both = v.interval[0] != 0 && v.interval[1] != 0;
    const double spread = std::clamp (v.width, 0.0, 100.0) / 100.0;
    for (size_t k = 0; k < 2; ++k)
    {
        const double pos = both ? (k == 0 ? -spread : spread) : 0.0;   // -1 left .. +1 right
        const double a = (pos + 1.0) * 0.25 * std::numbers::pi;
        panL[k] = std::cos (a);
        panR[k] = std::sin (a);
    }
}

std::array<int, 2> HarmonyVoices::currentNotes() const noexcept
{
    std::array<int, 2> out { -1, -1 };
    for (size_t k = 0; k < 2; ++k)
    {
        const auto r = voice[k].reading();
        if (params.interval[k] != 0 && r.voiced && r.targetMidi >= 0)
            out[k] = static_cast<int> (std::lround (r.sungMidi + r.correction));
    }
    return out;
}

void HarmonyVoices::process (const double* side, double* const* ch, int nch, double* mono, int n) noexcept
{
    if (isNeutral (params) && gain == 0.0)
    {
        if (running) reset();
        return;
    }
    running = true;
    if (std::abs (lowCutHz - designedLowCut) > 0.01)
    {
        designedLowCut = lowCutHz;
        const auto c = design::butterworth (true, std::max (lowCutHz, 60.0), sr);   // at least 60 Hz: no rumble in the backing
        for (auto& v : lowCut) for (auto& f : v) design::apply (f, c);
    }
    const double targetGain = isNeutral (params) ? 0.0 : fromDb (0.0) * std::clamp (params.level, 0.0, 100.0) / 100.0;
    const double maxFollow = fromDb (kMaxFollowDb);
    const int dlen = Saturation::kLatency + 1;

    for (int start = 0; start < n; start += kBlock)
    {
        const int len = std::min (kBlock, n - start);
        // Each voice from the input (ahead of the lead by the pitch latency, which the voice adds back).
        for (size_t k = 0; k < 2; ++k)
        {
            auto& b = buf[k];
            if (params.interval[k] == 0) { std::fill (b.begin(), b.begin() + len, 0.0); continue; }
            std::copy (side + start, side + start + len, b.begin());
            double* p = b.data();
            voice[k].process (&p, 1, len);
        }
        for (int i = 0; i < len; ++i)
        {
            const auto ii = static_cast<size_t> (i);
            // Each voice: low cut, then into the stereo backing bus (panned).
            double l = 0.0, r = 0.0;
            for (size_t k = 0; k < 2; ++k)
            {
                if (params.interval[k] == 0) continue;
                const double x = lowCut[k][1].process (lowCut[k][0].process (buf[k][ii]));
                voiceMs[k] += (x * x - voiceMs[k]) * followCoeff;
                l += panL[k] * x;
                r += panR[k] * x;
            }
            scratchL[ii] = l;
            scratchR[ii] = r;
        }
        // The lead's tone and de-essing on the backing bus.
        std::array<double*, 2> bus { scratchL.data(), scratchR.data() };
        eq.process (bus.data(), 2, len);
        deEsser.process (bus.data(), 2, len);

        for (int i = 0; i < len; ++i)
        {
            const auto ii = static_cast<size_t> (i);
            // Line up with the finished lead (Saturation's latency), then follow its level.
            const int w = alignPos, rd = (alignPos + 1) % dlen;
            align[static_cast<size_t> (w * 2)] = scratchL[ii];
            align[static_cast<size_t> (w * 2 + 1)] = scratchR[ii];
            const double dl = align[static_cast<size_t> (rd * 2)], dr = align[static_cast<size_t> (rd * 2 + 1)];
            alignPos = rd;

            double lead = 0.0;
            for (int c = 0; c < nch; ++c) lead += ch[c][start + i];
            lead /= nch;
            leadMs += (lead * lead - leadMs) * followCoeff;
            leadPeak = leadMs > leadPeak ? leadMs : leadPeak + (leadMs - leadPeak) * peakFall;
            // Follow the lead's level while it sings; in the gaps (40 dB under its recent level) hold, so
            // the voices never turn the room noise up.
            const int active = (params.interval[0] != 0 ? 1 : 0) + (params.interval[1] != 0 ? 1 : 0);
            const double vMs = (voiceMs[0] + voiceMs[1]) / std::max (1, active) + 1.0e-12;
            if (leadMs > 1.0e-4 * leadPeak && leadMs > 1.0e-10)
                follow = std::min (std::sqrt (leadMs / vMs), maxFollow);
            gain += (targetGain * follow - gain) * gainCoeff;
            if (targetGain == 0.0 && gain < 1.0e-6) gain = 0.0;

            const double ol = gain * dl, orr = gain * dr;
            if (nch >= 2) { ch[0][start + i] += ol; ch[1][start + i] += orr; }
            else ch[0][start + i] += 0.5 * (ol + orr);
            if (mono != nullptr) mono[start + i] += 0.5 * (ol + orr);
        }
    }
}

} // namespace vox
