#include "vox/HoneyTune.h"

#include <numbers>

#include <algorithm>
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
    Plan (const Track& t, const std::vector<double>& shifts, double offset = 0.0) : track (t), shift (shifts), start (offset) {}

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
};

} // namespace

// ------------------------------------------------------------------------------------------------
Track analyse (const std::vector<float>& mono, double sr)
{
    Track t;
    t.sampleRate = sr;
    PitchCorrector pc;
    pc.prepare (sr, 1);   // Amount 0: it only listens
    std::vector<double> buf (32);
    double lastTime = -1.0e18;
    for (size_t s = 0; s < mono.size(); s += buf.size())
    {
        const size_t n = std::min (buf.size(), mono.size() - s);
        for (size_t i = 0; i < n; ++i) buf[i] = mono[s + i];
        double* p = buf.data();
        pc.process (&p, 1, static_cast<int> (n));
        const auto r = pc.reading();
        if (r.time > lastTime + 0.5)
        {
            lastTime = r.time;
            t.time.push_back (r.time);
            t.midi.push_back (r.voiced ? r.sungMidi : 0.0);
            t.clarity.push_back (r.voiced ? r.clarity : 0.0);
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
    return t;
}

std::vector<Note> findNotes (const Track& t)
{
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
        if (t.midi[i] <= 0.0) { ++i; continue; }
        size_t start = i, j = i;
        double ref = sm[i];
        std::vector<double> sofar;
        while (j < n && t.midi[j] > 0.0)
        {
            sofar.push_back (sm[j]);
            if (sofar.size() >= 4) ref = median (sofar);
            // A new note when the (averaged) pitch stays more than 0.6 semitone away for `settle` readings.
            size_t k = j;
            while (k < n && t.midi[k] > 0.0 && std::abs (sm[k] - ref) > 0.6 && k - j < settle) ++k;
            if (k - j >= settle && j - start >= minLen)
            {
                close (start, j);
                start = j;
                sofar.clear();
                ref = sm[j];
            }
            ++j;
        }
        if (j - start >= minLen) close (start, j);
        else if (! notes.empty() && notes.back().lastReading + 1 >= static_cast<int> (start))
        {
            notes.back().lastReading = static_cast<int> (j - 1);   // a too-short tail joins the note before
            notes.back().end = t.time[j - 1];
        }
        i = j;
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
/** The shift (semitones) per reading for these notes. */
std::vector<double> planShift (const Track& t, const std::vector<Note>& notes, double transitionMs)
{
    const size_t n = t.midi.size();
    // Shift per reading = the note move (target - pitch), smoothed between notes, plus the drift /
    // vibrato change inside the note (not smoothed: it IS the fast part), faded in / out at note edges.
    std::vector<double> base (n, 0.0), detail (n, 0.0);
    const double hopSec = n > 1 ? (t.time.back() - t.time.front()) / static_cast<double> (n - 1) / t.sampleRate : 0.003;
    const auto win = static_cast<size_t> (std::max (1.0, 0.18 / hopSec));   // drift = ~2 vibrato cycles (360 ms)
    const auto edge = std::max (1.0, 0.02 / hopSec);
    for (const auto& note : notes)
    {
        const auto a0 = static_cast<size_t> (note.firstReading), b0 = static_cast<size_t> (note.lastReading) + 1;
        for (size_t i = a0; i < b0 && i < n; ++i)
        {
            base[i] = note.target - note.pitch;
            if (t.midi[i] <= 0.0) continue;
            double sum = 0.0; int cnt = 0;
            for (size_t j = (i > a0 + win ? i - win : a0); j < std::min (b0, i + win + 1); ++j)
                if (t.midi[j] > 0.0) { sum += t.midi[j]; ++cnt; }
            const double driftDev = (cnt ? sum / cnt : t.midi[i]) - note.pitch;
            const double vib = (t.midi[i] - note.pitch) - driftDev;
            const double fade = std::min ({ 1.0, static_cast<double> (i - a0 + 1) / edge, static_cast<double> (b0 - i) / edge });
            detail[i] = fade * (driftDev * (note.drift - 1.0) + vib * (note.vibrato - 1.0));
        }
    }
    // Smooth the note moves (forward + backward, so it doesn't lag).
    const double k = 1.0 - std::exp (-hopSec / std::max (0.001, transitionMs / 1000.0));
    for (int pass = 0; pass < 2; ++pass)
    {
        double y = 0.0;
        for (size_t c = 0; c < n; ++c)
        {
            const size_t i = pass == 0 ? c : n - 1 - c;
            y += (base[i] - y) * k;
            base[i] = y;
        }
    }
    std::vector<double> shift (n, 0.0);
    for (size_t i = 0; i < n; ++i) shift[i] = base[i] + detail[i];
    return shift;
}

/** Runs the shifter over input [from, to) (plus its latency) and hands each output sample to put(index, value). */
template <typename Put>
void runShifter (const std::vector<float>& mono, double sr, const Track& t, const std::vector<double>& shift, size_t from, size_t to, Put&& put)
{
    Plan plan (t, shift, static_cast<double> (from));
    PitchCorrector pc;
    PitchParams p;
    p.amount = 100.0;
    pc.setParams (p);
    pc.prepare (sr, 1);
    pc.setGuide (&plan);
    const auto lat = static_cast<size_t> (pc.latencySamples());
    std::vector<double> buf (256);
    const size_t end = std::min (mono.size(), to);
    for (size_t s = from; s < end + lat; s += buf.size())
    {
        const size_t len = std::min (buf.size(), end + lat - s);
        for (size_t i = 0; i < len; ++i) buf[i] = s + i < mono.size() ? mono[s + i] : 0.0;
        double* ptr = buf.data();
        pc.process (&ptr, 1, static_cast<int> (len));
        for (size_t i = 0; i < len; ++i)
            if (s + i >= from + lat && s + i - lat < end) put (s + i - lat, buf[i]);
    }
}
}

std::vector<float> render (const std::vector<float>& mono, double sr, const Track& t, const std::vector<Note>& notes, double transitionMs)
{
    std::vector<float> out (mono.size(), 0.0f);
    runShifter (mono, sr, t, planShift (t, notes, transitionMs), 0, mono.size(), [&out] (size_t o, double v) { out[o] = static_cast<float> (v); });
    return out;
}

bool changedRegion (const std::vector<Note>& before, const std::vector<Note>& after, double clipSamples, double sr, double& from, double& to)
{
    if (before.size() != after.size() || after.empty()) return false;
    auto same = [] (const Note& a, const Note& b)
    {
        return a.start == b.start && a.end == b.end && std::abs (a.target - b.target) < 1.0e-9
               && std::abs (a.drift - b.drift) < 1.0e-9 && std::abs (a.vibrato - b.vibrato) < 1.0e-9;
    };
    int first = -1, last = -1;
    for (size_t i = 0; i < after.size(); ++i)
        if (! same (before[i], after[i])) { if (first < 0) first = static_cast<int> (i); last = static_cast<int> (i); }
    if (first < 0) { from = to = 0.0; return true; }   // nothing changed
    // Grow over neighbours sung in one breath (gaps under 0.15 s: the note moves are smoothed across them).
    const double tight = 0.15 * sr;
    while (first > 0 && after[static_cast<size_t> (first)].start - after[static_cast<size_t> (first - 1)].end < tight) --first;
    while (last + 1 < static_cast<int> (after.size()) && after[static_cast<size_t> (last + 1)].start - after[static_cast<size_t> (last)].end < tight) ++last;
    // From / to: the middle of the gaps around them (quiet: the old and new render join there).
    from = first > 0 ? 0.5 * (after[static_cast<size_t> (first - 1)].end + after[static_cast<size_t> (first)].start) : 0.0;
    to = last + 1 < static_cast<int> (after.size()) ? 0.5 * (after[static_cast<size_t> (last)].end + after[static_cast<size_t> (last + 1)].start) : clipSamples;
    return true;
}

void renderPart (const std::vector<float>& mono, double sr, const Track& t, const std::vector<Note>& notes,
                 std::vector<float>& previous, double from, double to, double transitionMs)
{
    if (previous.size() != mono.size() || to <= from) return;
    // Start 0.3 s early (the shifter settles), crossfade 10 ms into the old render at both joins.
    const double xf = 0.01 * sr;
    const auto a = static_cast<size_t> (std::max (0.0, from - xf - 0.3 * sr));
    const auto b = static_cast<size_t> (std::min (static_cast<double> (mono.size()), to + xf + 1.0));
    runShifter (mono, sr, t, planShift (t, notes, transitionMs), a, b, [&] (size_t o, double v)
    {
        const double x = static_cast<double> (o);
        const double w = from <= 0.0 && x < from + 1.0 ? 1.0 : std::clamp (std::min ((x - (from - xf)) / xf, ((to + xf) - x) / xf), 0.0, 1.0);
        const double wt = to >= static_cast<double> (mono.size()) && x > to - 1.0 ? 1.0 : w;
        if (wt <= 0.0) return;
        const double e = 0.5 - 0.5 * std::cos (std::numbers::pi * wt);   // equal-gain S-curve
        previous[o] = static_cast<float> ((1.0 - e) * previous[o] + e * v);
    });
}

} // namespace vox::honey
