#include "vox/DynamicEq.h"

#include <algorithm>

namespace vox {

using design::onePole;

void DynamicEq::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto& info = kDynBandInfo[b];
        auto& band = bands[b];
        band.envCoeff = onePole (0.5 * info.averageS, sr);   // two stages: about averageS in all
        band.cutAtk = onePole (info.attackS, sr);
        band.cutRel = onePole (info.releaseS, sr);
    }
    freqGlide = onePole (0.020, sr / 8.0);
    voiceAvg = onePole (0.010, sr);
    voiceFall = onePole (2.0, sr);
    design::apply (refHp, design::butterworth (true, 80.0, sr));
    for (auto& f : sibHp) design::apply (f, design::butterworth (true, std::min (6000.0, 0.4 * sr), sr));
    reset();
}

void DynamicEq::reset() noexcept
{
    refHp.reset();
    voiceEnv = voiceEnv2 = voicePeak = 0.0;
    for (auto& f : sibHp) f.reset();
    sibEnv = sibEnv2 = 0.0;
    for (size_t b = 0; b < bands.size(); ++b)
    {
        clearBand (bands[b]);
        bands[b].cut = 0.0;
        bands[b].curFreq = std::clamp (params.freqHz[b], kDynBandInfo[b].lo, kDynBandInfo[b].hi);
    }
    countdown = 0;
}

void DynamicEq::clearBand (Band& band) noexcept
{
    band.detect.reset();
    for (auto& f : band.cutFilter) { f = Biquad {}; }   // identity: exactly transparent
    band.env = band.env2 = band.fullEnv = band.fullEnv2 = band.normal = 0.0;
    band.designedCut = 0.0;
    band.designedFreq = band.detectFreq = -1.0;
    band.learned = false;
    band.learnedSeconds = 0.0;
}

static design::Coeffs cutCoeffs (int band, double cutDb, double freqHz, double sr) noexcept
{
    return design::bell (std::clamp (freqHz, 20.0, 0.45 * sr), -cutDb, kDynBandInfo[static_cast<size_t> (band)].q, sr);
}

double DynamicEq::bandResponseDb (int band, double cutDb, double freqHz, double f, double sr)
{
    if (cutDb < 1.0e-6) return 0.0;
    const auto c = cutCoeffs (band, cutDb, freqHz, sr);
    const double w = 2.0 * std::numbers::pi * f / sr, cw = std::cos (w), c2w = std::cos (2.0 * w);
    const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2 + 2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * cw + 2.0 * c.b0 * c.b2 * c2w;
    const double den = 1.0 + c.a1 * c.a1 + c.a2 * c.a2 + 2.0 * (c.a1 + c.a1 * c.a2) * cw + 2.0 * c.a2 * c2w;
    return 10.0 * std::log10 (std::max (num, 1.0e-30) / std::max (den, 1.0e-30));
}

bool DynamicEq::bandActive (size_t b) const noexcept
{
    return (params.enabled && params.maxCutDb[b] >= 0.05) || bands[b].cut > 0.0;
}

void DynamicEq::design (Band& band, int index) noexcept
{
    const auto i = static_cast<size_t> (index);
    const double tf = std::clamp (params.freqHz[i], kDynBandInfo[i].lo, kDynBandInfo[i].hi);
    band.curFreq *= std::pow (tf / std::max (band.curFreq, 1.0), freqGlide);
    if (std::abs (tf - band.curFreq) < 0.01) band.curFreq = tf;

    if (std::abs (band.curFreq - band.detectFreq) > 0.01)
    {
        // The detector listens to exactly the slice the bell cuts.
        design::apply (band.detect, design::bandPass (band.curFreq, kDynBandInfo[i].q, sr));
        band.detectFreq = band.curFreq;
    }
    if (std::abs (band.cut - band.designedCut) > 0.005 || (band.cut == 0.0 && band.designedCut != 0.0)
        || (band.cut > 0.0 && band.curFreq != band.designedFreq))
    {
        const auto c = cutCoeffs (index, band.cut, band.curFreq, sr);
        for (auto& f : band.cutFilter) design::apply (f, c);
        band.designedCut = band.cut;
        band.designedFreq = band.curFreq;
    }
}

void DynamicEq::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, channels);
    bool any = false;
    for (size_t b = 0; b < bands.size(); ++b) any |= bandActive (b);
    if (! any)
    {
        // Off (or every Max Cut at 0) and nothing still releasing: untouched, bit for bit.
        if (countdown != 0) reset();
        return;
    }

    const double thr = thresholdDb (params.sensitivity);
    const double floorE = fromDb (2.0 * kVoiceFloorDb);   // energy
    const double windowE = fromDb (-2.0 * kVoiceWindowDb);
    const double halfKnee = 0.5 * kKneeDb;
    std::array<bool, kDynBands> active {};
    for (size_t b = 0; b < bands.size(); ++b)
    {
        active[b] = bandActive (b);
        if (! active[b] && (bands[b].learned || bands[b].designedFreq > 0.0))
            clearBand (bands[b]);
    }

    for (int i = 0; i < n; ++i)
    {
        if (--countdown <= 0)
        {
            countdown = 8;
            for (size_t b = 0; b < bands.size(); ++b)
                if (active[b]) design (bands[b], static_cast<int> (b));
        }

        double mono = 0.0;
        for (int c = 0; c < nch; ++c) mono += ch[c][i];
        mono /= nch;
        const double ref = refHp.process (mono);
        const double refSq = ref * ref;
        // Your recent voice level: rises at once, falls over seconds. The gaps between phrases
        // (room noise, bleed) sit far under it and are ignored, so they never teach "normal".
        voiceEnv += (refSq - voiceEnv) * voiceAvg;
        voiceEnv2 += (voiceEnv - voiceEnv2) * voiceAvg;
        voicePeak = voiceEnv2 > voicePeak ? voiceEnv2 : voicePeak + (voiceEnv2 - voicePeak) * voiceFall;
        const double voiceGate = std::max (floorE, voicePeak * windowE);
        bool sibilant = false;
        if (active[kDynBands - 1])
        {
            const double h = sibHp[1].process (sibHp[0].process (mono));
            const double coeff = bands[kDynBands - 1].envCoeff;
            sibEnv += (h * h - sibEnv) * coeff;
            sibEnv2 += (sibEnv - sibEnv2) * coeff;
            sibilant = sibEnv2 > bands[kDynBands - 1].env2;
        }

        for (size_t b = 0; b < bands.size(); ++b)
        {
            if (! active[b]) continue;
            auto& band = bands[b];
            const double d = band.detect.process (mono);
            const double dSq = d * d;
            // True average energy (not peaks: a buzzy voice's peaks would hide the band's share).
            band.env += (dSq - band.env) * band.envCoeff;
            band.env2 += (band.env - band.env2) * band.envCoeff;
            band.fullEnv += (refSq - band.fullEnv) * band.envCoeff;
            band.fullEnv2 += (band.fullEnv - band.fullEnv2) * band.envCoeff;

            double target = 0.0;
            const bool sHolds = sibilant && b == kDynBands - 1;   // an "s": the De-Esser's job
            if (band.fullEnv2 > voiceGate && band.env2 > 1.0e-30 && ! sHolds)
            {
                // How loud this slice is compared with the rest of the voice, vs how it usually is.
                // (Against the rest, not the whole: a bloom that dominates the voice still shows.)
                const double rest = std::max (band.fullEnv2 - band.env2, 0.02 * band.fullEnv2);
                const double ratioDb = 10.0 * std::log10 (band.env2 / rest);
                band.learned = true;
                if (band.learnedSeconds < kWarmUpSeconds) band.normal = ratioDb;   // the detector is still settling
                const double over = ratioDb - band.normal - thr;
                double knee = 0.0;
                if (over >= halfKnee) knee = over;
                else if (over > -halfKnee) knee = (over + halfKnee) * (over + halfKnee) / (2.0 * kKneeDb);
                const bool on = params.enabled && params.maxCutDb[b] >= 0.05;
                target = on && band.learnedSeconds >= kWarmUpSeconds ? std::clamp (kSlope * knee, 0.0, std::min (params.maxCutDb[b], kDynMaxCutDb)) : 0.0;

                // Learn the normal from the voice itself: a running percentile (each sample nudges it
                // up or down by a fixed step), so it settles where the band sits most of the time and
                // neither the jumps nor the dips pull it far, however much the voice swings.
                const double rate = (band.learnedSeconds < 1.5 ? kLearnFastDbPerS : kLearnDbPerS) / sr;
                band.normal += ratioDb > band.normal ? rate * kNormalPercentile : -rate * (1.0 - kNormalPercentile);
                band.learnedSeconds += 1.0 / sr;
            }
            band.cut += (target - band.cut) * (target > band.cut ? band.cutAtk : band.cutRel);
            if (band.cut < 1.0e-4 && target == 0.0) band.cut = 0.0;
            maxCut[b] = std::max (maxCut[b], band.cut);

            // The bell runs whenever the band is on: at 0 dB it is exactly transparent, so nothing
            // switches in or out between words (no ticks).
            for (int c = 0; c < nch; ++c)
                ch[c][i] = band.cutFilter[static_cast<size_t> (c)].process (ch[c][i]);
        }
    }
}

} // namespace vox
