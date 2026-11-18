#include "vox/CoachPlan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vox::coach {

namespace {

std::string num (double v, int decimals = 1)
{
    if (! std::isfinite (v)) v = 0.0;
    char buf[32];
    std::snprintf (buf, sizeof buf, "%.*f", decimals, v);
    std::string s (buf);
    if (s == "-0.0" || s == "-0") s = s.substr (1);
    return s;
}

std::string minus (const std::string& s)   // the interface's minus sign
{
    return ! s.empty() && s[0] == '-' ? "\xE2\x88\x92" + s.substr (1) : s;
}

std::string signedDb (double v) { return minus ((v >= 0.05 ? "+" : "") + num (v)) + " dB"; }

std::string hz (double f) { return f >= 1000.0 ? num (f / 1000.0, f >= 10000.0 || std::fmod (f, 1000.0) == 0.0 ? 0 : 1) + " kHz" : num (f, 0) + " Hz"; }
std::string range (double a, double b)
{
    if (a >= 1000.0 && b >= 1000.0) return num (a / 1000.0, std::fmod (a, 1000.0) == 0.0 ? 0 : 1) + " - " + hz (b);
    return num (a, 0) + " - " + hz (b);
}

/** A JSON text, written by hand (no library in the DSP code). */
struct Json
{
    std::string o;
    void str (const std::string& s)
    {
        o.push_back ('"');
        for (unsigned char c : s)
        {
            if (c == '"') o += "\\\"";
            else if (c == '\\') o += "\\\\";
            else if (c == '\n') o += "\\n";
            else if (c < 0x20) o += ' ';
            else o.push_back (static_cast<char> (c));
        }
        o.push_back ('"');
    }
    void key (const char* k) { if (! o.empty() && o.back() != '{' && o.back() != '[') o.push_back (','); str (k); o.push_back (':'); }
    void item() { if (! o.empty() && o.back() != '{' && o.back() != '[') o.push_back (','); }
    void number (double v, int d = 1) { o += num (v, d); }
    void array (const std::vector<double>& v)
    {
        o.push_back ('[');
        for (size_t i = 0; i < v.size(); ++i) { if (i) o.push_back (','); number (v[i]); }
        o.push_back (']');
    }
};

std::string cut (const std::string& s, std::size_t max)
{
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char> (s[n]) & 0xC0) == 0x80) --n;   // a UTF-8 boundary
    return s.substr (0, n) + "\xE2\x80\xA6";   // ...
}

/** Voxology's modules in signal order: Auto-Edit's reason key, the Rack's module key, the Module index, the caption. */
struct Step { const char* reason; const char* rackKey; Module module; const char* caption; };
const Step kSteps[] = {
    { "pitch", "vox.pitch", Module::pitch, "Tune & align" },
    { "cleanup", "vox.cleanup", Module::cleanup, "Clean the take" },
    { "eq", "vox.eq", Module::eq, "Shape tone" },
    { "dyneq", "vox.dyneq", Module::dynEq, "Tame harsh spots" },
    { "deess", "vox.deess", Module::deEsser, "Tame sibilance" },
    { "rider", "vox.rider", Module::rider, "Even levels" },
    { "comp", "vox.comp", Module::comp, "Add control" },
    { "sat", "vox.sat", Module::saturation, "Add character" },
    { "double", "vox.voices", Module::doubler, "Add width" },
    { "dimension", "vox.dimension", Module::dimension, "Movement" },
    { "delay", "vox.delay", Module::delay, "Add depth" },
    { "reverb", "vox.reverb", Module::reverb, "Add space" },
    { "out", "vox.output", Module::output, "Polish & finish" },
};

bool isOn (const ChainParams& p, Module m)
{
    switch (m)
    {
        case Module::pitch:      return p.pitch.enabled;
        case Module::cleanup:    return p.cleanup.enabled;
        case Module::eq:         return p.eq.enabled;
        case Module::dynEq:      return p.dynEq.enabled;
        case Module::deEsser:    return p.deEsser.enabled;
        case Module::rider:      return p.rider.enabled;
        case Module::comp:       return p.comp.enabled;
        case Module::saturation: return p.saturation.enabled;
        case Module::doubler:    return p.doubler.enabled;
        case Module::dimension:  return p.dimension.enabled && p.dimension.amount > 0.0;
        case Module::delay:      return p.delay.enabled;
        case Module::reverb:     return p.reverb.enabled;
        case Module::output:     return true;
        default:                 return false;
    }
}

const char* fixWith (const std::string& key)
{
    if (key == "low" || key == "mud" || key == "boxy") return "Tone EQ";
    if (key == "nasal" || key == "harsh") return "Tone EQ (or the Dynamic EQ)";
    if (key == "air") return "Tone EQ, or Saturation's Air";
    if (key == "sibilance") return "the De-Esser's Amount";
    if (key == "swing") return "the Rider's Range, or the Compressor";
    return "Voxology";
}
}

const std::vector<Region>& regions()
{
    static const std::vector<Region> r {
        { "low", "Boomy low end", 100.0, 160.0, 3.0, -4.0 },
        { "mud", "Mud", 200.0, 400.0, 2.5, -99.0 },
        { "boxy", "Boxiness", 500.0, 800.0, 2.5, -99.0 },
        { "nasal", "Honk", 1000.0, 1600.0, 2.5, -99.0 },
        { "harsh", "Harshness", 2000.0, 5000.0, 2.5, -4.0 },
        { "air", "Too much top", 10000.0, 16000.0, 3.5, -3.5 },
    };
    return r;
}

namespace {
std::string regionName (const Region& g, bool under)
{
    if (! under) return g.name;
    const std::string k = g.key;
    return k == "low" ? "Thin low end" : k == "harsh" ? "Dull presence" : k == "air" ? "Lack of air" : g.name;
}
}

double regionOffset (const std::vector<double>& bandDb, const std::vector<double>& target, double fromHz, double toHz)
{
    const auto& bands = analysisBands();
    double sum = 0.0;
    int n = 0;
    for (size_t i = 0; i < bands.size() && i < bandDb.size() && i < target.size(); ++i)
        if (bands[i] >= fromHz - 1.0 && bands[i] <= toHz + 1.0 && std::isfinite (bandDb[i]) && std::isfinite (target[i]))
        {
            sum += bandDb[i] - target[i];
            ++n;
        }
    return n > 0 ? sum / n : 0.0;
}

std::vector<Issue> issues (const VocalAnalysis& a, const std::vector<double>& target, double sibTargetDb, const std::string& aimName)
{
    std::vector<Issue> out;
    if (a.bandDb.size() == analysisBands().size() && target.size() == a.bandDb.size())
        for (const auto& g : regions())
        {
            const double off = regionOffset (a.bandDb, target, g.fromHz, g.toHz);
            const bool over = off >= g.overDb, under = off <= g.underDb;
            if (! over && ! under) continue;
            Issue i;
            i.key = g.key;
            i.name = regionName (g, under);
            i.fromHz = g.fromHz;
            i.toHz = g.toHz;
            i.db = off;
            i.warn = over;   // too much of something is worth fixing; too little is a matter of taste
            i.text = range (g.fromHz, g.toHz) + " sits " + num (std::abs (off)) + " dB " + (over ? "over " : "under ") + aimName;
            out.push_back (i);
        }
    if (a.sibilanceDb > -100.0 && a.sibilanceDb - sibTargetDb > 2.0)
    {
        Issue i;
        i.key = "sibilance";
        i.name = "Sibilance";
        const double centre = a.sibilanceHz > 0.0 ? a.sibilanceHz : 7000.0;
        i.fromHz = std::round (centre / 1.4 / 100.0) * 100.0;
        i.toHz = std::round (centre * 1.4 / 100.0) * 100.0;
        i.db = a.sibilanceDb - sibTargetDb;
        i.warn = true;
        i.text = "the \"s\" sounds peak " + signedDb (a.sibilanceDb) + " against the voice; the aim is " + signedDb (sibTargetDb);
        out.push_back (i);
    }
    if (a.rangeDb > 9.0)
    {
        Issue i;
        i.key = "swing";
        i.name = "Uneven level";
        i.db = a.rangeDb;
        i.warn = a.rangeDb > 12.0;
        i.text = "loud and quiet lines are " + num (a.rangeDb) + " dB apart";
        out.push_back (i);
    }
    if (a.heardGaps && a.noiseFloorDb > -55.0)
    {
        Issue i;
        i.key = "noise";
        i.name = "Background noise";
        i.db = a.noiseFloorDb;
        i.warn = a.noiseFloorDb > -48.0;
        i.text = "between phrases the room / noise sits at " + signedDb (a.noiseFloorDb);
        out.push_back (i);
    }
    if (a.clippedRuns > 0)
    {
        Issue i;
        i.key = "clipped";
        i.name = "Clipped recording";
        i.db = a.peakDb;
        i.warn = true;
        i.text = std::to_string (a.clippedRuns) + " flat-topped spot" + (a.clippedRuns > 1 ? "s" : "") + " in the recording itself: re-record those words lower if you can";
        out.push_back (i);
    }
    std::stable_sort (out.begin(), out.end(), [] (const Issue& x, const Issue& y) { return x.warn && ! y.warn; });
    if (out.size() > 8) out.resize (8);
    return out;
}

std::vector<Result> results (const VocalAnalysis& before, const VocalAnalysis& after, const std::vector<double>& target, double sibTargetDb)
{
    std::vector<Result> out;
    const bool bands = before.bandDb.size() == analysisBands().size() && after.bandDb.size() == before.bandDb.size() && target.size() == before.bandDb.size();
    if (bands)
        for (const auto& g : regions())
        {
            const double b = regionOffset (before.bandDb, target, g.fromHz, g.toHz);
            const double a = regionOffset (after.bandDb, target, g.fromHz, g.toHz);
            const bool wasOff = b >= g.overDb || b <= g.underDb;
            if (! wasOff && std::abs (a - b) < 1.0) continue;
            Result r;
            r.key = g.key;
            r.name = regionName (g, b <= g.underDb) + " (" + range (g.fromHz, g.toHz) + ")";
            r.before = b;
            r.after = a;
            r.unit = "dB vs the aim";
            r.better = std::abs (a) < std::abs (b) - 0.3;
            out.push_back (r);
        }
    if (before.sibilanceDb > -100.0 && after.sibilanceDb > -100.0)
    {
        Result r;
        r.key = "sibilance";
        r.name = "Sibilance (the \"s\" sounds)";
        r.before = before.sibilanceDb - sibTargetDb;
        r.after = after.sibilanceDb - sibTargetDb;
        r.unit = "dB vs the aim";
        r.better = r.after < r.before - 0.3 || r.after <= 0.5;
        if (r.before > 1.0 || std::abs (r.after - r.before) >= 1.0) out.push_back (r);
    }
    if (before.rangeDb > 0.0 && after.rangeDb > 0.0)
    {
        Result r;
        r.key = "swing";
        r.name = "Level swing (loud vs quiet lines)";
        r.before = before.rangeDb;
        r.after = after.rangeDb;
        r.unit = "dB apart";
        r.better = r.after < r.before - 0.3;
        out.push_back (r);
    }
    // The ones that changed most first; at most six.
    std::stable_sort (out.begin(), out.end(), [] (const Result& x, const Result& y) { return std::abs (x.after - x.before) > std::abs (y.after - y.before); });
    if (out.size() > 6) out.resize (6);
    return out;
}

std::string planJson (const AutoEditResult& r, const VocalAnalysis* after, const std::vector<double>& target, double sibTargetDb,
                      const AutoEditSettings& s, const std::string& reference, std::size_t maxBytes)
{
    const std::string styleName = kStyleNames[static_cast<size_t> (std::clamp (s.style, 0, kStyles - 1))];
    const std::string aim = reference.empty() ? styleName + "'s balance" : "the reference";
    const auto& a = r.analysis;
    const auto found = issues (a, target, sibTargetDb, aim);
    const auto measured = after != nullptr ? results (a, *after, target, sibTargetDb) : std::vector<Result> {};

    // Shorter until it fits: fewer reasons per module (each kept whole as long as possible), then shorter, then none.
    for (int pass = 0; pass < 5; ++pass)
    {
        static constexpr std::size_t kWhy[] = { 240, 200, 240, 120, 0 }, kPer[] = { 4, 2, 1, 1, 0 };
        const std::size_t whyMax = kWhy[pass], perModule = kPer[pass];
        Json j;
        j.o.push_back ('{');
        j.key ("ok"); j.o += r.ok ? "true" : "false";
        j.key ("style"); j.str (styleName);
        j.key ("intensity"); j.str (kIntensityNames[static_cast<size_t> (std::clamp (s.intensity, 0, kIntensities - 1))]);
        j.key ("reference"); j.str (reference);
        j.key ("aim"); j.str (aim);
        j.key ("summary"); j.str (cut (r.summary, pass < 4 ? 400 : 160));
        if (! r.ok)
        {
            j.key ("tipTitle"); j.str (r.tipTitle);
            j.key ("tip"); j.str (cut (r.tip, 300));
            j.o.push_back ('}');
            if (j.o.size() <= maxBytes || pass == 4) return j.o.size() <= maxBytes ? j.o : "{\"ok\":false}";
            continue;
        }

        // WHAT I HEARD: plain facts about the take.
        j.key ("heard");
        j.o.push_back ('[');
        std::vector<std::string> heard;
        if (a.voicedSeconds > 0.0)
            heard.push_back (a.heldShare < kRapHeldShare ? "Rapped: few held notes (" + num (a.heldShare, 0) + " %)"
                                                         : "Sung: " + num (a.heldShare, 0) + " % of the notes are held");
        if (a.inputLufs > -100.0)
            heard.push_back ("Comes in around " + minus (num (a.inputLufs)) + " LUFS, its loudest peak at " + minus (num (a.peakDb)) + " dB");
        if (a.heardGaps && a.noiseFloorDb > -100.0)
            heard.push_back ("Between phrases: " + minus (num (a.noiseFloorDb)) + " dB of room and noise");
        if (a.sibilanceDb > -100.0)
            heard.push_back ("The \"s\" sounds peak " + signedDb (a.sibilanceDb) + " against the voice (the aim: " + signedDb (sibTargetDb) + ")");
        if (a.rangeDb > 0.0)
            heard.push_back ("Loud and quiet lines " + num (a.rangeDb) + " dB apart");
        if (pass >= 3 && heard.size() > 3) heard.resize (3);
        for (const auto& h : heard) { j.item(); j.str (h); }
        j.o.push_back (']');

        j.key ("issues");
        j.o.push_back ('[');
        for (const auto& i : found)
        {
            j.item();
            j.o.push_back ('{');
            j.key ("key"); j.str (i.key);
            j.key ("name"); j.str (i.name);
            j.key ("from"); j.number (i.fromHz, 0);
            j.key ("to"); j.number (i.toHz, 0);
            j.key ("db"); j.number (i.db);
            j.key ("warn"); j.o += i.warn ? "true" : "false";
            j.key ("text"); j.str (i.text);
            j.o.push_back ('}');
        }
        j.o.push_back (']');

        // WHY I BUILT THIS RACK: the chain, each module with what Auto-Edit set and why.
        j.key ("chain");
        j.o.push_back ('[');
        int changes = 0, on = 0;
        for (const auto& step : kSteps)
        {
            const bool stepOn = isOn (r.params, step.module);
            if (stepOn) ++on;
            j.item();
            j.o.push_back ('{');
            j.key ("key"); j.str (step.rackKey);
            j.key ("on"); j.o += stepOn ? "true" : "false";
            j.key ("caption"); j.str (step.caption);
            const auto mi = static_cast<size_t> (step.module);
            j.key ("kept"); j.o += mi < r.kept.size() && r.kept[mi] ? "true" : "false";
            j.key ("set");
            j.o.push_back ('[');
            std::size_t n = 0;
            for (const auto& x : r.reasons)
            {
                if (x.module != step.reason) continue;
                ++changes;
                if (n++ >= perModule) continue;
                j.item();
                j.o.push_back ('{');
                j.key ("control"); j.str (x.control);
                j.key ("value"); j.str (minus (x.value));
                if (whyMax > 0) { j.key ("why"); j.str (cut (x.why, whyMax)); }
                j.o.push_back ('}');
            }
            j.o.push_back (']');
            j.o.push_back ('}');
        }
        j.o.push_back (']');
        j.key ("changes"); j.number (changes, 0);
        j.key ("modulesOn"); j.number (on, 0);

        // BEFORE vs PROPOSED: the take's tone before, through the plan, and the aim.
        if (pass < 4 && a.bandDb.size() == analysisBands().size() && target.size() == a.bandDb.size())
        {
            j.key ("bands");
            j.o.push_back ('{');
            j.key ("hz"); j.array (analysisBands());
            j.key ("before"); j.array (a.bandDb);
            if (after != nullptr && after->bandDb.size() == a.bandDb.size()) { j.key ("after"); j.array (after->bandDb); }
            j.key ("target"); j.array (target);
            j.o.push_back ('}');
        }

        j.key ("results");
        j.o.push_back ('[');
        for (const auto& m : measured)
        {
            j.item();
            j.o.push_back ('{');
            j.key ("key"); j.str (m.key);
            j.key ("name"); j.str (m.name);
            j.key ("before"); j.number (m.before);
            j.key ("after"); j.number (m.after);
            j.key ("unit"); j.str (m.unit);
            j.key ("better"); j.o += m.better ? "true" : "false";
            j.o.push_back ('}');
        }
        j.o.push_back (']');

        // ADJUST FIRST: Auto-Edit's own ideas, then what is still off after the plan.
        j.key ("first");
        j.o.push_back ('[');
        std::vector<std::string> first;
        for (const auto& t : r.suggestions)
            if (first.size() < 2) first.push_back (cut (t, whyMax > 0 ? whyMax : 120));
        for (const auto& m : measured)
        {
            if (first.size() >= 4) break;
            const bool stillOff = m.key == "swing" ? m.after > 9.0 : std::abs (m.after) >= 2.0;
            if (stillOff)
                first.push_back (m.name + " is still " + (m.key == "swing" ? num (m.after) + " dB apart" : signedDb (m.after))
                                 + " after this: try " + fixWith (m.key) + ", to taste.");
        }
        if (first.size() < 4) first.push_back ("Reverb and Delay: set them to taste for the space you want on this song.");
        for (const auto& f : first) { j.item(); j.str (f); }
        j.o.push_back (']');
        j.o.push_back ('}');
        if (j.o.size() <= maxBytes) return j.o;
    }
    return "{\"ok\":false,\"summary\":\"The plan was too long to show in the Rack: open Voxology to see its report.\"}";
}

} // namespace vox::coach
