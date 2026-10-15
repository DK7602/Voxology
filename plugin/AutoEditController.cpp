#include "AutoEditController.h"
#include "Params.h"

#include <map>

AutoEditController::AutoEditController (juce::AudioProcessorValueTreeState& s, std::function<double()> getBpm)
    : state (s), bpmSource (std::move (getBpm))
{
    const auto maxSamples = static_cast<size_t> (kMaxSampleRate * kMaxSeconds);
    captureL.assign (maxSamples, 0.0f);
    captureR.assign (maxSamples, 0.0f);
    captureBeat.assign (maxSamples, 0.0f);
    startTimerHz (15);   // Auto-Edit's progress, and Auto Analyze
}

AutoEditController::~AutoEditController()
{
    stopTimer();
    pool.removeAllJobs (true, 60000);
}

void AutoEditController::prepare (double newSampleRate)
{
    if (newSampleRate <= 0.0)
        return;
    const double old = sampleRate.exchange (newSampleRate);
    capacity.store (static_cast<int> (std::min (newSampleRate * kMaxSeconds, static_cast<double> (captureL.size()))));
    if (std::abs (old - newSampleRate) > 0.5 && (stateFlag.load() == listening || stateFlag.load() == watching))
    {
        written.store (0);   // different sample rate: start the listening over
        voiced.store (0);
    }
}

bool AutoEditController::start()
{
    if (stateFlag.load() == analysing || capacity.load() == 0)
        return false;
    ++session;
    written.store (0);
    voiced.store (0);
    heardAudio.store (false);
    sideSeen.store (false);
    hearingAudio = false;
    quietTicks = 0;
    stateFlag.store (listening, std::memory_order_release);   // (an Auto Analyze check still running is thrown away: new session)
    return true;
}

void AutoEditController::cancel()
{
    if (stateFlag.load() == listening)
        stateFlag.store (idle);
}

float AutoEditController::getProgress() const noexcept
{
    const double sr = sampleRate.load();
    if (sr <= 0.0) return 0.0f;
    const float byVoice = static_cast<float> (voiced.load() / (kVoiceSeconds * sr));
    const float byTotal = capacity.load() > 0 ? static_cast<float> (written.load()) / static_cast<float> (capacity.load()) : 0.0f;
    return juce::jmin (1.0f, juce::jmax (byVoice, byTotal));
}

void AutoEditController::timerCallback()
{
    if (heardAudio.exchange (false)) { hearingAudio = true; quietTicks = 0; }
    else if (++quietTicks > 20)       hearingAudio = false;

    const int st = stateFlag.load();
    if (st == listening && getProgress() >= 1.0f)
        launchAnalysis();

    // Auto Analyze listens while it's on, there's an Auto-Edit to compare with and the window is open.
    const bool canWatch = watchOn.load() && baselineOk && editorOpen.load();
    if (st == idle && canWatch)
    {
        ++session;
        written.store (0);
        voiced.store (0);
        sideSeen.store (false);
        stateFlag.store (watching, std::memory_order_release);
        ++watchVersion;
    }
    else if (st == watching && ! canWatch)
    {
        stateFlag.store (idle);
        ++watchVersion;
    }
    else if (st == watching && getProgress() >= 1.0f)
        launchCheck();

    if (resultReady.exchange (false))
    {
        std::unique_ptr<vox::AutoEditResult> result;
        std::vector<vox::AnalyzeNeed> found;
        int resultSession = -1;
        bool isCheck = false;
        {
            const juce::ScopedLock sl (resultLock);
            result = std::move (pendingResult);
            found = std::move (pendingNeeds);
            resultSession = pendingSession;
            isCheck = pendingIsCheck;
        }
        if (resultSession == session && ! isCheck && stateFlag.load() == analysing)
        {
            if (result != nullptr)
                apply (*result, pendingSettings);
            stateFlag.store (idle);
        }
        else if (resultSession == session && isCheck && stateFlag.load() == checking)
        {
            finishCheck (found, result.get());
            stateFlag.store (idle);   // listens to the next part from here (above, next tick)
        }
    }
}

void AutoEditController::copyCapture (std::vector<std::vector<float>>& audio, std::shared_ptr<const std::vector<float>>& beat) const
{
    const int n = std::min (written.load(), capacity.load());
    audio = { std::vector<float> (captureL.begin(), captureL.begin() + n) };
    if (stereo.load())
        audio.emplace_back (captureR.begin(), captureR.begin() + n);
    // Vocal Fit: the beat, when the side-chain was on while listening (silent or not: Auto-Edit says which).
    beat.reset();
    if (sideSeen.load())
        beat = std::make_shared<std::vector<float>> (captureBeat.begin(), captureBeat.begin() + n);
}

vox::AutoEditSettings AutoEditController::currentSettings (std::shared_ptr<const vox::ReferenceProfile>& ref)
{
    auto choice = [this] (const char* id) { return juce::roundToInt (state.getRawParameterValue (id)->load()); };
    vox::AutoEditSettings settings;
    settings.style = juce::jlimit (0, vox::kStyles - 1, choice ("aeStyle"));
    settings.intensity = juce::jlimit (0, vox::kIntensities - 1, choice ("aeIntensity"));
    settings.bpm = bpmSource ? bpmSource() : 0.0;
    if (choice ("ptKeySrc") == 0 && liveKeySource)
        liveKeySource (settings);
    settings.unmaskOnBeat = unmaskOnBeatSource && unmaskOnBeatSource();
    {
        const juce::ScopedLock sl (refLock);
        ref = reference;
    }
    return settings;   // (no reference pointer: the job keeps its own copy alive)
}

void AutoEditController::launchAnalysis()
{
    stateFlag.store (analysing);

    std::vector<std::vector<float>> audio;
    std::shared_ptr<const std::vector<float>> beat;
    copyCapture (audio, beat);
    const double sr = sampleRate.load();

    std::shared_ptr<const vox::ReferenceProfile> ref;
    const auto settings = currentSettings (ref);
    pendingSettings = settings;
    pendingRefName = ref != nullptr && ref->ok ? juce::String::fromUTF8 (ref->name.c_str()) : juce::String();

    const int thisSession = session;
    pool.addJob ([this, audio = std::move (audio), sr, settings, thisSession, ref, beat]
    {
        std::unique_ptr<vox::AutoEditResult> result;
        try
        {
            auto s = settings;
            s.reference = ref != nullptr && ref->ok ? ref.get() : nullptr;
            s.beat = beat.get();
            result = std::make_unique<vox::AutoEditResult> (vox::autoEdit (audio, sr, s));
        }
        catch (...)
        {
            result = std::make_unique<vox::AutoEditResult>();
            result->summary = "Something went wrong while analysing. Please try Auto-Edit again.";
        }
        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (result);
            pendingNeeds.clear();
            pendingSession = thisSession;
            pendingIsCheck = false;
        }
        resultReady.store (true);
    });
}

void AutoEditController::setParam (const juce::String& id, float value)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void AutoEditController::apply (const vox::AutoEditResult& r, const vox::AutoEditSettings& s)
{
    auto str = [] (const std::string& t) { return juce::String::fromUTF8 (t.c_str()); };
    auto* doc = new juce::DynamicObject();
    doc->setProperty ("ok", r.ok);
    doc->setProperty ("summary", str (r.summary));
    doc->setProperty ("tipTitle", str (r.tipTitle));
    doc->setProperty ("tip", str (r.tip));
    doc->setProperty ("style", juce::String (vox::kStyleNames[static_cast<size_t> (s.style)]));
    doc->setProperty ("intensity", juce::String (vox::kIntensityNames[static_cast<size_t> (s.intensity)]));
    doc->setProperty ("time", juce::Time::getCurrentTime().toISO8601 (true));
    doc->setProperty ("reference", pendingRefName);

    if (r.ok)
    {
        takeUndoSnapshot (true);

        auto* values = new juce::DynamicObject();
        setParam ("bypass", 0.0f);
        VoxParams::forEachValue (r.params, [&] (const juce::String& id, float v) { setParam (id, v); values->setProperty (id, v); });

        juce::Array<juce::var> notes, tips, reasons, kept;
        for (const auto& n : r.notes) notes.add (str (n));
        for (const auto& t : r.suggestions) tips.add (str (t));
        for (const auto& x : r.reasons)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("module", juce::String (x.module));
            o->setProperty ("control", str (x.control));
            o->setProperty ("value", str (x.value));
            o->setProperty ("why", str (x.why));
            reasons.add (juce::var (o));
        }
        for (bool k : r.kept) kept.add (k);
        doc->setProperty ("notes", notes);
        doc->setProperty ("tips", tips);
        doc->setProperty ("reasons", reasons);
        juce::Array<juce::var> fit;
        for (const auto& x : r.fit)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("control", str (x.control));
            o->setProperty ("value", str (x.value));
            o->setProperty ("why", str (x.why));
            fit.add (juce::var (o));
        }
        doc->setProperty ("fit", fit);
        if (r.fitLevelDb != 0.0)
        {
            auto* fl = new juce::DynamicObject();
            fl->setProperty ("db", r.fitLevelDb);
            fl->setProperty ("full", r.fitLevelFullDb);
            fl->setProperty ("output", juce::jlimit (-24.0, 24.0, r.params.outputDb + r.fitLevelDb));   // the Output knob's range
            doc->setProperty ("fitLevel", juce::var (fl));
        }
        doc->setProperty ("kept", kept);
        doc->setProperty ("values", juce::var (values));
        const auto& a = r.analysis;
        auto* an = new juce::DynamicObject();
        an->setProperty ("inputLufs", a.inputLufs);
        an->setProperty ("outputLufs", r.outputLufs);
        an->setProperty ("peakDb", a.peakDb);
        an->setProperty ("voiceDb", a.voiceRmsDb);
        an->setProperty ("noiseDb", a.noiseFloorDb);
        an->setProperty ("f0", a.f0Median);
        an->setProperty ("sibDb", a.sibilanceDb);
        an->setProperty ("rangeDb", a.rangeDb);
        an->setProperty ("voicedSeconds", a.voicedSeconds);
        // (for Auto Analyze: what it compares each new part with)
        an->setProperty ("heardGaps", a.heardGaps);
        an->setProperty ("clipped", a.clippedRuns);
        an->setProperty ("rumbleDb", a.rumbleDb);
        an->setProperty ("f0Low", a.f0Low);
        an->setProperty ("punchDb", a.microDynDb);
        juce::Array<juce::var> bands, bandHz;
        for (double v : a.bandDb) bands.add (v);
        for (double v : a.bandHz) bandHz.add (v);
        an->setProperty ("bandDb", bands);
        an->setProperty ("bandHz", bandHz);
        doc->setProperty ("analysis", juce::var (an));
        if (r.beat.heard)
        {
            auto* bt = new juce::DynamicObject();
            bt->setProperty ("seconds", r.beat.underVoiceSeconds);
            bt->setProperty ("vocalOverBeatDb", r.beat.vocalOverBeatDb);
            bt->setProperty ("brightDb", r.beat.brightDb);
            bt->setProperty ("lowDb", r.beat.lowDb);
            bt->setProperty ("busyDb", r.beat.busyDb);
            bt->setProperty ("yourVocal", r.beat.hasYourVocal);
            doc->setProperty ("beat", juce::var (bt));
        }
        else if (r.beat.hasYourVocal)
        {
            auto* bt = new juce::DynamicObject();
            bt->setProperty ("heard", false);
            bt->setProperty ("yourVocal", true);
            doc->setProperty ("beat", juce::var (bt));
        }
        dismissed.clear();
        clearNeeds();
    }
    setReport (juce::JSON::toString (juce::var (doc), true));
}

void AutoEditController::setReport (const juce::String& json)
{
    {
        const juce::ScopedLock sl (reportLock);
        report = json;
    }
    ++reportVersion;

    // Auto Analyze compares each new part with what this Auto-Edit heard (older reports keep fewer readings: those are skipped).
    baseline = {};
    baselineBeat = {};
    baselineOk = false;
    const auto doc = juce::JSON::parse (json);
    if (auto* o = doc.getDynamicObject(); o != nullptr && static_cast<bool> (o->getProperty ("ok")))
        if (auto* an = o->getProperty ("analysis").getDynamicObject())
        {
            auto get = [an] (const char* id, double def) { return an->hasProperty (id) ? static_cast<double> (an->getProperty (id)) : def; };
            auto& b = baseline;
            b.voicedSeconds = get ("voicedSeconds", 0.0);
            b.voiceRmsDb = get ("voiceDb", -120.0);
            b.noiseFloorDb = get ("noiseDb", -120.0);
            b.heardGaps = an->hasProperty ("heardGaps") ? static_cast<bool> (an->getProperty ("heardGaps")) : b.noiseFloorDb > -119.0;
            b.clippedRuns = static_cast<int> (get ("clipped", 0.0));
            b.rumbleDb = get ("rumbleDb", -120.0);
            b.f0Low = get ("f0Low", 0.0);
            b.f0Median = get ("f0", 0.0);
            b.sibilanceDb = get ("sibDb", -120.0);
            b.rangeDb = get ("rangeDb", 0.0);
            b.microDynDb = get ("punchDb", 0.0);
            if (auto* arr = an->getProperty ("bandDb").getArray())
                for (const auto& v : *arr) b.bandDb.push_back (v);
            if (auto* bt = o->getProperty ("beat").getDynamicObject())
            {
                baselineBeat.heard = ! bt->hasProperty ("heard") || static_cast<bool> (bt->getProperty ("heard"));
                baselineBeat.vocalOverBeatDb = bt->getProperty ("vocalOverBeatDb");
                baselineBeat.hasYourVocal = bt->hasProperty ("yourVocal") && static_cast<bool> (bt->getProperty ("yourVocal"));
            }
            baselineOk = b.voicedSeconds > 0.0 && b.voiceRmsDb > -119.0;
        }
    if (! baselineOk)
        clearNeeds();
    ++watchVersion;
}

// =================================================================================================
// Auto Analyze
void AutoEditController::setWatchOn (bool on)
{
    watchOn.store (on);
    if (! on)
        clearNeeds();
    ++watchVersion;
}

int AutoEditController::getWatchState() const noexcept
{
    if (! watchOn.load()) return 0;
    if (! baselineOk) return 1;
    switch (stateFlag.load())
    {
        case watching: return 2;
        case checking: return 3;
        case listening: case analysing: return 4;
        default: return 2;   // (between parts: listening again on the next tick)
    }
}

void AutoEditController::clearNeeds()
{
    needs.clear();
    checks = 0;
    lastFine = false;
    lastCheck = {};
    ++watchVersion;
}

void AutoEditController::launchCheck()
{
    stateFlag.store (checking);
    ++watchVersion;

    std::vector<std::vector<float>> audio;
    std::shared_ptr<const std::vector<float>> beat;
    copyCapture (audio, beat);
    const double sr = sampleRate.load();
    std::shared_ptr<const vox::ReferenceProfile> ref;
    const auto settings = currentSettings (ref);
    const auto was = baseline;
    const auto wasBeat = baselineBeat;
    const auto skip = dismissed;

    const int thisSession = session;
    pool.addJob ([this, audio = std::move (audio), sr, settings, thisSession, ref, beat, was, wasBeat, skip]
    {
        std::unique_ptr<vox::AutoEditResult> result;
        std::vector<vox::AnalyzeNeed> found;
        try
        {
            // First the measurements alone (a fraction of a second) ...
            const auto now = vox::analyseVocal (audio, sr);
            const auto nowBeat = beat != nullptr ? vox::analyseBeat (*beat, audio, sr) : vox::BeatAnalysis {};
            for (auto& n : vox::findNewNeeds (was, wasBeat, now, nowBeat))
                if (! skip.contains (juce::String (n.key)))
                    found.push_back (std::move (n));
            // ... and only when something needs a new setting, the full Auto-Edit on this part (seconds).
            const bool needSettings = std::any_of (found.begin(), found.end(), [] (const vox::AnalyzeNeed& n) { return ! n.retune.empty() || n.fitLevel; });
            if (needSettings)
            {
                auto s = settings;
                s.reference = ref != nullptr && ref->ok ? ref.get() : nullptr;
                s.beat = beat.get();
                result = std::make_unique<vox::AutoEditResult> (vox::autoEdit (audio, sr, s));
            }
        }
        catch (...)
        {
            found.clear();
            result.reset();
        }
        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (result);
            pendingNeeds = std::move (found);
            pendingSession = thisSession;
            pendingIsCheck = true;
        }
        resultReady.store (true);
    });
}

namespace {
/** The controls a module's retune may move (its On switch stays as it is). */
bool belongsTo (const juce::String& id, vox::Module m)
{
    if (id.endsWith ("On")) return false;
    using M = vox::Module;
    return (m == M::cleanup && id.startsWith ("cl")) || (m == M::eq && id.startsWith ("eq")) || (m == M::dynEq && id.startsWith ("dq"))
        || (m == M::deEsser && id.startsWith ("ds")) || (m == M::rider && id.startsWith ("rd")) || (m == M::comp && id.startsWith ("cp"));
}

const char* moduleKey (const juce::String& id)
{
    if (id.startsWith ("cl")) return "cleanup";
    if (id.startsWith ("eq")) return "eq";
    if (id.startsWith ("dq")) return "dyneq";
    if (id.startsWith ("ds")) return "deess";
    if (id.startsWith ("rd")) return "rider";
    if (id.startsWith ("cp")) return "comp";
    return "out";
}
}

void AutoEditController::finishCheck (const std::vector<vox::AnalyzeNeed>& found, const vox::AutoEditResult* result)
{
    ++checks;
    lastCheck = juce::Time::getCurrentTime().toISO8601 (true);
    lastFine = true;
    auto current = [this] (const juce::String& id) { auto* p = state.getParameter (id); return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f; };

    for (const auto& n : found)
    {
        juce::Array<juce::var> changes;
        auto addChange = [&] (const juce::String& id, float to)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("module", juce::String (moduleKey (id)));
            o->setProperty ("from", current (id));
            o->setProperty ("to", to);
            changes.add (juce::var (o));
        };
        if (result != nullptr && result->ok)
        {
            std::map<juce::String, float> fresh;
            VoxParams::forEachValue (result->params, [&] (const juce::String& id, float v) { fresh[id] = v; });
            for (const auto& [id, v] : fresh)
            {
                if (! std::any_of (n.retune.begin(), n.retune.end(), [&id] (vox::Module m) { return belongsTo (id, m); }))
                    continue;
                auto* p = state.getParameter (id);
                if (p == nullptr || std::abs (p->convertTo0to1 (v) - p->getValue()) < 0.03f)
                    continue;   // the same as now (or as near as makes no difference)
                // A band's frequency only matters when the band does something (now or after).
                if (id.startsWith ("eqFreq") || id.startsWith ("dqFreq"))
                {
                    const auto gainId = (id.startsWith ("eq") ? "eqGain" : "dqCut") + id.getLastCharacters (1);
                    if (std::abs (current (gainId)) < 0.5f && std::abs (fresh[gainId]) < 0.5f)
                        continue;
                }
                addChange (id, v);
            }
            if (n.fitLevel && std::abs (result->fitLevelDb) > 0.01)
            {
                const auto to = static_cast<float> (juce::jlimit (-24.0, 24.0, result->params.outputDb + result->fitLevelDb));
                if (std::abs (to - current ("outGain")) >= 1.0f)
                    addChange ("outGain", to);
            }
        }
        const bool fixable = ! n.retune.empty() || n.fitLevel;
        if (fixable && changes.isEmpty())
            continue;   // your settings already handle it
        lastFine = false;

        Shown shown { juce::String (n.key), juce::String::fromUTF8 (n.note.c_str()), changes };
        auto it = std::find_if (needs.begin(), needs.end(), [&shown] (const Shown& x) { return x.key == shown.key; });
        if (it != needs.end()) *it = std::move (shown);   // the newest reading of the same need
        else                   needs.push_back (std::move (shown));
    }
    ++watchVersion;
}

bool AutoEditController::applyNeed (const juce::String& key)
{
    auto it = std::find_if (needs.begin(), needs.end(), [&key] (const Shown& x) { return x.key == key; });
    if (it == needs.end())
        return false;
    if (auto* arr = it->changes.getArray(); arr != nullptr && ! arr->isEmpty())
    {
        takeUndoSnapshot (false);   // UNDO puts these back (the Auto-Edit report stays)
        for (const auto& c : *arr)
            setParam (c["id"].toString(), static_cast<float> (c["to"]));
    }
    needs.erase (it);
    ++watchVersion;
    return true;
}

bool AutoEditController::dismissNeed (const juce::String& key)
{
    auto it = std::find_if (needs.begin(), needs.end(), [&key] (const Shown& x) { return x.key == key; });
    if (it == needs.end())
        return false;
    dismissed.addIfNotAlreadyThere (key);
    needs.erase (it);
    ++watchVersion;
    return true;
}

juce::String AutoEditController::getWatchJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("on", watchOn.load());
    o->setProperty ("state", getWatchState());
    o->setProperty ("checks", checks);
    o->setProperty ("fine", lastFine);
    o->setProperty ("time", lastCheck);
    juce::Array<juce::var> list;
    for (const auto& n : needs)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("key", n.key);
        x->setProperty ("note", n.note);
        x->setProperty ("changes", n.changes);
        list.add (juce::var (x));
    }
    o->setProperty ("needs", list);
    return juce::JSON::toString (juce::var (o), true);
}

void AutoEditController::onStateRestored (const juce::String& savedReport)
{
    undoValid.store (false);
    setReport (savedReport);
}

juce::String AutoEditController::getReportForSaving() const
{
    const juce::ScopedLock sl (reportLock);
    return report;
}

void AutoEditController::takeUndoSnapshot (bool byAutoEdit)
{
    undoSnapshot.clear();
    undoValid.store (true);
    undoClearsReport = byAutoEdit;
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (! id.startsWith ("ae") && id != "listenA" && id != "levelMatch")
                undoSnapshot.emplace_back (rp, rp->getValue());
        }
}

bool AutoEditController::undo()
{
    if (! canUndo())
        return false;
    for (auto& [p, v] : undoSnapshot)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    }
    undoSnapshot.clear();
    undoValid.store (false);
    if (undoClearsReport) setReport ({});
    return true;
}

namespace {
juce::String profileToJson (const vox::ReferenceProfile& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", r.ok);
    o->setProperty ("name", juce::String::fromUTF8 (r.name.c_str()));
    o->setProperty ("problem", juce::String::fromUTF8 (r.problem.c_str()));
    o->setProperty ("warning", juce::String::fromUTF8 (r.warning.c_str()));
    o->setProperty ("about", juce::String::fromUTF8 (r.about.c_str()));
    o->setProperty ("builtin", r.builtin);
    o->setProperty ("split", r.split);
    o->setProperty ("sib", r.sibilanceDb);
    o->setProperty ("punch", r.microDynDb);
    o->setProperty ("tail", r.tailDb);
    o->setProperty ("voiced", r.voicedSeconds);
    o->setProperty ("f0", r.f0Median);
    juce::Array<juce::var> bands;
    for (double v : r.bandDb) bands.add (v);
    o->setProperty ("bands", bands);
    return juce::JSON::toString (juce::var (o), true);
}

std::shared_ptr<vox::ReferenceProfile> profileFromJson (const juce::String& saved)
{
    const auto v = juce::JSON::parse (saved);
    auto* o = v.getDynamicObject();
    if (o == nullptr) return nullptr;
    auto r = std::make_shared<vox::ReferenceProfile>();
    r->ok = o->getProperty ("ok");
    r->name = o->getProperty ("name").toString().toStdString();
    r->problem = o->getProperty ("problem").toString().toStdString();
    r->warning = o->getProperty ("warning").toString().toStdString();
    r->about = o->getProperty ("about").toString().toStdString();
    r->builtin = o->hasProperty ("builtin") && static_cast<bool> (o->getProperty ("builtin"));
    r->split = o->hasProperty ("split") ? static_cast<bool> (o->getProperty ("split")) : vox::looksSplit (r->name);   // (older files: by name)
    r->sibilanceDb = o->getProperty ("sib");
    r->microDynDb = o->getProperty ("punch");
    r->tailDb = o->getProperty ("tail");
    r->voicedSeconds = o->getProperty ("voiced");
    r->f0Median = o->hasProperty ("f0") ? static_cast<double> (o->getProperty ("f0")) : 0.0;
    if (auto* arr = o->getProperty ("bands").getArray())
        for (const auto& b : *arr) r->bandDb.push_back (b);
    if (r->bandDb.size() != vox::analysisBands().size()) r->ok = false;   // from an older version: re-load it
    return r;
}

juce::File userRefFile (const juce::String& name)
{
    return AutoEditController::userReferenceFolder().getChildFile (juce::File::createLegalFileName (name) + ".json");
}
}

// =================================================================================================
// Reference Match
void AutoEditController::loadReference (const juce::File& file)
{
    if (! file.existsAsFile())
        return;
    refLoading.store (true);
    ++refVersion;
    pool.addJob ([this, file]
    {
        auto profile = std::make_shared<vox::ReferenceProfile>();
        profile->name = file.getFileNameWithoutExtension().toStdString();
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            profile->problem = "Couldn't read this file. Use a WAV, AIFF, FLAC, MP3 or OGG file.";
        else
        {
            const double sr = reader->sampleRate;
            const auto total = reader->lengthInSamples;
            const auto want = static_cast<juce::int64> (kRefSeconds * sr);
            const auto start = total > want ? (total - want) / 2 : juce::int64 (0);   // the middle: past the intro
            const int n = static_cast<int> (std::min (total, want));
            const int nch = static_cast<int> (std::min (2u, reader->numChannels));
            juce::AudioBuffer<float> buf (nch, n);
            reader->read (&buf, 0, n, start, true, nch > 1);
            std::vector<std::vector<float>> audio (static_cast<size_t> (nch));
            for (int c = 0; c < nch; ++c) audio[static_cast<size_t> (c)].assign (buf.getReadPointer (c), buf.getReadPointer (c) + n);
            *profile = vox::analyseReference (audio, sr, profile->name);
            if (profile->ok)
            {
                // Keep its measurements in your reference library, so it's there in every project.
                const auto folder = userReferenceFolder();
                if (folder.createDirectory())
                    userRefFile (juce::String::fromUTF8 (profile->name.c_str())).replaceWithText (profileToJson (*profile));
            }
        }
        {
            const juce::ScopedLock sl (refLock);
            reference = std::move (profile);
        }
        refLoading.store (false);
        ++refVersion;
    });
}

void AutoEditController::clearReference()
{
    {
        const juce::ScopedLock sl (refLock);
        reference.reset();
    }
    ++refVersion;
}

juce::String AutoEditController::getReferenceJson() const
{
    auto* o = new juce::DynamicObject();
    std::shared_ptr<const vox::ReferenceProfile> r;
    {
        const juce::ScopedLock sl (refLock);
        r = reference;
    }
    o->setProperty ("state", refLoading.load() ? "loading" : r == nullptr ? "none" : r->ok ? "ok" : "problem");
    if (r != nullptr)
    {
        o->setProperty ("name", juce::String::fromUTF8 (r->name.c_str()));
        o->setProperty ("problem", juce::String::fromUTF8 (r->problem.c_str()));
        o->setProperty ("warning", juce::String::fromUTF8 (r->warning.c_str()));
        o->setProperty ("seconds", r->voicedSeconds);
        o->setProperty ("about", juce::String::fromUTF8 (r->about.c_str()));
        o->setProperty ("builtin", r->builtin);
        o->setProperty ("split", r->split);
    }
    return juce::JSON::toString (juce::var (o), true);
}

juce::String AutoEditController::getReferenceForSaving() const
{
    std::shared_ptr<const vox::ReferenceProfile> r;
    {
        const juce::ScopedLock sl (refLock);
        r = reference;
    }
    return r == nullptr ? juce::String() : profileToJson (*r);
}

void AutoEditController::restoreReference (const juce::String& saved)
{
    std::shared_ptr<vox::ReferenceProfile> r = saved.isEmpty() ? nullptr : profileFromJson (saved);
    // A built-in saved by an older version: today's version of it (renamed in v0.28, measurements may be updated).
    if (r != nullptr && r->builtin)
        if (const auto* b = vox::findBuiltinReference (r->name); b != nullptr) r = std::make_shared<vox::ReferenceProfile> (*b);
    {
        const juce::ScopedLock sl (refLock);
        reference = std::move (r);
    }
    ++refVersion;
}

juce::File AutoEditController::userReferenceFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Voxology").getChildFile ("References");
}

juce::String AutoEditController::listReferencesJson() const
{
    juce::Array<juce::var> builtin, yours;
    for (const auto& r : vox::builtinReferences())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String::fromUTF8 (r.name.c_str()));
        o->setProperty ("about", juce::String::fromUTF8 (r.about.c_str()));
        o->setProperty ("section", juce::String::fromUTF8 (r.section.c_str()));
        o->setProperty ("pitch", juce::String::fromUTF8 (r.pitch.c_str()));
        builtin.add (juce::var (o));
    }
    auto files = userReferenceFolder().findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();
    for (const auto& f : files)
        if (auto r = profileFromJson (f.loadFileAsString()); r != nullptr && r->ok)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", juce::String::fromUTF8 (r->name.c_str()));
            o->setProperty ("split", r->split);
            yours.add (juce::var (o));
        }
    std::shared_ptr<const vox::ReferenceProfile> cur;
    {
        const juce::ScopedLock sl (refLock);
        cur = reference;
    }
    auto* res = new juce::DynamicObject();
    res->setProperty ("builtin", builtin);
    res->setProperty ("yours", yours);
    res->setProperty ("folder", userReferenceFolder().getFullPathName());
    res->setProperty ("current", cur == nullptr ? juce::String() : juce::String::fromUTF8 (cur->name.c_str()));
    return juce::JSON::toString (juce::var (res), true);
}

void AutoEditController::selectReference (bool builtin, const juce::String& name)
{
    std::shared_ptr<vox::ReferenceProfile> r;
    if (builtin)
    {
        if (const auto* b = vox::findBuiltinReference (name.toStdString()); b != nullptr)
            r = std::make_shared<vox::ReferenceProfile> (*b);
    }
    else if (const auto f = userRefFile (name); f.existsAsFile())
        r = profileFromJson (f.loadFileAsString());
    if (r == nullptr) return;
    {
        const juce::ScopedLock sl (refLock);
        reference = std::move (r);
    }
    ++refVersion;
}

void AutoEditController::setReferenceSplit (const juce::String& name, bool split)
{
    // Your saved reference, and the one in use if it's that one.
    if (const auto f = userRefFile (name); f.existsAsFile())
        if (auto r = profileFromJson (f.loadFileAsString()); r != nullptr)
        {
            r->split = split;
            f.replaceWithText (profileToJson (*r));
        }
    {
        const juce::ScopedLock sl (refLock);
        if (reference != nullptr && ! reference->builtin && juce::String::fromUTF8 (reference->name.c_str()) == name)
        {
            auto r = std::make_shared<vox::ReferenceProfile> (*reference);
            r->split = split;
            reference = std::move (r);
        }
    }
    ++refVersion;
}

void AutoEditController::deleteUserReference (const juce::String& name)
{
    userRefFile (name).deleteFile();
    ++refVersion;
}
