// Cleanup audit: Auto-Edit's Cleanup settings for a vocal, then the low cut, gate, plosive remover and
// breath control run on it the way the chain runs them (pops / breaths listen to the input, the audio is
// Pitch's latency behind), and what each one did to the words.
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/cleanup_audit.cpp build-t/dsp/libvox_dsp.a -lpthread -o cleanup_audit
// Run:   cleanup_audit in.f64 style [intensity]   (raw doubles, mono 48 kHz; style 0..7 as kStyleNames)
#include "vox/AutoEdit.h"
#include "vox/PitchCorrector.h"
#include "vox/PopBreath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace vox;

static double db (double e) { return 10.0 * std::log10 (e + 1.0e-20); }

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: cleanup_audit in.f64 style [intensity]\n"); return 1; }
    const double sr = 48000.0;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<double> x;
    double v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (v);
    const int n = static_cast<int> (x.size());

    AutoEditSettings s;
    s.style = std::atoi (argv[2]);
    s.intensity = argc > 3 ? std::atoi (argv[3]) : 1;
    std::vector<std::vector<float>> audio (1, std::vector<float> (x.begin(), x.end()));
    const auto r = autoEdit (audio, sr, s);
    const auto& c = r.params.cleanup;
    const auto& a = r.analysis;
    std::printf ("%s  style %s\n", argv[1], kStyleNames[static_cast<size_t> (s.style)]);
    std::printf ("analysis: voice %.1f dB rms, noise floor %.1f, noise peak %.1f, quiet word peak %.1f, gaps %d, f0 low %.0f Hz median %.0f, rumble %.1f\n",
                 a.voiceRmsDb, a.noiseFloorDb, a.noisePeakDb, a.quietWordPeakDb, a.heardGaps, a.f0Low, a.f0Median, a.rumbleDb);
    std::printf ("settings: low cut %.0f Hz, gate %s (thr %.1f, range %.1f), pops %.0f %%, breaths %.0f dB\n",
                 c.lowCutHz, c.gateRangeDb > 0.05 ? "on" : "off", c.gateThrDb, c.gateRangeDb, c.popAmount, c.breathDb);
    for (const auto& rs : r.reasons) if (rs.module == "cleanup") std::printf ("  [%s] %s\n", rs.control.c_str(), rs.value.c_str());

    PitchCorrector pc; pc.prepare (sr, 1);
    const int L = pc.latencySamples();

    // Stage 1: low cut only, then gate only (per sample, to read its gain).
    std::vector<double> lc (x), gated, gateDb (static_cast<size_t> (n), 0.0);
    {
        Cleanup cl; cl.prepare (sr, 1);
        CleanupParams p = c; p.gateRangeDb = 0.0; cl.setParams (p);
        double* ptr = lc.data(); cl.process (&ptr, 1, n);
    }
    // The gate listens to the input (lc here), the audio it turns down is L samples behind; results shifted back.
    gated.assign (static_cast<size_t> (n), 0.0);
    {
        Cleanup g; g.prepare (sr, 1, L, 1);
        CleanupParams p = c; p.lowCutHz = 20.0; g.setParams (p);
        std::vector<double> d (static_cast<size_t> (n + L), 0.0), gd (static_cast<size_t> (n + L), 0.0);
        for (int i = 0; i < n; ++i) d[static_cast<size_t> (i + L)] = lc[static_cast<size_t> (i)];
        const double zero = 0.0;
        for (int i = 0; i < n + L; ++i)
        {
            const double* lp = i < n ? lc.data() + i : &zero;
            if (std::getenv ("NO_LOOKAHEAD") == nullptr || *std::getenv ("NO_LOOKAHEAD") == 0) g.listen (&lp, 1, 1);
            double* ptr = d.data() + i; g.process (&ptr, 1, 1); gd[static_cast<size_t> (i)] = g.takeGateDb();
        }
        for (int i = 0; i < n; ++i) { gated[static_cast<size_t> (i)] = d[static_cast<size_t> (i + L)]; gateDb[static_cast<size_t> (i)] = gd[static_cast<size_t> (i + L)]; }
    }
    // Stage 2: pops then breaths, side-chain = the input, audio L samples behind.
    std::vector<double> sc (x), aud (static_cast<size_t> (n), 0.0), popDb (static_cast<size_t> (n), 0.0), brDb (static_cast<size_t> (n), 0.0);
    for (int i = L; i < n; ++i) aud[static_cast<size_t> (i)] = gated[static_cast<size_t> (i - L)];
    std::vector<double> afterPop;
    {
        PopRemover pr; pr.prepare (sr, 1, L); pr.setParams ({ c.popAmount });
        BreathControl bc; bc.prepare (sr, 1, L); bc.setParams ({ c.breathDb });
        for (int i = 0; i < n; ++i)
        {
            double* ptr = aud.data() + i;
            pr.process (&ptr, 1, 1, sc.data() + i);
            popDb[static_cast<size_t> (i)] = pr.currentCutDb();
            bc.process (&ptr, 1, 1, sc.data() + i);
            brDb[static_cast<size_t> (i)] = bc.currentGainDb();
        }
    }

    // Frames of 10 ms on the (aligned) input after the low cut: level, voiced (autocorrelation clarity).
    const int F = 480;
    const int nf = n / F - 4;
    std::vector<double> lev (static_cast<size_t> (nf)), clar (static_cast<size_t> (nf));
    for (int f = 0; f < nf; ++f)
    {
        const double* q = lc.data() + f * F;
        double e = 0; for (int i = 0; i < F; ++i) e += q[i] * q[i];
        lev[static_cast<size_t> (f)] = db (e / F);
        // clarity on a 30 ms window, lags 75 - 500 Hz
        const int W = 1440; double best = 0;
        if (f * F + W + 640 < n)
            for (int lag = 96; lag <= 640; lag += 1)
            {
                double xy = 0, xx = 0, yy = 0;
                for (int i = 0; i < W; i += 2) { xy += q[i] * q[i + lag]; xx += q[i] * q[i]; yy += q[i + lag] * q[i + lag]; }
                best = std::max (best, xy / std::sqrt (xx * yy + 1e-30));
            }
        clar[static_cast<size_t> (f)] = best;
    }
    std::vector<double> sorted;
    for (double l : lev) if (l > -70) sorted.push_back (l);
    std::sort (sorted.begin(), sorted.end());
    const double voice = sorted.empty() ? -30 : sorted[sorted.size() * 7 / 10];
    std::printf ("frame voice level (P70) %.1f dB\n", voice);

    auto frameMin = [&] (const std::vector<double>& g, int f, int shift) { double m = 0; for (int i = 0; i < F; ++i) m = std::min (m, g[static_cast<size_t> (f * F + i + shift)]); return m; };

    // Gate: words (within 30 dB of the voice) turned down; openings: loss in the first 20 ms of words.
    if (c.gateRangeDb > 0.05)
    {
        int wordFrames = 0, wordCut = 0, voicedFrames = 0, voicedCut = 0, opens = 0;
        double eIn = 0, eOut = 0;
        for (int f = 0; f < nf; ++f)
        {
            const double gmin = frameMin (gateDb, f, 0);
            const bool word = lev[static_cast<size_t> (f)] > voice - 30;
            const bool voiced = clar[static_cast<size_t> (f)] > 0.7 && lev[static_cast<size_t> (f)] > voice - 40;
            if (word) { ++wordFrames; if (gmin < -1) ++wordCut; }
            if (voiced) { ++voicedFrames; if (gmin < -1) ++voicedCut; }
            if (word)
                for (int i = 0; i < F; ++i) { const double s0 = lc[static_cast<size_t> (f * F + i)], s1 = gated[static_cast<size_t> (f * F + i)]; eIn += s0 * s0; eOut += s1 * s1; }
        }
        // soft starts: a frame over voice-30 after >= 100 ms of gate closed: energy lost in its first 30 ms
        std::vector<double> starts, lags, vstarts;
        for (int f = 10; f < nf - 3; ++f)
        {
            bool closedBefore = true;
            for (int k = 1; k <= 10; ++k) if (frameMin (gateDb, f - k, 0) > -c.gateRangeDb + 1) closedBefore = false;
            if (! closedBefore || lev[static_cast<size_t> (f)] <= voice - 30) continue;
            // the word itself: walk back while the frames stay within 30 dB of the voice
            int b = f; while (b > f - 10 && lev[static_cast<size_t> (b - 1)] > voice - 30) --b;
            // a voiced start, if there is one (else the rise may be a breath, which the gate should turn down)
            int vb = -1; for (int k = b; k <= f + 3; ++k) if (clar[static_cast<size_t> (k)] > 0.7) { vb = k; break; }
            if (vb >= 0)
            {
                double e0 = 0, e1 = 0;
                for (int i = vb * F; i < (vb + 3) * F; ++i) { e0 += lc[static_cast<size_t> (i)] * lc[static_cast<size_t> (i)]; e1 += gated[static_cast<size_t> (i)] * gated[static_cast<size_t> (i)]; }
                vstarts.push_back (db (e1) - db (e0));
                if (std::getenv ("SHOW") != nullptr) std::printf ("    start %.2f s: rise from %.2f, voiced at %.2f, lost %.1f dB (voiced 30 ms)\n", f * F / sr, b * F / sr, vb * F / sr, db (e1) - db (e0));
            }
            int lag = 0; while (lag < 100 * F && gateDb[static_cast<size_t> (b * F + lag)] < -1) ++lag;
            lags.push_back (1000.0 * lag / sr);
            double e0 = 0, e1 = 0;
            for (int i = b * F; i < (f + 3) * F; ++i) { e0 += lc[static_cast<size_t> (i)] * lc[static_cast<size_t> (i)]; e1 += gated[static_cast<size_t> (i)] * gated[static_cast<size_t> (i)]; }
            starts.push_back (db (e1) - db (e0));
            if (std::getenv ("SHOW") != nullptr)
            {
                std::printf ("    rise %.2f s (loud at %.2f), lost %.1f dB, open after %.0f ms; frames lev/clar:", b * F / sr, f * F / sr, db (e1) - db (e0), lags.back());
                for (int k = b; k <= std::min (f + 4, b + 14); ++k) std::printf (" %.0f/%.2f", lev[static_cast<size_t> (k)], clar[static_cast<size_t> (k)]);
                std::printf ("\n");
            }
            ++opens;
            f += 10;
        }
        std::sort (starts.begin(), starts.end());
        std::printf ("GATE: word frames turned down %d / %d (%.1f %%), voiced frames %d / %d; words' energy lost %.2f dB\n",
                     wordCut, wordFrames, 100.0 * wordCut / std::max (1, wordFrames), voicedCut, voicedFrames, db (eOut) - db (eIn));
        if (! starts.empty())
            std::printf ("      %d phrase starts: start energy lost median %.1f dB, worst %.1f dB, %d worse than -3 dB\n", opens,
                         starts[starts.size() / 2], starts.front(), static_cast<int> (std::count_if (starts.begin(), starts.end(), [] (double d) { return d < -3; })));
        std::sort (vstarts.begin(), vstarts.end());
        if (! vstarts.empty()) std::printf ("      %d voiced starts: first 30 ms of voice lost median %.1f dB, worst %.1f dB, %d worse than -3 dB\n", static_cast<int> (vstarts.size()),
                                            vstarts[vstarts.size() / 2], vstarts.front(), static_cast<int> (std::count_if (vstarts.begin(), vstarts.end(), [] (double d) { return d < -3; })));
        std::sort (lags.begin(), lags.end());
        if (! lags.empty()) std::printf ("      gate fully open after the word starts: median %.0f ms, worst %.0f ms\n", lags[lags.size() / 2], lags.back());
    }

    // Pops: events (cut > 3 dB), depth, and whether they land on voiced frames.
    // lowLev: the input under 100 Hz (10 ms rms, dB), to see if a real thump is there.
    std::vector<double> lowLev (static_cast<size_t> (nf), -200.0);
    {
        Biquad b0, b1; design::apply (b0, design::butterworth (false, 100.0, sr)); design::apply (b1, design::butterworth (false, 100.0, sr));
        std::vector<double> lo (x); for (auto& s0 : lo) s0 = b1.process (b0.process (s0));
        for (int f = 0; f < nf; ++f) { double e = 0; for (int i = 0; i < F; ++i) e += lo[static_cast<size_t> (f * F + i)] * lo[static_cast<size_t> (f * F + i)]; lowLev[static_cast<size_t> (f)] = db (e / F); }
    }
    std::vector<double> vl; for (int f = 0; f < nf; ++f) if (clar[static_cast<size_t> (f)] > 0.7 && lev[static_cast<size_t> (f)] > voice - 20) vl.push_back (lowLev[static_cast<size_t> (f)] - lev[static_cast<size_t> (f)]);
    std::sort (vl.begin(), vl.end());
    const double lowNorm = vl.empty() ? -30 : vl[vl.size() / 2];
    std::printf ("voiced frames: under-100 Hz vs whole, median %.1f dB\n", lowNorm);
    {
        int events = 0, onVoiced = 0; double deepest = 0; bool inE = false; int start = 0; double dur = 0, evDeep = 0; int over6 = 0; double over6Tot = 0; int noThump = 0;
        for (int i = 0; i < n; ++i)
        {
            const double cdb = popDb[static_cast<size_t> (i)];
            if (cdb > 3 && ! inE) { inE = true; start = i; ++events; }
            if (inE) { deepest = std::max (deepest, cdb); evDeep = std::max (evDeep, cdb); if (cdb > 6) ++over6; }
            if (cdb > 6)
            {
                // cut while the input (lined up) has no thump: low band within 10 dB of its voiced normal
                const int fa = std::clamp ((i - L) / F, 0, nf - 1);
                if (lowLev[static_cast<size_t> (fa)] - lev[static_cast<size_t> (fa)] < lowNorm + 10 && lev[static_cast<size_t> (fa)] > voice - 30) ++noThump;
            }
            if (inE && cdb < 0.5)
            {
                inE = false; dur += (i - start) / sr;
                const int f = std::clamp ((start - L) / F, 0, nf - 1);
                const bool v = clar[static_cast<size_t> (f)] > 0.7 && lev[static_cast<size_t> (f)] > voice - 20;
                if (v) ++onVoiced;
                double lowPk = -200, levAt = -200; const int fe = std::min (nf - 1, (i - L) / F);
                for (int ff = f; ff <= fe; ++ff) if (lowLev[static_cast<size_t> (ff)] - lev[static_cast<size_t> (ff)] > lowPk) { lowPk = lowLev[static_cast<size_t> (ff)] - lev[static_cast<size_t> (ff)]; levAt = lev[static_cast<size_t> (ff)]; }
                std::printf ("  pop at %.2f s: %.0f ms (%.0f ms > 6 dB), cut %.1f dB, low/whole peak %+.1f dB (voiced norm %+.1f) at %.1f dB%s\n", (start - L) / sr, 1000.0 * (i - start) / sr,
                             1000.0 * over6 / sr, evDeep, lowPk, lowNorm, levAt, v ? " (voiced frame)" : "");
                over6Tot += over6 / sr; over6 = 0; evDeep = 0;
            }
        }
        std::printf ("POPS: %d events, deepest %.1f dB, avg length %.0f ms (%.0f ms > 6 dB), %d on clearly voiced frames; > 6 dB cut on sound with no thump: %.2f s\n", events, deepest, events ? 1000 * dur / events : 0.0, events ? 1000 * over6Tot / events : 0.0, onVoiced, noThump / sr);
    }
    // Breaths: events (gain < -1 dB), how many overlap voiced frames.
    if (c.breathDb > 0.05)
    {
        int events = 0, voicedHit = 0, voicedFramesDucked = 0; bool inE = false; int start = 0; double tot = 0;
        for (int i = 0; i < n; ++i)
        {
            const bool d = brDb[static_cast<size_t> (i)] < -1;
            if (d && ! inE) { inE = true; start = i; ++events; }
            if (inE && ! d)
            {
                inE = false; tot += (i - start) / sr;
                int vf = 0, fr = 0;
                for (int f = (start - L) / F; f < (i - L) / F && f < nf; ++f)
                {
                    if (f < 0) continue;
                    ++fr;
                    if (clar[static_cast<size_t> (f)] > 0.8 && lev[static_cast<size_t> (f)] > voice - 15) ++vf;
                }
                voicedFramesDucked += vf;
                if (vf >= 2) { ++voicedHit; std::printf ("  breath duck over voice at %.2f s, %d of %d frames voiced+loud\n", (start - L) / sr, vf, fr); }
            }
        }
        std::printf ("BREATHS: %d ducks, %.1f s total, %d overlap a loud voiced sound (%d frames)\n", events, tot, voicedHit, voicedFramesDucked);
    }
    return 0;
}
