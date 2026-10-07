#include "vox/Modules.h"

#include <algorithm>

namespace vox {

using design::onePole;

// ================================================================================================
// Cleanup
void Cleanup::prepare (double sampleRate, int numChannels, int lookAheadAvailable, int maxBlock)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    const int la = static_cast<int> (std::lround (kLookAheadSeconds * sr));
    lookAhead = std::min (la, std::max (0, lookAheadAvailable));
    peakLine.assign (static_cast<size_t> (std::max (0, lookAheadAvailable - la) + 1), 0.0);
    heard.assign (static_cast<size_t> (std::max (0, maxBlock)), 0.0);
    envRelease = onePole (0.030, sr);
    gainAttack = onePole (0.001, sr);
    gainRelease = onePole (0.120, sr);
    reset();
}

void Cleanup::reset() noexcept
{
    for (auto& c : hp) for (auto& f : c) f.reset();
    for (auto& c : listenHp) for (auto& f : c) f.reset();
    designedHz = -1.0;
    std::fill (peakLine.begin(), peakLine.end(), 0.0);
    peakPos = 0;
    listened = false;
    env = 0.0;
    gain = 1.0;
    gainDb = 0.0;
    holdLeft = 0;
    open = true;
}

void Cleanup::updateFilter() noexcept
{
    if (std::abs (params.lowCutHz - designedHz) < 0.01)
        return;
    designedHz = params.lowCutHz;
    const auto c = design::butterworth (true, std::max (params.lowCutHz, 10.0), sr);
    for (auto& chan : hp)
        for (auto& f : chan) design::apply (f, c);
    for (auto& chan : listenHp)
        for (auto& f : chan) design::apply (f, c);
}

void Cleanup::listen (const double* const* in, int nch, int n) noexcept
{
    listened = false;
    nch = std::min (nch, channels);
    if (isNeutral (params) || params.gateRangeDb < 0.05 || n > static_cast<int> (heard.size()))
        return;
    const bool lowCut = params.lowCutHz > kLowCutOffHz;
    if (lowCut) updateFilter();
    for (int i = 0; i < n; ++i)
    {
        double peak = 0.0;
        for (int c = 0; c < nch; ++c)
        {
            double x = in[c][i];
            if (lowCut)
                x = listenHp[static_cast<size_t> (c)][1].process (listenHp[static_cast<size_t> (c)][0].process (x));
            peak = std::max (peak, std::abs (x));
        }
        heard[static_cast<size_t> (i)] = peak;
    }
    listened = true;
}

void Cleanup::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, channels);
    if (isNeutral (params))
    {
        if (gain != 1.0 || designedHz > 0.0) reset();
        return;
    }

    const bool lowCut = params.lowCutHz > kLowCutOffHz;
    if (lowCut) updateFilter();
    const bool gateOn = params.gateRangeDb >= 0.05;
    const double openLevel = fromDb (params.gateThrDb), closeLevel = fromDb (params.gateThrDb - kHysteresisDb);
    // Listening ahead: the gate also closes that much earlier, so hold it open that much longer.
    const bool ahead = listened;
    listened = false;
    const int hold = static_cast<int> (kHoldSeconds * sr) + (ahead ? lookAhead : 0);

    for (int i = 0; i < n; ++i)
    {
        double peak = 0.0;
        for (int c = 0; c < nch; ++c)
        {
            double x = ch[c][i];
            if (lowCut)
                x = hp[static_cast<size_t> (c)][1].process (hp[static_cast<size_t> (c)][0].process (x));
            ch[c][i] = x;
            peak = std::max (peak, std::abs (x));
        }
        if (! gateOn)
            continue;

        if (ahead)
        {
            // What the input had lookAheadAvailable - kLookAheadSeconds ago: kLookAheadSeconds ahead of this sample.
            peakLine[static_cast<size_t> (peakPos)] = heard[static_cast<size_t> (i)];
            if (++peakPos >= static_cast<int> (peakLine.size())) peakPos = 0;
            peak = peakLine[static_cast<size_t> (peakPos)];
        }
        env = peak > env ? peak : env + (peak - env) * envRelease;
        if (env > openLevel) { open = true; holdLeft = hold; }
        else if (env < closeLevel && holdLeft <= 0) open = false;
        if (holdLeft > 0) --holdLeft;

        // Fade in dB (like a fader), so the last few dB down don't take ages.
        const double target = open ? 0.0 : -params.gateRangeDb;
        if (gainDb != target)
        {
            gainDb += (target - gainDb) * (target > gainDb ? gainAttack : gainRelease);
            if (std::abs (target - gainDb) < 1.0e-4) gainDb = target;
            gain = fromDb (gainDb);
        }
        for (int c = 0; c < nch; ++c)
            ch[c][i] *= gain;
        minGainDb = std::min (minGainDb, toDb (gain));
    }
    if (! gateOn) { gain = 1.0; gainDb = 0.0; }
}

// ================================================================================================
// VocalEQ
void VocalEQ::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    glide = onePole (0.020, sr / kChunk);
    reset();
}

void VocalEQ::reset() noexcept
{
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        curGain[i] = params.gainDb[i];
        curFreq[i] = params.freqHz[i];
        designedGain[i] = 1.0e9;   // force a design
        designedFreq[i] = -1.0;
    }
    for (auto& c : filt) for (auto& f : c) f.reset();
}

static design::Coeffs eqCoeffs (int band, double gainDb, double freqHz, double sr) noexcept
{
    const auto& info = kEqBandInfo[static_cast<size_t> (band)];
    const double f = std::clamp (freqHz, 20.0, 0.45 * sr);
    if (info.type == 0) return design::lowShelf (f, gainDb, sr, info.q);
    if (info.type == 2) return design::highShelf (f, gainDb, sr, info.q);
    return design::bell (f, gainDb, info.q, sr);
}

double VocalEQ::bandResponseDb (int band, double gainDb, double freqHz, double f, double sr)
{
    if (std::abs (gainDb) < 1.0e-6)
        return 0.0;
    const auto c = eqCoeffs (band, gainDb, freqHz, sr);
    const double w = 2.0 * std::numbers::pi * f / sr;
    // |H(e^jw)|^2 for a biquad
    const double cw = std::cos (w), c2w = std::cos (2.0 * w);
    const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2 + 2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * cw + 2.0 * c.b0 * c.b2 * c2w;
    const double den = 1.0 + c.a1 * c.a1 + c.a2 * c.a2 + 2.0 * (c.a1 + c.a1 * c.a2) * cw + 2.0 * c.a2 * c2w;
    return 10.0 * std::log10 (std::max (num, 1.0e-30) / std::max (den, 1.0e-30));
}

double VocalEQ::responseDb (const EqParams& p, double f, double sr)
{
    if (! p.enabled) return 0.0;
    double db = 0.0;
    for (int b = 0; b < kEqBands; ++b)
        db += bandResponseDb (b, p.gainDb[static_cast<size_t> (b)], p.freqHz[static_cast<size_t> (b)], f, sr);
    return db;
}

void VocalEQ::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, channels);
    for (int start = 0; start < n; start += kChunk)
    {
        const int len = std::min (kChunk, n - start);
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto i = static_cast<size_t> (b);
            const double tg = params.enabled ? std::clamp (params.gainDb[i], -kEqMaxDb, kEqMaxDb) : 0.0;
            const double tf = std::clamp (params.freqHz[i], kEqBandInfo[i].lo, kEqBandInfo[i].hi);
            curGain[i] += (tg - curGain[i]) * glide;
            if (std::abs (tg - curGain[i]) < 0.001) curGain[i] = tg;
            curFreq[i] *= std::pow (tf / std::max (curFreq[i], 1.0), glide);
            if (std::abs (tf - curFreq[i]) < 0.01) curFreq[i] = tf;

            if (curGain[i] == 0.0)
            {
                // Exactly flat: skip the band (and start it clean when it comes back).
                if (designedGain[i] != 0.0)
                {
                    for (auto& c : filt) c[i].reset();
                    designedGain[i] = 0.0;
                }
                continue;
            }
            if (curGain[i] != designedGain[i] || curFreq[i] != designedFreq[i])
            {
                const auto c = eqCoeffs (b, curGain[i], curFreq[i], sr);
                for (auto& chan : filt) design::apply (chan[i], c);
                designedGain[i] = curGain[i];
                designedFreq[i] = curFreq[i];
            }
            for (int c = 0; c < nch; ++c)
            {
                auto& f = filt[static_cast<size_t> (c)][i];
                double* x = ch[c] + start;
                for (int k = 0; k < len; ++k) x[k] = f.process (x[k]);
            }
        }
    }
}

// ================================================================================================
// DeEsser
void DeEsser::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    envAtk = onePole (0.0005, sr);
    envRel = onePole (0.030, sr);
    cutAtk = onePole (0.001, sr);
    cutRel = onePole (0.040, sr);
    reset();
}

void DeEsser::reset() noexcept
{
    for (auto& f : detHp) f.reset();
    for (auto& f : shelf) f.reset();
    detHz = -1.0;
    hfEnv = fullEnv = 0.0;
    cut = designedCut = 0.0;
    countdown = 0;
}

void DeEsser::design (double cutDb) noexcept
{
    designedCut = cutDb;
    const auto c = design::highShelf (std::min (params.freqHz, 0.4 * sr), -cutDb, sr, 0.6);
    for (auto& f : shelf) design::apply (f, c);
}

void DeEsser::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, channels);
    if (isNeutral (params))
    {
        if (cut != 0.0 || designedCut != 0.0) reset();
        return;
    }
    // The detector listens a little below the cut frequency, where "s" energy starts.
    const double dHz = std::clamp (params.freqHz * 0.8, 2000.0, 0.4 * sr);
    if (std::abs (dHz - detHz) > 0.5)
    {
        detHz = dHz;
        const auto c = design::butterworth (true, dHz, sr);
        for (auto& f : detHp) design::apply (f, c);
    }
    const double thr = thresholdDb (params.sensitivity);
    const double maxCutDb = kMaxCutDb * std::clamp (params.amount, 0.0, 100.0) / 100.0;
    const double slope = 1.0 + params.amount / 50.0;   // dB of cut per dB over the threshold
    const double floorE = 1.0e-7;                       // -70 dBFS: too quiet to be a sibilant

    for (int i = 0; i < n; ++i)
    {
        double mono = 0.0;
        for (int c = 0; c < nch; ++c) mono += ch[c][i];
        mono /= nch;
        const double hf = detHp[1].process (detHp[0].process (mono));
        const double hfSq = hf * hf, fullSq = mono * mono;
        hfEnv += (hfSq - hfEnv) * (hfSq > hfEnv ? envAtk : envRel);
        fullEnv += (fullSq - fullEnv) * (fullSq > fullEnv ? envAtk : envRel);

        double target = 0.0;
        if (hfEnv > floorE && fullEnv > 0.0)
        {
            const double ratioDb = 10.0 * std::log10 (hfEnv / fullEnv);
            target = std::clamp ((ratioDb - thr) * slope, 0.0, maxCutDb);
        }
        cut += (target - cut) * (target > cut ? cutAtk : cutRel);
        if (cut < 1.0e-4 && target == 0.0) cut = 0.0;
        maxCut = std::max (maxCut, cut);

        // The shelf always runs while the de-esser is on: at 0 dB it is exactly transparent, so
        // nothing ever switches in or out (switching made a tick at the end of every "s").
        if (--countdown <= 0)
        {
            countdown = 8;
            if (std::abs (cut - designedCut) > 0.005 || (cut == 0.0 && designedCut != 0.0))
                design (cut);
        }
        for (int c = 0; c < nch; ++c)
            ch[c][i] = shelf[static_cast<size_t> (c)].process (ch[c][i]);
    }
}

// ================================================================================================
// Rider
void Rider::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    hpCoeff = std::exp (-2.0 * std::numbers::pi * 100.0 / sr);
    msCoeff = onePole (0.150, sr);
    reset();
}

void Rider::reset() noexcept
{
    hpState = hpPrev = 0.0;
    ms = 0.0;
    gain = 1.0;
}

void Rider::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, channels);
    if (isNeutral (params))
    {
        gain = 1.0;
        ms = 0.0;
        return;
    }
    static constexpr double kSpeedSeconds[3] = { 0.8, 0.4, 0.2 };
    const double glide = onePole (kSpeedSeconds[std::clamp (params.speed, 0, 2)], sr);
    const double voiceMs = std::pow (10.0, (std::max (params.targetDb - kVoiceBelowTargetDb, -60.0)) / 10.0);
    const double lo = fromDb (-params.rangeDb), hi = fromDb (params.rangeDb);

    for (int i = 0; i < n; ++i)
    {
        double mono = 0.0;
        for (int c = 0; c < nch; ++c) mono += ch[c][i];
        mono /= nch;
        // One-pole high-pass at 100 Hz so rumble doesn't count as voice.
        hpState = hpCoeff * (hpState + mono - hpPrev);
        hpPrev = mono;
        ms += (hpState * hpState - ms) * msCoeff;

        if (ms > voiceMs)
        {
            // Mean square of a sine is half its peak^2; RMS target in dBFS -> energy.
            const double want = std::clamp (std::sqrt (std::pow (10.0, params.targetDb / 10.0) / ms), lo, hi);
            gain *= std::pow (want / gain, glide);
        }
        for (int c = 0; c < nch; ++c)
            ch[c][i] *= gain;
    }
}

// ================================================================================================
// VocalCompressor
void VocalCompressor::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    design::apply (scHp, design::butterworth (true, 80.0, sr));
    a1 = onePole (0.001, sr);
    r1 = onePole (0.060, sr);
    a2 = onePole (0.010, sr);
    // Detectors average over more than one cycle of the lowest voice (~12 ms at 80 Hz), so the gain
    // follows words, not the waveform (a 5 ms detector rode each cycle: gritty "crackle").
    msC = onePole (0.030, sr);
    pkRel = onePole (0.015, sr);
    susC = onePole (1.0, sr);
    glide = onePole (0.020, sr);
    reset();
}

void VocalCompressor::reset() noexcept
{
    scHp.reset();
    gr1 = gr2 = 0.0;
    ms2 = 0.0;
    pkEnv = 0.0;
    sustained = 0.0;
    makeup = fromDb (params.makeupDb);
    mixGlide = std::clamp (params.mix, 0.0, 100.0) / 100.0;
}

double VocalCompressor::peakGrDb (double levelDb, double thrDb) noexcept
{
    const double over = levelDb - thrDb, k = kPeakKneeDb;
    if (over <= -k / 2.0) return 0.0;
    const double slope = 1.0 / kPeakRatio - 1.0;
    if (over >= k / 2.0) return slope * over;
    const double x = over + k / 2.0;
    return slope * x * x / (2.0 * k);
}

double VocalCompressor::levelGrDb (double levelDb, double thrDb, double ratio) noexcept
{
    if (ratio <= 1.0) return 0.0;
    const double over = levelDb - thrDb, k = kLevelKneeDb;
    if (over <= -k / 2.0) return 0.0;
    const double slope = 1.0 / ratio - 1.0;
    if (over >= k / 2.0) return slope * over;
    const double x = over + k / 2.0;
    return slope * x * x / (2.0 * k);
}

void VocalCompressor::process (double* const* ch, int nch, int n) noexcept
{
    nch = std::min (nch, channels);
    if (isNeutral (params))
    {
        if (gr1 != 0.0 || gr2 != 0.0 || ms2 != 0.0) reset();
        makeup = 1.0;
        return;
    }
    const bool peakOn = params.peakThrDb <= -0.05;
    const bool levelOn = params.ratio >= 1.005;
    const double makeupTarget = fromDb (params.makeupDb);
    const double mixTarget = std::clamp (params.mix, 0.0, 100.0) / 100.0;

    for (int i = 0; i < n; ++i)
    {
        double mono = 0.0;
        for (int c = 0; c < nch; ++c) mono += ch[c][i];
        mono /= nch;
        const double sc = scHp.process (mono);

        // Peak stage: a peak envelope that holds through each cycle (15 ms release), then fast
        // smoothing of the gain reduction (in dB).
        const double asc = std::abs (sc);
        pkEnv = asc > pkEnv ? asc : pkEnv + (asc - pkEnv) * pkRel;
        double t1 = 0.0;
        if (peakOn)
            t1 = peakGrDb (toDb (pkEnv + 1.0e-12), params.peakThrDb);
        gr1 += (t1 - gr1) * (t1 < gr1 ? a1 : r1);

        // Level stage: hears the output of the peak stage (5 ms RMS), program-dependent release.
        double t2 = 0.0;
        if (levelOn)
        {
            const double s2 = sc * fromDb (gr1);
            ms2 += (s2 * s2 - ms2) * msC;   // 30 ms RMS
            // RMS of the sidechain, +3 dB so a sine's RMS reads at its peak level (like the meters).
            t2 = levelGrDb (10.0 * std::log10 (ms2 + 1.0e-24) + 3.0103, params.thrDb, params.ratio);
        }
        sustained += (std::min (gr2, 0.0) - sustained) * susC;
        const double relSeconds = 0.060 + 0.540 * std::clamp (-sustained / 6.0, 0.0, 1.0);
        gr2 += (t2 - gr2) * (t2 < gr2 ? a2 : onePole (relSeconds, sr));

        makeup += (makeupTarget - makeup) * glide;
        mixGlide += (mixTarget - mixGlide) * glide;
        const double wet = fromDb (gr1 + gr2) * makeup;
        const double g = mixGlide * wet + (1.0 - mixGlide);
        for (int c = 0; c < nch; ++c)
            ch[c][i] *= g;
        minPeak = std::min (minPeak, gr1);
        minLevel = std::min (minLevel, gr2);
    }
}

} // namespace vox
