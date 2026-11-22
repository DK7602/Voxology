#include "vox/HoneyTune.h"
#include "vox/Crepe.h"
#include "vox/Modules.h"

#include <thread>

#include <numbers>

#include <algorithm>
#include <array>
#include <cmath>

namespace vox::honey {

namespace {

double median (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end());
    return v[v.size() / 2];
}

/** The plan the live shifter follows: for any input time, the period and the shift. */
class Plan final : public PitchCorrector::Guide
{
public:
    /** offset: the clip sample the shifter's first input sample is (a re-render that starts mid-clip). */
    Plan (const Track& t, const std::vector<double>& shifts, double offset = 0.0, const std::vector<double>* formants = nullptr)
        : track (t), shift (shifts), start (offset), formant (formants) {}

    double formantAt (double shifterTime) const override
    {
        if (formant == nullptr || track.time.empty()) return 0.0;
        const double time = shifterTime + start;
        const auto it = std::lower_bound (track.time.begin(), track.time.end(), time);
        const auto i = static_cast<size_t> (std::clamp<long> (it - track.time.begin(), 0, static_cast<long> (track.time.size()) - 1));
        return (*formant)[i];
    }

    void at (double shifterTime, double& period, double& shiftSemis) const override
    {
        const double time = shifterTime + start;
        period = 0.0; shiftSemis = 0.0;
        const auto& tm = track.time;
        if (tm.empty() || time < tm.front() || time > tm.back()) return;
        const auto it = std::lower_bound (tm.begin(), tm.end(), time);
        const auto i1 = static_cast<size_t> (std::max<long> (1, it - tm.begin()));
        const size_t i0 = i1 - 1;
        const double m0 = track.midi[i0], m1 = track.midi[std::min (i1, tm.size() - 1)];
        if (m0 <= 0.0 || m1 <= 0.0)
        {
            // Edge of a note: use the nearer side, so a note's last grains keep its pitch.
            const double m = (time - tm[i0] < tm[std::min (i1, tm.size() - 1)] - time) ? m0 : m1;
            const size_t k = (m == m0) ? i0 : std::min (i1, tm.size() - 1);
            if (m <= 0.0) return;
            period = track.sampleRate / (440.0 * std::pow (2.0, (m - 69.0) / 12.0));
            shiftSemis = shift[k];
            return;
        }
        const double x = std::clamp ((time - tm[i0]) / std::max (1.0, tm[std::min (i1, tm.size() - 1)] - tm[i0]), 0.0, 1.0);
        const double m = m0 + (m1 - m0) * x;
        period = track.sampleRate / (440.0 * std::pow (2.0, (m - 69.0) / 12.0));
        shiftSemis = shift[i0] + (shift[std::min (i1, tm.size() - 1)] - shift[i0]) * x;
    }

private:
    const Track& track;
    const std::vector<double>& shift;
    double start = 0.0;
    const std::vector<double>* formant = nullptr;
};

} // namespace

// ------------------------------------------------------------------------------------------------
void aiCheck (Track& t, const std::vector<float>& mono, double sr, const std::atomic<bool>* cancel)
{
    const size_t n = t.midi.size();
    if (n == 0 || mono.empty()) return;
    const auto x = crepe::resampleTo16k (mono, sr);
    const double hop = 0.03;   // seconds between checks
    // Loud enough: within 40 dB of the clip's loudest moment.
    std::vector<double> times, levels;
    for (double ts = 0.04; (ts + 0.04) * crepe::kRate < static_cast<double> (x.size()); ts += hop)
    {
        const auto c = static_cast<size_t> (ts * crepe::kRate);
        double e = 0.0;
        for (size_t i = c - 512; i < c + 512; ++i) e += static_cast<double> (x[i]) * x[i];
        times.push_back (ts);
        levels.push_back (10.0 * std::log10 (e / 1024.0 + 1.0e-20));
    }
    if (times.empty()) return;
    const double loudest = *std::max_element (levels.begin(), levels.end());
    std::vector<size_t> todo;
    for (size_t k = 0; k < times.size(); ++k) if (levels[k] > loudest - 40.0) todo.push_back (k);
    std::vector<crepe::Estimate> est (times.size());
    // Half the cores at most (the DAW and its audio come first), each taking every n-th check. A thread that can't be
    // started leaves its share to this one (never an exception out of here).
    const unsigned threads = std::max (1u, std::min (8u, std::thread::hardware_concurrency() / 2));
    auto work = [&] (unsigned w, unsigned of)
    {
        crepe::Tiny net;
        for (size_t j = w; j < todo.size(); j += of)
        {
            if (cancel != nullptr && cancel->load (std::memory_order_relaxed)) return;
            const auto c = static_cast<size_t> (times[todo[j]] * crepe::kRate);
            est[todo[j]] = net.run (x.data() + c - 512);
        }
    };
    std::vector<std::thread> pool;
    std::vector<unsigned> mine { 0 };   // the shares this thread works itself
    for (unsigned w = 1; w < threads; ++w)
    {
        try { pool.emplace_back (work, w, threads); }
        catch (...) { mine.push_back (w); }
    }
    for (unsigned w : mine) work (w, threads);
    for (auto& th : pool) th.join();
    t.aiChecked = static_cast<int> (todo.size());

    // The detector's reading nearest each check (index into the track).
    auto nearest = [&] (double seconds)
    {
        const double tin = seconds * sr;
        const auto it = std::lower_bound (t.time.begin(), t.time.end(), tin);
        auto i = static_cast<size_t> (std::clamp<long> (it - t.time.begin(), 0, static_cast<long> (n) - 1));
        if (i > 0 && std::abs (t.time[i - 1] - tin) < std::abs (t.time[i] - tin)) --i;
        return i;
    };
    std::vector<double> ym (times.size(), 0.0), am (times.size(), 0.0);
    for (size_t k = 0; k < times.size(); ++k)
    {
        ym[k] = t.midi[nearest (times[k])];
        if (est[k].confidence > 0.6 && est[k].hz > 0.0) am[k] = 69.0 + 12.0 * std::log2 (est[k].hz / 440.0);
    }
    auto octaveApart = [] (double a, double b) { const double d = std::abs (a - b); return std::abs (d - 12.0) < 0.7 || std::abs (d - 24.0) < 0.7; };
    // Apply a decision to the readings within half a hop of check k.
    auto apply = [&] (size_t k, auto&& fn)
    {
        const double a = (times[k] - 0.5 * hop) * sr, b = (times[k] + 0.5 * hop) * sr;
        for (auto i = static_cast<size_t> (std::lower_bound (t.time.begin(), t.time.end(), a) - t.time.begin()); i < n && t.time[i] < b; ++i) fn (i);
    };
    for (size_t k = 0; k < times.size(); ++k)
    {
        if (am[k] <= 0.0) continue;
        if (ym[k] > 0.0 && octaveApart (ym[k], am[k]))
        {
            // Context: the moments around (+-120 ms) where both agree.
            std::vector<double> agree;
            for (size_t j = (k > 4 ? k - 4 : 0); j < std::min (times.size(), k + 5); ++j)
                if (j != k && am[j] > 0.0 && ym[j] > 0.0 && std::abs (am[j] - ym[j]) < 0.5) agree.push_back (am[j]);
            bool aiWins = est[k].confidence > 0.8;
            if (! agree.empty())
            {
                const double m = median (agree);
                aiWins = std::abs (am[k] - m) < std::abs (ym[k] - m);
            }
            if (! aiWins) continue;
            const double shift = 12.0 * std::round ((am[k] - ym[k]) / 12.0);
            apply (k, [&] (size_t i) { if (t.midi[i] > 0.0) t.midi[i] += shift; });
            ++t.aiFixed;
        }
        else if (ym[k] <= 0.0 && est[k].confidence > 0.75 && levels[k] > loudest - 20.0)
        {
            // A loud moment (within 20 dB of the loudest: not a note's fading tail) the detector skipped (rasp,
            // grit) that the AI is sure is a note.
            apply (k, [&] (size_t i) { if (t.midi[i] <= 0.0) { t.midi[i] = am[k]; t.clarity[i] = 0.5 * est[k].confidence; } });
            ++t.aiFound;
        }
    }
}

float levelOver (const Track& t, double a, double b)
{
    if (t.level.empty()) return 0.0f;
    const double hop = Track::kLevelHopSeconds;
    const auto last = static_cast<long> (t.level.size()) - 1;
    const auto i0 = static_cast<long> (std::floor (std::min (a, b) / hop)), i1 = static_cast<long> (std::floor (std::max (a, b) / hop));
    if (i1 < 0 || i0 > last) return 0.0f;
    float peak = 0.0f;
    for (long i = std::max (0L, i0); i <= std::min (last, i1); ++i) peak = std::max (peak, t.level[static_cast<size_t> (i)]);
    return peak;
}

float rmsOver (const Track& t, double a, double b)
{
    if (t.power.empty()) return 0.0f;
    const double hop = Track::kLevelHopSeconds;
    const auto last = static_cast<long> (t.power.size()) - 1;
    const auto i0 = static_cast<long> (std::floor (std::min (a, b) / hop)), i1 = static_cast<long> (std::floor (std::max (a, b) / hop));
    if (i1 < 0 || i0 > last) return 0.0f;
    double sum = 0.0;
    long count = 0;
    for (long i = std::max (0L, i0); i <= std::min (last, i1); ++i, ++count) sum += t.power[static_cast<size_t> (i)];
    return count > 0 ? static_cast<float> (std::sqrt (sum / static_cast<double> (count))) : 0.0f;
}

namespace {
/** The waveform (Track::level, Track::power) and how loud the singing gets (levelTop: 98th percentile where a pitch was heard). */
void measureLevel (Track& t, const std::vector<float>& mono, double sr)
{
    const auto hop = std::max<size_t> (1, static_cast<size_t> (std::lround (Track::kLevelHopSeconds * sr)));
    const size_t steps = (mono.size() + hop - 1) / hop;
    t.level.assign (steps, 0.0f);
    t.power.assign (steps, 0.0f);
    for (size_t k = 0; k < steps; ++k)
    {
        float peak = 0.0f;
        double sum = 0.0;
        const size_t a = k * hop, b = std::min (mono.size(), a + hop);
        for (size_t i = a; i < b; ++i)
        {
            peak = std::max (peak, std::abs (mono[i]));
            sum += static_cast<double> (mono[i]) * mono[i];
        }
        t.level[k] = peak;
        t.power[k] = static_cast<float> (sum / static_cast<double> (b - a));
    }
    std::vector<float> sung;
    for (size_t r = 0; r < t.midi.size(); ++r)
        if (t.midi[r] > 0.0)
        {
            const auto k = static_cast<size_t> (t.time[r] / sr / Track::kLevelHopSeconds);
            if (k < t.level.size()) sung.push_back (t.level[k]);
        }
    if (sung.empty()) sung = t.level;
    if (sung.empty()) return;
    const auto k = static_cast<long> (0.98 * static_cast<double> (sung.size() - 1));
    std::nth_element (sung.begin(), sung.begin() + k, sung.end());
    t.levelTop = sung[static_cast<size_t> (k)];
}
}

namespace {
/** The detector says "sung" only once its window is full of voice and two readings agree: 20 - 35 ms after the voice
    starts (more on low voices), and it lets go before the release has faded. Those moments belong to the note: moved
    with it, its attack sounds at the new pitch (it used to start at the old pitch and scoop in, and slide back at its
    end). Each sung run reaches back up to 50 ms to where the voice starts and on up to 40 ms into its release, while
    the sound stays within 30 dB of the note and is voice, not noise (it repeats at about the note's period, or crosses
    zero no faster than a voice: an "s" does neither), at the run's first / last pitch. (Audit 3, AUDIT-ENGINE.md #1 / #6: a moved note's first 50 ms 65 - 148 cents off -> a few cents.) */
void extendToOnsets (Track& t, const std::vector<float>& mono, double sr)
{
    const size_t n = t.midi.size();
    if (n < 2 || mono.empty()) return;
    const auto w = static_cast<long> (0.005 * sr);   // +-5 ms around a moment
    auto level = [&] (double c)
    {
        const long a = std::max (0L, static_cast<long> (c) - w), b = std::min (static_cast<long> (mono.size()), static_cast<long> (c) + w);
        double e = 0.0;
        for (long i = a; i < b; ++i) e += static_cast<double> (mono[static_cast<size_t> (i)]) * mono[static_cast<size_t> (i)];
        return 10.0 * std::log10 (e / static_cast<double> (std::max (1L, b - a)) + 1.0e-20);
    };
    auto crossings = [&] (double c)   // zero crossings per second
    {
        const long a = std::max (1L, static_cast<long> (c) - w), b = std::min (static_cast<long> (mono.size()), static_cast<long> (c) + w);
        int z = 0;
        for (long i = a; i < b; ++i)
            if ((mono[static_cast<size_t> (i)] >= 0.0f) != (mono[static_cast<size_t> (i - 1)] >= 0.0f)) ++z;
        return z * sr / static_cast<double> (std::max (1L, b - a));
    };
    // Gaps between sung runs (readings): the onset takes up to 60 % of a gap, the release what's left of the rest, and
    // 10 ms always stay unsung (two notes never run into one).
    // Voice, not noise: the waveform repeats at about the note's period here (a scoop can be a semitone or two off), or
    // it crosses zero no more often than a voice does. (Bright real voices cross zero a lot: the repeat test catches them.)
    auto repeats = [&] (double c, double midi)
    {
        if (midi <= 0.0) return false;
        const double P = sr / (440.0 * std::pow (2.0, (midi - 69.0) / 12.0));
        const auto len = static_cast<long> (std::max (2.0 * P, 0.005 * sr));
        const long a = static_cast<long> (c) - len / 2;
        if (a < 0 || a + len + static_cast<long> (1.25 * P) + 2 >= static_cast<long> (mono.size())) return false;
        double best = 0.0;
        for (double lag = 0.88 * P; lag <= 1.14 * P; lag += std::max (1.0, 0.02 * P))
        {
            const auto L = static_cast<long> (std::lround (lag));
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (long i = a; i < a + len; ++i)
            {
                const double u = mono[static_cast<size_t> (i)], v = mono[static_cast<size_t> (i + L)];
                xy += u * v; xx += u * u; yy += v * v;
            }
            best = std::max (best, xy / std::sqrt (xx * yy + 1.0e-30));
        }
        return best > 0.6;
    };
    auto voiceLike = [&] (double c, double midi) { return crossings (c) <= 3500.0 || repeats (c, midi); };
    const auto sung = t.midi;
    t.filled.assign (n, 0);
    const double hop = n > 1 ? (t.time.back() - t.time.front()) / static_cast<double> (n - 1) : 128.0;
    const auto keep = static_cast<size_t> (std::ceil (0.01 * sr / hop));
    size_t i = 0;
    while (i < n && ! (sung[i] > 0.0)) ++i;
    // (the first run's onset: the gap is everything before it)
    size_t prevEnd = 0;   // one past the last run's end
    bool first = true;
    while (i < n)
    {
        size_t runEnd = i;
        while (runEnd < n && sung[runEnd] > 0.0) ++runEnd;
        if (t.time[runEnd - 1] - t.time[i] < 0.05 * sr)
        {
            // Too short to be a note (a stray reading in a breath, a ringing decay): left as it is, not grown into one.
            i = runEnd;
            while (i < n && ! (sung[i] > 0.0)) ++i;
            continue;
        }
        // Onset: back from i.
        const size_t gapStart = first ? 0 : prevEnd;
        const size_t gap = i - gapStart;
        const size_t usable = first ? gap : (gap > keep ? gap - keep : 0);
        const size_t onsetMax = first ? usable : static_cast<size_t> (0.6 * static_cast<double> (usable));
        size_t onset = 0;
        if (i > 0)
        {
            const double ref = level (t.time[i] + 0.01 * sr);
            for (size_t k = i; k-- > gapStart && onset < onsetMax;)
            {
                if (t.time[i] - t.time[k] > 0.05 * sr) break;
                if (level (t.time[k]) < ref - 30.0 || ! voiceLike (t.time[k], sung[i])) break;
                t.midi[k] = sung[i];
                t.clarity[k] = t.clarity[i];
                t.filled[k] = 1;
                ++onset;
            }
        }
        // Release of the run before, into what's left of the gap.
        if (! first)
        {
            const size_t releaseMax = usable > onset ? usable - onset : 0;
            const double ref = level (t.time[prevEnd - 1] - 0.01 * sr);
            size_t rel = 0;
            for (size_t k = prevEnd; k < i - onset && rel < releaseMax; ++k, ++rel)
            {
                if (t.time[k] - t.time[prevEnd - 1] > 0.04 * sr) break;
                if (level (t.time[k]) < ref - 30.0 || ! voiceLike (t.time[k], sung[prevEnd - 1])) break;
                t.midi[k] = sung[prevEnd - 1];
                t.clarity[k] = t.clarity[prevEnd - 1];
                t.filled[k] = 1;
            }
        }
        prevEnd = runEnd;
        first = false;
        i = runEnd;
        while (i < n && ! (sung[i] > 0.0)) ++i;
    }
    // The last run's release (nothing after it).
    if (! first && prevEnd > 0 && prevEnd < n)
    {
        const double ref = level (t.time[prevEnd - 1] - 0.01 * sr);
        for (size_t k = prevEnd; k < n; ++k)
        {
            if (t.time[k] - t.time[prevEnd - 1] > 0.04 * sr) break;
            if (level (t.time[k]) < ref - 30.0 || ! voiceLike (t.time[k], sung[prevEnd - 1])) break;
            t.midi[k] = sung[prevEnd - 1];
            t.clarity[k] = t.clarity[prevEnd - 1];
            t.filled[k] = 1;
        }
    }
}
}

Track analyse (const std::vector<float>& mono, double sr, bool ai, const std::atomic<bool>* cancel)
{
    Track t;
    t.sampleRate = sr;
    PitchCorrector pc;
    pc.prepare (sr, 1);   // Amount 0: it only listens
    std::vector<double> buf (32);
    double lastTime = -1.0e18;
    for (size_t s = 0; s < mono.size(); s += buf.size())
    {
        if (s % 8192 == 0 && cancel != nullptr && cancel->load (std::memory_order_relaxed)) return t;
        const size_t n = std::min (buf.size(), mono.size() - s);
        for (size_t i = 0; i < n; ++i) buf[i] = mono[s + i];
        double* p = buf.data();
        pc.process (&p, 1, static_cast<int> (n));
        const auto r = pc.reading();
        if (r.time > lastTime + 0.5)
        {
            lastTime = r.time;
            const bool sung = r.voiced && std::isfinite (r.sungMidi) && r.sungMidi > 0.0;   // (never a NaN reading)
            t.time.push_back (r.time);
            t.midi.push_back (sung ? r.sungMidi : 0.0);
            t.clarity.push_back (sung && std::isfinite (r.clarity) ? r.clarity : 0.0);
        }
    }
    const size_t n = t.midi.size();
    if (n == 0) return t;

    // Octave slips: compare with the median of the voiced readings around it (past AND future).
    std::vector<double> fixed = t.midi;
    for (size_t i = 0; i < n; ++i)
    {
        if (t.midi[i] <= 0.0) continue;
        std::vector<double> around;
        for (size_t j = i >= 12 ? i - 12 : 0; j < std::min (n, i + 13); ++j) if (t.midi[j] > 0.0) around.push_back (t.midi[j]);
        const double m = median (around);
        const double d = t.midi[i] - m;
        if (std::abs (d) > 9.0) fixed[i] = t.midi[i] - 12.0 * std::round (d / 12.0);
    }
    // Bridge tiny gaps (under ~16 ms) inside a note.
    for (size_t i = 1; i + 1 < n; ++i)
    {
        if (fixed[i] > 0.0) continue;
        size_t j = i;
        while (j < n && fixed[j] <= 0.0) ++j;
        if (j < n && j - i <= 6 && fixed[i - 1] > 0.0 && std::abs (fixed[j] - fixed[i - 1]) < 1.0)
            for (size_t k = i; k < j; ++k)
            {
                const double x = static_cast<double> (k - i + 1) / static_cast<double> (j - i + 1);
                fixed[k] = fixed[i - 1] + (fixed[j] - fixed[i - 1]) * x;
                t.clarity[k] = std::min (t.clarity[i - 1], t.clarity[j]);
            }
        i = j;
    }
    // Median of five smooths detector jitter (voiced readings only).
    t.midi = fixed;
    for (size_t i = 2; i + 2 < n; ++i)
    {
        if (fixed[i] <= 0.0) continue;
        std::vector<double> w;
        for (size_t j = i - 2; j <= i + 2; ++j) if (fixed[j] > 0.0) w.push_back (fixed[j]);
        if (w.size() >= 3) t.midi[i] = median (w);
    }
    if (ai) aiCheck (t, mono, sr, cancel);
    extendToOnsets (t, mono, sr);
    measureLevel (t, mono, sr);
    return t;
}

std::vector<Note> findNotes (const Track& track)
{
    // The notes are decided on what the detector heard: the attacks / releases analyse() filled in (one pitch copied
    // back / on) would look like a level start to a gliding syllable and split it. Each note takes its own back after.
    Track t = track;
    for (size_t i = 0; i < t.midi.size() && i < track.filled.size(); ++i)
        if (track.filled[i] != 0) t.midi[i] = 0.0;
    std::vector<Note> notes;
    const size_t n = t.midi.size();
    const double sr = t.sampleRate;
    const double hopSec = n > 1 ? (t.time.back() - t.time.front()) / static_cast<double> (n - 1) / sr : 0.003;
    const auto minLen = static_cast<size_t> (std::max (3.0, 0.05 / hopSec));     // 50 ms
    const auto settle = static_cast<size_t> (std::max (2.0, 0.03 / hopSec));     // 30 ms away = a new note

    auto close = [&] (size_t a, size_t b)
    {
        if (b <= a + 1) return;
        Note note;
        note.firstReading = static_cast<int> (a);
        note.lastReading = static_cast<int> (b - 1);
        note.start = t.time[a];
        note.end = t.time[b - 1];
        // The note's pitch from its middle (slides in and out don't count).
        const size_t cut = (b - a) / 6;
        std::vector<double> mid (t.midi.begin() + static_cast<long> (a + cut), t.midi.begin() + static_cast<long> (b - cut));
        note.pitch = note.target = median (mid);
        notes.push_back (note);
    };

    // Decide boundaries on the pitch averaged over ~one vibrato cycle (180 ms, centred, within the
    // phrase): vibrato never splits a note, a real note change still does.
    const auto half = static_cast<size_t> (std::max (1.0, 0.09 / hopSec));
    std::vector<double> sm (n, 0.0);
    for (size_t k = 0; k < n; ++k)
    {
        if (t.midi[k] <= 0.0) continue;
        double sum = 0.0; int cnt = 0;
        for (size_t j = k > half ? k - half : 0; j < std::min (n, k + half + 1); ++j)
        {
            if (t.midi[j] <= 0.0) { if (j < k) { sum = 0.0; cnt = 0; continue; } break; }
            sum += t.midi[j]; ++cnt;
        }
        sm[k] = cnt ? sum / cnt : t.midi[k];
    }

    size_t i = 0;
    while (i < n)
    {
        if (! (t.midi[i] > 0.0)) { ++i; continue; }   // (written so a NaN counts as unsung: it can't stall the loop)
        size_t start = i, j = i;
        double ref = sm[i];
        std::vector<double> sofar;
        size_t runEnd = i;
        while (runEnd < n && t.midi[runEnd] > 0.0) ++runEnd;
        size_t holdUntil = i;   // the averages still in the change to a new note: not taken as its level
        auto nearer = [&] (size_t q, double oldLevel, double newLevel) { return std::abs (t.midi[q] - newLevel) < std::abs (t.midi[q] - oldLevel); };
        while (j < n && t.midi[j] > 0.0)
        {
            if (j >= holdUntil) sofar.push_back (sm[j]);
            if (sofar.size() >= 4) ref = median (sofar);
            // A new note when the (averaged) pitch stays more than 0.6 semitone away for `settle` readings.
            size_t k = j;
            while (k < n && t.midi[k] > 0.0 && std::abs (sm[k] - ref) > 0.6 && k - j < settle) ++k;
            if (k - j >= settle && j - start >= minLen)
            {
                // The new note's level: what's sung just after the change (it's offline: look ahead), never the average
                // still in the change (that made a second boundary once the average settled: legato notes split in two). The
                // boundary: where the sung pitch itself is first nearer the new level (three readings running), not
                // where the 180 ms average started to move (boundaries were ~25 ms late). (AUDIT-ENGINE.md #4.)
                std::vector<double> ahead;   // the sung pitch 45 - 135 ms on (the 180 ms average moves ~40 ms before the note does)
                for (size_t q = j + half / 2; q < std::min (runEnd, j + half + half / 2); ++q) ahead.push_back (t.midi[q]);
                const double oldLevel = ref, newLevel = ahead.size() >= 3 ? median (ahead) : sm[std::min (runEnd - 1, j + half)];
                size_t b = j;
                for (size_t q = (j > start + minLen / 2 + 2 * half ? j - 2 * half : start + minLen / 2); q < std::min (runEnd, j + half); ++q)
                    if (q + 2 < runEnd && nearer (q, oldLevel, newLevel) && nearer (q + 1, oldLevel, newLevel) && nearer (q + 2, oldLevel, newLevel))
                    {
                        b = q;
                        break;
                    }
                if (b <= start + 2) b = j;
                close (start, b);
                start = b;
                sofar.clear();
                ref = newLevel;
                holdUntil = j + 2 * half;
                j = std::max (j, b);
            }
            ++j;
        }
        if (j - start >= minLen) close (start, j);
        else if (! notes.empty() && notes.back().lastReading + 1 >= static_cast<int> (start))
        {
            notes.back().lastReading = static_cast<int> (j - 1);   // a too-short tail joins the note before
            notes.back().end = t.time[j - 1];
        }
        i = std::max (j, i + 1);   // always on
    }

    // A short scrap (under 100 ms) sung on into a note at the same pitch (within 0.7 semitone, no gap between) is that
    // note's start or end: joined to it, so moving the note takes all of it.
    const auto touching = [] (const Note& a, const Note& b) { return b.firstReading - a.lastReading <= 2; };
    const double scrap = 0.1 * sr;
    for (size_t k = 0; k < notes.size();)
    {
        const bool shortOne = notes[k].end - notes[k].start < scrap;
        const bool withPrev = shortOne && k > 0 && touching (notes[k - 1], notes[k]) && std::abs (notes[k - 1].pitch - notes[k].pitch) < 0.7;
        const bool withNext = shortOne && k + 1 < notes.size() && touching (notes[k], notes[k + 1]) && std::abs (notes[k + 1].pitch - notes[k].pitch) < 0.7;
        if (! withPrev && ! withNext) { ++k; continue; }
        const size_t into = withPrev && (! withNext || std::abs (notes[k - 1].pitch - notes[k].pitch) <= std::abs (notes[k + 1].pitch - notes[k].pitch)) ? k - 1 : k + 1;
        const size_t a = std::min (into, k), b = std::max (into, k);
        const int first = notes[a].firstReading, last = notes[b].lastReading;
        notes.erase (notes.begin() + static_cast<long> (b));
        notes.erase (notes.begin() + static_cast<long> (a));
        const auto before = notes.size();
        close (static_cast<size_t> (first), static_cast<size_t> (last) + 1);   // (appends the joined note)
        if (notes.size() > before)
        {
            auto joined = notes.back();
            notes.pop_back();
            notes.insert (notes.begin() + static_cast<long> (a), joined);
        }
        k = a;   // (look at the joined note again)
    }

    // Each note takes back its attack and release (the filled moments next to it, up to its neighbours).
    if (! track.filled.empty())
        for (size_t k = 0; k < notes.size(); ++k)
        {
            auto& note = notes[k];
            const int lo = k > 0 ? notes[k - 1].lastReading + 1 : 0;
            const int hi = k + 1 < notes.size() ? notes[k + 1].firstReading - 1 : static_cast<int> (n) - 1;
            while (note.firstReading - 1 >= lo && track.filled[static_cast<size_t> (note.firstReading - 1)] != 0 && track.midi[static_cast<size_t> (note.firstReading - 1)] > 0.0)
                --note.firstReading;
            while (note.lastReading + 1 <= hi && track.filled[static_cast<size_t> (note.lastReading + 1)] != 0 && track.midi[static_cast<size_t> (note.lastReading + 1)] > 0.0)
                ++note.lastReading;
            note.start = track.time[static_cast<size_t> (note.firstReading)];
            note.end = track.time[static_cast<size_t> (note.lastReading)];
        }
    return notes;
}

void snapToKey (std::vector<Note>& notes, int key, int scale, double amount)
{
    for (auto& n : notes)
    {
        const int tgt = PitchCorrector::targetNote (n.pitch, key, scale, -1);
        n.target = n.pitch + (tgt - n.pitch) * std::clamp (amount, 0.0, 1.0);
        n.edited = true;
    }
}

double centsOff (const Note& n, int key, int scale)
{
    return 100.0 * (n.pitch - PitchCorrector::targetNote (n.pitch, key, scale, -1));
}

namespace {
/** The reading spacing of a track (seconds). */
double hopOf (const Track& t)
{
    const size_t n = t.midi.size();
    return n > 1 ? (t.time.back() - t.time.front()) / static_cast<double> (n - 1) / t.sampleRate : 0.003;
}

/** One note's movement split in two, per reading from its first to its last (0 where no pitch was heard): the drift
    (the slow part, below ~2.5 Hz: a Gaussian-weighted average of the sung pitch around each reading, within the note)
    and the vibrato (all the rest: vibrato, a shaky voice's wobble, any faster movement). Both are measured from the
    note's pitch, so drift + vibrato = the whole movement: keeping neither leaves a steady note.
    (A Gaussian, not a plain moving average: that one's side lobes made "drift" swing against a 3.5 - 5 Hz wobble, so
    taking the drift out made that wobble deeper.) */
struct Movement { std::vector<double> drift, vibrato; };
Movement movementOf (const Track& t, const Note& note)
{
    Movement m;
    const size_t n = t.midi.size();
    const auto a0 = static_cast<size_t> (std::max (0, note.firstReading));
    const size_t b0 = std::min (n, static_cast<size_t> (std::max (0, note.lastReading)) + 1);
    if (b0 <= a0) return m;
    m.drift.assign (b0 - a0, 0.0);
    m.vibrato.assign (b0 - a0, 0.0);
    const double hop = hopOf (t);
    const double sigma = kDriftSigmaSeconds / hop;   // readings
    const auto reach = static_cast<size_t> (std::ceil (3.0 * sigma));
    std::vector<double> w (reach + 1);
    for (size_t k = 0; k <= reach; ++k) w[k] = std::exp (-0.5 * static_cast<double> (k * k) / (sigma * sigma));
    for (size_t i = a0; i < b0; ++i)
    {
        if (t.midi[i] <= 0.0) continue;
        double sum = 0.0, weight = 0.0;
        for (size_t j = (i > a0 + reach ? i - reach : a0); j < std::min (b0, i + reach + 1); ++j)
            if (t.midi[j] > 0.0)
            {
                const double wj = w[i > j ? i - j : j - i];
                sum += wj * t.midi[j];
                weight += wj;
            }
        const double drift = (weight > 0.0 ? sum / weight : t.midi[i]) - note.pitch;
        m.drift[i - a0] = drift;
        m.vibrato[i - a0] = (t.midi[i] - note.pitch) - drift;
    }
    return m;
}

/** How much of a note's own movement changes at reading i (semitones): eased in / out over its edges
    (steadyEdgeSeconds), so its scoop in and fall off stay as sung. */
double movementChange (const Movement& m, const Note& note, size_t i, size_t a0, size_t b0, double edgeReadings)
{
    const double x = std::min ({ 1.0, static_cast<double> (i - a0 + 1) / edgeReadings, static_cast<double> (b0 - i) / edgeReadings });
    const double fade = 0.5 - 0.5 * std::cos (std::numbers::pi * x);
    return fade * (m.drift[i - a0] * (note.drift - 1.0) + m.vibrato[i - a0] * (note.vibrato - 1.0));
}

/** The edge of a note in readings (steadyEdgeSeconds). */
double edgeOf (const Track& t, const Note& note)
{
    const double hop = hopOf (t);
    return std::max (1.0, steadyEdgeSeconds ((note.end - note.start) / t.sampleRate) / hop);
}

/** The shift (semitones) per reading for these notes. */
std::vector<double> planShift (const Track& t, const std::vector<Note>& notes, double transitionMs)
{
    const size_t n = t.midi.size();
    // Shift per reading = the note move (target - pitch), smoothed between notes, plus the drift /
    // vibrato change inside the note (not smoothed: it IS the fast part), faded in / out at note edges.
    std::vector<double> base (n, 0.0), detail (n, 0.0);
    const double hopSec = hopOf (t);
    for (const auto& note : notes)
    {
        const auto a0 = static_cast<size_t> (std::max (0, note.firstReading));
        const size_t b0 = std::min (n, static_cast<size_t> (std::max (0, note.lastReading)) + 1);
        const bool shaped = std::abs (note.drift - 1.0) > 1.0e-9 || std::abs (note.vibrato - 1.0) > 1.0e-9;
        const auto m = shaped ? movementOf (t, note) : Movement {};
        const double noteEdge = edgeOf (t, note);
        for (size_t i = a0; i < b0; ++i)
        {
            base[i] = note.target - note.pitch;
            if (shaped && t.midi[i] > 0.0) detail[i] = movementChange (m, note, i, a0, b0, noteEdge);
        }
    }
    // Between notes. Inside a note it's exactly the note's move, from its first moment to its last (its attack used to
    // sit on a move smoothed out over 25 ms either side of the gap: a moved note started off its new pitch and scooped
    // in, by 50 - 160 cents on real vocals with short gaps, and slid back at its end; AUDIT-ENGINE.md #1, #12):
    // - across a gap: an S-curve from one note's move to the next one's inside the gap (nothing sung there to hear it);
    // - two notes sung straight into each other: the move follows the singer's own glide from one to the other (where
    //   the sung pitch is between them), or an S-curve over the transition time around the boundary if there's no glide;
    // - before the first note / after the last: their move.
    std::vector<int> ordered;
    for (size_t k = 0; k < notes.size(); ++k)
        if (notes[k].lastReading >= notes[k].firstReading && notes[k].firstReading >= 0 && static_cast<size_t> (notes[k].lastReading) < n)
            ordered.push_back (static_cast<int> (k));
    std::sort (ordered.begin(), ordered.end(), [&] (int a, int b) { return notes[static_cast<size_t> (a)].firstReading < notes[static_cast<size_t> (b)].firstReading; });
    auto ease = [] (double x) { x = std::clamp (x, 0.0, 1.0); return 0.5 - 0.5 * std::cos (std::numbers::pi * x); };
    if (! ordered.empty())
    {
        const auto& first = notes[static_cast<size_t> (ordered.front())];
        const auto& last = notes[static_cast<size_t> (ordered.back())];
        for (size_t i = 0; i < static_cast<size_t> (first.firstReading); ++i) base[i] = first.target - first.pitch;
        for (auto i = static_cast<size_t> (last.lastReading) + 1; i < n; ++i) base[i] = last.target - last.pitch;
    }
    const auto edge = static_cast<long> (std::max (1.0, 0.5 * transitionMs / 1000.0 / hopSec));   // half the transition
    const auto glideReach = static_cast<long> (std::max (2.0, 0.06 / hopSec));                       // where a glide is looked for
    for (size_t q = 0; q + 1 < ordered.size(); ++q)
    {
        const auto& A = notes[static_cast<size_t> (ordered[q])];
        const auto& B = notes[static_cast<size_t> (ordered[q + 1])];
        const double mA = A.target - A.pitch, mB = B.target - B.pitch;
        const long la = A.lastReading, fb = B.firstReading;
        if (fb - la > 1)
        {
            for (long i = la + 1; i < fb; ++i)
                base[static_cast<size_t> (i)] = mA + (mB - mA) * ease (static_cast<double> (i - la) / static_cast<double> (fb - la));
            continue;
        }
        if (std::abs (mB - mA) < 1.0e-9) continue;
        // Sung on into each other: the singer's glide between the two notes' pitches, if there is one around the join.
        const long g0 = std::max<long> (A.firstReading, fb - glideReach), g1 = std::min<long> (B.lastReading, la + glideReach);
        bool shaped = false;
        if (std::abs (B.pitch - A.pitch) >= 0.5 && g1 > g0 + 2)
        {
            std::vector<double> p;
            double hi = 0.0;
            for (long i = g0; i <= g1; ++i)
            {
                const double m = t.midi[static_cast<size_t> (i)];
                const double x = m > 0.0 ? std::clamp ((m - A.pitch) / (B.pitch - A.pitch), 0.0, 1.0) : hi;
                hi = std::max (hi, x);   // only ever on towards the next note
                p.push_back (hi);
            }
            const double p0 = p.front(), p1 = p.back();
            if (p1 - p0 > 0.5)
            {
                for (long i = g0; i <= g1; ++i)
                    base[static_cast<size_t> (i)] = mA + (mB - mA) * std::clamp ((p[static_cast<size_t> (i - g0)] - p0) / (p1 - p0), 0.0, 1.0);
                shaped = true;
            }
        }
        if (! shaped)
            for (long i = std::max<long> (A.firstReading, fb - edge); i <= std::min<long> (B.lastReading, la + edge); ++i)
                base[static_cast<size_t> (i)] = mA + (mB - mA) * ease (static_cast<double> (i - (fb - edge)) / static_cast<double> (2 * edge));
    }
    std::vector<double> shift (n, 0.0);
    for (size_t i = 0; i < n; ++i) shift[i] = base[i] + detail[i];
    return shift;
}

/** Each reading's formant move: the note's, faded in / out over 20 ms at its edges. */
std::vector<double> planFormant (const Track& t, const std::vector<Note>& notes)
{
    std::vector<double> f (t.midi.size(), 0.0);
    const double edge = 0.02 * t.sampleRate;
    for (const auto& note : notes)
    {
        if (std::abs (note.formant) < 0.01) continue;
        for (auto i = static_cast<size_t> (std::max (0, note.firstReading)); i <= static_cast<size_t> (note.lastReading) && i < f.size(); ++i)
        {
            const double tm = t.time[i];
            f[i] = note.formant * std::clamp (std::min (tm - note.start, note.end - tm) / edge, 0.0, 1.0);
        }
    }
    return f;
}

/** Runs one shifter over the channels (kMaxChannels at a time: their grains join at the same moments, so a stereo image
    stays where it was; one shifter per channel made its own joins and smeared it) over input [from, to) plus its
    latency, and hands each output sample to put(channel, index, value). */
template <typename Put>
void runShifter (const std::vector<const std::vector<float>*>& chans, double sr, const Track& t, const std::vector<double>& shift,
                 const std::vector<double>& formant, size_t from, size_t to, const std::atomic<bool>* cancel, Put&& put)
{
    for (size_t c0 = 0; c0 < chans.size(); c0 += static_cast<size_t> (vox::kMaxChannels))
    {
        const auto nc = std::min (static_cast<size_t> (vox::kMaxChannels), chans.size() - c0);
        Plan plan (t, shift, static_cast<double> (from), &formant);
        PitchCorrector pc;
        PitchParams p;
        p.amount = 100.0;
        pc.setParams (p);
        pc.prepare (sr, static_cast<int> (nc));
        pc.setGuide (&plan);
        const auto lat = static_cast<size_t> (pc.latencySamples());
        const size_t size = chans[c0]->size();
        std::array<std::vector<double>, vox::kMaxChannels> buf;
        std::array<double*, vox::kMaxChannels> ptr {};
        for (size_t c = 0; c < nc; ++c) { buf[c].assign (256, 0.0); ptr[c] = buf[c].data(); }
        const size_t end = std::min (size, to);
        for (size_t s = from; s < end + lat; s += 256)
        {
            if (cancel != nullptr && cancel->load (std::memory_order_relaxed)) return;
            const size_t len = std::min<size_t> (256, end + lat - s);
            for (size_t c = 0; c < nc; ++c)
            {
                const auto& x = *chans[c0 + c];
                for (size_t i = 0; i < len; ++i) buf[c][i] = s + i < x.size() ? x[s + i] : 0.0;
            }
            pc.process (ptr.data(), static_cast<int> (nc), static_cast<int> (len));
            for (size_t c = 0; c < nc; ++c)
                for (size_t i = 0; i < len; ++i)
                    if (s + i >= from + lat && s + i - lat < end) put (c0 + c, s + i - lat, buf[c][i]);
        }
    }
}

std::vector<const std::vector<float>*> pointers (const Channels& chans)
{
    std::vector<const std::vector<float>*> p;
    for (const auto& c : chans) p.push_back (&c);
    return p;
}

/** A note the render changes (moved, re-shaped, its tone changed): its loudness is matched to how it was sung. */
bool changed (const Note& n)
{
    return std::abs (n.target - n.pitch) > 0.01 || std::abs (n.formant) > 0.01 || std::abs (n.drift - 1.0) > 1.0e-9 || std::abs (n.vibrato - 1.0) > 1.0e-9;
}

/** Each changed note as loud as it was sung: the shifter's grains lose 1 - 6 dB on a move (more upward: their windows
    overlap the voice's pulses; AUDIT-ENGINE.md #2). One gain per note (its middle's energy in vs out, all channels,
    +-6 dB at most), eased in and out over 12 ms around it. `in`: the (re-timed) input, whole length; `out`: rendered
    samples from `offset`; samples [lo, hi). */
void matchLoudness (const Channels& in, Channels& out, size_t offset, const std::vector<Note>& notes, double sr, size_t lo, size_t hi)
{
    if (in.empty() || out.empty() || hi <= lo) return;
    const double edge = 0.012 * sr, trim = 0.015 * sr;
    std::vector<double> gain;
    for (const auto& n : notes)
    {
        if (! changed (n)) continue;
        double a = n.start + trim, b = n.end - trim;
        if (b - a < 0.02 * sr) { a = n.start; b = n.end; }
        const auto i0 = static_cast<size_t> (std::max (static_cast<double> (lo), a)), i1 = static_cast<size_t> (std::min (static_cast<double> (hi), b));
        if (i1 <= i0 + static_cast<size_t> (0.01 * sr)) continue;
        double ein = 0.0, eout = 0.0;
        for (size_t c = 0; c < in.size() && c < out.size(); ++c)
            for (size_t i = i0; i < i1; ++i)
            {
                ein += static_cast<double> (in[c][i]) * in[c][i];
                const double y = out[c][i - offset];
                eout += y * y;
            }
        if (eout < 1.0e-12 || ein < 1.0e-12) continue;
        const double g = std::clamp (std::sqrt (ein / eout), 0.5, 2.0);
        if (std::abs (g - 1.0) < 0.01) continue;
        if (gain.empty()) gain.assign (hi - lo, 1.0);
        const double from = n.start - edge, to = n.end + edge;
        for (auto i = static_cast<size_t> (std::max (static_cast<double> (lo), from)); i < std::min (static_cast<double> (hi), to); ++i)
        {
            const double x = std::clamp (std::min (static_cast<double> (i) - from, to - static_cast<double> (i)) / (2.0 * edge), 0.0, 1.0);
            const double w = 0.5 - 0.5 * std::cos (std::numbers::pi * x);
            gain[i - lo] *= 1.0 + (g - 1.0) * w;
        }
    }
    if (gain.empty()) return;
    for (auto& ch : out)
        for (size_t i = lo; i < hi; ++i)
            if (i - offset < ch.size()) ch[i - offset] = static_cast<float> (ch[i - offset] * gain[i - lo]);
}

/** The clip as heard after moves / stretches: warped audio, the readings at their new times, the notes at
    their new places (only when something moved). */
struct Retimed
{
    Channels audio;
    Track track;
    std::vector<Note> notes;
};
Retimed retime (const Channels& chans, double sr, const Track& t, const std::vector<Note>& notes, const TimeMap& map, size_t from, size_t to)
{
    Retimed r;
    r.audio = warp (chans, sr, t, map, from, to);
    r.track = t;
    for (auto& tm : r.track.time) tm = map.outAt (tm);
    r.notes = notes;
    for (auto& n : r.notes) { n.start = n.soundStart(); n.end = n.soundEnd(); n.outStart = n.outEnd = -1.0; }
    return r;
}
}

// ------------------------------------------------------------------------------------------------
// Moving and stretching notes in time.
namespace {
double mapThrough (const std::vector<double>& from, const std::vector<double>& to, double v)
{
    if (from.size() < 2) return v;
    const auto it = std::upper_bound (from.begin(), from.end(), v);
    const auto k = static_cast<size_t> (std::clamp<long> (it - from.begin(), 1, static_cast<long> (from.size()) - 1));
    const double a = from[k - 1], b = from[k];
    if (v < from.front() || v > from.back()) return v + (v < from.front() ? to.front() - from.front() : to.back() - from.back());
    return to[k - 1] + (to[k] - to[k - 1]) * (b > a ? (v - a) / (b - a) : 0.0);
}
}

double TimeMap::inAt (double tOut) const { return identity ? tOut : mapThrough (out, in, tOut); }
double TimeMap::outAt (double tIn) const { return identity ? tIn : mapThrough (in, out, tIn); }

TimeMap timeMap (const std::vector<Note>& notes, double clip, double sr)
{
    TimeMap m;
    m.in = { 0.0 };
    m.out = { 0.0 };
    const double minGap = 0.005 * sr, minLen = 0.03 * sr;
    double lastIn = 0.0, lastOut = 0.0;
    for (const auto& n : notes)
    {
        if (n.start <= lastIn + 1.0 || n.end > clip - 1.0) continue;
        // Keep the order: never before the last note's end, never past the clip's end. A note that no longer fits there
        // (the editor keeps notes between their neighbours, so only a damaged edit) ends the map: the rest follows the
        // last segment, and the knots keep rising.
        if (lastOut + minGap + minLen > clip - minGap) break;
        if (n.timeMoved()) m.identity = false;
        const double sOut = std::clamp (n.soundStart(), lastOut + minGap, clip - minGap - minLen);
        const double eOut = std::clamp (n.soundEnd(), sOut + minLen, clip - minGap);
        m.in.push_back (n.start); m.out.push_back (sOut);
        m.in.push_back (n.end); m.out.push_back (eOut);
        lastIn = n.end; lastOut = eOut;
    }
    m.in.push_back (clip); m.out.push_back (clip);
    return m;
}

Channels warp (const Channels& chans, double sr, const Track& t, const TimeMap& map, size_t from, size_t to)
{
    Channels ys;
    for (const auto& x : chans) ys.emplace_back (x.size(), 0.0f);
    if (chans.empty()) return ys;
    const size_t size = chans.front().size();
    to = std::min (to, size);
    if (from >= to) return ys;
    if (map.identity)
    {
        for (size_t c = 0; c < chans.size(); ++c)
            std::copy (chans[c].begin() + static_cast<long> (from), chans[c].begin() + static_cast<long> (to), ys[c].begin() + static_cast<long> (from));
        return ys;
    }

    // The grains are placed once, on the channels' mix, and every channel is read with the same grains (the channels stay
    // in step: a stereo image doesn't smear).
    std::vector<float> mixed;
    if (chans.size() > 1)
    {
        mixed.assign (size, 0.0f);
        for (const auto& ch : chans)
            for (size_t i = 0; i < size; ++i) mixed[i] += ch[i] / static_cast<float> (chans.size());
    }
    const auto& ref = chans.size() > 1 ? mixed : chans.front();

    const double uvP = 0.005 * sr, maxP = sr / PitchCorrector::kMinHz;
    const auto lo = static_cast<size_t> (std::max (0.0, static_cast<double> (from) - 3.0 * maxP));
    const auto hi = std::min (size, to + static_cast<size_t> (3.0 * maxP));
    auto read = [size] (const std::vector<float>& x, double pos)
    {
        pos = std::clamp (pos, 1.0, static_cast<double> (size) - 3.0);
        const auto i = static_cast<size_t> (pos);
        const double f = pos - static_cast<double> (i);
        const double ym1 = x[i - 1], y0 = x[i], y1 = x[i + 1], y2 = x[i + 2];
        const double c1 = 0.5 * (y1 - ym1), c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    };
    auto periodAt = [&] (double tin)
    {
        const auto& tm = t.time;
        if (tm.empty()) return 0.0;
        const auto it = std::lower_bound (tm.begin(), tm.end(), tin);
        auto i = static_cast<size_t> (std::clamp<long> (it - tm.begin(), 0, static_cast<long> (tm.size()) - 1));
        if (i > 0 && std::abs (tm[i - 1] - tin) < std::abs (tm[i] - tin)) --i;
        const double m = t.midi[i];
        return m > 0.0 ? sr / (440.0 * std::pow (2.0, (m - 69.0) / 12.0)) : 0.0;
    };
    // Where near `cand` the waveform best continues the one at `ana` (+-15 % of a period).
    auto align = [&] (double ana, double cand, double P)
    {
        const int len = static_cast<int> (P), reach = std::max (2, static_cast<int> (0.15 * P));
        double best = -2.0; int bestK = 0;
        for (int k = -reach; k <= reach; ++k)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int j = 0; j < len; j += 2)
            {
                const double a = read (ref, ana + j), b = read (ref, cand + k + j);
                xy += a * b; xx += a * a; yy += b * b;
            }
            const double r = xy / std::sqrt (xx * yy + 1.0e-30);
            if (r > best) { best = r; bestK = k; }
        }
        return cand + bestK;
    };

    // Grains laid one period apart in the output, each read where the map says that moment comes from,
    // plus a drift that only changes smoothly (a period repeated / skipped at a matching point when it
    // passes half a period), exactly like the pitch shifter's.
    const size_t nc = chans.size();
    std::vector<std::vector<double>> acc (nc, std::vector<double> (hi - lo, 0.0));
    std::vector<double> ws (hi - lo, 0.0);
    double tOut = static_cast<double> (lo), drift = 0.0;
    while (tOut < static_cast<double> (hi))
    {
        const double tin = map.inAt (tOut);
        const double P = periodAt (tin);
        double ana = tin + drift;
        if (P > 0.0 && std::abs (drift) > 0.5 * P)
        {
            ana = align (ana, ana - (drift > 0.0 ? P : -P), P);
            drift = ana - tin;
        }
        if (P <= 0.0) { drift = 0.0; ana = tin; }   // breath / consonant: read where the map says
        const double half = P > 0.0 ? P : uvP;
        for (auto j = static_cast<long> (std::ceil (tOut - half)); j <= static_cast<long> (std::floor (tOut + half)); ++j)
        {
            if (j < static_cast<long> (lo) || j >= static_cast<long> (hi)) continue;
            const double u = static_cast<double> (j) - tOut;
            const double w = 0.5 + 0.5 * std::cos (std::numbers::pi * u / half);
            for (size_t c = 0; c < nc; ++c) acc[c][static_cast<size_t> (j) - lo] += w * read (chans[c], ana + u);
            ws[static_cast<size_t> (j) - lo] += w;
        }
        const double step = half;
        drift += step - (map.inAt (tOut + step) - tin);
        tOut += step;
    }
    for (size_t c = 0; c < nc; ++c)
        for (size_t j = from; j < to; ++j)
        {
            const double w = ws[j - lo];
            ys[c][j] = static_cast<float> (w > 0.05 ? acc[c][j - lo] / w : read (chans[c], map.inAt (static_cast<double> (j))));
        }
    return ys;
}

std::vector<float> warp (const std::vector<float>& x, double sr, const Track& t, const TimeMap& map, size_t from, size_t to)
{
    return std::move (warp (Channels { x }, sr, t, map, from, to).front());
}

std::vector<double> soundingLine (const Track& t, const Note& note)
{
    const size_t n = t.midi.size();
    const auto a0 = static_cast<size_t> (std::max (0, note.firstReading));
    const size_t b0 = std::min (n, static_cast<size_t> (std::max (0, note.lastReading)) + 1);
    std::vector<double> line;
    if (b0 <= a0) return line;
    line.assign (b0 - a0, 0.0);
    const bool shaped = std::abs (note.drift - 1.0) > 1.0e-9 || std::abs (note.vibrato - 1.0) > 1.0e-9;
    const auto m = shaped ? movementOf (t, note) : Movement {};
    const double edge = edgeOf (t, note);
    for (size_t i = a0; i < b0; ++i)
        if (t.midi[i] > 0.0)
            line[i - a0] = t.midi[i] + (note.target - note.pitch) + (shaped ? movementChange (m, note, i, a0, b0, edge) : 0.0);
    return line;
}

double movementCents (const Track& t, const Note& note)
{
    const auto line = soundingLine (t, note);
    // Inside the note (its first and last 40 ms are its attack and release, and steadying spares its edges), around its median.
    const auto skip = static_cast<size_t> (std::max (0.04, steadyEdgeSeconds ((note.end - note.start) / t.sampleRate)) / hopOf (t));
    std::vector<double> v;
    for (size_t i = skip; i + skip < line.size(); ++i) if (line[i] > 0.0) v.push_back (line[i]);
    if (v.size() < 4) return 0.0;
    auto sorted = v;
    std::sort (sorted.begin(), sorted.end());
    const double mid = sorted[sorted.size() / 2];
    double e = 0.0;
    for (double x : v) e += (x - mid) * (x - mid);
    return 100.0 * std::sqrt (e / static_cast<double> (v.size()));
}

Channels render (const Channels& chans, double sr, const Track& t, const std::vector<Note>& notes, double transitionMs,
                 const std::atomic<bool>* cancel)
{
    Channels out;
    for (const auto& x : chans) out.emplace_back (x.size(), 0.0f);
    if (chans.empty() || chans.front().empty()) return out;
    const size_t size = chans.front().size();
    auto put = [&out] (size_t c, size_t o, double v) { out[c][o] = static_cast<float> (v); };
    const auto map = timeMap (notes, static_cast<double> (size), sr);
    if (map.identity)
    {
        runShifter (pointers (chans), sr, t, planShift (t, notes, transitionMs), planFormant (t, notes), 0, size, cancel, put);
        matchLoudness (chans, out, 0, notes, sr, 0, size);
        return out;
    }
    const auto r = retime (chans, sr, t, notes, map, 0, size);
    runShifter (pointers (r.audio), sr, r.track, planShift (r.track, r.notes, transitionMs), planFormant (r.track, r.notes), 0, size, cancel, put);
    matchLoudness (r.audio, out, 0, r.notes, sr, 0, size);
    return out;
}

std::vector<float> render (const std::vector<float>& mono, double sr, const Track& t, const std::vector<Note>& notes, double transitionMs,
                           const std::atomic<bool>* cancel)
{
    return std::move (render (Channels { mono }, sr, t, notes, transitionMs, cancel).front());
}

bool changedRegion (const std::vector<Note>& before, const std::vector<Note>& after, double clipSamples, double sr, double& from, double& to)
{
    if (before.size() != after.size() || after.empty()) return false;
    auto same = [] (const Note& a, const Note& b)
    {
        return a.start == b.start && a.end == b.end && std::abs (a.target - b.target) < 1.0e-9
               && std::abs (a.drift - b.drift) < 1.0e-9 && std::abs (a.vibrato - b.vibrato) < 1.0e-9
               && std::abs (a.formant - b.formant) < 1.0e-9 && std::abs (a.soundStart() - b.soundStart()) < 0.5
               && std::abs (a.soundEnd() - b.soundEnd()) < 0.5;
    };
    int first = -1, last = -1;
    bool timing = false;
    for (size_t i = 0; i < after.size(); ++i)
        if (! same (before[i], after[i]))
        {
            if (first < 0) first = static_cast<int> (i);
            last = static_cast<int> (i);
            timing = timing || std::abs (before[i].soundStart() - after[i].soundStart()) >= 0.5 || std::abs (before[i].soundEnd() - after[i].soundEnd()) >= 0.5;
        }
    if (first < 0) { from = to = 0.0; return true; }   // nothing changed
    const auto count = static_cast<int> (after.size());
    if (timing)
    {
        // A note moved onto (or past) a neighbour, or past the clip's ends, before or after the edit: the time map
        // squeezes the notes it runs into, wherever they are: render it all.
        auto fits = [&] (const std::vector<Note>& list, int i)
        {
            const auto& n = list[static_cast<size_t> (i)];
            return n.soundStart() >= 0.0 && n.soundEnd() <= clipSamples
                   && (i == 0 || n.soundStart() >= list[static_cast<size_t> (i - 1)].soundEnd())
                   && (i + 1 >= count || n.soundEnd() <= list[static_cast<size_t> (i + 1)].soundStart());
        };
        for (int i = first; i <= last; ++i)
            if (! same (before[static_cast<size_t> (i)], after[static_cast<size_t> (i)]) && (! fits (before, i) || ! fits (after, i)))
                return false;
        // A move / stretch re-times the gaps up to the neighbouring notes: they come along.
        first = std::max (0, first - 1);
        last = std::min (count - 1, last + 1);
    }
    // Grow over neighbours sung in one breath (gaps under 0.15 s: the note moves are smoothed across them), as they were
    // before the edit and as they are now.
    const double tight = 0.15 * sr;
    auto gapBefore = [&] (const std::vector<Note>& l, int i)
    {
        return std::min (l[static_cast<size_t> (i)].soundStart() - l[static_cast<size_t> (i - 1)].soundEnd(),
                         l[static_cast<size_t> (i)].start - l[static_cast<size_t> (i - 1)].end);
    };
    auto tightBefore = [&] (int i) { return gapBefore (after, i) < tight || gapBefore (before, i) < tight; };
    while (first > 0 && tightBefore (first)) --first;
    while (last + 1 < count && tightBefore (last + 1)) ++last;
    // From / to: the middle of the gaps around them (quiet: the old and new render join there), where the notes sound
    // now, where they sounded before the edit and where they were sung. Outside [first, last] nothing moved, so these
    // output times are also the input times there.
    from = 0.0;
    to = clipSamples;
    if (first > 0)
    {
        const auto& a0 = after[static_cast<size_t> (first - 1)];
        const auto& a1 = after[static_cast<size_t> (first)];
        const auto& b1 = before[static_cast<size_t> (first)];
        from = std::min ({ 0.5 * (a0.soundEnd() + a1.soundStart()), 0.5 * (a0.soundEnd() + b1.soundStart()), 0.5 * (a0.end + a1.start) });
    }
    if (last + 1 < count)
    {
        const auto& a0 = after[static_cast<size_t> (last)];
        const auto& b0 = before[static_cast<size_t> (last)];
        const auto& a1 = after[static_cast<size_t> (last + 1)];
        to = std::max ({ 0.5 * (a0.soundEnd() + a1.soundStart()), 0.5 * (b0.soundEnd() + a1.soundStart()), 0.5 * (a0.end + a1.start) });
    }
    return true;
}

void renderPart (const Channels& chans, double sr, const Track& t, const std::vector<Note>& notes,
                 Channels& previous, double from, double to, double transitionMs, const std::atomic<bool>* cancel)
{
    if (chans.empty() || previous.size() != chans.size() || to <= from) return;
    const size_t size = chans.front().size();
    for (const auto& p : previous) if (p.size() != size) return;
    // Start 0.3 s early (the shifter settles), crossfade 10 ms into the old render at both joins.
    const double xf = 0.01 * sr;
    const auto a = static_cast<size_t> (std::max (0.0, from - xf - 0.3 * sr));
    const auto b = static_cast<size_t> (std::min (static_cast<double> (size), to + xf + 1.0));
    const auto map = timeMap (notes, static_cast<double> (size), sr);
    Retimed r;
    if (! map.identity) r = retime (chans, sr, t, notes, map, a, b);
    const auto& src = map.identity ? chans : r.audio;
    const auto& trk = map.identity ? t : r.track;
    const auto& nts = map.identity ? notes : r.notes;
    // Rendered into [a, b) first (each moved note's loudness matched there, like a full render), then crossfaded in.
    Channels fresh (chans.size(), std::vector<float> (b - a, 0.0f));
    runShifter (pointers (src), sr, trk, planShift (trk, nts, transitionMs), planFormant (trk, nts), a, b, cancel,
                [&] (size_t c, size_t o, double v) { fresh[c][o - a] = static_cast<float> (v); });
    if (cancel != nullptr && cancel->load (std::memory_order_relaxed)) return;
    matchLoudness (src, fresh, a, nts, sr, a, b);
    for (size_t o = a; o < b; ++o)
    {
        const double x = static_cast<double> (o);
        const double w = from <= 0.0 && x < from + 1.0 ? 1.0 : std::clamp (std::min ((x - (from - xf)) / xf, ((to + xf) - x) / xf), 0.0, 1.0);
        const double wt = to >= static_cast<double> (size) && x > to - 1.0 ? 1.0 : w;
        if (wt <= 0.0) continue;
        const double e = 0.5 - 0.5 * std::cos (std::numbers::pi * wt);   // equal-gain S-curve
        for (size_t c = 0; c < chans.size(); ++c)
            previous[c][o] = static_cast<float> ((1.0 - e) * previous[c][o] + e * fresh[c][o - a]);
    }
}

void renderPart (const std::vector<float>& mono, double sr, const Track& t, const std::vector<Note>& notes,
                 std::vector<float>& previous, double from, double to, double transitionMs, const std::atomic<bool>* cancel)
{
    if (previous.size() != mono.size()) return;
    Channels prev { std::move (previous) };
    renderPart (Channels { mono }, sr, t, notes, prev, from, to, transitionMs, cancel);
    previous = std::move (prev.front());
}

} // namespace vox::honey
