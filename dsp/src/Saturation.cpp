#include "vox/Saturation.h"

#include <numbers>

namespace vox {

double Saturation::shape (double x, SaturationMode mode, double s) noexcept
{
    const double u = x / s;
    switch (mode)
    {
        case SaturationMode::tape:
            return s * std::tanh (u);
        case SaturationMode::tube:
        {
            const double tb = std::tanh (kTubeBias);
            return s * (std::tanh (u + kTubeBias) - tb) / (1.0 - tb * tb);
        }
        case SaturationMode::clip:
        {
            const double u2 = u * u;
            return x / std::sqrt (std::sqrt (1.0 + u2 * u2));
        }
    }
    return x;
}

double Saturation::estimateHarmonicsDb (const std::vector<std::vector<float>>& channels, const SaturationParams& p)
{
    const double m = effectiveMix (p);
    if (m <= 0.0)
        return -100.0;
    const double s = std::pow (10.0, (kHeadroomDb - std::clamp (p.driveDb, 0.0, kMaxDriveDb)) / 20.0);
    double res = 0.0, sig = 0.0;
    for (const auto& ch : channels)
    {
        double sum = 0.0, sumSq = 0.0;
        for (float v : ch)
        {
            const double x = v, r = shape (x, p.mode, s) - x;
            sum += r;
            sumSq += r * r;
            sig += x * x;
        }
        const double n = std::max<double> (1.0, static_cast<double> (ch.size()));
        res += sumSq - sum * sum / n;   // without the DC the tube curve makes (removed in the plug-in)
    }
    const double ratio = sig > 0.0 ? m * m * res / sig : 0.0;
    return ratio > 1.0e-10 ? 10.0 * std::log10 (ratio) : -100.0;
}

void Saturation::resetAir (Channel& ch) noexcept
{
    design::apply (ch.airTop, airTopC);
    design::apply (ch.airKeepA, airKeepC);
    design::apply (ch.airKeepB, airKeepC);
    ch.airTop.reset();
    ch.airKeepA.reset();
    ch.airKeepB.reset();
    ch.envTop = ch.envAll = 0.0;
}

double Saturation::airHarmonics (Channel& ch, double v) noexcept
{
    const double top = ch.airTop.process (v);
    const double at = std::abs (top), aa = std::abs (v);
    ch.envTop += (at - ch.envTop) * (at > ch.envTop ? airAtk : airRel);
    ch.envAll += (aa - ch.envAll) * (aa > ch.envAll ? airAtk : airRel);
    const double x = std::clamp (top / (ch.envTop + 1.0e-9), -1.0, 1.0);
    // Even harmonics only (x squared: the 2nd; |x|: the 2nd, 4th, 6th, gently falling): they add to the top without a
    // copy of it (no odd term with its own "fundamental" to cancel) and land where the filters down keep them out of
    // the audible band when they pass it.
    const double gen = x * x + 0.5 * std::abs (x);
    const double w = std::clamp (1.3 - 1.5 * ch.envTop / (ch.envAll + 1.0e-9), 0.25, 1.0);   // less on an "s"
    return kAirMax * ch.airKeepB.process (ch.airKeepA.process (gen * ch.envTop * w));
}

void Saturation::prepare (double sampleRate, int numChannels)
{
    channelCount = std::clamp (numChannels, 1, kMaxChannels);
    channelState.assign (static_cast<size_t> (channelCount), Channel {});
    for (auto& ch : channelState)
        ch.dry.assign (kLatency + 1, 0.0);
    glideCoeff = 1.0 - std::exp (-1.0 / (0.030 * sampleRate));
    mixCoeff = 1.0 - std::exp (-4.0 / (0.020 * sampleRate));   // settles within ~20 ms
    hpCoeff = std::exp (-2.0 * std::numbers::pi * 1.0 / sampleRate);
    // Air runs at 4x the rate.
    const double sr4 = 4.0 * sampleRate;
    airTopC = design::butterworth (true, kAirFromHz, sr4);
    airKeepC = design::butterworth (true, kAirKeepHz, sr4);
    airAtk = 1.0 - std::exp (-1.0 / (0.0005 * sr4));
    airRel = 1.0 - std::exp (-1.0 / (0.030 * sr4));
    reset();
}

void Saturation::reset() noexcept
{
    for (auto& ch : channelState)
    {
        for (auto& s : ch.stages) s.reset();
        std::fill (ch.dry.begin(), ch.dry.end(), 0.0);
        ch.dryPos = 0;
        ch.hpX = ch.hpY = 0.0;
        resetAir (ch);
    }
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);
    bend = bendTarget;
    // Start fully in the requested state (offline renders and a fresh prepare): no fade-in.
    running = ! isNeutral (params);
    mix = effectiveMix (params);
    airGain = params.enabled ? std::clamp (params.air, 0.0, 100.0) / 100.0 : 0.0;
    warmup = 0;
    resEnergy = sigEnergy = 0.0;
}

void Saturation::processFrame (double* frame, int numChannels) noexcept
{
    const int nch = std::min (numChannels, channelCount);
    const double mixTarget = effectiveMix (params);
    const double airTarget = params.enabled ? std::clamp (params.air, 0.0, 100.0) / 100.0 : 0.0;
    bendTarget = std::pow (10.0, (kHeadroomDb - std::clamp (params.driveDb, 0.0, kMaxDriveDb)) / 20.0);

    const bool idle = mix < 1.0e-6 && mixTarget == 0.0 && airGain < 1.0e-6 && airTarget == 0.0 && warmup == 0;
    if (idle)
    {
        if (mix != 0.0 || airGain != 0.0 || running)
        {
            mix = airGain = 0.0;
            running = false;
            for (auto& ch : channelState)
            {
                for (auto& st : ch.stages) st.reset();   // start clean next time
                ch.hpX = ch.hpY = 0.0;
                resetAir (ch);
            }
        }
    }
    else
    {
        if (! running)
        {
            running = true;
            warmup = 2 * kLatency;   // let the filters fill before any of the wet path is heard
            bend = bendTarget;
        }
        if (warmup > 0)
            --warmup;
        else
        {
            mix += (mixTarget - mix) * mixCoeff;
            airGain += (airTarget - airGain) * mixCoeff;
        }
        bend += (bendTarget - bend) * glideCoeff;
    }

    for (int c = 0; c < nch; ++c)
    {
        auto& ch = channelState[static_cast<size_t> (c)];
        const double x = frame[c];

        // Dry path: a plain delay, so pass-through is exact.
        const int size = static_cast<int> (ch.dry.size());
        ch.dry[static_cast<size_t> (ch.dryPos)] = x;
        const double dry = ch.dry[static_cast<size_t> ((ch.dryPos + 1) % size)];   // kLatency samples ago
        ch.dryPos = (ch.dryPos + 1) % size;

        if (idle)
        {
            frame[c] = dry;
            continue;
        }

        // Up 4x, keep only what the curve adds (the clean signal never goes through the filters),
        // down 4x, remove DC.
        double a, b, u[4];
        ch.stages[0].upsample (x, a, b);
        ch.stages[1].upsample (a, u[0], u[1]);
        ch.stages[1].upsample (b, u[2], u[3]);
        // What the curve adds (at this Mix) and Air's harmonics, together through the same filters down.
        for (double& v : u) v = mix * (curve (v) - v) + (airGain > 0.0 || airTarget > 0.0 ? airGain * airHarmonics (ch, v) : 0.0);
        const double ra = ch.stages[1].downsample (u[0], u[1]);
        const double rb = ch.stages[1].downsample (u[2], u[3]);
        const double res = ch.stages[0].downsample (ra, rb);
        // Only the tube curve is asymmetric and makes DC; a 1 Hz high-pass removes it without
        // shifting the bass part of the residual against the dry signal.
        if (params.mode == SaturationMode::tube)
        {
            ch.hpY = hpCoeff * (ch.hpY + res - ch.hpX);
            ch.hpX = res;
        }
        else
        {
            ch.hpY = res;
            ch.hpX = 0.0;
        }

        resEnergy += ch.hpY * ch.hpY;   // what is actually added (at this Mix, with Air)
        sigEnergy += dry * dry;
        frame[c] = dry + ch.hpY;
    }
}

} // namespace vox
