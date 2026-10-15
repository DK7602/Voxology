#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "vox/AutoAnalyze.h"
#include "vox/AutoEdit.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

/** Runs Auto-Edit inside the plug-in.

    1. start(): the audio thread copies the incoming vocal (before any processing) into a buffer, and
       the beat on the side-chain (when it's on) into another at the same moments (Vocal Fit).
       Exact digital silence (transport stopped) is skipped; the gaps between phrases are kept,
       because Auto-Edit needs them to hear your room noise. It stops once it has heard
       kVoiceSeconds of voice (or kMaxSeconds in all).
    2. A background thread runs vox::autoEdit() on it.
    3. Back on the message thread the chosen values are set as normal host parameter changes (the
       DAW sees them; they can be automated or undone), a snapshot is kept for Undo, and the report
       is saved with the plug-in state.

    The capture buffer is allocated once, for the highest supported sample rate, so nothing ever
    reallocates while the audio thread may be writing. Each run has a session number; a result from
    an older session is thrown away.

    Auto Analyze (after an Auto-Edit, when switched on): the same buffers keep listening while the song plays and the
    window is open. Every kVoiceSeconds of voice a background check measures the part (vox::findNewNeeds against what
    Auto-Edit heard); only when it finds something new does it run the full Auto-Edit on the part, and each need
    becomes a card in Learn with the knob moves that fix it (now -> new). Nothing changes until APPLY. */
class AutoEditController final : private juce::Timer
{
public:
    static constexpr double kVoiceSeconds = 12.0;
    static constexpr double kMaxSeconds = 30.0;
    static constexpr double kMaxSampleRate = 192000.0;
    enum State { idle = 0, listening = 1, analysing = 2, watching = 3, checking = 4 };   // watching / checking: Auto Analyze

    AutoEditController (juce::AudioProcessorValueTreeState& state, std::function<double()> getBpm);
    ~AutoEditController() override;

    void prepare (double sampleRate);

    /** Audio thread: records the vocal (and the beat on the side-chain, side = nullptr when it's off) while
        listening. Never allocates or locks. */
    template <typename Sample>
    void capture (const juce::AudioBuffer<Sample>& buffer, int numInputChannels, const juce::AudioBuffer<Sample>* side) noexcept
    {
        const int st = stateFlag.load (std::memory_order_acquire);
        if (st != listening && (st != watching || offline.load (std::memory_order_relaxed)))
            return;
        const int nch = std::min (numInputChannels, buffer.getNumChannels()), n = buffer.getNumSamples();
        if (nch <= 0 || n == 0)
            return;
        Sample peak = 0;
        for (int c = 0; c < nch; ++c)
            peak = std::max (peak, buffer.getMagnitude (c, 0, n));
        if (peak < static_cast<Sample> (1.0e-5))   // -100 dBFS: transport stopped, not a gap
            return;
        if (peak > static_cast<Sample> (0.01))     // -40 dBFS: someone is singing
        {
            heardAudio.store (true, std::memory_order_relaxed);
            voiced.fetch_add (n, std::memory_order_relaxed);
        }
        int w = written.load (std::memory_order_acquire);
        const int cap = capacity.load (std::memory_order_acquire);
        const int todo = std::min (n, cap - w);
        if (todo <= 0)
            return;
        const Sample* l = buffer.getReadPointer (0);
        const Sample* r = buffer.getReadPointer (std::min (1, nch - 1));
        for (int i = 0; i < todo; ++i)
        {
            captureL[static_cast<size_t> (w + i)] = static_cast<float> (l[i]);
            captureR[static_cast<size_t> (w + i)] = static_cast<float> (r[i]);
        }
        const int scn = side != nullptr ? std::min (side->getNumChannels(), 2) : 0;
        if (scn > 0 && side->getNumSamples() >= todo)
        {
            sideSeen.store (true, std::memory_order_relaxed);
            const Sample* a = side->getReadPointer (0);
            const Sample* b = side->getReadPointer (scn - 1);
            for (int i = 0; i < todo; ++i)
                captureBeat[static_cast<size_t> (w + i)] = static_cast<float> (0.5 * (static_cast<double> (a[i]) + static_cast<double> (b[i])));
        }
        else
            std::fill_n (captureBeat.begin() + w, todo, 0.0f);
        stereo.store (nch > 1, std::memory_order_relaxed);
        written.compare_exchange_strong (w, w + todo, std::memory_order_acq_rel);
    }

    bool start();
    void cancel();
    bool undo();

    void onStateRestored (const juce::String& savedReport);
    juce::String getReportForSaving() const;

    /** Reference Match: reads an audio file on a background thread (WAV, AIFF, FLAC, MP3, OGG; up to
        kRefSeconds from the middle) and keeps only its measurements, which are saved with the project. */
    static constexpr double kRefSeconds = 120.0;
    void loadReference (const juce::File& file);
    void clearReference();
    juce::String getReferenceJson() const;            // for the UI: state, name, problem, warning
    juce::String getReferenceForSaving() const;       // the measurements (no audio)
    void restoreReference (const juce::String& saved);

    /** The reference library: the built-in pro references, plus every vocal file you've loaded (its measurements are
        saved as a small .json in userReferenceFolder(), so it's there in every project). */
    static juce::File userReferenceFolder();
    juce::String listReferencesJson() const;          // { builtin: [{name, about, section, pitch}], yours: [{name}], folder, current }
    void selectReference (bool builtin, const juce::String& name);
    void deleteUserReference (const juce::String& name);
    /** Marks one of your references as split from a full song (or not): then its low end and space aren't used. */
    void setReferenceSplit (const juce::String& name, bool split);
    int getReferenceVersion() const noexcept { return refVersion.load(); }

    /** Auto-Edit's state (Auto Analyze's listening shows as idle: the AUTO-EDIT button is free). */
    State getState() const noexcept           { const int st = stateFlag.load(); return st >= watching ? idle : static_cast<State> (st); }
    float getProgress() const noexcept;
    bool isHearingAudio() const noexcept       { return hearingAudio; }
    bool canUndo() const noexcept              { return undoValid.load() && ! undoSnapshot.empty(); }
    /** Keeps every setting for UNDO (Auto-Edit, or a chain / look loaded: then UNDO leaves the report alone). */
    void takeUndoSnapshot (bool byAutoEdit);
    int getReportVersion() const noexcept      { return reportVersion.load(); }
    juce::String getReportJson() const         { return getReportForSaving(); }

    // Auto Analyze
    void setWatchOn (bool on);
    bool isWatchOn() const noexcept            { return watchOn.load(); }
    /** What the last Auto-Edit heard in the recording itself (for the suite link: Mixology says fix it first). */
    float recordedPeakDb() const noexcept      { return recordedPeak.load(); }
    int recordedClippedSpots() const noexcept  { return recordedClips.load(); }
    void setEditorOpen (bool open) noexcept    { editorOpen.store (open); }
    void setOffline (bool isOffline) noexcept  { offline.store (isOffline, std::memory_order_relaxed); }
    /** 0 = off, 1 = waiting for an Auto-Edit, 2 = listening, 3 = checking, 4 = paused (Auto-Edit running). */
    int getWatchState() const noexcept;
    float getWatchProgress() const noexcept    { return stateFlag.load() == watching ? getProgress() : 0.0f; }
    int getWatchVersion() const noexcept       { return watchVersion.load(); }
    juce::String getWatchJson() const;
    bool applyNeed (const juce::String& key);
    bool dismissNeed (const juce::String& key);

private:
    void timerCallback() override;
    void launchAnalysis();
    void apply (const vox::AutoEditResult& result, const vox::AutoEditSettings& settings);
    void setParam (const juce::String& id, float value);
    void setReport (const juce::String& json);
    void copyCapture (std::vector<std::vector<float>>& audio, std::shared_ptr<const std::vector<float>>& beat) const;
    vox::AutoEditSettings currentSettings (std::shared_ptr<const vox::ReferenceProfile>& ref);
    void launchCheck();
    void finishCheck (const std::vector<vox::AnalyzeNeed>& needs, const vox::AutoEditResult* result);
    void clearNeeds();

    juce::AudioProcessorValueTreeState& state;
    std::function<double()> bpmSource;
public:
    /** The key Pitch follows right now (Key on Auto), if any: fills settings.beatKey* (message thread). */
    std::function<void (vox::AutoEditSettings&)> liveKeySource;
    /** Is there a Voxology in BEAT mode in this project (Unmask can already make room)? (message thread) */
    std::function<bool()> unmaskOnBeatSource;
private:

    std::vector<float> captureL, captureR, captureBeat;
    std::atomic<int> capacity { 0 };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> written { 0 }, voiced { 0 };
    std::atomic<int> stateFlag { idle };
    std::atomic<bool> heardAudio { false }, stereo { false }, sideSeen { false };
    bool hearingAudio = false;
    int quietTicks = 0;
    int session = 0;

    juce::CriticalSection resultLock;
    std::unique_ptr<vox::AutoEditResult> pendingResult;
    int pendingSession = -1;
    vox::AutoEditSettings pendingSettings;
    std::atomic<bool> resultReady { false };

    // Auto Analyze
    struct Shown { juce::String key, note; juce::var changes; };   // changes: [{ id, module, from, to }]
    std::atomic<bool> watchOn { false }, editorOpen { false }, offline { false };
    bool baselineOk = false;                  // the report has Auto-Edit's measurements to compare with
    std::atomic<float> recordedPeak { -100.0f };
    std::atomic<int> recordedClips { 0 };
    vox::VocalAnalysis baseline;
    vox::BeatAnalysis baselineBeat;
    std::vector<Shown> needs;                 // message thread
    juce::StringArray dismissed;              // not shown again until the next Auto-Edit
    int checks = 0;
    bool lastFine = false;
    juce::String lastCheck;                   // ISO time of the last check
    std::atomic<int> watchVersion { 0 };
    std::vector<vox::AnalyzeNeed> pendingNeeds;
    bool pendingIsCheck = false;

    std::vector<std::pair<juce::RangedAudioParameter*, float>> undoSnapshot;
    std::atomic<bool> undoValid { false };
    bool undoClearsReport = true;
    std::atomic<int> reportVersion { 0 };
    mutable juce::CriticalSection reportLock;
    juce::String report;

    mutable juce::CriticalSection refLock;
    std::shared_ptr<const vox::ReferenceProfile> reference;
    juce::String pendingRefName;
    std::atomic<bool> refLoading { false };
    std::atomic<int> refVersion { 0 };

    juce::ThreadPool pool { 1 };              // last: destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoEditController)
};
