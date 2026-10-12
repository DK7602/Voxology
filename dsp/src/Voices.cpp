#include "vox/Voices.h"

#include "vox/Saturation.h"

#include <algorithm>
#include <cmath>

namespace vox {

using design::onePole;

static constexpr int kBlock = 256;   // VocalChain::kChunk: the chain never sends more at once

void HarmonyVoices::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& v : voice) v.prepare (sr, 1);
    for (auto& b : buf) b.assign (kBlock, 0.0);
    eq.prepare (sr, 2);
    deEsser.prepare (sr, 2);
    align.assign (static_cast<size_t> (Saturation::kLatency + 1) * 2, 0.0);
    for (auto& l : late) l.assign (static_cast<size_t> (std::lround (kMaxLateMs / 1000.0 * sr)) + 1, 0.0);
    followCoeff = onePole (kFollowSeconds, sr);
    gainCoeff = onePole (0.020, sr);
    peakFall = onePole (2.0, sr);
    scratchL.assign (kBlock, 0.0);
    scratchR.assign (kBlock, 0.0);
    fadeStep = 1.0 / (kFadeSeconds * sr);
    smoothCoeff = onePole (0.050, sr);
    slots = sing = {};   // (the next setParams works out each voice's delay at this rate)
    reset();
}

void HarmonyVoices::reset() noexcept
{
    for (auto& v : voice) v.reset();
    for (auto& v : lowCut) for (auto& f : v) f.reset();
    designedLowCut = -1.0;
    eq.reset();
    deEsser.reset();
    std::fill (align.begin(), align.end(), 0.0);
    alignPos = 0;
    for (auto& l : late) std::fill (l.begin(), l.end(), 0.0);
    latePos.fill (0);
    voiceMs.fill (0.0);
    slotGain.fill (0.0);
    hold.fill (0);
    for (size_t k = 0; k < slots.size(); ++k) warm[k] = slots[k].on ? voice[k].latencySamples() + lateLen[k] : 0;   // (from silence)
    panL = panLTarget;
    panR = panRTarget;
    voiceWeight = weightTarget;
    leadMs = leadPeak = 0.0;
    follow = 0.0;
    gain = 0.0;
    running = false;
}

std::array<HarmonyVoices::Slot, kMaxVoices> HarmonyVoices::slotsFor (const VoicesParams& v) noexcept
{
    std::array<Slot, kMaxVoices> s {};
    const bool both = v.interval[0] != 0 && v.interval[1] != 0;
    for (size_t k = 0; k < 2; ++k)
    {
        const int h = std::clamp (v.interval[k], 0, kHarmonies - 1);
        if (h == 0) continue;
        s[k].on = true;
        s[k].harmony = h;
        s[k].cents = h == kHarmonyUnison ? kUnisonCents[k] : 0.0;
        s[k].lateMs = h == kHarmonyUnison ? kUnisonMs[k] : 0.0;
        s[k].pan = both ? (k == 0 ? -1.0 : 1.0) : 0.0;   // voice 1 left, voice 2 right; one alone in the middle
    }
    const int stack = kStackSizes[static_cast<size_t> (std::clamp (v.stack, 0, static_cast<int> (kStackSizes.size()) - 1))];
    for (size_t j = 0; j < static_cast<size_t> (stack); ++j)
    {
        auto& slot = s[2 + j];
        slot.on = true;
        slot.harmony = kHarmonyUnison;
        slot.cents = kStackCents[j];
        slot.lateMs = kStackMs[j];
        slot.pan = kStackPan[j];
    }
    return s;
}

namespace {
/** The held notes (each once, at most kMaxVoices), lowest first. */
int heldNotes (const VoicesParams& v, std::array<int, kMaxVoices>& held) noexcept
{
    int n = 0;
    for (int note : v.notes)
    {
        if (note < 0 || n >= kMaxVoices) continue;
        note = std::clamp (note, 24, 108);
        if (std::find (held.begin(), held.begin() + n, note) == held.begin() + n) held[static_cast<size_t> (n++)] = note;
    }
    std::sort (held.begin(), held.begin() + n);
    return n;
}
}

std::array<HarmonyVoices::Slot, kMaxVoices> HarmonyVoices::midiSlots (const VoicesParams& v) const noexcept
{
    std::array<int, kMaxVoices> held {};
    const int n = heldNotes (v, held);
    std::array<Slot, kMaxVoices> s {};
    std::array<bool, kMaxVoices> placed {};
    auto put = [&] (size_t k, int j)
    {
        auto& slot = s[k];
        slot.on = true;
        slot.harmony = kHarmonyUnison;
        slot.fixedNote = held[static_cast<size_t> (j)];
        slot.cents = kMidiCents[k];
        slot.lateMs = kMidiMs[k];
        slot.pan = n == 1 ? 0.0 : -1.0 + 2.0 * j / (n - 1);   // lowest note left .. highest right
        placed[static_cast<size_t> (j)] = true;
    };
    // A note still held stays on its voice.
    for (size_t k = 0; k < s.size(); ++k)
        if (slots[k].on)
            for (int j = 0; j < n; ++j)
                if (! placed[static_cast<size_t> (j)] && held[static_cast<size_t> (j)] == slots[k].fixedNote) { put (k, j); break; }
    // A new note: a voice just let go of (a chord change) slides over to it, the nearest one within kMaxGlide;
    // else a silent voice starts on it; else (all busy) the nearest voice slides, however far.
    auto freeVoice = [&] (int note, bool sounding, int maxDistance)
    {
        size_t best = s.size();
        int bestD = maxDistance + 1;
        for (size_t k = 0; k < s.size(); ++k)
        {
            if (s[k].on || (slots[k].on || slotGain[k] > 0.0 || hold[k] > 0) != sounding) continue;
            const int d = sounding ? std::abs (sing[k].fixedNote - note) : 0;
            if (d < bestD) { best = k; bestD = d; }
        }
        return best;
    };
    for (int j = 0; j < n; ++j)
    {
        if (placed[static_cast<size_t> (j)]) continue;
        const int note = held[static_cast<size_t> (j)];
        size_t k = freeVoice (note, true, kMaxGlide);
        if (k == s.size()) k = freeVoice (note, false, 0);
        if (k == s.size()) k = freeVoice (note, true, 1000);
        if (k < s.size()) put (k, j);
    }
    return s;
}

int HarmonyVoices::activeVoices (const VoicesParams& v) noexcept
{
    if (v.midi)
    {
        std::array<int, kMaxVoices> held {};
        return heldNotes (v, held);
    }
    int n = 0;
    for (const auto& s : slotsFor (v)) n += s.on ? 1 : 0;
    return n;
}

void HarmonyVoices::setParams (const VoicesParams& v, const PitchParams& lead, double lowCut_, const EqParams& eqp, const DeEsserParams& ds) noexcept
{
    const auto next = v.midi ? midiSlots (v) : slotsFor (v);
    for (size_t k = 0; k < next.size(); ++k)
    {
        const auto& b = next[k];
        if (! b.on)
        {
            // Let go of: it keeps singing what it sang, until the audio heard now reaches the moment it was let go
            // (the voice's latency), then fades out.
            if (slots[k].on) hold[k] = warm[k] > 0 ? 0 : voice[k].latencySamples();
            continue;
        }
        hold[k] = 0;
        const auto& a = sing[k];
        const bool start = ! slots[k].on && slotGain[k] == 0.0 && hold[k] == 0;
        if (start || a.harmony != b.harmony || a.lateMs != b.lateMs)   // no stale unison audio
        {
            std::fill (late[k].begin(), late[k].end(), 0.0);
            latePos[k] = 0;
            lateLen[k] = std::min (static_cast<int> (late[k].size()), static_cast<int> (std::lround (b.lateMs / 1000.0 * sr)));
        }
        if (start)
        {
            // From silence, not what it sang when it last stopped: it runs unheard until its output is real.
            voice[k].reset();
            for (auto& f : lowCut[k]) f.reset();
            warm[k] = voice[k].latencySamples() + lateLen[k];
        }
        sing[k] = b;
    }
    slots = next;
    params = v;
    int count = 0;
    for (size_t k = 0; k < voice.size(); ++k)
    {
        const auto& slot = sing[k];
        count += slots[k].on ? 1 : 0;
        PitchParams p;
        p.enabled = true;
        p.amount = 100.0;
        p.key = lead.key;
        p.scale = lead.scale;
        p.speedMs = std::min (lead.speedMs, 40.0);   // backing voices hold their notes; a slow lead retune would smear them
        p.humanize = lead.humanize;
        p.formant = std::clamp (v.formant, -kMaxFormant, kMaxFormant);
        p.harmony = slot.harmony;
        p.fixedNote = slot.fixedNote;
        // In the beat's tuning like the lead; a unison voice sits a few cents off it (another singer, not a copy).
        p.tuneCents = lead.tuneCents + slot.cents;
        voice[k].setParams (p);
    }
    weightTarget = std::sqrt (2.0 / std::max (2, count));
    lowCutHz = lowCut_;
    eq.setParams (eqp);
    deEsser.setParams (ds);
    const double spread = std::clamp (v.width, 0.0, 100.0) / 100.0;
    for (size_t k = 0; k < slots.size(); ++k)
    {
        const double pos = std::clamp (sing[k].pan * spread, -1.0, 1.0);   // -1 left .. +1 right (equal power)
        const double a = (pos + 1.0) * 0.25 * std::numbers::pi;
        panLTarget[k] = std::cos (a);
        panRTarget[k] = std::sin (a);
        if (slotGain[k] == 0.0) { panL[k] = panLTarget[k]; panR[k] = panRTarget[k]; }   // a voice starts where it sits
    }
    if (! running) voiceWeight = weightTarget;
}

std::array<int, kMaxVoices> HarmonyVoices::currentNotes() const noexcept
{
    std::array<int, kMaxVoices> out;
    out.fill (-1);
    for (size_t k = 0; k < slots.size(); ++k)
    {
        const auto r = voice[k].reading();
        if (slots[k].on && slotGain[k] > 0.0 && r.voiced)
            out[k] = static_cast<int> (std::lround (r.sungMidi + r.correction));
    }
    return out;
}

void HarmonyVoices::process (const double* side, double* const* ch, int nch, double* mono, int n) noexcept
{
    if (isNeutral (params) && gain == 0.0)
    {
        if (running) reset();
        return;
    }
    running = true;
    if (std::abs (lowCutHz - designedLowCut) > 0.01)
    {
        designedLowCut = lowCutHz;
        const auto c = design::butterworth (true, std::max (lowCutHz, 60.0), sr);   // at least 60 Hz: no rumble in the backing
        for (auto& v : lowCut) for (auto& f : v) design::apply (f, c);
    }
    const double targetGain = isNeutral (params) ? 0.0 : std::clamp (params.level, 0.0, 100.0) / 100.0;
    const double maxFollow = fromDb (kMaxFollowDb);
    const int dlen = Saturation::kLatency + 1;

    for (int start = 0; start < n; start += kBlock)
    {
        const int len = std::min (kBlock, n - start);
        // Each voice from the input (ahead of the lead by the pitch latency, which the voice adds back).
        std::array<bool, kMaxVoices> sounding {};
        for (size_t k = 0; k < slots.size(); ++k)
        {
            sounding[k] = slots[k].on || slotGain[k] > 0.0 || hold[k] > 0;
            if (! sounding[k]) continue;
            auto& b = buf[k];
            std::copy (side + start, side + start + len, b.begin());
            double* p = b.data();
            voice[k].process (&p, 1, len);
            if (lateLen[k] > 0)
            {
                auto& d = late[k];
                for (int i = 0; i < len; ++i)
                {
                    const auto ii = static_cast<size_t> (i), at = static_cast<size_t> (latePos[k]);
                    const double y = d[at];
                    d[at] = b[ii];
                    b[ii] = y;
                    latePos[k] = (latePos[k] + 1) % lateLen[k];
                }
            }
        }
        int active = 0;   // the voices on and heard (their level readings are real)
        for (int i = 0; i < len; ++i)
        {
            const auto ii = static_cast<size_t> (i);
            // Each voice: low cut, then into the stereo backing bus (panned).
            double l = 0.0, r = 0.0;
            for (size_t k = 0; k < slots.size(); ++k)
            {
                if (! sounding[k]) continue;
                const double x = lowCut[k][1].process (lowCut[k][0].process (buf[k][ii]));
                if (warm[k] > 0)
                {
                    if (--warm[k] == 0)
                    {
                        // Heard from now: its level reading starts at the others' (one from silence would turn the bus up).
                        double sum = 0.0;
                        int others = 0;
                        for (size_t j = 0; j < slots.size(); ++j)
                            if (j != k && warm[j] == 0 && slotGain[j] > 0.0) { sum += voiceMs[j]; ++others; }
                        voiceMs[k] = others > 0 ? sum / others : leadMs;
                    }
                    continue;
                }
                voiceMs[k] += (x * x - voiceMs[k]) * followCoeff;
                // Each voice fades in / out (a MIDI note on / off), and slides to a new place.
                if (hold[k] > 0) --hold[k];
                slotGain[k] = slots[k].on || hold[k] > 0 ? std::min (1.0, slotGain[k] + fadeStep) : std::max (0.0, slotGain[k] - fadeStep);
                panL[k] += (panLTarget[k] - panL[k]) * smoothCoeff;
                panR[k] += (panRTarget[k] - panR[k]) * smoothCoeff;
                l += slotGain[k] * panL[k] * x;
                r += slotGain[k] * panR[k] * x;
            }
            voiceWeight += (weightTarget - voiceWeight) * smoothCoeff;
            scratchL[ii] = voiceWeight * l;
            scratchR[ii] = voiceWeight * r;
        }
        // The lead's tone and de-essing on the backing bus.
        std::array<double*, 2> bus { scratchL.data(), scratchR.data() };
        eq.process (bus.data(), 2, len);
        deEsser.process (bus.data(), 2, len);

        for (int i = 0; i < len; ++i)
        {
            const auto ii = static_cast<size_t> (i);
            // Line up with the finished lead (Saturation's latency), then follow its level.
            const int w = alignPos, rd = (alignPos + 1) % dlen;
            align[static_cast<size_t> (w * 2)] = scratchL[ii];
            align[static_cast<size_t> (w * 2 + 1)] = scratchR[ii];
            const double dl = align[static_cast<size_t> (rd * 2)], dr = align[static_cast<size_t> (rd * 2 + 1)];
            alignPos = rd;

            double lead = 0.0;
            for (int c = 0; c < nch; ++c) lead += ch[c][start + i];
            lead /= nch;
            leadMs += (lead * lead - leadMs) * followCoeff;
            leadPeak = leadMs > leadPeak ? leadMs : leadPeak + (leadMs - leadPeak) * peakFall;
            // Follow the lead's level while it sings (each voice at the lead's level, times Level); in the gaps
            // (40 dB under its recent level) hold, so the voices never turn the room noise up.
            double vSum = 0.0;
            active = 0;
            for (size_t k = 0; k < slots.size(); ++k)
                if (slots[k].on && warm[k] == 0) { vSum += voiceMs[k]; ++active; }
            const double vMs = vSum / std::max (1, active) + 1.0e-12;
            if (leadMs > 1.0e-4 * leadPeak && leadMs > 1.0e-10 && active > 0)
                follow = std::min (std::sqrt (leadMs / vMs), maxFollow);
            gain += (targetGain * follow - gain) * gainCoeff;
            if (targetGain == 0.0 && gain < 1.0e-6) gain = 0.0;

            const double ol = gain * dl, orr = gain * dr;
            if (nch >= 2) { ch[0][start + i] += ol; ch[1][start + i] += orr; }
            else ch[0][start + i] += 0.5 * (ol + orr);
            if (mono != nullptr) mono[start + i] += 0.5 * (ol + orr);
        }
    }
}

} // namespace vox
