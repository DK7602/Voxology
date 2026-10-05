#include "vox/Space.h"

#include <algorithm>
#include <numbers>

namespace vox {

using design::onePole;

double duckGain (double envMs, double duckPercent) noexcept
{
    if (duckPercent <= 0.0) return 1.0;
    // Fully ducked once the dry vocal is at about -30 dBFS or louder, not at all below -50.
    const double levelDb = 10.0 * std::log10 (envMs + 1.0e-24) + 3.0103;
    const double amount = std::clamp ((levelDb + 50.0) / 20.0, 0.0, 1.0);
    return fromDb (-18.0 * std::clamp (duckPercent, 0.0, 100.0) / 100.0 * amount);
}

// ================================================================================================
// Doubler
void Doubler::prepare (double sampleRate)
{
    sr = sampleRate;
    line.allocate (static_cast<int> (0.05 * sr));
    const auto hp = design::butterworth (true, 150.0, sr), lp = design::butterworth (false, 9000.0, sr);
    design::apply (hpL, hp); design::apply (hpR, hp);
    design::apply (lpL, lp); design::apply (lpR, lp);
    glide = onePole (0.030, sr);
    reset();
}

void Doubler::reset() noexcept
{
    line.clear();
    phase = { 0.0, 0.5 };
    phase2 = { 0.25, 0.8 };
    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    level = 0.0;
}

void Doubler::process (const double* mono, double* const* out, int nch, int n) noexcept
{
    if (isNeutral (params))
    {
        if (level != 0.0) reset();
        return;
    }
    const double target = 0.7 * std::clamp (params.amount, 0.0, 100.0) / 100.0;
    const double w = std::clamp (params.width, 0.0, 100.0) / 100.0;
    const double near = 0.5 + 0.5 * w, far = 0.5 - 0.5 * w;
    constexpr double twoPi = 2.0 * std::numbers::pi;
    const std::array<double, 2> base { 0.012 * sr, 0.0175 * sr }, depth { 0.0015 * sr, 0.0020 * sr }, depth2 { 0.0007 * sr, 0.0006 * sr };
    const std::array<double, 2> rate { 0.37 / sr, 0.29 / sr }, rate2 { 0.13 / sr, 0.17 / sr };

    for (int i = 0; i < n; ++i)
    {
        line.push (mono[i]);
        std::array<double, 2> v {};
        for (size_t k = 0; k < 2; ++k)
        {
            phase[k] += rate[k]; if (phase[k] >= 1.0) phase[k] -= 1.0;
            phase2[k] += rate2[k]; if (phase2[k] >= 1.0) phase2[k] -= 1.0;
            const double d = base[k] + depth[k] * std::sin (twoPi * phase[k]) + depth2[k] * std::sin (twoPi * phase2[k]);
            v[k] = line.read (d);
        }
        const double l = lpL.process (hpL.process (v[0]));
        const double r = lpR.process (hpR.process (v[1]));
        level += (target - level) * glide;
        if (nch >= 2)
        {
            out[0][i] += level * (near * l + far * r);
            out[1][i] += level * (near * r + far * l);
        }
        else
            out[0][i] += level * 0.5 * (l + r);
    }
}

// ================================================================================================
// EchoDelay
void EchoDelay::prepare (double sampleRate)
{
    sr = sampleRate;
    lineL.allocate (static_cast<int> (kMaxSeconds * sr) + 8);
    lineR.allocate (static_cast<int> (kMaxSeconds * sr) + 8);
    design::apply (inHp, design::butterworth (true, 250.0, sr));
    delayGlide = onePole (0.15, sr);
    duckAtk = onePole (0.010, sr);
    duckRel = onePole (0.300, sr);
    glide = onePole (0.030, sr);
    reset();
}

void EchoDelay::reset() noexcept
{
    lineL.clear();
    lineR.clear();
    inHp.reset(); lpL.reset(); lpR.reset();
    curDelay = -1.0;
    designedTone = -1.0;
    duckEnv = 0.0;
    wet = 0.0;
    silent = true;
}

double EchoDelay::delaySeconds (const DelayParams& p) noexcept
{
    const double bpm = std::clamp (p.bpm > 0.0 ? p.bpm : 120.0, 30.0, 300.0);
    const double beats = kDelayBeats[static_cast<size_t> (std::clamp (p.division, 0, kDelayDivisions - 1))];
    return std::min (kMaxSeconds - 0.01, 60.0 / bpm * beats);
}

void EchoDelay::process (const double* mono, double* const* out, int nch, int n) noexcept
{
    if (isNeutral (params))
    {
        if (! silent) reset();
        return;
    }
    silent = false;
    if (std::abs (params.toneHz - designedTone) > 0.5)
    {
        designedTone = params.toneHz;
        const auto c = design::butterworth (false, std::clamp (params.toneHz, 500.0, 0.45 * sr), sr);
        design::apply (lpL, c); design::apply (lpR, c);
    }
    const double target = delaySeconds (params) * sr;
    if (curDelay < 0.0) curDelay = target;
    const double fb = std::clamp (params.feedback, 0.0, 95.0) / 100.0;
    const double wetTarget = std::clamp (params.mix, 0.0, 100.0) / 100.0;

    for (int i = 0; i < n; ++i)
    {
        curDelay += (target - curDelay) * delayGlide;
        const double x = mono[i];
        duckEnv += (x * x - duckEnv) * (x * x > duckEnv ? duckAtk : duckRel);
        const double in = inHp.process (x);

        const double l = lpL.process (lineL.read (curDelay));
        const double r = lpR.process (lineR.read (curDelay));
        if (params.pingPong)
        {
            lineL.push (in + fb * r);
            lineR.push (fb * l);
        }
        else
        {
            lineL.push (in + fb * l);
            lineR.push (0.0);
        }
        wet += (wetTarget * duckGain (duckEnv, params.duck) - wet) * glide;
        const double wl = l, wr = params.pingPong ? r : l;
        if (nch >= 2)
        {
            out[0][i] += wet * wl;
            out[1][i] += wet * wr;
        }
        else
            out[0][i] += wet * 0.5 * (wl + wr);
    }
}

// ================================================================================================
// Reverb
void Reverb::prepare (double sampleRate)
{
    sr = sampleRate;
    pre.allocate (static_cast<int> (0.25 * sr) + 8);
    constexpr std::array<double, 4> diffMs { 4.77, 3.59, 12.73, 9.30 };
    for (size_t k = 0; k < diffusers.size(); ++k)
    {
        diffusers[k].buf.assign (static_cast<size_t> (std::max (1.0, diffMs[k] * 0.001 * sr)), 0.0);
        diffusers[k].pos = 0;
        diffusers[k].g = k < 2 ? 0.65 : 0.55;
    }
    constexpr std::array<double, kLines> lineMs { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
    for (size_t k = 0; k < static_cast<size_t> (kLines); ++k)
    {
        lineLen[k] = std::max (8, static_cast<int> (lineMs[k] * 0.001 * sr));
        lines[k].assign (static_cast<size_t> (lineLen[k]), 0.0);
    }
    design::apply (inHp, design::butterworth (true, 200.0, sr));
    duckAtk = onePole (0.010, sr);
    duckRel = onePole (0.400, sr);
    glide = onePole (0.030, sr);
    reset();
}

void Reverb::reset() noexcept
{
    pre.clear();
    for (auto& d : diffusers) { std::fill (d.buf.begin(), d.buf.end(), 0.0); d.pos = 0; }
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0);
    linePos.fill (0);
    lp.fill (0.0);
    inHp.reset();
    designedDecay = designedTone = -1.0;
    duckEnv = 0.0;
    wet = 0.0;
    silent = true;
}

void Reverb::design() noexcept
{
    designedDecay = params.decayS;
    designedTone = params.toneHz;
    const double t60 = std::clamp (params.decayS, 0.2, 10.0);
    for (size_t k = 0; k < static_cast<size_t> (kLines); ++k)
        fbGain[k] = std::pow (10.0, -3.0 * lineLen[k] / (t60 * sr));
    lpCoeff = 1.0 - std::exp (-2.0 * std::numbers::pi * std::clamp (params.toneHz, 1000.0, 0.45 * sr) / sr);
}

void Reverb::process (const double* mono, double* const* out, int nch, int n) noexcept
{
    if (isNeutral (params))
    {
        if (! silent) reset();
        return;
    }
    silent = false;
    if (params.decayS != designedDecay || params.toneHz != designedTone)
        design();
    const double preSamples = std::clamp (params.predelayMs, 0.0, 200.0) * 0.001 * sr;
    const double wetTarget = std::clamp (params.mix, 0.0, 100.0) / 100.0;
    constexpr double householder = 2.0 / kLines;
    constexpr std::array<double, kLines> inSign { 1, -1, 1, 1, -1, 1, -1, -1 };
    const double outScale = 0.6;

    for (int i = 0; i < n; ++i)
    {
        const double x = mono[i];
        duckEnv += (x * x - duckEnv) * (x * x > duckEnv ? duckAtk : duckRel);
        pre.push (inHp.process (x));
        double d = pre.read (preSamples);
        for (auto& ap : diffusers) d = ap.process (d);

        std::array<double, kLines> o {};
        double sum = 0.0;
        for (size_t k = 0; k < static_cast<size_t> (kLines); ++k)
        {
            const double v = lines[k][static_cast<size_t> (linePos[k])];
            lp[k] += (v - lp[k]) * lpCoeff;
            o[k] = lp[k] * fbGain[k];
            sum += o[k];
        }
        const double mixSum = sum * householder;
        for (size_t k = 0; k < static_cast<size_t> (kLines); ++k)
        {
            lines[k][static_cast<size_t> (linePos[k])] = o[k] - mixSum + 0.35 * inSign[k] * d;
            if (++linePos[k] >= lineLen[k]) linePos[k] = 0;
        }
        const double l = outScale * (o[0] - o[2] + o[4] - o[6] + 0.5 * (o[1] - o[5]));
        const double r = outScale * (o[1] - o[3] + o[5] - o[7] + 0.5 * (o[2] - o[6]));
        wet += (wetTarget * duckGain (duckEnv, params.duck) - wet) * glide;
        if (nch >= 2)
        {
            out[0][i] += wet * l;
            out[1][i] += wet * r;
        }
        else
            out[0][i] += wet * 0.5 * (l + r);
    }
}

} // namespace vox
