#include "vox/Listen.h"

namespace vox {

void ListenMix::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    fadeStep = 1.0 / (kFadeSeconds * sr);
    loudCoeff = 1.0 - std::exp (-1.0 / (kLoudnessSeconds * sr));
    peakRelease = std::exp (-1.0 / (kPeakReleaseSeconds * sr));
    levelDown = 1.0 - std::exp (-1.0 / (0.02 * sr));
    levelUp = 1.0 - std::exp (-1.0 / (0.3 * sr));
    measureSamples = static_cast<int> (std::lround (kMeasureSeconds * sr));
    for (auto& k : kwVocal) k.prepare (sr);
    for (auto& k : kwSignal) k.prepare (sr);
    reset();
}

void ListenMix::reset() noexcept
{
    wanted = computing = listen::off;
    wantedArm = armLeft = 0;
    justStarted = false;
    vocalGain = 1.0;
    signalGain = 0.0;
    level = levelTarget = 1.0;
    for (auto& k : kwVocal) k.reset();
    for (auto& k : kwSignal) k.reset();
    vocalMs = signalMs = peak = 0.0;
    tracked = 0;
    countdown = 0;
}

int ListenMix::beginBlock() noexcept
{
    justStarted = false;
    if (computing == listen::off && wanted != listen::off)
    {
        // Start the new signal: its delay lines fill and its level is measured before it is heard.
        computing = wanted;
        armLeft = wantedArm + measureSamples;
        justStarted = true;
        signalGain = 0.0;
        level = levelTarget = 1.0;
        for (auto& k : kwSignal) k.reset();
        vocalMs = signalMs = peak = 0.0;
        tracked = 0;
        countdown = 0;
    }
    return computing;
}

void ListenMix::trackVocal (const double* v, int nch) noexcept
{
    double e = 0.0;
    for (int c = 0; c < nch; ++c) { const double a = kwVocal[static_cast<size_t> (c)].process (v[c]); e += a * a; }
    // Averaged only while the signal is complete, so the two averages cover the same audio.
    if (computing != listen::off && armLeft <= measureSamples)
        vocalMs += (e - vocalMs) * std::max (1.0 / static_cast<double> (tracked + 1), loudCoeff);
}

void ListenMix::trackSignal (const double* s, int nch) noexcept
{
    double e = 0.0;
    for (int c = 0; c < nch; ++c) { const double a = kwSignal[static_cast<size_t> (c)].process (s[c]); e += a * a; }
    if (armLeft > measureSamples)
        return;   // still filling its delay lines: not the real signal yet
    // A plain average at first (no bias toward silence), then a ~1 s moving one.
    ++tracked;
    signalMs += (e - signalMs) * std::max (1.0 / static_cast<double> (tracked), loudCoeff);
    if (signalMs < 1.0e-30) signalMs = 0.0;
    peak = std::max (std::max (std::abs (s[0]), std::abs (s[nch - 1])), peak * peakRelease);
}

double ListenMix::targetLevel() const noexcept
{
    if (! autoLevel)
        return 1.0;
    if (signalMs <= 1.0e-14 || vocalMs <= 1.0e-12)
        return levelTarget;   // nothing to measure (silence): keep the gain it has
    double db = std::clamp (10.0 * std::log10 (vocalMs / signalMs) - kTargetUnderVocalDb, 0.0, kMaxBoostDb);
    if (peak > 1.0e-9)
        db = std::min (db, std::max (0.0, 20.0 * std::log10 (0.95 / peak)));   // its peaks stay under 0 dBFS
    return std::pow (10.0, db / 20.0);
}

void ListenMix::step() noexcept
{
    if (computing != listen::off && computing == wanted)
    {
        if (armLeft > 0)
        {
            // Filling and measuring: the vocal keeps playing.
            if (--armLeft == 0)
            {
                levelTarget = level = targetLevel();   // fade in at the measured level
                countdown = 0;
            }
            return;
        }
        signalGain = std::min (1.0, signalGain + fadeStep);
        vocalGain = std::max (0.0, vocalGain - fadeStep);
    }
    else if (computing != listen::off)
    {
        // Another mode (or the vocal) is wanted: fade this one out first.
        signalGain = std::max (0.0, signalGain - fadeStep);
        if (wanted == listen::off)
            vocalGain = std::min (1.0, vocalGain + fadeStep);
        if (signalGain <= 0.0 && (wanted != listen::off || vocalGain >= 1.0))
            computing = listen::off;   // the next beginBlock() starts the wanted one
    }
    else if (wanted == listen::off)
        vocalGain = std::min (1.0, vocalGain + fadeStep);
    if (computing != listen::off && armLeft == 0)
    {
        if (--countdown < 0)
        {
            levelTarget = targetLevel();
            countdown = 31;
        }
        level += (levelTarget - level) * (levelTarget < level ? levelDown : levelUp);
    }
}

} // namespace vox
