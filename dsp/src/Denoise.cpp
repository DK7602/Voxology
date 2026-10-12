#include "vox/Denoise.h"
#include "vox/Fft.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vox {

namespace {

constexpr double kSmoothing = 0.85;         // the level the noise tracker watches: ~30 ms smoothing per bin
constexpr int kSubWindows = 8;              // minimum statistics: the 1.5 s window in 8 parts (Martin 2001)
constexpr double kMinimumBias = 2.9;        // the minimum of the smoothed level sits this far under the noise's mean
                                            // (measured on white and pink noise, see test_denoise.cpp)
constexpr double kDecisionDirected = 0.98;  // Ephraim & Malah: how much of the last frame's clean estimate to trust
constexpr double kMinPrioriSnr = 0.003;     // -25 dB: keeps the gain from collapsing on a quiet frame
constexpr double kQuietRatio = 15.85;       // 12 dB: a frame this close to the quietest of the last ~5 s counts as quiet
constexpr double kLevelWindowSeconds = 5.0;
constexpr double kTailPeakSeconds = 0.15;
constexpr double kRoomOverSubtract = 3.0;   // in a fading tail the decay model alone under-counts the echo
constexpr double kTailRatioOff = 0.7, kTailRatioFull = 0.4;   // Room acts where the level fell 1.5 dB (none) .. 4 dB (all) in 50 ms

/** E1(x), the exponential integral, x > 0 (series below 1, continued fraction above: Numerical Recipes). */
double expint1 (double x) noexcept
{
    if (x <= 0.0) return 50.0;
    if (x < 1.0)
    {
        double sum = 0.0, term = 1.0;
        for (int k = 1; k < 30; ++k)
        {
            term *= -x / k;
            sum += term / k;
            if (std::abs (term) < 1.0e-12) break;
        }
        return -0.5772156649015329 - std::log (x) - sum;
    }
    double b = x + 1.0, c = 1.0e300, d = 1.0 / b, h = d;
    for (int i = 1; i < 100; ++i)
    {
        const double a = -static_cast<double> (i) * i;
        b += 2.0;
        d = 1.0 / (a * d + b);
        c = b + a / c;
        const double del = c * d;
        h *= del;
        if (std::abs (del - 1.0) < 1.0e-10) break;
    }
    return h * std::exp (-x);
}

} // namespace

int Denoiser::frameSize (double sampleRate) noexcept
{
    int size = 256;
    while (size < 0.02 * sampleRate) size *= 2;   // 1024 at 44.1 / 48 kHz, 2048 at 88.2 / 96, 4096 at 176.4 / 192
    return size;
}

void Denoiser::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    n = frameSize (sampleRate);
    hop = n / 4;
    bins = n / 2 + 1;
    window.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)   // square-root periodic Hann: analysis x synthesis = Hann, which sums to 2 at 75 % overlap
        window[static_cast<size_t> (i)] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n));
    for (auto& b : inBuf) b.assign (static_cast<size_t> (n), 0.0);
    for (auto& b : outBuf) b.assign (static_cast<size_t> (n), 0.0);
    for (auto& b : delay) b.assign (static_cast<size_t> (n), 0.0);
    for (auto& s : spec) s.assign (static_cast<size_t> (n), {});
    for (auto* v : { &power, &smooth, &noise, &curMin, &prevGain, &prevPost, &gain, &gainSm })
        v->assign (static_cast<size_t> (bins), 0.0);
    const double framesPerSecond = sr / hop;
    subFrames = std::max (1, static_cast<int> (std::ceil (kNoiseWindowSeconds * framesPerSecond / kSubWindows)));
    subMin.assign (kSubWindows, std::vector<double> (static_cast<size_t> (bins), 0.0));
    lvlSubFrames = std::max (1, static_cast<int> (std::ceil (kLevelWindowSeconds * framesPerSecond / kSubWindows)));
    loBin = static_cast<size_t> (std::ceil (100.0 * n / sr));
    hiBin = static_cast<size_t> (std::min (8000.0, 0.45 * sr) * n / sr);
    roomFrames = std::max (1, static_cast<int> (std::lround (kRoomDelaySeconds * framesPerSecond)));
    history.assign (static_cast<size_t> (roomFrames + 1), std::vector<double> (static_cast<size_t> (bins), 0.0));
    levelHistory.assign (static_cast<size_t> (std::max (2, static_cast<int> (std::lround (kTailPeakSeconds * framesPerSecond)))), 0.0);
    reset();
}

void Denoiser::reset() noexcept
{
    for (auto& b : inBuf) std::fill (b.begin(), b.end(), 0.0);
    for (auto& b : outBuf) std::fill (b.begin(), b.end(), 0.0);
    for (auto& b : delay) std::fill (b.begin(), b.end(), 0.0);
    for (auto* v : { &power, &smooth, &noise, &prevPost, &gainSm })
        std::fill (v->begin(), v->end(), 0.0);
    std::fill (prevGain.begin(), prevGain.end(), 1.0);
    std::fill (gain.begin(), gain.end(), 1.0);
    for (auto& m : subMin) std::fill (m.begin(), m.end(), std::numeric_limits<double>::max());
    std::fill (curMin.begin(), curMin.end(), std::numeric_limits<double>::max());
    std::fill (lvlSubMin.begin(), lvlSubMin.end(), std::numeric_limits<double>::max());
    lvlCurMin = std::numeric_limits<double>::max();
    lvlIndex = lvlCount = lvlFilled = 0;
    for (auto& h : history) std::fill (h.begin(), h.end(), 0.0);
    std::fill (levelHistory.begin(), levelHistory.end(), 0.0);
    count = delayPos = subIndex = subCount = histPos = levelPos = 0;
    warm = false;
    sumIn = sumOut = 0.0;
}

double Denoiser::takeReductionDb() noexcept
{
    const double db = sumIn > 1.0e-20 ? 10.0 * std::log10 (std::max (sumOut, 1.0e-30) / sumIn) : 0.0;
    sumIn = sumOut = 0.0;
    return std::min (0.0, db);
}

void Denoiser::process (double* const* ch, int nch, int len) noexcept
{
    nch = std::min (nch, channels);
    const bool neutral = isNeutral (params);
    for (int i = 0; i < len; ++i)
    {
        for (int c = 0; c < nch; ++c)
        {
            const auto cs = static_cast<size_t> (c);
            const double x = ch[c][i];
            inBuf[cs][static_cast<size_t> (n - hop + count)] = x;
            const double delayed = delay[cs][static_cast<size_t> (delayPos)];
            delay[cs][static_cast<size_t> (delayPos)] = x;
            // Neutral: the input exactly, one frame late (the same latency as the processed path).
            ch[c][i] = neutral ? delayed : outBuf[cs][static_cast<size_t> (count)];
        }
        if (++delayPos >= n) delayPos = 0;
        if (++count >= hop)
        {
            count = 0;
            processFrame (nch);
        }
    }
}

void Denoiser::processFrame (int nch) noexcept
{
    const auto un = static_cast<size_t> (n), uh = static_cast<size_t> (hop), ub = static_cast<size_t> (bins);

    // Spectra of the last frame, and their (linked) power.
    std::fill (power.begin(), power.end(), 0.0);
    for (int c = 0; c < nch; ++c)
    {
        auto& s = spec[static_cast<size_t> (c)];
        const auto& in = inBuf[static_cast<size_t> (c)];
        for (size_t i = 0; i < un; ++i) s[i] = { in[i] * window[i], 0.0 };
        fft (s);
        for (size_t k = 0; k < ub; ++k) power[k] += std::norm (s[k]) / nch;
    }

    for (size_t k = 0; k < ub; ++k)
        smooth[k] = warm ? kSmoothing * smooth[k] + (1.0 - kSmoothing) * power[k] : power[k];
    warm = true;

    // Quiet moments: the frame's level (100 Hz - 8 kHz) within kQuietDb of the quietest of the last ~5 s. Only those
    // teach it the noise, so a long held note (or a voice that never pauses) is never taken for noise.
    double level = 0.0;
    for (size_t k = loBin; k <= hiBin && k < ub; ++k) level += smooth[k];
    lvlCurMin = std::min (lvlCurMin, level);
    if (++lvlCount >= lvlSubFrames)
    {
        lvlSubMin[static_cast<size_t> (lvlIndex)] = lvlCurMin;
        lvlIndex = (lvlIndex + 1) % kSubWindows;
        lvlCount = 0;
        lvlCurMin = level;
        ++lvlFilled;
    }
    double quietest = lvlCurMin;
    for (double m : lvlSubMin) quietest = std::min (quietest, m);
    const bool quiet = lvlFilled >= 2 && level <= kQuietRatio * quietest;   // (after ~1 s of listening)

    // Noise: over the quiet moments, the minimum of the smoothed level of the last ~1.5 s of them (sub-window
    // minima), bias corrected. Nothing (no reduction) until the first quiet moment.
    if (quiet)
    {
        for (size_t k = 0; k < ub; ++k) curMin[k] = std::min (curMin[k], smooth[k]);
        if (++subCount >= subFrames)
        {
            subMin[static_cast<size_t> (subIndex)] = curMin;
            subIndex = (subIndex + 1) % kSubWindows;
            subCount = 0;
            curMin = smooth;
        }
        for (size_t k = 0; k < ub; ++k)
        {
            double m = curMin[k];
            for (const auto& sm : subMin) m = std::min (m, sm[k]);
            noise[k] = kMinimumBias * m;
        }
    }

    // Room: the late echo now = the level kRoomDelaySeconds ago, decayed at a T60 of kRoomT60Seconds.
    const auto& past = history[static_cast<size_t> ((histPos + 1) % static_cast<int> (history.size()))];
    // Only while the sound is dying away like an echo (its overall level 1.5 - 4 dB or more under its peak of the
    // last kTailPeakSeconds): a held or rising note hides its own echo, so it's left alone. (Overall, not per bin:
    // a room's echo flickers bin by bin.)
    levelHistory[static_cast<size_t> (levelPos)] = level;
    levelPos = (levelPos + 1) % static_cast<int> (levelHistory.size());
    const double peak = *std::max_element (levelHistory.begin(), levelHistory.end());
    const double fall = peak > 1.0e-30 ? level / peak : 1.0;
    const double tailWeight = std::clamp ((kTailRatioOff - fall) / (kTailRatioOff - kTailRatioFull), 0.0, 1.0);
    history[static_cast<size_t> (histPos)] = smooth;
    histPos = (histPos + 1) % static_cast<int> (history.size());
    const double roomDecay = std::exp (-2.0 * (3.0 * std::log (10.0) / kRoomT60Seconds) * (roomFrames * static_cast<double> (hop) / sr));
    const bool roomOn = params.roomPct >= 0.05;
    // Room % is also how much echo it assumes is there (a dry, close-miked take has little): at 100 % the whole
    // decay; at 50 % a quarter of it. A dry vocal at 50 % keeps its held notes (~0.5 dB).
    const double roomAmount = std::pow (std::clamp (params.roomPct, 0.0, 100.0) / 100.0, 2.0);
    const double floorNoise = fromDb (-std::clamp (params.noiseDb, 0.0, kMaxNoiseDb));
    const double floorRoom = fromDb (-std::clamp (params.roomPct, 0.0, 100.0) / 100.0 * kMaxRoomDb);
    // Deep settings count the noise a little higher (over-subtraction), or the estimator alone stops near 16 dB.
    const double overSubtract = 1.0 + std::max (0.0, params.noiseDb - 6.0) / 12.0;
    // ... and the a priori SNR may go lower too (at -25 dB the gain itself can't go much past -22 dB).
    const double depthDb = std::max (params.noiseDb, params.roomPct / 100.0 * kMaxRoomDb);
    const double minPrio = std::min (kMinPrioriSnr, std::pow (10.0, -(depthDb + 6.0) / 10.0));   // a power ratio

    // Gain per bin: log-spectral MMSE (Ephraim & Malah 1985) with the decision-directed a priori SNR.
    for (size_t k = 0; k < ub; ++k)
    {
        const double nz = params.noiseDb >= 0.05 ? overSubtract * noise[k] : 0.0;
        const double late = roomOn ? kRoomOverSubtract * roomAmount * tailWeight * roomDecay * past[k] : 0.0;
        const double interference = nz + late;
        if (interference <= 1.0e-30)
        {
            gain[k] = 1.0;
            prevGain[k] = 1.0;
            prevPost[k] = 0.0;
            continue;
        }
        const double post = std::min (power[k] / interference, 1.0e4);
        const double prio = std::max (kDecisionDirected * prevGain[k] * prevGain[k] * prevPost[k]
                                        + (1.0 - kDecisionDirected) * std::max (post - 1.0, 0.0), minPrio);
        const double v = prio * post / (1.0 + prio);
        const double g = std::min (1.0, prio / (1.0 + prio) * (v > 10.0 ? 1.0 : std::exp (0.5 * expint1 (v))));
        prevGain[k] = g;
        prevPost[k] = post;
        // Never further down than asked: the floor is the Noise / Room setting, weighted by which one is there.
        const double floor = (nz * floorNoise + late * floorRoom) / interference;
        gain[k] = std::max (g, floor);
    }
    // A touch of smoothing across frequency, so single bins don't twinkle ("musical noise").
    for (size_t k = 0; k < ub; ++k)
    {
        const double lo = gain[k > 0 ? k - 1 : k], hi = gain[k + 1 < ub ? k + 1 : k];
        gainSm[k] = 0.25 * lo + 0.5 * gain[k] + 0.25 * hi;
    }

    // Apply, back to time, overlap-add (the windows' Hann sum is 2 at 75 % overlap: x 0.5).
    for (auto& o : outBuf)
    {
        std::copy (o.begin() + static_cast<std::ptrdiff_t> (uh), o.end(), o.begin());
        std::fill (o.end() - static_cast<std::ptrdiff_t> (uh), o.end(), 0.0);
    }
    double eIn = 0.0, eOut = 0.0;
    for (size_t k = 0; k < ub; ++k) { eIn += power[k]; eOut += power[k] * gainSm[k] * gainSm[k]; }
    sumIn += eIn;
    sumOut += eOut;
    for (int c = 0; c < nch; ++c)
    {
        auto& s = spec[static_cast<size_t> (c)];
        for (size_t k = 0; k < ub; ++k)
        {
            s[k] *= gainSm[k];
            if (k > 0 && k < un - k) s[un - k] = std::conj (s[k]);
        }
        for (auto& v : s) v = std::conj (v);
        fft (s);
        auto& o = outBuf[static_cast<size_t> (c)];
        const double scale = 0.5 / static_cast<double> (n);
        for (size_t i = 0; i < un; ++i) o[i] += scale * window[i] * s[i].real();   // conj(fft(conj X)) / n: the real part
    }
    for (auto& b : inBuf)
        std::copy (b.begin() + static_cast<std::ptrdiff_t> (uh), b.end(), b.begin());
}

} // namespace vox
