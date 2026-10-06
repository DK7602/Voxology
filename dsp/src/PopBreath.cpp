#include "vox/PopBreath.h"

#include <algorithm>

namespace vox {

using design::onePole;

namespace {
constexpr double kVoiceFloorMs = 1.0e-8;   // -80 dBFS: quieter than this is silence
constexpr double kPriorRatioDb = 0.0;      // cautious start (a deep voice's low band vs the rest), until it has heard you
constexpr double kNormalPercentile = 0.6, kLearnDbPerS = 8.0, kLearnFastDbPerS = 40.0;
}

// ================================================================================================
// PopRemover
void PopRemover::prepare (double sampleRate, int numChannels, int lookAheadAvailable)
{
    sr = sampleRate;
    const int la = static_cast<int> (std::lround (kLookAheadSeconds * sr));
    delayLine.assign (static_cast<size_t> (std::max (0, lookAheadAvailable - la) + 1), 0.0);
    channels = std::clamp (numChannels, 1, kMaxChannels);
    for (auto& f : lowLp) design::apply (f, design::butterworth (false, kSplitHz, sr));
    for (auto& f : restHp) design::apply (f, design::butterworth (true, kSplitHz, sr));
    envAtk = onePole (0.0003, sr);
    envRel = onePole (0.030, sr);
    voiceAvg = onePole (0.005, sr);
    voiceFall = onePole (2.0, sr);
    cutAtk = onePole (0.0005, sr);
    cutRel = onePole (0.050, sr);
    reset();
}

void PopRemover::reset() noexcept
{
    for (auto& f : lowLp) f.reset();
    for (auto& f : restHp) f.reset();
    for (auto& c : shelf) for (auto& f : c) f = Biquad {};   // identity: exactly transparent
    lowEnv = restEnv = 0.0;
    voiceMs = voiceMs2 = voicePeak = 0.0;
    normal = kPriorRatioDb;
    learnedSeconds = 0.0;
    cut = designedCut = 0.0;
    countdown = 0;
    running = false;
    std::fill (delayLine.begin(), delayLine.end(), 0.0);
    delayPos = 0;
}

void PopRemover::design() noexcept
{
    designedCut = cut;
    // Two shelves in series: twice as steep, so the thump goes and the voice above 200 Hz stays.
    const auto c = design::lowShelf (kShelfHz, -0.5 * cut, sr, 0.707);
    for (auto& chan : shelf)
        for (auto& f : chan) design::apply (f, c);
}

void PopRemover::process (double* const* ch, int nch, int n, const double* sc) noexcept
{
    nch = std::min (nch, channels);
    if (isNeutral (params) && cut == 0.0)
    {
        if (running) reset();
        return;
    }
    running = true;
    const double maxCutDb = kMaxCutDb * std::clamp (params.amount, 0.0, 100.0) / 100.0;
    const double loud = fromDb (-kLoudWindowDb);   // amplitude ratio
    const double halfKnee = 0.5 * kKneeDb;

    for (int i = 0; i < n; ++i)
    {
        double mono = 0.0;
        if (sc != nullptr) mono = sc[i];
        else
        {
            for (int c = 0; c < nch; ++c) mono += ch[c][i];
            mono /= nch;
        }

        const double low = lowLp[1].process (lowLp[0].process (mono));
        const double rest = restHp[1].process (restHp[0].process (mono));
        const double al = std::abs (low), ar = std::abs (rest);
        lowEnv += (al - lowEnv) * (al > lowEnv ? envAtk : envRel);
        restEnv += (ar - restEnv) * (ar > restEnv ? envAtk : envRel);

        // Recent voice level (energy): rises at once, falls over seconds.
        voiceMs += (mono * mono - voiceMs) * voiceAvg;
        voiceMs2 += (voiceMs - voiceMs2) * voiceAvg;
        voicePeak = voiceMs2 > voicePeak ? voiceMs2 : voicePeak + (voiceMs2 - voicePeak) * voiceFall;

        double target = 0.0;
        if (voiceMs2 > kVoiceFloorMs && lowEnv > 1.0e-9)
        {
            const double ratioDb = 20.0 * std::log10 ((lowEnv + 1.0e-12) / (restEnv + 1.0e-12));
            const double over = ratioDb - normal - kThresholdDb;
            double k = 0.0;
            if (over >= halfKnee) k = over;
            else if (over > -halfKnee) k = (over + halfKnee) * (over + halfKnee) / (2.0 * kKneeDb);
            // A pop is loud: its low end reaches near the voice's own level (a soft low hum isn't a pop).
            const bool isLoud = lowEnv * lowEnv > 2.0 * voicePeak * loud * loud;
            if (isLoud) target = std::clamp (kSlope * k, 0.0, maxCutDb);

            // Learn how the low band normally sits (a running 60th percentile), but not from the pops.
            if (target < 1.0)
            {
                const double rate = (learnedSeconds < 1.5 ? kLearnFastDbPerS : kLearnDbPerS) / sr;
                normal += ratioDb > normal ? rate * kNormalPercentile : -rate * (1.0 - kNormalPercentile);
                learnedSeconds += 1.0 / sr;
            }
        }
        if (sc != nullptr)
        {
            // Hold the decision back so it lands kLookAheadSeconds before the thump in the audio.
            delayLine[static_cast<size_t> (delayPos)] = target;
            if (++delayPos >= static_cast<int> (delayLine.size())) delayPos = 0;
            target = delayLine[static_cast<size_t> (delayPos)];
        }
        cut += (target - cut) * (target > cut ? cutAtk : cutRel);
        if (cut < 1.0e-4 && target == 0.0) cut = 0.0;
        maxCut = std::max (maxCut, cut);

        if (--countdown <= 0)
        {
            countdown = 4;
            if (std::abs (cut - designedCut) > 0.01 || (cut == 0.0 && designedCut != 0.0))
                design();
        }
        // The shelves always run while it's on: at 0 dB they're exactly transparent (no switching ticks).
        for (int c = 0; c < nch; ++c)
        {
            auto& s = shelf[static_cast<size_t> (c)];
            ch[c][i] = s[1].process (s[0].process (ch[c][i]));
        }
    }
}

// ================================================================================================
// BreathControl
void BreathControl::prepare (double sampleRate, int numChannels, int lookAheadAvailable)
{
    sr = sampleRate;
    const int la = static_cast<int> (std::lround (kLookAheadSeconds * sr));
    delayLine.assign (static_cast<size_t> (std::max (0, lookAheadAvailable - la) + 1), 0);
    channels = std::clamp (numChannels, 1, kMaxChannels);
    design::apply (body[0], design::butterworth (true, 100.0, sr));
    design::apply (body[1], design::butterworth (false, 800.0, sr));
    design::apply (air[0], design::butterworth (true, 1500.0, sr));
    design::apply (air[1], design::butterworth (false, std::min (6000.0, 0.4 * sr), sr));
    design::apply (top, design::butterworth (true, std::min (6000.0, 0.4 * sr), sr));
    design::apply (refHp, design::butterworth (true, 80.0, sr));
    avg = onePole (0.005, sr);   // two stages: about 10 ms
    voiceFall = onePole (2.0, sr);
    downCoeff = onePole (0.020, sr);
    upCoeff = onePole (0.040, sr);
    fastUpCoeff = onePole (0.004, sr);
    reset();
}

void BreathControl::reset() noexcept
{
    for (auto& f : body) f.reset();
    for (auto& f : air) f.reset();
    top.reset();
    refHp.reset();
    bodyMs = airMs = topMs = fullMs = 0.0;
    bodyMs2 = airMs2 = topMs2 = fullMs2 = 0.0;
    voicePeak = 0.0;
    likeCount = unlikeCount = 0;
    breath = false;
    gainDb = 0.0;
    gain = 1.0;
    running = false;
    std::fill (delayLine.begin(), delayLine.end(), static_cast<signed char> (0));
    delayPos = 0;
}

void BreathControl::process (double* const* ch, int nch, int n, const double* sc) noexcept
{
    nch = std::min (nch, channels);
    if (isNeutral (params) && gainDb == 0.0)
    {
        if (running) reset();
        return;
    }
    running = true;
    const double reduction = std::clamp (params.reductionDb, 0.0, kMaxReductionDb);
    const double below = fromDb (-2.0 * kBelowVoiceDb), floorBelow = fromDb (-2.0 * kFloorBelowVoiceDb);   // energy ratios
    const double airy = fromDb (2.0 * kAiryDb);
    const int minSamples = static_cast<int> (kMinSeconds * sr), holdSamples = static_cast<int> (kHoldSeconds * sr);

    for (int i = 0; i < n; ++i)
    {
        double mono = 0.0;
        if (sc != nullptr) mono = sc[i];
        else
        {
            for (int c = 0; c < nch; ++c) mono += ch[c][i];
            mono /= nch;
        }

        const double b = body[1].process (body[0].process (mono));
        const double a = air[1].process (air[0].process (mono));
        const double t = top.process (mono);
        const double f = refHp.process (mono);
        bodyMs += (b * b - bodyMs) * avg;  bodyMs2 += (bodyMs - bodyMs2) * avg;
        airMs += (a * a - airMs) * avg;    airMs2 += (airMs - airMs2) * avg;
        topMs += (t * t - topMs) * avg;    topMs2 += (topMs - topMs2) * avg;
        fullMs += (f * f - fullMs) * avg;  fullMs2 += (fullMs - fullMs2) * avg;
        voicePeak = fullMs2 > voicePeak ? fullMs2 : voicePeak + (fullMs2 - voicePeak) * voiceFall;

        // A breath: quieter than the voice (but not silence), airy rather than voiced, not an "s".
        // A word: as loud as the voice, or voiced (body beats air), or an "s": let go at once.
        const bool audible = fullMs2 > kVoiceFloorMs && fullMs2 > voicePeak * floorBelow;
        const bool word = audible && (fullMs2 >= voicePeak * below || bodyMs2 >= airMs2 || topMs2 >= 1.5 * airMs2);
        const bool like = audible && ! word && airMs2 > bodyMs2 * airy;
        signed char decision = 0;
        if (like)      { ++likeCount; unlikeCount = 0; if (likeCount >= minSamples) breath = true; }
        else if (word) { likeCount = 0; unlikeCount = 0; breath = false; decision = -1; }
        else           { ++unlikeCount; likeCount = 0; if (unlikeCount >= holdSamples) breath = false; }
        if (breath) decision = 1;

        if (sc != nullptr)
        {
            // Hold the decision back so the release lands kLookAheadSeconds before the word.
            delayLine[static_cast<size_t> (delayPos)] = decision;
            if (++delayPos >= static_cast<int> (delayLine.size())) delayPos = 0;
            decision = delayLine[static_cast<size_t> (delayPos)];
        }
        const double target = decision == 1 ? -reduction : 0.0;
        if (gainDb != target)
        {
            gainDb += (target - gainDb) * (target < gainDb ? downCoeff : decision == -1 ? fastUpCoeff : upCoeff);
            if (std::abs (target - gainDb) < 1.0e-4) gainDb = target;
            gain = fromDb (gainDb);
        }
        if (gainDb != 0.0)
            for (int c = 0; c < nch; ++c) ch[c][i] *= gain;
        minGain = std::min (minGain, gainDb);
    }
}

} // namespace vox
