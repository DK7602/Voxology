#include "vox/AutoEdit.h"
#include "vox/ClipWatch.h"

#include "vox/DynamicEq.h"
#include "vox/Fft.h"
#include "vox/LoudnessMeter.h"
#include "vox/PopBreath.h"

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
constexpr double kFullSongLowDb = -14.0;   // tuned on MUSDB18-7 (vocals vs mixtures)

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

/** Reference Match's tone: finds the Tone EQ (5 bands, gains and frequencies) and Low Cut that bring a
    vocal's third-octave balance (`have`) closest to `want`, by coordinate descent over every band's
    frequency grid and gain. The balance is relative to 500 Hz - 2 kHz, so the EQ's own effect there is
    taken out. Weights: 160 Hz - 10 kHz count fully, the extremes less. */
struct ToneFit { EqParams eq; double lowCutHz = 20.0; double errBefore = 0.0, errAfter = 0.0; };

/** The tone's overall shape: each third-octave averaged (as energy) with its neighbours, so the fit follows
    the voice's envelope, not where a particular note's harmonics happen to land. */
std::vector<double> smoothedTone (const std::vector<double>& t)
{
    std::vector<double> out (t.size());
    for (size_t b = 0; b < t.size(); ++b)
    {
        double e = 0.0, wsum = 0.0;
        for (int o = -1; o <= 1; ++o)
        {
            const auto k = static_cast<long> (b) + o;
            if (k < 0 || k >= static_cast<long> (t.size())) continue;
            const double w = o == 0 ? 2.0 : 1.0;
            e += w * std::pow (10.0, t[static_cast<size_t> (k)] / 10.0);
            wsum += w;
        }
        out[b] = 10.0 * std::log10 (e / wsum + 1.0e-30);
    }
    return out;
}

ToneFit fitTone (const std::vector<double>& haveRaw, const std::vector<double>& wantRaw, double lowMin, double lowMax, double sr)
{
    const auto have = smoothedTone (haveRaw), want = smoothedTone (wantRaw);
    const auto& bands = analysisBands();
    const size_t nb = std::min ({ bands.size(), have.size(), want.size() });
    std::vector<double> w (nb);
    for (size_t b = 0; b < nb; ++b) w[b] = bands[b] >= 160.0 && bands[b] <= 10000.0 ? 1.0 : 0.4;
    auto lowCutDb = [] (double fc, double f) { return fc <= Cleanup::kLowCutOffHz ? 0.0 : -20.0 * std::log10 (1.0 + std::pow (fc / f, 4.0)); };
    std::array<std::vector<double>, kEqBands> bandResp;
    for (auto& r : bandResp) r.assign (nb, 0.0);
    std::vector<double> lcResp (nb, 0.0);
    auto error = [&] ()
    {
        std::vector<double> d (nb);
        double mid = 0.0; int nm = 0;
        for (size_t b = 0; b < nb; ++b)
        {
            d[b] = lcResp[b];
            for (const auto& r : bandResp) d[b] += r[b];
            if (bands[b] >= 500.0 && bands[b] <= 2000.0) { mid += d[b]; ++nm; }
        }
        mid /= std::max (1, nm);
        double e = 0.0, ws = 0.0;
        for (size_t b = 0; b < nb; ++b) { const double x = have[b] + d[b] - mid - want[b]; e += w[b] * x * x; ws += w[b]; }
        return std::sqrt (e / ws);
    };
    ToneFit fit;
    fit.eq.gainDb = { 0, 0, 0, 0, 0 };
    fit.lowCutHz = lowMin;
    for (size_t b = 0; b < nb; ++b) lcResp[b] = lowCutDb (lowMin, bands[b]);
    fit.errBefore = error();
    auto setBand = [&] (int k, double g, double f)
    {
        for (size_t b = 0; b < nb; ++b) bandResp[static_cast<size_t> (k)][b] = VocalEQ::bandResponseDb (k, g, f, bands[b], sr);
    };
    for (int sweep = 0; sweep < 5; ++sweep)
    {
        for (int k = 0; k < kEqBands; ++k)
        {
            const auto& info = kEqBandInfo[static_cast<size_t> (k)];
            const double gLo = (k == 1 || k == 2) ? -9.0 : -8.0, gHi = (k == 1 || k == 2) ? 3.0 : 8.0;   // Mud / Nasal: mostly cuts
            double bestE = 1.0e9, bestG = fit.eq.gainDb[static_cast<size_t> (k)], bestF = fit.eq.freqHz[static_cast<size_t> (k)];
            for (int fi = 0; fi < 12; ++fi)
            {
                const double f = info.lo * std::pow (info.hi / info.lo, fi / 11.0);
                for (double g = gLo; g <= gHi + 1.0e-9; g += 0.5)
                {
                    setBand (k, g, f);
                    const double e = error();
                    if (e < bestE - 1.0e-9) { bestE = e; bestG = g; bestF = f; }
                }
            }
            fit.eq.gainDb[static_cast<size_t> (k)] = bestG;
            fit.eq.freqHz[static_cast<size_t> (k)] = std::round (bestF);
            setBand (k, bestG, bestF);
        }
        double bestE = 1.0e9, bestL = fit.lowCutHz;
        for (int li = 0; li < 16; ++li)
        {
            const double fc = lowMin * std::pow (std::max (lowMax, lowMin) / lowMin, li / 15.0);
            for (size_t b = 0; b < nb; ++b) lcResp[b] = lowCutDb (fc, bands[b]);
            const double e = error();
            if (e < bestE - 1.0e-9) { bestE = e; bestL = fc; }
        }
        fit.lowCutHz = std::round (bestL / 5.0) * 5.0;
        for (size_t b = 0; b < nb; ++b) lcResp[b] = lowCutDb (fit.lowCutHz, bands[b]);
    }
    fit.errAfter = error();
    return fit;
}

/** Punch: 50 ms levels (5 frames of 10 ms) while singing; how far the loud moments stand over the middle.
    Low = compressed. */
double punchDb (const Frames& f, const std::vector<bool>& mask)
{
    std::vector<double> lv;
    for (size_t i = 0; i + 5 <= f.rmsDb.size() && i + 5 <= mask.size(); i += 5)
    {
        bool on = true;
        double e = 0.0;
        for (size_t j = i; j < i + 5; ++j) { on = on && mask[j]; e += std::pow (10.0, f.rmsDb[j] / 10.0); }
        if (on) lv.push_back (energyDb (e / 5.0));
    }
    return lv.size() >= 20 ? percentile (lv, 95.0) - percentile (lv, 50.0) : 0.0;
}

/** Space: at each phrase end (level falls from within 12 dB of the loud part to 20 dB under it and stays
    there for 300 ms), what's left 100 - 300 ms later vs the last 200 ms of the phrase. -120 = unknown. */
double spaceDb (const Frames& f)
{
    std::vector<double> live;
    for (double v : f.rmsDb) if (v > -100.0) live.push_back (v);
    if (live.empty()) return -120.0;
    const double loud = percentile (live, 95.0);
    auto meanDb = [&f] (size_t from, size_t to)
    {
        double e = 0.0;
        for (size_t j = from; j < to; ++j) e += std::pow (10.0, f.rmsDb[j] / 10.0);
        return energyDb (e / static_cast<double> (std::max<size_t> (1, to - from)));
    };
    std::vector<double> tails;
    for (size_t i = 20; i + 31 < f.rmsDb.size(); ++i)
    {
        if (! (f.rmsDb[i - 1] > loud - 12.0 && f.rmsDb[i] <= loud - 12.0)) continue;
        bool quiet = true;   // stays down for 300 ms (a real phrase end, not a gap between syllables)
        for (size_t j = i + 10; j < i + 30; ++j) quiet = quiet && f.rmsDb[j] < loud - 20.0;
        if (quiet) tails.push_back (meanDb (i + 10, i + 30) - meanDb (i - 20, i));
    }
    return tails.size() >= 2 ? percentile (tails, 50.0) : -120.0;
}

/** A beat under the vocal: lots of energy under 100 Hz (kick, 808, bass). On MUSDB18-7, isolated vocals
    sit at -20 dB or lower (95 %), full mixes at -10.5 dB or higher (95 %). */
bool fullSong (const VocalAnalysis& a)
{
    return a.lowBassDb > kFullSongLowDb;
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
    // Pop
    { 0.0, 1.5, 2.5,   25, 70,  0, 20, 12, 5500, 55, false,  1.8, 30, 15, 7500, 35,  SaturationMode::tube, -36,  5.0, 4.0, 4.0,  10, 1.0, 1,  -5.0,
      "bright, polished and upfront, with a clean plate and a touch of width" },
    // Folk (no doubler, no delay: one voice in a room)
    { 1.0, 0.0, 0.5,   0, 0,    0, 10, 0, 5000, 50, false,   1.4, 20, 14, 6500, 20,  SaturationMode::tape, -42,  3.0, 2.5, 2.5,  6, 0.7, 0,   -6.0,
      "warm, close and natural, like a singer in a room with an acoustic guitar" },
    // Natural Singer (the lightest touch: your own voice, cleaner and more even)
    { 0.5, 0.5, 1.0,   0, 0,    0, 10, 0, 5000, 50, false,   1.2, 20, 10, 7000, 30,  SaturationMode::tape, -44,  3.0, 2.0, 2.0,  6, 0.8, 0,   -6.0,
      "your own voice, just cleaner and more even: light control, little colour, a small room" },
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

    // Clipping: runs of 3+ samples stuck at the same loud value, at any level (ClipWatch).
    {
        ClipWatch w;
        w.prepare (static_cast<int> (audio.size()));
        std::vector<std::vector<double>> d (audio.size());
        std::vector<const double*> ptr (audio.size());
        for (size_t c = 0; c < audio.size(); ++c) { d[c].assign (audio[c].begin(), audio[c].end()); ptr[c] = d[c].data(); }
        size_t n = audio.empty() ? 0 : audio.front().size();
        for (const auto& ch : audio) n = std::min (n, ch.size());
        if (n > 0) w.process (ptr.data(), static_cast<int> (audio.size()), static_cast<int> (n));
        a.clippedRuns = w.take().runs;
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

    a.microDynDb = punchDb (f, mask);
    a.tailDb = spaceDb (f);
    {
        double low = 0.0, all = 0.0;
        for (size_t i = 1; i < spec.size(); ++i) { all += spec[i]; if (static_cast<double> (i) * sr / static_cast<double> (kFftN) < 100.0) low += spec[i]; }
        a.lowBassDb = energyDb (low) - energyDb (all);
    }
    return a;
}

ReferenceProfile analyseReference (const std::vector<std::vector<float>>& audio, double sr, const std::string& name)
{
    ReferenceProfile r;
    r.name = name;
    const auto a = analyseVocal (audio, sr);
    r.voicedSeconds = a.voicedSeconds;
    r.bandDb = a.bandDb;
    r.sibilanceDb = a.sibilanceDb;
    r.microDynDb = a.microDynDb;
    r.tailDb = a.tailDb;
    if (a.voicedSeconds < 3.0)
        r.problem = "Only " + num (a.voicedSeconds) + " s of voice in it. Pick a file with at least a verse or a hook of vocals (10 s or more is best).";
    else if (fullSong (a))
        r.warning = "This sounds like a full song (beat and vocal together), so the match will be rough: with a beat under it, "
                    "a vocal's tone reads about 7 dB off (measured on 144 pro songs).";
    r.ok = r.problem.empty();
    return r;
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
    const ReferenceProfile* ref = settings.reference != nullptr && settings.reference->ok ? settings.reference : nullptr;
    const std::string aimed = ref != nullptr ? "the reference (" + ref->name + ")"
                                             : std::string ("a finished ") + kStyleNames[static_cast<size_t> (style)] + " vocal";
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
    if (a.peakDb > 0.0)
        r.notes.push_back ("TOO HOT: the vocal reaches +" + db (a.peakDb) + ", over the top of the meter, before Voxology. Cubase doesn't clip inside a mix, "
                           "but anything that limits later (like a limiter on the Stereo Out) has to squash it hard, which crackles."
                           "\nNEED: Yes if you hear crackle or the master limiter works hard."
                           "\nSTEP: Lower the vocal's clip gain (or the fader of whatever feeds it) until the loudest words peak around -6 dB."
                           "\nSTEP: Check your master limiter only shaves 1 - 3 dB.");
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
        struct Tune { int mode; double speed, humanize, amount; const char* feel; };
        static constexpr std::array<Tune, kStyles> tunes {{
            { kPitchClassic, 10.0, 20.0, 100.0, "a tight, modern trap tune: notes snap in, long notes keep a little life" },
            { kPitchNatural, 60.0, 30.0, 70.0, "a light touch: it keeps sung bits near the note without sounding tuned" },
            { kPitchClassic, 5.0, 10.0, 100.0, "the hard melodic-trap sound: every note locks on" },
            { kPitchRobot, 0.0, 0.0, 100.0, "the full robotic effect, the classic ad-lib sound" },
            { kPitchNatural, 25.0, 50.0, 100.0, "natural R&B tuning: each note lands, slides and vibrato stay" },
            { kPitchNatural, 15.0, 40.0, 100.0, "a polished pop tune: notes land clean and on time, vibrato stays" },
            { kPitchNatural, 50.0, 60.0, 70.0, "a light, hidden touch: drifting notes are nudged, slides and character stay" },
            { kPitchNatural, 40.0, 60.0, 80.0, "a transparent touch: only the centre of each note is nudged, nothing sounds tuned" },
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
            pt.mode = kPitchNatural;   // if you turn it up yourself
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
            pt.mode = t.mode;
            pt.vibrato = 0.0;
            const bool sure = kg.confidence >= 0.6 && ! kg.ambiguous;   // a wrong key is worse than Chromatic
            pt.key = kg.key;
            pt.scale = sure ? (kg.minor ? 2 : 1) : 0;
            if (settings.beatKeyKnown)
            {
                // The beat decides (Pitch follows it live); Key / Scale are only its fallback.
                pt.key = settings.beatKeyNote;
                pt.scale = settings.beatScale;
                if (settings.beatKeyFromVoice)
                    reason ("pitch", "Key", "From your voice: " + settings.beatKeyName, "No beat is heard, so Voxology learned the key from the notes you sing: "
                            + settings.beatKeyName + ". Pitch follows it live. For the surest key, let Voxology hear your beat: feed it to the side-chain, or put a second Voxology on the beat in BEAT mode.");
                else
                    reason ("pitch", "Key", "From beat: " + settings.beatKeyName, "Voxology hears your beat in " + settings.beatKeyName
                            + ", so Pitch follows the beat. Your voice alone suggested " + keyName + "; the beat always wins.");
            }
            else if (sure)
                reason ("pitch", "Key", keyName, "Your sung notes fit " + keyName + " best (" + num (100.0 * kg.confidence, 0) +
                        " % sure), so notes are pulled only to notes of that scale. If your beat is in another key, change Key / Scale in the Pitch module: the beat's key always wins.");
            else
                reason ("pitch", "Key", "Chromatic", "Auto-Edit couldn't tell the key from this part (best guess " + keyName +
                        "), so Pitch uses all 12 notes: it can't pull you to a wrong-key note. Set Key / Scale to your beat's key for a tighter tune.");
            reason ("pitch", "Mode", kPitchModeNames[static_cast<size_t> (pt.mode)],
                    pt.mode == kPitchNatural ? "Natural pulls the centre of each note onto pitch and leaves your vibrato, scoops and slides alone: you, just in tune."
                    : pt.mode == kPitchRobot ? "Robot snaps every note instantly and lays it flat: the hard, stepped effect. Switch to Natural for a human sound."
                                             : "Classic glides the whole pitch line onto the note at the Retune speed: the familiar auto-tune sound. Natural keeps more of your own voice; Robot is harder.");
            reason ("pitch", "Retune", std::to_string (static_cast<int> (pt.speedMs)) + " ms, Humanize " + pct (pt.humanize),
                    std::string ("For ") + kStyleNames[static_cast<size_t> (style)] + ": " + t.feel + ". Lower = more robotic, higher = more natural.");
            reason ("pitch", "Amount", pct (pt.amount), "You sing on average " + num (kg.offCents, 0) + " cents away from the nearest note" +
                    (kg.offCents < 12.0 ? " (already close: the tune will be subtle)." : kg.offCents < 25.0 ? " (normal for a take: the tune tightens it)." : " (quite loose: the tune makes a big difference)."));
            if (! sure && ! settings.beatKeyKnown)
                r.notes.push_back ("KEY UNSURE: from this part Auto-Edit can't be sure of the key (best guess " + keyName + "), so Pitch is set to Chromatic."
                                   "\nNEED: Optional. Chromatic works; the right key sounds tighter."
                                   "\nSTEP: Easiest: leave Key on AUTO and let Voxology hear your beat: in Cubase, turn on Voxology's side-chain and send the beat track to it (or put a second Voxology on the beat in BEAT mode)."
                                   "\nSTEP: With no beat, AUTO learns the key from your singing after about 20 - 40 s of held notes (rap may never be sure)."
                                   "\nSTEP: Or set Key and Scale yourself (the key is often in the beat's name, e.g. \"A min\"), with Key on MANUAL.");
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

    // ---------------------------------------------------------------------------------------- 02 Cleanup: pops + breaths
    {
        // Runs both detectors on the vocal after the low cut and gate, and counts what they find.
        const Signal cleaned = renderMono (raw, sr, p);
        const double minutes = std::max (a.seconds / 60.0, 0.05);
        int popCount = 0;
        double popDeepest = 0.0;
        {
            PopRemover pr;
            pr.prepare (sr, 1);
            pr.setParams ({ 100.0 });
            Signal y = cleaned;
            bool in = false;
            const auto hop = static_cast<size_t> (std::max (1, static_cast<int> (0.001 * sr)));
            for (size_t s0 = 0; s0 < y.size(); s0 += hop)
            {
                double* ptr = y.data() + s0;
                pr.process (&ptr, 1, static_cast<int> (std::min (hop, y.size() - s0)));
                const double c = -pr.takeCutDb();
                if (c > 6.0 && ! in) { in = true; ++popCount; }
                if (in && c < 1.0) in = false;
                popDeepest = std::max (popDeepest, c);
            }
        }
        if (popCount > 0)
        {
            static constexpr std::array<double, kIntensities> amounts { 60.0, 80.0, 100.0 };
            p.cleanup.popAmount = amounts[static_cast<size_t> (intensity)];
            reason ("cleanup", "Pops", pct (p.cleanup.popAmount),
                    "Auto-Edit heard " + std::to_string (popCount) + (popCount == 1 ? " pop" : " pops") +
                    " (the low thump of a \"p\" or \"b\" hitting the mic, up to " + num (popDeepest, 0) +
                    " dB over your voice's normal low end). The remover cuts that thump for the few hundredths of a second it lasts, and leaves the rest of the word alone.");
            if (static_cast<double> (popCount) / minutes > 6.0)
                r.notes.push_back ("POPPY MIC: " + std::to_string (popCount) + " pops in " + num (a.seconds, 0) + " s, so the mic gets hit by air a lot."
                                   "\nNEED: No. The plosive remover handles them."
                                   "\nSTEP: Next take, put a pop filter 5 - 10 cm in front of the mic, or angle the mic slightly to the side of your mouth.");
        }
        else
        {
            // It only acts on a real pop, so a light setting is a free safety net for the rest of the song.
            p.cleanup.popAmount = 60.0;
            reason ("cleanup", "Pops", "60 %", "No \"p\" or \"b\" pops hit the mic in the part Auto-Edit heard, but other parts of the song may have them. "
                    "The remover only acts when a pop actually hits, so it stays on at a light 60 % as a safety net and does nothing the rest of the time.");
        }

        int breathCount = 0;
        double breathSeconds = 0.0, breathEnergy = 0.0;
        size_t breathSamples = 0;
        {
            BreathControl bc;
            bc.prepare (sr, 1);
            bc.setParams ({ 12.0 });
            Signal y = cleaned;
            bool in = false;
            const auto hop = static_cast<size_t> (std::max (1, static_cast<int> (0.001 * sr)));
            for (size_t s0 = 0; s0 < y.size(); s0 += hop)
            {
                double* ptr = y.data() + s0;
                const auto len = std::min (hop, y.size() - s0);
                bc.process (&ptr, 1, static_cast<int> (len));
                const bool b = bc.inBreath();
                if (b && ! in) ++breathCount;
                in = b;
                if (b)
                {
                    breathSeconds += static_cast<double> (len) / sr;
                    for (size_t k2 = s0; k2 < s0 + len; ++k2) breathEnergy += cleaned[k2] * cleaned[k2];
                    breathSamples += len;
                }
            }
        }
        const double breathDb = breathSamples ? energyDb (breathEnergy / static_cast<double> (breathSamples)) - a.voiceRmsDb : -120.0;
        static constexpr std::array<double, kStyles> breathBase { 9.0, 10.0, 6.0, 12.0, 5.0, 7.0, 3.0, 4.0 };   // Trap Lead, Rap, Melodic, Ad-libs, R&B, Pop, Folk, Natural Singer
        if (breathCount >= 2 && breathDb > -36.0)
        {
            const double amt = std::clamp (std::round (breathBase[static_cast<size_t> (style)] * k / 0.85), 3.0, 18.0);
            p.cleanup.breathDb = amt;
            reason ("cleanup", "Breaths", "\xE2\x88\x92" + num (amt, 0) + " dB",
                    "Auto-Edit heard " + std::to_string (breathCount) + " breaths, about " + num (-breathDb, 0) + " dB under your voice. The compressor and saturation later in the chain bring quiet sounds up, so breaths would get louder. They're turned down " +
                    num (amt, 0) + " dB" + (isSungStyle (style) ? ", only a little, because a bit of breath sounds natural on sung parts." : ", so the gaps between lines stay clean and the words hit harder.") +
                    " Words and \"s\" sounds are left alone.");
        }
        else
        {
            p.cleanup.breathDb = 0.0;
            reason ("cleanup", "Breaths", "Off", breathCount < 2 ? "Auto-Edit heard almost no breaths in this part, so breath control stays off."
                                                               : "Your breaths are already quiet (about " + num (-breathDb, 0) + " dB under your voice), so there's nothing to turn down.");
        }
    }

    // ---------------------------------------------------------------------------------------- 03 Tone EQ
    {
    if (ref != nullptr)
    {
        // Fit Tone EQ + Low Cut to the reference's tone (the low cut may rise, never past your lowest notes).
        const double lowMin = p.cleanup.lowCutHz;
        const double lowMax = a.f0Low > 0.0 ? std::max (lowMin, std::min (0.95 * a.f0Low, 250.0)) : std::max (lowMin, 150.0);
        auto fit = fitTone (a.bandDb, ref->bandDb, lowMin, lowMax, sr);
        const double strength = intensity == 0 ? 0.7 : 1.0;
        for (auto& g : fit.eq.gainDb) { g = std::round (g * strength * 2.0) / 2.0; if (std::abs (g) < 0.5) g = 0.0; }
        p.eq = fit.eq;
        p.eq.enabled = true;
        const double oldLow = p.cleanup.lowCutHz;
        p.cleanup.lowCutHz = fit.lowCutHz;
        const auto& bands = analysisBands();
        const auto refShape = smoothedTone (ref->bandDb), myShape = smoothedTone (a.bandDb);
        auto diffNear = [&] (double f)   // reference minus yours at the nearest analysis band (dB, overall shape)
        {
            size_t best = 0;
            for (size_t b = 1; b < bands.size(); ++b) if (std::abs (std::log (bands[b] / f)) < std::abs (std::log (bands[best] / f))) best = b;
            return refShape[best] - myShape[best];
        };
        static constexpr std::array<const char*, kEqBands> zone { "low weight", "boxy low-mids", "honky mids", "presence (where the words live)", "air on top" };
        for (int k2 = 0; k2 < kEqBands; ++k2)
        {
            const auto kb = static_cast<size_t> (k2);
            const double g = p.eq.gainDb[kb], f = p.eq.freqHz[kb], d = diffNear (f);
            reason ("eq", kEqBandInfo[kb].name, g == 0.0 ? "0 dB" : signedDb (g) + " at " + hz (f),
                    g == 0.0 ? std::string ("Your ") + zone[kb] + " already sit where the reference's do."
                             : std::string ("The reference has ") + num (std::abs (d)) + " dB " + (d > 0.0 ? "more " : "less ") + zone[kb] + " than your vocal around " + hz (f) +
                                   ", so this " + (g > 0.0 ? "lifts" : "trims") + " yours toward it.");
        }
        if (p.cleanup.lowCutHz > oldLow + 1.0)
            reason ("cleanup", "Low Cut", hz (p.cleanup.lowCutHz), "Raised from " + hz (oldLow) + " to match the reference: its vocal is cleaner and thinner down low than yours. It stays under your lowest notes.");
        reason ("eq", "Tone match", num (fit.errBefore) + " \xE2\x86\x92 " + num (fit.errAfter) + " dB",
                "How far your vocal's tone is from the reference's, averaged across the spectrum, before and after these moves"
                + std::string (fit.errAfter > 4.0 ? ". What's left is mostly your voice itself (its shape and the room), which no EQ can turn into someone else's." : "."));
        if (intensity == 0) reason ("eq", "Strength", "70 %", "Light intensity: the moves go 70 % of the way to the reference.");
    }
    else
    {
        const auto target = ref != nullptr ? ref->bandDb : styleTarget (style);
        // Matching a reference you chose: correct more of the difference than the generic style does.
        const double kq = ref != nullptr ? std::min (1.35 * kq, 1.35) : k;
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
        eq.gainDb[0] = std::clamp (-body * 0.5 * kq, -4.0, 3.0);
        eq.freqHz[0] = style == 3 ? 250.0 : 180.0;
        if (std::abs (eq.gainDb[0]) < 0.5) eq.gainDb[0] = 0.0;
        reason ("eq", "Body", eq.gainDb[0] == 0.0 ? "0 dB" : signedDb (eq.gainDb[0]) + " at " + hz (eq.freqHz[0]),
                eq.gainDb[0] == 0.0 ? "The weight of your voice (100 - 250 Hz) is already right for " + (ref != nullptr ? std::string ("the reference") : std::string (kStyleNames[static_cast<size_t> (style)])) + "."
                : eq.gainDb[0] < 0.0 ? "Your voice has " + num (body) + " dB more low weight than " + aimed + " (often the mic's proximity effect: singing very close). Trimming it keeps the vocal from fighting the 808."
                                     : "Your voice is " + num (-body) + " dB thinner down low than " + aimed + ", so a little body adds warmth and weight.");
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
        eq.gainDb[1] = mudEx > 1.5 ? -std::clamp ((mudEx - 1.0) * 0.6 * kq, 0.0, 6.0) : 0.0;
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
        eq.gainDb[2] = nasEx > 2.0 ? -std::clamp ((nasEx - 1.5) * 0.6 * kq, 0.0, 5.0) : 0.0;
        eq.freqHz[2] = std::clamp (nasHz, 500.0, 2000.0);
        if (eq.gainDb[2] > -0.5) eq.gainDb[2] = 0.0;
        reason ("eq", "Nasal", eq.gainDb[2] == 0.0 ? "0 dB" : signedDb (eq.gainDb[2]) + " at " + hz (eq.freqHz[2]),
                eq.gainDb[2] == 0.0 ? "No honky / nasal peak (800 Hz - 1.6 kHz)."
                                    : "A honky, phone-like peak sits around " + hz (eq.freqHz[2]) + " (" + num (nasEx) + " dB over). Cutting it makes the voice sound fuller and less pinched.");

        // Presence and Air: boost or cut toward the style.
        double presHz = 4000.0;
        const double pres = diffAt (2500, 5000, presHz, true);
        eq.gainDb[3] = std::clamp (-pres * 0.6 * kq, -4.0, 5.0);
        eq.freqHz[3] = pres > 0.0 ? std::clamp (presHz, 2000.0, 8000.0) : (style == 4 || style == 6 || style == 7 ? 3500.0 : 4000.0);
        if (std::abs (eq.gainDb[3]) < 0.5) eq.gainDb[3] = 0.0;
        reason ("eq", "Presence", eq.gainDb[3] == 0.0 ? "0 dB" : signedDb (eq.gainDb[3]) + " at " + hz (eq.freqHz[3]),
                eq.gainDb[3] == 0.0 ? "Your words already cut through (2.5 - 5 kHz is on target)."
                : eq.gainDb[3] > 0.0 ? "Your vocal is " + num (-pres) + " dB short of " + aimed + " in the presence range, where the words live. This lifts it so lyrics are clear over the beat."
                                     : "Your vocal is " + num (pres) + " dB hotter than the target around " + hz (eq.freqHz[3]) + ", which can sound harsh or shouty on headphones. A gentle cut smooths it.");

        double airHz = 12000.0;
        const double air = diffAt (8000, 12500, airHz, true);
        double airGain = std::clamp (-air * 0.6 * kq, -3.0, 6.0);
        if (a.noiseFloorDb > -60.0) airGain = std::min (airGain, 2.0);
        if (std::abs (airGain) < 0.5) airGain = 0.0;
        eq.gainDb[4] = airGain;
        eq.freqHz[4] = intensity == 2 ? 10000.0 : 12000.0;
        reason ("eq", "Air", airGain == 0.0 ? "0 dB" : signedDb (airGain) + " above " + hz (eq.freqHz[4]),
                airGain == 0.0 ? "The top end (8 - 12.5 kHz) is already where a finished vocal sits."
                : airGain > 0.0 ? "Your vocal is " + num (-air) + " dB darker on top than " + aimed + ". Air adds the breathy, expensive sheen modern vocals have."
                                    + (a.noiseFloorDb > -60.0 ? " Capped at +2 dB because it would also lift the hiss in your recording." : "")
                                : "Your top end is " + num (air) + " dB brighter than the target, so it's eased down a little to avoid fizz.");
        }
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
        const double target = ref != nullptr && ref->sibilanceDb > -100.0 ? std::clamp (ref->sibilanceDb, -10.0, 0.0)
                                                                          : spec.sibTarget - (intensity == 2 ? 1.0 : intensity == 0 ? -1.0 : 0.0);
        if (s.levelDb <= target || s.levelDb <= -100.0)
        {
            p.deEsser.amount = 0.0;
            keep (Module::deEsser);
            reason ("deess", "Amount", "0 %", s.levelDb <= -100.0 ? "Auto-Edit heard almost no \"s\" sounds in this part, so there's nothing to tame."
                                                                  : "Your \"s\" sounds peak at " + signedDb (s.levelDb) + " vs your voice, already under the " + signedDb (target) + " " + aimed + " allows.");
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
                    " brings them to " + signedDb (after) + ", where " + aimed + " sits, and only while they happen.");
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
        double refPunchFrom = 0.0;
        if (ref != nullptr && ref->microDynDb > 0.5)
        {
            // Match the reference's punch (how far its loud moments stand over the middle): move the
            // leveler until the compressed vocal measures the same, within 0.5 - 10 dB of average squeeze.
            auto punchOf = [&] (const CompParams& q)
            {
                const auto rr = runComp (afterRider, sr, q, f.hop);
                return std::make_pair (punchDb (frames (highPass (rr.out, 80.0, sr), sr), mask), meanActive (rr.levelGr, mask));
            };
            refPunchFrom = punchDb (frames (highPass (afterRider, 80.0, sr), sr), mask);
            double tlo = -60.0, thi = 0.0;
            for (int it = 0; it < 14; ++it)
            {
                CompParams q = c;
                q.thrDb = 0.5 * (tlo + thi);
                const auto [punch, squeeze] = punchOf (q);
                if (punch > ref->microDynDb && squeeze < 10.0) thi = q.thrDb; else tlo = q.thrDb;
            }
            c.thrDb = std::round (0.5 * (tlo + thi) * 2.0) / 2.0;
        }
        const auto run = runComp (afterRider, sr, c, f.hop);
        const double before = activeRmsDb (afterRider, mask, f.hop), after = activeRmsDb (run.out, mask, f.hop);
        c.makeupDb = std::clamp (std::round ((before - after) * 2.0) / 2.0, 0.0, 18.0);
        c.mix = 100.0;
        p.comp = c;
        const double gotPeak = meanOfDeepest (run.peakGr, mask, 0.05), gotLevel = meanActive (run.levelGr, mask);
        reason ("comp", "Peak", db (c.peakThrDb), "Catches the sudden loud syllables: about " + num (gotPeak) + " dB off the loudest 5 % of moments, so nothing jumps out of the beat.");
        if (ref != nullptr && ref->microDynDb > 0.5)
            reason ("comp", "Level", db (c.thrDb) + ", " + num (c.ratio, 1) + ":1",
                    "Matched to the reference's punch: its loud moments stand " + num (ref->microDynDb) + " dB over the middle of its level (yours: " + num (refPunchFrom) +
                    " dB before compression, " + num (punchDb (frames (highPass (run.out, 80.0, sr), sr), mask)) + " dB after). That takes about " + num (gotLevel) +
                    " dB of smoothing on average" + (gotLevel > 9.5 ? ", the most Auto-Edit allows: the reference is squeezed harder than that." : "."));
        else
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
            reason ("double", "Amount", "Off", std::string (kStyleNames[static_cast<size_t> (style)]) + " vocals stay single and centred, " +
                    (style >= 6 ? "so it sounds like one real voice, close to the listener." : "so every word hits hard in the middle."));
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
        if (p.delay.mix < 1.0)
        {
            p.delay.mix = 0.0;
            keep (Module::delay);
            reason ("delay", "Mix", "Off", std::string ("No echo for ") + kStyleNames[static_cast<size_t> (style)] + ": the small room of the reverb is all the space it needs, so the voice stays natural and close.");
        }
        else
        {
        reason ("delay", "Time", std::string (kDelayNames[static_cast<size_t> (spec.dlDiv)]) + " at " + num (p.delay.bpm, 0) + " BPM",
                std::string ("Echoes on the beat grid fill the gaps between lines. Duck ") + pct (spec.dlDuck) + " keeps them quiet while you're singing or rapping, so words stay clear."
                + (settings.bpm > 0.0 ? "" : " Your session tempo wasn't available, so 120 BPM is used: it locks to Cubase's tempo once playback runs."));
        reason ("delay", "Mix", pct (p.delay.mix) + ", Feedback " + pct (p.delay.feedback), "Low enough to feel, not hear as a separate echo" + std::string (spec.dlPing ? "; Ping-Pong bounces the repeats left / right." : "."));
        }

        p.reverb.decayS = spec.rvDecay;
        p.reverb.predelayMs = spec.rvPre;
        p.reverb.mix = std::round (spec.rvMix * ks);
        p.reverb.toneHz = spec.rvTone;
        p.reverb.duck = spec.rvDuck;
        reason ("reverb", "Decay", num (spec.rvDecay) + " s, Pre-delay " + std::to_string (static_cast<int> (spec.rvPre)) + " ms",
                std::string ("A ") + (spec.rvDecay < 1.2 ? "short room" : spec.rvDecay < 2.0 ? "medium plate" : "long, lush plate") +
                    " that gives the vocal a place to live. The pre-delay keeps the start of each word dry and clear.");
        if (ref != nullptr && ref->tailDb > -100.0)
        {
            // Match the reference's space: what rings on after a phrase ends, measured the same way on
            // the finished chain while the reverb mix moves.
            auto tailWith = [&] (const ChainParams& q) { return spaceDb (frames (highPass (renderMono (raw, sr, q), 80.0, sr), sr)); };
            ChainParams q = p;
            q.reverb.mix = 0.0;
            double dry = tailWith (q);
            if (dry > ref->tailDb + 2.0 && q.delay.mix > 0.0) { p.delay.mix = q.delay.mix = 0.0; dry = tailWith (q); }
            if (dry >= ref->tailDb - 1.0 || dry <= -100.0)
                p.reverb.mix = 0.0;
            else
            {
                double mlo = 0.0, mhi = 60.0;
                for (int it = 0; it < 8; ++it)
                {
                    q.reverb.mix = 0.5 * (mlo + mhi);
                    if (tailWith (q) < ref->tailDb) mlo = q.reverb.mix; else mhi = q.reverb.mix;
                }
                p.reverb.mix = std::round (0.5 * (mlo + mhi));
            }
            reason ("reverb", "Mix", pct (p.reverb.mix),
                    p.reverb.mix == 0.0 ? "The reference is about as dry as your recording already is (what rings on after its phrases sits " + num (-ref->tailDb, 0) +
                                              " dB down), so no reverb is added" + std::string (p.delay.mix == 0.0 ? " and the delay is off too." : ".")
                                        : "Matched to the reference's space: after its phrases end, " + num (-ref->tailDb, 0) + " dB of tail rings on. " + pct (p.reverb.mix) +
                                              " reverb gives your vocal the same, measured on the finished chain.");
        }
        else
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
    if (ref != nullptr)
    {
        r.summary = "Listened to " + num (a.voicedSeconds) + " s of voice and matched it to your reference, \"" + ref->name + "\": its tone, how bright its \"s\" sounds are, its punch and its space. "
                    "Your vocal came in at " + num (a.inputLufs) + " LUFS with peaks at " + db (a.peakDb) + ". Pitch, clean-up and saturation still follow " +
                    kStyleNames[static_cast<size_t> (style)] + ". Every change is explained below.";
        if (! ref->warning.empty())
            r.notes.push_back ("REFERENCE IS A FULL SONG: " + ref->warning +
                               "\nNEED: Yes, for a close match. It still works, roughly."
                               "\nSTEP: Search for the song's acapella (\"<song name> acapella\") or ask the producer for the vocal stem."
                               "\nSTEP: Load that as the reference (REF in the header) and run Auto-Edit again.");
    }
    r.params = p;
    r.ok = true;
    return r;
}

} // namespace vox
