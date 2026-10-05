#include "vox/AutoEdit.h"

#include "vox/DynamicEq.h"
#include "vox/Fft.h"
#include "vox/LoudnessMeter.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace vox {

namespace {

using Signal = std::vector<double>;

double percentile (std::vector<double> v, double p)
{
    if (v.empty()) return -120.0;
    std::sort (v.begin(), v.end());
    const double pos = std::clamp (p, 0.0, 100.0) / 100.0 * static_cast<double> (v.size() - 1);
    const auto i = static_cast<size_t> (pos);
    const double f = pos - static_cast<double> (i);
    return i + 1 < v.size() ? v[i] * (1.0 - f) + v[i + 1] * f : v[i];
}

double energyDb (double e) { return e > 1.0e-24 ? 10.0 * std::log10 (e) : -240.0; }

std::string num (double v, int digits = 1)
{
    char buf[32];
    std::snprintf (buf, sizeof buf, "%.*f", digits, std::abs (v) < 0.5 * std::pow (10.0, -digits) ? 0.0 : v);
    std::string s (buf);
    if (! s.empty() && s[0] == '-') s = "\xE2\x88\x92" + s.substr (1);   // proper minus sign
    return s;
}
std::string signedDb (double v) { return (v > 0.049 ? "+" : "") + num (v) + " dB"; }
std::string db (double v) { return num (v) + " dB"; }
std::string hz (double f)
{
    if (f >= 1000.0) return num (f / 1000.0, f >= 10000.0 ? 1 : 1) + " kHz";
    return std::to_string (static_cast<int> (std::lround (f))) + " Hz";
}
std::string pct (double v) { return std::to_string (static_cast<int> (std::lround (v))) + " %"; }

Signal toMono (const std::vector<std::vector<float>>& audio)
{
    const size_t n = audio.empty() ? 0 : audio.front().size();
    Signal m (n, 0.0);
    for (const auto& ch : audio)
        for (size_t i = 0; i < n && i < ch.size(); ++i) m[i] += ch[i];
    const double k = audio.empty() ? 1.0 : 1.0 / static_cast<double> (audio.size());
    for (auto& v : m) v *= k;
    return m;
}

Signal highPass (const Signal& x, double hz, double sr, int stages = 1)
{
    Signal y = x;
    for (int s = 0; s < stages; ++s)
    {
        Biquad f;
        design::apply (f, design::butterworth (true, hz, sr));
        for (auto& v : y) v = f.process (v);
    }
    return y;
}

/** 10 ms frame statistics of a signal. */
struct Frames
{
    int hop = 480;
    std::vector<double> rmsDb, peakDb;
};

Frames frames (const Signal& x, double sr)
{
    Frames f;
    f.hop = std::max (1, static_cast<int> (std::lround (0.010 * sr)));
    const size_t hop = static_cast<size_t> (f.hop);
    for (size_t s = 0; s + 2 * hop <= x.size(); s += hop)
    {
        double e = 0.0, pk = 0.0;
        for (size_t i = s; i < s + 2 * hop; ++i) { e += x[i] * x[i]; pk = std::max (pk, std::abs (x[i])); }
        f.rmsDb.push_back (energyDb (e / static_cast<double> (2 * hop)));
        f.peakDb.push_back (pk > 0.0 ? 20.0 * std::log10 (pk) : -240.0);
    }
    return f;
}

std::vector<bool> activeMask (const Frames& f, double& activeThr)
{
    std::vector<double> live;
    for (double v : f.rmsDb) if (v > -100.0) live.push_back (v);
    const double loud = percentile (live, 95.0);
    activeThr = std::max (loud - 30.0, -65.0);
    std::vector<bool> m (f.rmsDb.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = f.rmsDb[i] > activeThr;
    return m;
}

double integratedLufs (const Signal& x, double sr)
{
    LoudnessMeter m;
    m.prepare (sr, 1);
    const double* p = x.data();
    m.process (&p, 1, static_cast<int> (x.size()));
    return m.integratedLufs();
}

double activeRmsDb (const Signal& x, const std::vector<bool>& mask, int hop)
{
    double e = 0.0;
    size_t count = 0;
    for (size_t f = 0; f < mask.size(); ++f)
    {
        if (! mask[f]) continue;
        for (size_t i = f * static_cast<size_t> (hop); i < std::min (x.size(), (f + 1) * static_cast<size_t> (hop)); ++i) { e += x[i] * x[i]; ++count; }
    }
    return count ? energyDb (e / static_cast<double> (count)) : -120.0;
}

// --- pitch -------------------------------------------------------------------------------------
std::vector<double> pitchTrack (const Signal& x, double sr, const std::vector<bool>& mask, int hop, std::vector<long>* stepIndex = nullptr)
{
    const int dec = std::max (1, static_cast<int> (std::lround (sr / 12000.0)));
    const double fs = sr / dec;
    Signal d (x.size() / static_cast<size_t> (dec));
    for (size_t i = 0; i < d.size(); ++i)
    {
        double s = 0.0;
        for (int k = 0; k < dec; ++k) s += x[i * static_cast<size_t> (dec) + static_cast<size_t> (k)];
        d[i] = s / dec;
    }
    const auto win = static_cast<size_t> (0.040 * fs), step = static_cast<size_t> (0.020 * fs);
    const auto minLag = static_cast<size_t> (fs / 700.0), maxLag = static_cast<size_t> (fs / 65.0);
    std::vector<double> f0s;
    std::vector<double> r (maxLag + 1);
    for (size_t s = 0; s + win + maxLag < d.size(); s += step)
    {
        const size_t frame = (s * static_cast<size_t> (dec)) / static_cast<size_t> (hop);
        if (frame >= mask.size() || ! mask[frame]) continue;
        double e0 = 0.0;
        for (size_t i = 0; i < win; ++i) e0 += d[s + i] * d[s + i];
        if (e0 <= 1.0e-12) continue;
        double best = 0.0;
        for (size_t lag = minLag; lag <= maxLag; ++lag)
        {
            double c = 0.0, e1 = 0.0;
            for (size_t i = 0; i < win; ++i) { c += d[s + i] * d[s + i + lag]; e1 += d[s + i + lag] * d[s + i + lag]; }
            r[lag] = c / std::sqrt (e0 * e1 + 1.0e-24);
            best = std::max (best, r[lag]);
        }
        if (best < 0.6) continue;
        // The shortest lag that is nearly as good as the best avoids octave-down mistakes.
        for (size_t lag = minLag + 1; lag < maxLag; ++lag)
            if (r[lag] >= 0.9 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1])
            {
                // Parabolic interpolation: whole-sample lags at 12 kHz are up to ~20 cents apart.
                const double a = r[lag - 1], b = r[lag], c = r[lag + 1], den = a - 2.0 * b + c;
                const double frac = std::abs (den) > 1.0e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
                f0s.push_back (fs / (static_cast<double> (lag) + frac));
                if (stepIndex) stepIndex->push_back (static_cast<long> (s / step));
                break;
            }
    }
    return f0s;
}

// --- spectrum ----------------------------------------------------------------------------------
constexpr size_t kFftN = 4096;

std::vector<double> averageSpectrum (const Signal& x, const std::vector<bool>& mask, int hop, std::vector<bool>* only = nullptr)
{
    std::vector<double> acc (kFftN / 2 + 1, 0.0);
    int count = 0;
    for (size_t s = 0; s + kFftN <= x.size(); s += kFftN / 2)
    {
        const size_t mid = (s + kFftN / 2) / static_cast<size_t> (hop);
        if (mid >= mask.size() || ! mask[mid]) continue;
        if (only && ! (*only)[mid]) continue;
        const auto p = powerSpectrum (x.data() + s, kFftN);
        for (size_t i = 0; i < acc.size(); ++i) acc[i] += p[i];
        ++count;
    }
    if (count > 0) for (auto& v : acc) v /= count;
    return acc;
}

double bandEnergy (const std::vector<double>& spec, double lo, double hi, double sr)
{
    const double binHz = sr / static_cast<double> (kFftN);
    const auto a = static_cast<size_t> (std::max (1.0, std::ceil (lo / binHz)));
    const auto b = std::min (spec.size() - 1, static_cast<size_t> (std::floor (hi / binHz)));
    double e = 0.0;
    if (b < a)
        e = spec[std::min (spec.size() - 1, static_cast<size_t> (std::lround (0.5 * (lo + hi) / binHz)))] * (hi - lo) / binHz;
    else
        for (size_t i = a; i <= b; ++i) e += spec[i];
    return e;
}

std::vector<double> bandBalance (const std::vector<double>& spec, double sr)
{
    const auto& bands = analysisBands();
    std::vector<double> e (bands.size());
    for (size_t k = 0; k < bands.size(); ++k)
        e[k] = bandEnergy (spec, bands[k] * std::pow (2.0, -1.0 / 6.0), std::min (bands[k] * std::pow (2.0, 1.0 / 6.0), 0.49 * sr), sr);
    double ref = 0.0;
    int n = 0;
    for (size_t k = 0; k < bands.size(); ++k)
        if (bands[k] >= 500.0 && bands[k] <= 2000.0) { ref += e[k]; ++n; }
    ref = energyDb (ref / std::max (1, n));
    std::vector<double> out (bands.size());
    for (size_t k = 0; k < bands.size(); ++k) out[k] = energyDb (e[k]) - ref;
    return out;
}

// --- sibilance -----------------------------------------------------------------------------------
struct Sibilance { double levelDb = -120.0; double share = 0.0; double hz = 0.0; };

Sibilance sibilance (const Signal& x, double sr, const std::vector<bool>& mask, int hop, double voiceRmsDb)
{
    Sibilance s;
    const Signal hf = highPass (x, 5000.0, sr, 2);
    std::vector<double> levels;
    std::vector<bool> sib (mask.size(), false);
    size_t active = 0;
    for (size_t f = 0; f < mask.size(); ++f)
    {
        if (! mask[f]) continue;
        ++active;
        double eh = 0.0, ef = 0.0;
        const size_t a = f * static_cast<size_t> (hop), b = std::min (x.size(), a + 2 * static_cast<size_t> (hop));
        for (size_t i = a; i < b; ++i) { eh += hf[i] * hf[i]; ef += x[i] * x[i]; }
        if (ef <= 0.0) continue;
        if (energyDb (eh / ef) > -6.0)
        {
            sib[f] = true;
            levels.push_back (energyDb (eh / static_cast<double> (b - a)));
        }
    }
    if (active == 0) return s;
    s.share = 100.0 * static_cast<double> (levels.size()) / static_cast<double> (active);
    if (levels.size() < std::max<size_t> (3, active / 200)) return s;   // under 0.5 %: no real sibilance
    s.levelDb = percentile (levels, 90.0) - voiceRmsDb;

    // Where the "s" sits: the loudest third-octave between 4 and 11 kHz in the sibilant frames.
    std::vector<double> acc (1024 / 2 + 1, 0.0);   // powerSpectrum(1024) has 513 bins
    for (size_t f = 0; f < sib.size(); ++f)
    {
        if (! sib[f]) continue;
        const size_t c = f * static_cast<size_t> (hop) + static_cast<size_t> (hop);
        if (c < 1024 / 2 || c + 1024 / 2 > x.size()) continue;
        const auto p = powerSpectrum (x.data() + c - 512, 1024);
        for (size_t i = 0; i < acc.size(); ++i) acc[i] += p[i];
    }
    double best = -1.0;
    for (double fc : { 4000.0, 5000.0, 6300.0, 8000.0, 10000.0 })
    {
        if (fc > 0.45 * sr) break;
        const double binHz = sr / 1024.0;
        double e = 0.0;
        for (size_t i = static_cast<size_t> (fc * 0.89 / binHz); i <= static_cast<size_t> (fc * 1.12 / binHz) && i < acc.size(); ++i) e += acc[i];
        e /= fc;   // per Hz, so wider bands don't win just for being wide
        if (e > best) { best = e; s.hz = fc; }
    }
    return s;
}

double rangeDb (const Signal& x, double sr, double activeThr, double* median = nullptr)
{
    const auto win = static_cast<size_t> (0.4 * sr), step = static_cast<size_t> (0.1 * sr);
    std::vector<double> lv;
    for (size_t s = 0; s + win <= x.size(); s += step)
    {
        double e = 0.0;
        for (size_t i = s; i < s + win; ++i) e += x[i] * x[i];
        const double d = energyDb (e / static_cast<double> (win));
        if (d > activeThr + 6.0) lv.push_back (d);
    }
    if (median) *median = lv.empty() ? -120.0 : percentile (lv, 50.0);
    return lv.size() < 5 ? 0.0 : percentile (lv, 90.0) - percentile (lv, 10.0);
}

// --- rendering helpers ---------------------------------------------------------------------------
Signal renderMono (const Signal& x, double sr, const ChainParams& p)
{
    std::vector<std::vector<float>> in (1, std::vector<float> (x.size()));
    for (size_t i = 0; i < x.size(); ++i) in[0][i] = static_cast<float> (x[i]);
    const auto out = VocalChain::render (in, sr, p);
    return Signal (out[0].begin(), out[0].end());
}

/** Everything off (neutral), the starting point for each partial render. */
ChainParams neutralChain()
{
    ChainParams p;
    p.deEsser.amount = 0.0;
    p.doubler.amount = 0.0;
    p.delay.mix = 0.0;
    p.reverb.mix = 0.0;
    return p;
}

/** Compressor gain reduction per 10 ms frame (peak stage, level stage) and its output. */
struct CompRun { std::vector<double> peakGr, levelGr; Signal out; };
CompRun runComp (const Signal& x, double sr, const CompParams& p, int hop)
{
    VocalCompressor c;
    c.prepare (sr, 1);
    c.setParams (p);
    CompRun r;
    r.out = x;
    for (size_t s = 0; s < x.size(); s += static_cast<size_t> (hop))
    {
        double* ptr = r.out.data() + s;
        const int len = static_cast<int> (std::min<size_t> (static_cast<size_t> (hop), x.size() - s));
        c.process (&ptr, 1, len);
        r.peakGr.push_back (c.takePeakGrDb());
        r.levelGr.push_back (c.takeLevelGrDb());
    }
    return r;
}

double meanOfDeepest (std::vector<double> gr, const std::vector<bool>& mask, double share)
{
    std::vector<double> v;
    for (size_t i = 0; i < gr.size() && i < mask.size(); ++i) if (mask[i]) v.push_back (-gr[i]);
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end(), std::greater<>());
    const size_t n = std::max<size_t> (1, static_cast<size_t> (static_cast<double> (v.size()) * share));
    return std::accumulate (v.begin(), v.begin() + static_cast<long> (n), 0.0) / static_cast<double> (n);
}

double meanActive (const std::vector<double>& gr, const std::vector<bool>& mask)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = 0; i < gr.size() && i < mask.size(); ++i) if (mask[i]) { s += -gr[i]; ++n; }
    return n ? s / static_cast<double> (n) : 0.0;
}

/** Per-style choices. */
struct StyleSpec
{
    double bodyOff, presOff, airOff;
    double dblAmount, dblWidth;
    int dlDiv; double dlFb, dlMix, dlTone, dlDuck; bool dlPing;
    double rvDecay, rvPre, rvMix, rvTone, rvDuck;
    SaturationMode satMode; double satTargetDb;
    double peakGr, levelGr, ratio;
    double gateRange, riderScale; int riderSpeed;
    double sibTarget;
    const char* sound;
};

const std::array<StyleSpec, kStyles> kSpecs {{
    // Trap Lead
    { -1.0, 1.5, 2.0,  30, 80,  0, 22, 14, 5000, 60, false,  1.4, 30, 12, 6500, 40,  SaturationMode::tube, -32,  5.0, 4.0, 4.0,  12, 1.0, 1,  -4.0,
      "upfront, bright and controlled, with a short wide space around it" },
    // Rap
    { 0.0, 1.0, 1.0,   0, 0,    1, 15, 8, 4500, 75, false,   0.9, 20, 7, 6000, 50,   SaturationMode::tape, -34,  6.0, 5.0, 4.0,  14, 1.0, 2,  -4.0,
      "dry, punchy and clear so every word lands" },
    // Melodic
    { 0.5, 0.0, 1.5,   35, 70,  3, 32, 18, 5500, 50, true,   2.2, 35, 18, 7000, 30,  SaturationMode::tape, -38,  4.0, 3.0, 3.0,  9, 0.8, 0,   -5.0,
      "smooth and airy, with a wider space for the sung notes to ring" },
    // Ad-libs
    { -3.0, 2.0, 1.0,  50, 100, 1, 30, 22, 4500, 40, true,   1.6, 20, 18, 6000, 20,  SaturationMode::clip, -28,  6.0, 6.0, 5.0,  15, 0.6, 1,  -3.0,
      "thinner, wider and more effected so they sit around the lead, not on top of it" },
    // R&B
    { 1.0, 0.0, 1.5,   20, 60,  0, 25, 12, 5000, 55, false,  2.6, 40, 20, 7500, 30,  SaturationMode::tube, -40,  3.0, 2.5, 2.5,  8, 0.8, 0,   -6.0,
      "warm, silky and natural, with a lush tail" },
}};

constexpr std::array<double, kIntensities> kIntensityScale { 0.6, 0.85, 1.1 };
constexpr std::array<double, kIntensities> kSpaceScale { 0.75, 1.0, 1.25 };

} // namespace

// =================================================================================================
const std::vector<double>& analysisBands()
{
    static const std::vector<double> b { 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                         2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
    return b;
}

std::vector<double> styleTarget (int style)
{
    // A finished modern vocal, third-octave energy vs its 500 Hz - 2 kHz average.
    static const std::vector<double> base { -9, -6, -3.5, -2, -1, 0, 0, 0, -0.5, -1, -2, -3, -4, -5,
                                            -5.5, -6, -7, -8.5, -10, -12, -14, -17, -22 };
    const auto& s = kSpecs[static_cast<size_t> (std::clamp (style, 0, kStyles - 1))];
    const auto& bands = analysisBands();
    std::vector<double> t = base;
    for (size_t k = 0; k < bands.size(); ++k)
    {
        const double f = bands[k];
        if (f >= 100 && f <= 250) t[k] += s.bodyOff;
        if (f >= 2500 && f <= 5000) t[k] += s.presOff;
        if (f >= 8000) t[k] += s.airOff;
    }
    return t;
}

VocalAnalysis analyseVocal (const std::vector<std::vector<float>>& audio, double sr)
{
    VocalAnalysis a;
    const Signal raw = toMono (audio);
    if (raw.empty()) return a;
    a.seconds = static_cast<double> (raw.size()) / sr;
    for (const auto& ch : audio)
        for (float v : ch) a.peakDb = std::max (a.peakDb, v != 0.0f ? 20.0 * std::log10 (std::abs (static_cast<double> (v))) : -120.0);
    a.inputLufs = integratedLufs (raw, sr);

    // Clipping: runs of 3+ samples stuck at the very top.
    for (const auto& ch : audio)
    {
        int run = 0;
        for (float v : ch)
        {
            if (std::abs (v) >= 0.989f) { if (++run == 3) ++a.clippedRuns; }
            else run = 0;
        }
    }
    if (audio.size() >= 2)
    {
        double el = 0.0, er = 0.0;
        for (size_t i = 0; i < audio[0].size(); ++i) { el += audio[0][i] * audio[0][i]; er += audio[1][i] * audio[1][i]; }
        a.stereo = true;
        a.oneSided = std::abs (energyDb (el + 1.0e-30) - energyDb (er + 1.0e-30)) > 30.0;
    }

    const Signal x = highPass (raw, 80.0, sr);
    const Frames f = frames (x, sr);
    double activeThr = -65.0;
    const auto mask = activeMask (f, activeThr);
    std::vector<double> voice, quietRms, quietPeak, voicePeak;
    for (size_t i = 0; i < mask.size(); ++i)
    {
        if (mask[i]) { voice.push_back (f.rmsDb[i]); voicePeak.push_back (f.peakDb[i]); }
        else if (f.rmsDb[i] > -100.0) { quietRms.push_back (f.rmsDb[i]); quietPeak.push_back (f.peakDb[i]); }
    }
    a.voicedSeconds = static_cast<double> (voice.size()) * 0.010;
    a.voiceRmsDb = percentile (voice, 50.0);
    a.quietWordPeakDb = percentile (voicePeak, 10.0);
    a.heardGaps = quietRms.size() >= 30;   // 0.3 s or more of gaps
    if (a.heardGaps)
    {
        a.noiseFloorDb = percentile (quietRms, 30.0);
        a.noisePeakDb = percentile (quietPeak, 90.0);
    }

    std::vector<long> steps;
    const auto f0 = pitchTrack (raw, sr, mask, f.hop, &steps);
    {
        // Held notes: runs of consecutive 20 ms readings that stay within half a semitone of the
        // run's start for 160 ms or more. Singing holds notes; rap glides (even when it's pitched).
        size_t held = 0, i = 0;
        while (i < f0.size())
        {
            size_t j = i + 1;
            const double m0 = 12.0 * std::log2 (f0[i] / 440.0);
            while (j < f0.size() && steps[j] == steps[j - 1] + 1 && std::abs (12.0 * std::log2 (f0[j] / 440.0) - m0) < 0.5) ++j;
            if (j - i >= 8) held += j - i;
            i = j;
        }
        a.heldShare = f0.empty() ? 0.0 : 100.0 * static_cast<double> (held) / static_cast<double> (f0.size());
    }
    if (f0.size() >= 10)
    {
        a.f0Median = percentile (f0, 50.0);
        a.f0Low = percentile (f0, 10.0);
    }
    {
        // pitchTrack hops 20 ms over singing frames (10 ms apart): share of them with a clear pitch.
        const double tries = std::max (1.0, static_cast<double> (voice.size()) / 2.0);
        a.pitchedShare = std::min (100.0, 100.0 * static_cast<double> (f0.size()) / tries);
        std::vector<double> midi;
        for (double hz : f0) midi.push_back (69.0 + 12.0 * std::log2 (hz / 440.0));
        a.key = detectKey (midi);
    }

    const auto spec = averageSpectrum (raw, mask, f.hop);
    double total = 0.0;
    for (size_t i = 1; i < spec.size(); ++i) total += spec[i];
    a.rumbleDb = energyDb (bandEnergy (spec, 1.0, 60.0, sr)) - energyDb (total);
    a.bandHz = analysisBands();
    a.bandDb = bandBalance (spec, sr);

    const auto s = sibilance (raw, sr, mask, f.hop, a.voiceRmsDb);
    a.sibilanceDb = s.levelDb;
    a.sibilanceHz = s.hz;
    a.sibilantShare = s.share;
    a.rangeDb = rangeDb (x, sr, activeThr);
    return a;
}

// =================================================================================================
AutoEditResult autoEdit (const std::vector<std::vector<float>>& audio, double sr, const AutoEditSettings& settings)
{
    AutoEditResult r;
    const int style = std::clamp (settings.style, 0, kStyles - 1);
    const int intensity = std::clamp (settings.intensity, 0, kIntensities - 1);
    const auto& spec = kSpecs[static_cast<size_t> (style)];
    const double k = kIntensityScale[static_cast<size_t> (intensity)];
    const double ks = kSpaceScale[static_cast<size_t> (intensity)];
    auto reason = [&r] (const char* m, const std::string& c, const std::string& v, const std::string& why) { r.reasons.push_back ({ m, c, v, why }); };
    auto keep = [&r] (Module m) { r.kept[static_cast<size_t> (m)] = true; };

    VocalAnalysis& a = r.analysis;
    a = analyseVocal (audio, sr);
    if (a.voicedSeconds < 3.0)
    {
        r.ok = false;
        r.summary = "Auto-Edit heard only " + num (a.voicedSeconds) + " s of vocal, too little to judge it.";
        r.tipTitle = "PLAY A SUNG PART";
        r.tip = "Start playback where you're rapping or singing (a verse or hook), then press Auto-Edit. It needs about 12 seconds of voice.";
        return r;
    }
    ChainParams p = neutralChain();
    const Signal raw = toMono (audio);

    // ---------------------------------------------------------------------------------------- warnings
    if (a.clippedRuns >= 3)
        r.notes.push_back ("CLIPPED RECORDING: the vocal hits the very top of the meter " + std::to_string (a.clippedRuns) +
                           " times. Those spots are distorted in the recording itself; no plug-in can fully undo that."
                           "\nNEED: Yes if you hear crackle on loud words. If it sounds fine to you, it's optional."
                           "\nSTEP: Next take, turn the gain knob on your audio interface down until your loudest words peak around -10 dB on Cubase's meter."
                           "\nSTEP: For this take, re-record the clipped lines if you can, or repair them with a declipper (Cubase Pro: SpectraLayers, or iZotope RX De-clip).");
    if (a.peakDb < -24.0)
        r.notes.push_back ("QUIET RECORDING: your loudest words only reach " + db (a.peakDb) + ". It works, but the noise of your room and interface sits closer to your voice."
                           "\nNEED: No. Auto-Edit adds the level back."
                           "\nSTEP: Next take, turn the interface gain up until your loudest words peak around -10 dB.");
    if (a.oneSided)
        r.notes.push_back ("ONE-SIDED TRACK: the vocal is only in one side of a stereo track, so it will sit off to one side."
                           "\nNEED: Yes."
                           "\nSTEP: In Cubase, record vocals on a MONO track (Add Track > Audio > Configuration: Mono)."
                           "\nSTEP: For this take, put a Cubase MixConvert or Stereo Combined Panner before Voxology, or re-import the file to a mono track.");
    const bool fewPitches = a.f0Median <= 0.0;
    if (fewPitches && a.rumbleDb > -10.0)
        r.notes.push_back ("NOT A SOLO VOCAL?: this sounds more like a beat or a full mix than a voice (strong bass, no clear pitch). Voxology is made for a single vocal track."
                           "\nNEED: Yes if this is your beat or Stereo Out."
                           "\nSTEP: Move Voxology to the vocal track's inserts (or the vocal group), then run Auto-Edit again.");

    // ---------------------------------------------------------------------------------------- 01 Pitch
    {
        struct Tune { double speed, humanize, amount; const char* feel; };
        static constexpr std::array<Tune, kStyles> tunes {{
            { 10.0, 20.0, 100.0, "a tight, modern trap tune: notes snap in, long notes keep a little life" },
            { 60.0, 30.0, 70.0, "a light touch: it keeps sung bits near the note without sounding tuned" },
            { 5.0, 10.0, 100.0, "the hard melodic-trap sound: every note locks on" },
            { 0.0, 0.0, 100.0, "the full robotic effect, the classic ad-lib sound" },
            { 80.0, 50.0, 90.0, "natural R&B tuning: slides and vibrato stay, only drift is fixed" },
        }};
        const auto& t = tunes[static_cast<size_t> (style)];
        auto& pt = p.pitch;
        const auto& kg = a.key;
        std::string keyName = std::string (kNoteNames[static_cast<size_t> (kg.key)]) + (kg.minor ? " minor" : " major");
        if (kg.ambiguous)
            keyName += std::string (" or ") + kNoteNames[static_cast<size_t> (kg.altKey)] + (kg.altMinor ? " minor" : " major");
        if (a.f0Median <= 0.0 || a.pitchedShare < 15.0)
        {
            pt.amount = 0.0;
            keep (Module::pitch);
            reason ("pitch", "Amount", "Off", "Auto-Edit heard almost no held notes (" + num (a.pitchedShare, 0) +
                    " % of the vocal has a clear pitch), so this sounds like rapping, not singing. Tuning spoken words only adds artefacts, so Pitch stays off. Turn it up yourself for the robotic effect.");
        }
        else
        {
            double speed = t.speed, amount = t.amount;
            if (intensity == 0) { speed = speed * 2.0 + 20.0; amount *= 0.8; }
            if (intensity == 2) { speed *= 0.5; amount = 100.0; }
            pt.amount = std::round (amount);
            pt.speedMs = std::round (speed);
            pt.humanize = t.humanize;
            const bool sure = kg.confidence >= 0.6 && ! kg.ambiguous;   // a wrong key is worse than Chromatic
            pt.key = kg.key;
            pt.scale = sure ? (kg.minor ? 2 : 1) : 0;
            if (sure)
                reason ("pitch", "Key", keyName, "Your sung notes fit " + keyName + " best (" + num (100.0 * kg.confidence, 0) +
                        " % sure), so notes are pulled only to notes of that scale. If your beat is in another key, change Key / Scale in the Pitch module: the beat's key always wins.");
            else
                reason ("pitch", "Key", "Chromatic", "Auto-Edit couldn't tell the key from this part (best guess " + keyName +
                        "), so Pitch uses all 12 notes: it can't pull you to a wrong-key note. Set Key / Scale to your beat's key for a tighter tune.");
            reason ("pitch", "Retune", std::to_string (static_cast<int> (pt.speedMs)) + " ms, Humanize " + pct (pt.humanize),
                    std::string ("For ") + kStyleNames[static_cast<size_t> (style)] + ": " + t.feel + ". Lower = more robotic, higher = more natural.");
            reason ("pitch", "Amount", pct (pt.amount), "You sing on average " + num (kg.offCents, 0) + " cents away from the nearest note" +
                    (kg.offCents < 12.0 ? " (already close: the tune will be subtle)." : kg.offCents < 25.0 ? " (normal for a take: the tune tightens it)." : " (quite loose: the tune makes a big difference)."));
            if (! sure)
                r.notes.push_back ("KEY UNSURE: from this part Auto-Edit can't be sure of the key (best guess " + keyName + "), so Pitch is set to Chromatic."
                                   "\nNEED: Optional. Chromatic works; the right key sounds tighter."
                                   "\nSTEP: Find your beat's key (it's often in the beat's file name or listing, e.g. \"A min\")."
                                   "\nSTEP: In the Pitch module, set Key and Scale to it.");
        }
    }

    // ---------------------------------------------------------------------------------------- 02 Cleanup
    {
        double hpf = 80.0;
        if (a.f0Low > 0.0) hpf = std::clamp (std::round (0.72 * a.f0Low / 5.0) * 5.0, 50.0, 150.0);
        if (style == 3) hpf = std::min (hpf + 30.0, 180.0);
        p.cleanup.lowCutHz = hpf;
        reason ("cleanup", "Low Cut", hz (hpf),
                a.f0Low > 0.0
                    ? "Your lowest notes sit around " + hz (a.f0Low) + ", so everything under " + hz (hpf) + " is rumble, mic handling and pops, not voice. Cutting it cleans the low end for the 808 and kick."
                          + (style == 3 ? " Ad-libs get a higher cut so they stay out of the lead's way." : "")
                    : "No clear pitch was found, so a safe " + hz (hpf) + " cut removes rumble without thinning the voice.");
        if (a.rumbleDb > -18.0 && ! fewPitches)
            r.notes.push_back ("RUMBLE: there's strong energy under 60 Hz (" + db (a.rumbleDb) + " vs the voice), usually AC, traffic, a desk or footsteps through the mic stand."
                               "\nNEED: No. The Low Cut removes it."
                               "\nSTEP: Next time, use the mic's shock mount and turn on its low-cut switch (if it has one).");

        const double snr = a.voiceRmsDb - a.noiseFloorDb;
        if (a.heardGaps && a.noiseFloorDb > -72.0 && snr < 55.0)
        {
            double thr = a.noisePeakDb + 6.0;
            const double ceiling = a.quietWordPeakDb - 6.0;
            bool squeezed = false;
            if (thr > ceiling) { thr = 0.5 * (a.noisePeakDb + a.quietWordPeakDb); squeezed = true; }
            const double range = std::clamp (spec.gateRange * k / 0.85, 4.0, 24.0);
            p.cleanup.gateThrDb = std::clamp (thr, -80.0, -20.0);
            p.cleanup.gateRangeDb = range;
            reason ("cleanup", "Gate", db (p.cleanup.gateThrDb) + ", " + db (range) + " down",
                    "The gaps between your phrases have noise at " + db (a.noiseFloorDb) + " (room, interface hiss or headphone bleed), " + num (snr, 0) +
                    " dB under your voice. The gate turns those gaps down " + num (range, 0) + " dB, so the compressor and saturation don't bring that noise up. It opens at " +
                    db (p.cleanup.gateThrDb) + ", under your quietest words.");
            if (squeezed || snr < 30.0)
                r.notes.push_back ("NOISY RECORDING: the noise in your gaps is only " + num (snr, 0) + " dB under your voice, so very soft words and breaths sit close to it."
                                   "\nNEED: Optional. The gate handles most of it; listen to quiet word endings."
                                   "\nSTEP: If soft words get cut off, lower the Gate threshold 3 dB at a time (Cleanup module)."
                                   "\nSTEP: Next take: turn off fans / AC, record closer to the mic (a fist away), keep headphones quieter so they don't leak.");
        }
        else
            reason ("cleanup", "Gate", "Off", a.heardGaps ? "The gaps between your phrases are already quiet (" + db (a.noiseFloorDb) + "), so a gate would only risk chopping word endings."
                                                         : "Auto-Edit heard no gaps between phrases (or you sing straight through), so the gate stays off to protect word endings.");
    }

    // ---------------------------------------------------------------------------------------- 03 Tone EQ
    {
        const auto target = styleTarget (style);
        const auto& bands = analysisBands();
        auto diffAt = [&] (double lo, double hi, double& peakHz, bool wantExcess)
        {
            double sum = 0.0, best = -1.0e9; int n = 0;
            for (size_t b = 0; b < bands.size(); ++b)
            {
                if (bands[b] < lo || bands[b] > hi || bands[b] < p.cleanup.lowCutHz * 1.3) continue;
                const double d = a.bandDb[b] - target[b];   // + = more than the target
                sum += d; ++n;
                const double score = wantExcess ? d : -d;
                if (score > best) { best = score; peakHz = bands[b]; }
            }
            return n ? sum / n : 0.0;
        };
        auto& eq = p.eq;
        double pk = 0.0;

        // Body (low shelf)
        const double body = diffAt (100, 250, pk, true);
        eq.gainDb[0] = std::clamp (-body * 0.5 * k, -4.0, 3.0);
        eq.freqHz[0] = style == 3 ? 250.0 : 180.0;
        if (std::abs (eq.gainDb[0]) < 0.5) eq.gainDb[0] = 0.0;
        reason ("eq", "Body", eq.gainDb[0] == 0.0 ? "0 dB" : signedDb (eq.gainDb[0]) + " at " + hz (eq.freqHz[0]),
                eq.gainDb[0] == 0.0 ? "The weight of your voice (100 - 250 Hz) is already right for " + std::string (kStyleNames[static_cast<size_t> (style)]) + "."
                : eq.gainDb[0] < 0.0 ? "Your voice has " + num (body) + " dB more low weight than a finished " + kStyleNames[static_cast<size_t> (style)] + " vocal (often the mic's proximity effect: singing very close). Trimming it keeps the vocal from fighting the 808."
                                     : "Your voice is " + num (-body) + " dB thinner down low than a finished " + kStyleNames[static_cast<size_t> (style)] + " vocal, so a little body adds warmth and weight.");
        if (body > 6.0)
            r.notes.push_back ("BOOMY MIC: your recording has a lot of low weight (" + num (body) + " dB over the target), usually from singing right on the mic."
                               "\nNEED: No. The Body band takes care of it."
                               "\nSTEP: Next take, back off to about a fist's distance (10 - 15 cm) from the mic.");

        // Mud and Nasal: cuts only, aimed at the worst third-octave.
        double mudHz = 300.0;
        const double mudAvg = diffAt (200, 630, mudHz, true);   // Nasal starts at 800 Hz: no overlap
        double mudPeak = 0.0;
        for (size_t b = 0; b < bands.size(); ++b) if (bands[b] == mudHz) mudPeak = a.bandDb[b] - target[b];
        const double mudEx = std::max (mudAvg, 0.6 * mudPeak);
        eq.gainDb[1] = mudEx > 1.5 ? -std::clamp ((mudEx - 1.0) * 0.6 * k, 0.0, 6.0) : 0.0;
        eq.freqHz[1] = std::clamp (mudHz, 150.0, 800.0);
        if (eq.gainDb[1] > -0.5) eq.gainDb[1] = 0.0;
        reason ("eq", "Mud", eq.gainDb[1] == 0.0 ? "0 dB" : signedDb (eq.gainDb[1]) + " at " + hz (eq.freqHz[1]),
                eq.gainDb[1] == 0.0 ? "No mud build-up (200 - 630 Hz): your vocal is clear there."
                                    : "There's a build-up around " + hz (eq.freqHz[1]) + " (" + num (mudEx) + " dB over a finished vocal) that makes it sound boxy or cloudy. A narrow cut there cleans it without thinning the voice.");

        double nasHz = 900.0;
        const double nasAvg = diffAt (800, 1600, nasHz, true);
        double nasPeak = 0.0;
        for (size_t b = 0; b < bands.size(); ++b) if (bands[b] == nasHz) nasPeak = a.bandDb[b] - target[b];
        const double nasEx = std::max (nasAvg, 0.6 * nasPeak);
        eq.gainDb[2] = nasEx > 2.0 ? -std::clamp ((nasEx - 1.5) * 0.6 * k, 0.0, 5.0) : 0.0;
        eq.freqHz[2] = std::clamp (nasHz, 500.0, 2000.0);
        if (eq.gainDb[2] > -0.5) eq.gainDb[2] = 0.0;
        reason ("eq", "Nasal", eq.gainDb[2] == 0.0 ? "0 dB" : signedDb (eq.gainDb[2]) + " at " + hz (eq.freqHz[2]),
                eq.gainDb[2] == 0.0 ? "No honky / nasal peak (800 Hz - 1.6 kHz)."
                                    : "A honky, phone-like peak sits around " + hz (eq.freqHz[2]) + " (" + num (nasEx) + " dB over). Cutting it makes the voice sound fuller and less pinched.");

        // Presence and Air: boost or cut toward the style.
        double presHz = 4000.0;
        const double pres = diffAt (2500, 5000, presHz, true);
        eq.gainDb[3] = std::clamp (-pres * 0.6 * k, -4.0, 5.0);
        eq.freqHz[3] = pres > 0.0 ? std::clamp (presHz, 2000.0, 8000.0) : (style == 4 ? 3500.0 : 4000.0);
        if (std::abs (eq.gainDb[3]) < 0.5) eq.gainDb[3] = 0.0;
        reason ("eq", "Presence", eq.gainDb[3] == 0.0 ? "0 dB" : signedDb (eq.gainDb[3]) + " at " + hz (eq.freqHz[3]),
                eq.gainDb[3] == 0.0 ? "Your words already cut through (2.5 - 5 kHz is on target)."
                : eq.gainDb[3] > 0.0 ? "Your vocal is " + num (-pres) + " dB short of a finished " + kStyleNames[static_cast<size_t> (style)] + " vocal in the presence range, where the words live. This lifts it so lyrics are clear over the beat."
                                     : "Your vocal is " + num (pres) + " dB hotter than the target around " + hz (eq.freqHz[3]) + ", which can sound harsh or shouty on headphones. A gentle cut smooths it.");

        double airHz = 12000.0;
        const double air = diffAt (8000, 12500, airHz, true);
        double airGain = std::clamp (-air * 0.6 * k, -3.0, 6.0);
        if (a.noiseFloorDb > -60.0) airGain = std::min (airGain, 2.0);
        if (std::abs (airGain) < 0.5) airGain = 0.0;
        eq.gainDb[4] = airGain;
        eq.freqHz[4] = intensity == 2 ? 10000.0 : 12000.0;
        reason ("eq", "Air", airGain == 0.0 ? "0 dB" : signedDb (airGain) + " above " + hz (eq.freqHz[4]),
                airGain == 0.0 ? "The top end (8 - 12.5 kHz) is already where a finished vocal sits."
                : airGain > 0.0 ? "Your vocal is " + num (-air) + " dB darker on top than a finished " + kStyleNames[static_cast<size_t> (style)] + " vocal. Air adds the breathy, expensive sheen modern vocals have."
                                    + (a.noiseFloorDb > -60.0 ? " Capped at +2 dB because it would also lift the hiss in your recording." : "")
                                : "Your top end is " + num (air) + " dB brighter than the target, so it's eased down a little to avoid fizz.");
    }

    // ---------------------------------------------------------------------------------------- 04 Dynamic EQ
    {
        // Runs the Dynamic EQ on the vocal (after Tone EQ) one band at a time, at each third-octave in
        // the band's zone, and keeps the spot that jumps out most. Max Cut follows how far it jumps.
        const Signal afterTone = renderMono (raw, sr, p);
        const Frames f = frames (highPass (afterTone, 80.0, sr), sr);
        double thr = 0.0;
        const auto mask = activeMask (f, thr);
        // "s" moments belong to the De-Esser: they don't count here.
        const Signal hf = highPass (afterTone, 5000.0, sr, 2);
        std::vector<bool> use (mask.size(), false);
        for (size_t i = 0; i < mask.size(); ++i)
        {
            if (! mask[i]) continue;
            double eh = 0.0, ef = 0.0;
            const size_t a0 = i * static_cast<size_t> (f.hop), b0 = std::min (afterTone.size(), a0 + 2 * static_cast<size_t> (f.hop));
            for (size_t k2 = a0; k2 < b0; ++k2) { eh += hf[k2] * hf[k2]; ef += afterTone[k2] * afterTone[k2]; }
            use[i] = ef > 0.0 && energyDb (eh / ef) < -6.0;
        }
        struct Run { double p95 = 0.0, share = 0.0; };
        auto runBand = [&] (const DynEqParams& d, int band)
        {
            DynamicEq de;
            de.prepare (sr, 1);
            de.setParams (d);
            Signal y = afterTone;
            std::vector<double> cuts;
            size_t frame = 0;
            for (size_t s0 = 0; s0 < y.size(); s0 += static_cast<size_t> (f.hop), ++frame)
            {
                double* ptr = y.data() + s0;
                de.process (&ptr, 1, static_cast<int> (std::min<size_t> (static_cast<size_t> (f.hop), y.size() - s0)));
                const double c = -de.takeCutDb()[static_cast<size_t> (band)];
                if (frame < use.size() && use[frame]) cuts.push_back (c);
            }
            Run r2;
            if (cuts.empty()) return r2;
            r2.p95 = percentile (cuts, 95.0);
            r2.share = 100.0 * static_cast<double> (std::count_if (cuts.begin(), cuts.end(), [] (double c) { return c > 1.0; })) / static_cast<double> (cuts.size());
            return r2;
        };

        // Each band searches its own zone (the zones don't overlap, like Tone EQ's; above 4 kHz is the De-Esser's).
        static constexpr std::array<std::array<double, 2>, kDynBands> zones {{ { 100, 200 }, { 250, 630 }, { 800, 2000 }, { 2500, 4000 } }};
        static constexpr std::array<const char*, kDynBands> sounds {
            "boom: the low end swells on some words (singing close to the mic, low notes, p and b sounds)",
            "mud: some vowels (\"oh\", \"oo\") cloud up and sound boxy",
            "honk: some words get nasal and pinched",
            "harshness: loud notes and shouted words get piercing" };
        auto& d = p.dynEq;
        d = {};
        d.sensitivity = 50.0;
        const double scale = k / 0.85;
        int used = 0;
        for (int b = 0; b < kDynBands; ++b)
        {
            const auto bi = static_cast<size_t> (b);
            const auto& info = kDynBandInfo[bi];
            double bestHz = info.def;
            Run best;
            for (double fc : analysisBands())
            {
                if (fc < zones[bi][0] || fc > zones[bi][1] || fc < p.cleanup.lowCutHz * 1.3 || fc > 0.4 * sr) continue;
                DynEqParams one;
                one.maxCutDb[bi] = kDynMaxCutDb;
                one.freqHz[bi] = fc;
                one.sensitivity = d.sensitivity;
                const Run r2 = runBand (one, b);
                if (r2.share <= 20.0 && r2.p95 > best.p95) { best = r2; bestHz = fc; }
            }
            d.freqHz[bi] = bestHz;
            const std::string name = info.name;
            // Only clear, occasional jumps count: a spot that's over its normal much of the time is
            // just how the voice moves (or a steady excess, Tone EQ's job), not a problem moment.
            if (best.p95 < 2.5 || best.share > 20.0)
            {
                reason ("dyneq", name, "Off", "Your " + std::string (b == 0 ? "low end" : b == 1 ? "low mids" : b == 2 ? "mids" : "upper mids") +
                        " (" + hz (zones[bi][0]) + " - " + hz (zones[bi][1]) + ") stay steady from word to word, so there's nothing to catch here.");
                continue;
            }
            d.maxCutDb[bi] = std::clamp (std::round (best.p95 * scale * 2.0) / 2.0, 2.0, 6.0);
            ++used;
            reason ("dyneq", name, "up to " + db (-d.maxCutDb[bi]) + " at " + hz (bestHz),
                    "Some words jump out around " + hz (bestHz) + ": " + sounds[bi] + ". The loudest of those moments rise about " + num (best.p95) +
                    " dB past your voice's normal there (about " + num (best.share, 0) + " % of the time). The band cuts up to " + num (d.maxCutDb[bi]) +
                    " dB only while that happens; the rest of the time it does nothing.");
        }
        if (used == 0)
        {
            keep (Module::dynEq);
            reason ("dyneq", "Sensitivity", "50 %", "No spot in your voice jumps out from word to word, so the Dynamic EQ stays off. Tone EQ already handles the steady balance.");
        }
        else
        {
            reason ("dyneq", "Sensitivity", pct (d.sensitivity),
                    "A band is pulled back once it rises " + num (DynamicEq::thresholdDb (d.sensitivity)) +
                    " dB past how it usually sits in your voice. It learns that from your voice as it plays, so it works the same on quiet and loud lines.");
        }
    }

    // ---------------------------------------------------------------------------------------- 05 De-Esser
    Signal afterEq;
    {
        ChainParams q = p;
        afterEq = renderMono (raw, sr, q);
        const Frames f = frames (highPass (afterEq, 80.0, sr), sr);
        double thr = 0.0;
        const auto mask = activeMask (f, thr);
        const double voice = activeRmsDb (afterEq, mask, f.hop);
        const auto s = sibilance (afterEq, sr, mask, f.hop, voice);
        const double target = spec.sibTarget - (intensity == 2 ? 1.0 : intensity == 0 ? -1.0 : 0.0);
        if (s.levelDb <= target || s.levelDb <= -100.0)
        {
            p.deEsser.amount = 0.0;
            keep (Module::deEsser);
            reason ("deess", "Amount", "0 %", s.levelDb <= -100.0 ? "Auto-Edit heard almost no \"s\" sounds in this part, so there's nothing to tame."
                                                                  : "Your \"s\" sounds peak at " + signedDb (s.levelDb) + " vs your voice, already under the " + signedDb (target) + " a finished " + kStyleNames[static_cast<size_t> (style)] + " vocal allows.");
        }
        else
        {
            DeEsserParams d;
            d.freqHz = std::clamp (s.hz > 0.0 ? s.hz * 0.85 : 6000.0, 3500.0, 10000.0);
            d.sensitivity = 50.0;
            auto levelWith = [&] (double amount)
            {
                d.amount = amount;
                DeEsser de;
                de.prepare (sr, 1);
                de.setParams (d);
                Signal y = afterEq;
                double* ptr = y.data();
                de.process (&ptr, 1, static_cast<int> (y.size()));
                return sibilance (y, sr, mask, f.hop, voice).levelDb;
            };
            double lo = 0.0, hi = 100.0;
            const double atMax = levelWith (100.0);
            if (atMax > target) lo = 100.0;
            else
                for (int it = 0; it < 7; ++it)
                {
                    const double mid = 0.5 * (lo + hi);
                    if (levelWith (mid) > target) lo = mid; else hi = mid;
                }
            d.amount = std::clamp (std::round (hi), 10.0, 100.0);
            const double after = levelWith (d.amount);
            p.deEsser = d;
            reason ("deess", "Amount", pct (d.amount) + " at " + hz (d.freqHz),
                    "Your loudest \"s\" and \"t\" sounds peak at " + signedDb (s.levelDb) + " vs your voice (around " + hz (s.hz) + "), which is sharp on headphones and earbuds"
                    + (p.eq.gainDb[3] > 0.0 || p.eq.gainDb[4] > 0.0 ? ", and more so after the Presence / Air boost" : "") + ". " + pct (d.amount) +
                    " brings them to " + signedDb (after) + ", where a finished " + kStyleNames[static_cast<size_t> (style)] + " vocal sits, and only while they happen.");
            if (atMax > target + 3.0)
                r.notes.push_back ("VERY SHARP S's: even at full strength the de-esser leaves your \"s\" sounds " + num (atMax - target) + " dB brighter than ideal."
                                   "\nNEED: Optional. Listen on earbuds: if \"s\" still stings, fix it at the source."
                                   "\nSTEP: Next take, angle the mic slightly off your mouth (point it at your chin) and use a pop filter."
                                   "\nSTEP: For this take, lower the Air band 1 - 2 dB in Tone EQ.");
        }
    }

    // ---------------------------------------------------------------------------------------- 04 Rider
    Signal afterDs = renderMono (raw, sr, p);
    {
        const Frames f = frames (highPass (afterDs, 80.0, sr), sr);
        double thr = 0.0;
        const auto mask = activeMask (f, thr);
        double phraseLevel = -120.0;
        const double spread = rangeDb (highPass (afterDs, 80.0, sr), sr, thr, &phraseLevel);
        double range = std::clamp ((spread - 6.0) * 0.5, 0.0, 6.0) * spec.riderScale * k / 0.85;
        range = std::round (range * 2.0) / 2.0;
        if (range < 1.0)
        {
            keep (Module::rider);
            reason ("rider", "Range", "Off", "Your loud and quiet lines are only " + num (spread) + " dB apart, already even, so the compressor alone keeps it steady.");
        }
        else
        {
            p.rider.rangeDb = range;
            p.rider.targetDb = std::clamp (std::round (phraseLevel), -40.0, -6.0);
            p.rider.speed = spec.riderSpeed;
            static const char* speeds[] = { "Slow", "Medium", "Fast" };
            reason ("rider", "Range", "\xC2\xB1" + db (range) + ", " + speeds[p.rider.speed],
                    "Your loud and quiet lines are " + num (spread) + " dB apart. The rider turns quiet words up and loud ones down (by up to " + num (range) +
                    " dB) toward " + db (p.rider.targetDb) + ", like riding a fader, so the compressor can stay gentle and natural. It holds still in the gaps, so it never pumps up noise.");
            if (spread > 18.0)
                r.notes.push_back ("UNEVEN TAKE: some lines are much louder than others (" + num (spread) + " dB apart), usually from moving toward and away from the mic."
                                   "\nNEED: No. The Rider and Compressor even it out."
                                   "\nSTEP: Next take, keep the same distance from the mic and turn your head away from it only for breaths.");
        }
    }

    // ---------------------------------------------------------------------------------------- 05 Compressor
    Signal afterRider = renderMono (raw, sr, p);
    {
        const Frames f = frames (highPass (afterRider, 80.0, sr), sr);
        double thr = 0.0;
        const auto mask = activeMask (f, thr);
        const double wantPeak = spec.peakGr * k / 0.85, wantLevel = spec.levelGr * k / 0.85;

        CompParams c;
        c.ratio = 1.0;
        // Peak stage: the deepest 5 % of moments get about wantPeak dB.
        double lo = -50.0, hi = 0.0;
        for (int it = 0; it < 14; ++it)
        {
            c.peakThrDb = 0.5 * (lo + hi);
            const auto run = runComp (afterRider, sr, c, f.hop);
            if (meanOfDeepest (run.peakGr, mask, 0.05) > wantPeak) lo = c.peakThrDb; else hi = c.peakThrDb;
        }
        c.peakThrDb = std::round (0.5 * (lo + hi) * 2.0) / 2.0;
        // Level stage: about wantLevel dB on average while singing.
        c.ratio = spec.ratio;
        lo = -60.0; hi = 0.0;
        for (int it = 0; it < 14; ++it)
        {
            c.thrDb = 0.5 * (lo + hi);
            const auto run = runComp (afterRider, sr, c, f.hop);
            if (meanActive (run.levelGr, mask) > wantLevel) lo = c.thrDb; else hi = c.thrDb;
        }
        c.thrDb = std::round (0.5 * (lo + hi) * 2.0) / 2.0;
        const auto run = runComp (afterRider, sr, c, f.hop);
        const double before = activeRmsDb (afterRider, mask, f.hop), after = activeRmsDb (run.out, mask, f.hop);
        c.makeupDb = std::clamp (std::round ((before - after) * 2.0) / 2.0, 0.0, 18.0);
        c.mix = 100.0;
        p.comp = c;
        const double gotPeak = meanOfDeepest (run.peakGr, mask, 0.05), gotLevel = meanActive (run.levelGr, mask);
        reason ("comp", "Peak", db (c.peakThrDb), "Catches the sudden loud syllables: about " + num (gotPeak) + " dB off the loudest 5 % of moments, so nothing jumps out of the beat.");
        reason ("comp", "Level", db (c.thrDb) + ", " + num (c.ratio, 1) + ":1",
                "Smooths the whole performance by about " + num (gotLevel) + " dB on average, the amount a " + kStyleNames[static_cast<size_t> (style)] +
                " vocal usually gets so it sits at one steady level " + (style == 1 ? "and every word punches through." : "on top of the beat."));
        reason ("comp", "Makeup", signedDb (c.makeupDb), "Puts back the level the compressor took away, so you compare tone, not loudness.");
    }

    // ---------------------------------------------------------------------------------------- 06 Saturation
    Signal afterComp = renderMono (raw, sr, p);
    {
        SaturationParams s;
        s.mode = spec.satMode;
        s.mix = 50.0;
        const double target = spec.satTargetDb + (intensity == 0 ? -4.0 : intensity == 2 ? 3.0 : 0.0);
        const std::vector<std::vector<float>> ch (1, std::vector<float> (afterComp.begin(), afterComp.end()));
        double lo = 0.0, hi = Saturation::kMaxDriveDb;
        for (int it = 0; it < 12; ++it)
        {
            s.driveDb = 0.5 * (lo + hi);
            if (Saturation::estimateHarmonicsDb (ch, s) < target) lo = s.driveDb; else hi = s.driveDb;
        }
        s.driveDb = std::round (0.5 * (lo + hi) * 2.0) / 2.0;
        if (s.driveDb < 1.0) s.driveDb = 1.0;
        p.saturation = s;
        static const char* modes[] = { "Tape", "Tube", "Clip" };
        static const char* feel[] = { "smooth glue and density", "warmth and a rich, forward low-mid", "grit and edge that cuts through busy beats" };
        const auto mi = static_cast<size_t> (s.mode);
        reason ("sat", "Mode", modes[mi], std::string ("For ") + kStyleNames[static_cast<size_t> (style)] + ", " + modes[mi] + " adds " + feel[mi] + ".");
        reason ("sat", "Drive", db (s.driveDb) + ", Mix 50 %",
                "Set so the added harmonics sit around " + db (target) + " under your voice: felt more than heard. It makes the vocal sound finished and helps it stay audible on phone speakers.");
    }

    // ---------------------------------------------------------------------------------------- 07-09 Space
    {
        p.doubler.amount = std::round (spec.dblAmount * ks);
        p.doubler.width = spec.dblWidth;
        if (p.doubler.amount < 1.0)
        {
            p.doubler.amount = 0.0;
            keep (Module::doubler);
            reason ("double", "Amount", "Off", std::string (kStyleNames[static_cast<size_t> (style)]) + " vocals stay single and centred, so every word hits hard in the middle.");
        }
        else
            reason ("double", "Amount", pct (p.doubler.amount) + ", Width " + pct (p.doubler.width),
                    "Adds two drifting copies left and right, like a stacked double take, for a bigger, wider " + std::string (kStyleNames[static_cast<size_t> (style)]) + " sound. The lead stays centred.");

        p.delay.division = spec.dlDiv;
        p.delay.feedback = spec.dlFb;
        p.delay.mix = std::round (spec.dlMix * ks);
        p.delay.toneHz = spec.dlTone;
        p.delay.duck = spec.dlDuck;
        p.delay.pingPong = spec.dlPing;
        p.delay.bpm = settings.bpm > 0.0 ? settings.bpm : 120.0;
        reason ("delay", "Time", std::string (kDelayNames[static_cast<size_t> (spec.dlDiv)]) + " at " + num (p.delay.bpm, 0) + " BPM",
                std::string ("Echoes on the beat grid fill the gaps between lines. Duck ") + pct (spec.dlDuck) + " keeps them quiet while you're rapping, so words stay clear."
                + (settings.bpm > 0.0 ? "" : " Your session tempo wasn't available, so 120 BPM is used: it locks to Cubase's tempo once playback runs."));
        reason ("delay", "Mix", pct (p.delay.mix) + ", Feedback " + pct (p.delay.feedback), "Low enough to feel, not hear as a separate echo" + std::string (spec.dlPing ? "; Ping-Pong bounces the repeats left / right." : "."));

        p.reverb.decayS = spec.rvDecay;
        p.reverb.predelayMs = spec.rvPre;
        p.reverb.mix = std::round (spec.rvMix * ks);
        p.reverb.toneHz = spec.rvTone;
        p.reverb.duck = spec.rvDuck;
        reason ("reverb", "Decay", num (spec.rvDecay) + " s, Pre-delay " + std::to_string (static_cast<int> (spec.rvPre)) + " ms",
                std::string ("A ") + (spec.rvDecay < 1.2 ? "short room" : spec.rvDecay < 2.0 ? "medium plate" : "long, lush plate") +
                    " that gives the vocal a place to live. The pre-delay keeps the start of each word dry and clear.");
        reason ("reverb", "Mix", pct (p.reverb.mix), "Just enough space to sound like a record without washing out the words; the low end of the reverb is cut so it never muddies the 808.");
    }

    // ---------------------------------------------------------------------------------------- Output
    {
        const Signal full = renderMono (raw, sr, p);
        const double inL = a.inputLufs, outL = integratedLufs (full, sr);
        p.outputDb = std::isfinite (inL) && std::isfinite (outL) ? std::clamp (std::round ((inL - outL) * 2.0) / 2.0, -12.0, 12.0) : 0.0;
        r.outputLufs = std::isfinite (outL) ? outL + p.outputDb : -120.0;
        reason ("out", "Output", signedDb (p.outputDb),
                "Matches the processed vocal to the loudness it came in at (" + num (inL) + " LUFS), so it doesn't jump in your mix and your fader stays where it was. Set the vocal's balance with the track fader.");
    }

    // ---------------------------------------------------------------------------------------- Suggestions
    r.suggestions.push_back ("Compare with A / B and MATCH on: MATCH plays both at the same loudness, so you judge the tone, not the volume.");
    if (style == 0 || style == 2)
        r.suggestions.push_back ("Try Ad-libs style on your ad-lib track and Trap Lead on the main vocal: different roles, different chains.");
    if (p.reverb.mix > 0.0 || p.delay.mix > 0.0)
        r.suggestions.push_back ("Several vocal tracks? Turn Delay and Reverb off here and use one shared FX send instead: it glues the stack together and saves CPU.");
    if (p.pitch.amount > 0.0)
        r.suggestions.push_back ("Already using Auto-Tune or Melodyne? Turn Voxology's Pitch off (one tuner is enough).");

    r.summary = "Listened to " + num (a.voicedSeconds) + " s of voice (" + kStyleNames[static_cast<size_t> (style)] + ", " + kIntensityNames[static_cast<size_t> (intensity)] +
                "). Your vocal came in at " + num (a.inputLufs) + " LUFS with peaks at " + db (a.peakDb) + ". The chain is set for a " + kStyleNames[static_cast<size_t> (style)] +
                " sound: " + spec.sound + ". Every change is explained below.";
    r.params = p;
    r.ok = true;
    return r;
}

} // namespace vox
