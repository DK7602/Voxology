#include "AutoEditController.h"
#include "Params.h"

AutoEditController::AutoEditController (juce::AudioProcessorValueTreeState& s, std::function<double()> getBpm)
    : state (s), bpmSource (std::move (getBpm))
{
    const auto maxSamples = static_cast<size_t> (kMaxSampleRate * kMaxSeconds);
    captureL.assign (maxSamples, 0.0f);
    captureR.assign (maxSamples, 0.0f);
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
    if (std::abs (old - newSampleRate) > 0.5 && stateFlag.load() == listening)
    {
        written.store (0);   // different sample rate: start the listening over
        voiced.store (0);
    }
}

bool AutoEditController::start()
{
    if (getState() == analysing || capacity.load() == 0)
        return false;
    ++session;
    written.store (0);
    voiced.store (0);
    heardAudio.store (false);
    hearingAudio = false;
    quietTicks = 0;
    stateFlag.store (listening, std::memory_order_release);
    startTimerHz (15);
    return true;
}

void AutoEditController::cancel()
{
    if (getState() == listening)
    {
        stateFlag.store (idle);
        stopTimer();
    }
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

    if (getState() == listening && getProgress() >= 1.0f)
        launchAnalysis();

    if (resultReady.exchange (false))
    {
        std::unique_ptr<vox::AutoEditResult> result;
        int resultSession = -1;
        {
            const juce::ScopedLock sl (resultLock);
            result = std::move (pendingResult);
            resultSession = pendingSession;
        }
        if (resultSession == session && getState() == analysing)
        {
            if (result != nullptr)
                apply (*result, pendingSettings);
            stateFlag.store (idle);
            stopTimer();
        }
    }
}

void AutoEditController::launchAnalysis()
{
    stateFlag.store (analysing);

    const int n = std::min (written.load(), capacity.load());
    std::vector<std::vector<float>> audio { std::vector<float> (captureL.begin(), captureL.begin() + n) };
    if (stereo.load())
        audio.emplace_back (captureR.begin(), captureR.begin() + n);
    const double sr = sampleRate.load();

    auto choice = [this] (const char* id) { return juce::roundToInt (state.getRawParameterValue (id)->load()); };
    vox::AutoEditSettings settings;
    settings.style = juce::jlimit (0, vox::kStyles - 1, choice ("aeStyle"));
    settings.intensity = juce::jlimit (0, vox::kIntensities - 1, choice ("aeIntensity"));
    settings.bpm = bpmSource ? bpmSource() : 0.0;
    pendingSettings = settings;

    const int thisSession = session;
    pool.addJob ([this, audio = std::move (audio), sr, settings, thisSession]
    {
        std::unique_ptr<vox::AutoEditResult> result;
        try
        {
            result = std::make_unique<vox::AutoEditResult> (vox::autoEdit (audio, sr, settings));
        }
        catch (...)
        {
            result = std::make_unique<vox::AutoEditResult>();
            result->summary = "Something went wrong while analysing. Please try Auto-Edit again.";
        }
        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (result);
            pendingSession = thisSession;
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

    if (r.ok)
    {
        undoSnapshot.clear();
        undoValid.store (true);
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto id = rp->getParameterID();
                if (! id.startsWith ("ae") && id != "listenA" && id != "levelMatch")
                    undoSnapshot.emplace_back (rp, rp->getValue());
            }

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
        juce::Array<juce::var> bands, bandHz;
        for (double v : a.bandDb) bands.add (v);
        for (double v : a.bandHz) bandHz.add (v);
        an->setProperty ("bandDb", bands);
        an->setProperty ("bandHz", bandHz);
        doc->setProperty ("analysis", juce::var (an));
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
    setReport ({});
    return true;
}
