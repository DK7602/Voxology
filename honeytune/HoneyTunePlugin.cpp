// Honey Tune: the note editor, as an ARA plug-in (applied to a clip, like Melodyne).
//
// Each audio source (the clip's recording) is read in full, analysed into notes (vox::honey) on a
// background thread, and rendered with the document's settings plus your hand edits per note. The
// playback renderer plays the rendered audio; until it's ready the host plays the clip untouched.
// Settings and note edits are saved in the host's project through the ARA archive.

#include <juce_audio_utils/juce_audio_utils.h>

#include "HoneyPanel.h"

#include <map>
#include <memory>

using namespace juce;
using honeyui::Settings;
using honeyui::NoteEdit;

namespace
{
    constexpr int64 kArchiveV2 = 0x484e5932;   // "HNY2"

    /** A hand edit as saved: tied to the note's start, so it finds its note again after re-analysis. */
    struct SavedEdit { double start = 0.0; NoteEdit edit; };

    /** What we know about one recording, shared between the model, the background jobs, the editor
        and the audio thread. The rendered audio is swapped in under a spin lock (the audio thread only tries). */
    struct SourceState
    {
        std::atomic<bool> alive { true };
        std::atomic<int> status { 0 };   // 0 waiting, 1 listening, 2 ready, 3 couldn't read
        std::atomic<bool> samplesChanged { true };
        std::string persistentId, name;

        // Background thread only
        double sampleRate = 48000.0;
        std::vector<std::vector<float>> original;   // per channel

        // Shared (dataLock)
        CriticalSection dataLock;
        std::shared_ptr<const vox::honey::Track> track;
        std::vector<vox::honey::Note> notes;
        vox::KeyGuess guess;
        std::vector<NoteEdit> edits;           // one per note
        std::vector<SavedEdit> pending;        // restored from the project, waiting for the notes

        // Render requests (coalesced: one job at a time, re-run if asked again meanwhile)
        std::atomic<int> wanted { 0 };
        std::atomic<bool> queued { false };

        SpinLock lock;
        std::shared_ptr<const std::vector<std::vector<float>>> rendered;

        std::shared_ptr<const std::vector<std::vector<float>>> getRendered()
        {
            const SpinLock::ScopedTryLockType sl (lock);
            return sl.isLocked() ? rendered : nullptr;
        }

        /** Hand edits re-attached to the notes by start time (within 30 ms). Call with dataLock held. */
        void attach (const std::vector<SavedEdit>& saved)
        {
            edits.assign (notes.size(), {});
            const double tolerance = 0.03 * (track != nullptr ? track->sampleRate : 48000.0);
            for (const auto& s : saved)
            {
                int best = -1;
                double bestDist = tolerance;
                for (size_t i = 0; i < notes.size(); ++i)
                    if (const double d = std::abs (notes[i].start - s.start); d <= bestDist) { bestDist = d; best = static_cast<int> (i); }
                if (best >= 0) edits[static_cast<size_t> (best)] = s.edit;
            }
        }

        /** The hand edits as saved. Call with dataLock held. */
        std::vector<SavedEdit> saved() const
        {
            if (track == nullptr) return pending;
            std::vector<SavedEdit> out;
            for (size_t i = 0; i < notes.size() && i < edits.size(); ++i)
                if (! edits[i].isDefault()) out.push_back ({ notes[i].start, edits[i] });
            return out;
        }
    };
}

//==============================================================================
class HoneyAudioSource final : public ARAAudioSource
{
public:
    using ARAAudioSource::ARAAudioSource;
    ~HoneyAudioSource() override { state->alive = false; }
    std::shared_ptr<SourceState> state = std::make_shared<SourceState>();
};

//==============================================================================
class HoneyDocumentController;

class HoneyPlaybackRenderer final : public ARAPlaybackRenderer
{
public:
    using ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay (double sampleRateIn, int maximumSamplesPerBlockIn, int numChannelsIn,
                        AudioProcessor::ProcessingPrecision, AlwaysNonRealtime) override
    {
        sampleRate = sampleRateIn;
        numChannels = numChannelsIn;
        maxBlock = maximumSamplesPerBlockIn;
    }

    bool processBlock (AudioBuffer<float>& buffer, AudioProcessor::Realtime,
                       const AudioPlayHead::PositionInfo& positionInfo) noexcept override
    {
        buffer.clear();
        if (! positionInfo.getIsPlaying())
            return true;
        const auto numSamples = buffer.getNumSamples();
        const auto blockStart = positionInfo.getTimeInSamples().orFallback (0);
        const auto blockRange = Range<int64>::withStartAndLength (blockStart, numSamples);
        bool ok = true;

        for (auto* region : getPlaybackRegions())
        {
            const auto songRange = region->getSampleRange (sampleRate, ARAPlaybackRegion::IncludeHeadAndTail::no);
            auto renderRange = blockRange.getIntersectionWith (songRange);
            if (renderRange.isEmpty())
                continue;
            const Range<int64> modRange { region->getStartInAudioModificationSamples(), region->getEndInAudioModificationSamples() };
            const auto offset = modRange.getStart() - songRange.getStart();
            renderRange = renderRange.getIntersectionWith (modRange.movedToStartAt (songRange.getStart()));
            if (renderRange.isEmpty())
                continue;

            auto* source = static_cast<HoneyAudioSource*> (region->getAudioModification()->getAudioSource());
            auto rendered = source->state->getRendered();
            if (rendered == nullptr || rendered->empty() || std::abs (source->getSampleRate() - sampleRate) > 0.5)
            {
                ok = false;   // not ready (or a sample-rate mismatch): the host plays the clip as it is
                continue;
            }
            const auto& chans = *rendered;
            const int start = static_cast<int> (renderRange.getStart() - blockStart);
            const auto srcStart = renderRange.getStart() + offset;
            const int len = static_cast<int> (renderRange.getLength());
            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                const auto& ch = chans[static_cast<size_t> (std::min<int> (c, static_cast<int> (chans.size()) - 1))];
                auto* out = buffer.getWritePointer (c, start);
                for (int i = 0; i < len; ++i)
                {
                    const auto s = srcStart + i;
                    out[i] += (s >= 0 && s < static_cast<int64> (ch.size())) ? ch[static_cast<size_t> (s)] : 0.0f;
                }
            }
        }
        return ok;
    }

    using ARAPlaybackRenderer::processBlock;

private:
    double sampleRate = 48000.0;
    int numChannels = 2, maxBlock = 512;
};

//==============================================================================
class HoneyDocumentController final : public ARADocumentControllerSpecialisation
{
public:
    using ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;
    ~HoneyDocumentController() override { *controllerAlive = false; pool.removeAllJobs (true, 60000); }

    Settings getSettings() const { const ScopedLock sl (settingsLock); return settings; }
    void setSettings (const Settings& s)
    {
        {
            const ScopedLock sl (settingsLock);
            if (s == settings) return;
            settings = s;
        }
        for (auto* src : sources())
            requestRender (src->state);
    }

    std::vector<HoneyAudioSource*> sources()
    {
        return getDocumentController()->getDocument<ARADocument>()->getAudioSources<HoneyAudioSource>();
    }

    honeyui::Snapshot snapshot (const std::shared_ptr<SourceState>& st)
    {
        const auto s = getSettings();
        honeyui::Snapshot snap;
        {
            const ScopedLock sl (st->dataLock);
            snap = honeyui::makeSnapshot (st->track, st->notes, st->edits, st->guess, s);
        }
        snap.status = st->status;
        snap.name = st->name;
        return snap;
    }

    void setEdit (const std::shared_ptr<SourceState>& st, int index, const NoteEdit& e)
    {
        {
            const ScopedLock sl (st->dataLock);
            if (index < 0 || index >= static_cast<int> (st->edits.size())) return;
            st->edits[static_cast<size_t> (index)] = e;
        }
        requestRender (st);
    }

    void resetAllEdits (const std::shared_ptr<SourceState>& st)
    {
        {
            const ScopedLock sl (st->dataLock);
            st->edits.assign (st->notes.size(), {});
        }
        requestRender (st);
    }

    ChangeBroadcaster changes;   // a clip finished listening / rendering (the editor listens)

protected:
    ARAAudioSource* doCreateAudioSource (ARADocument* document, ARA::ARAAudioSourceHostRef hostRef) noexcept override
    {
        auto* src = new HoneyAudioSource (document, hostRef);
        src->addListener (this);
        return src;
    }

    void doUpdateAudioSourceContent (ARAAudioSource* audioSource, ARAContentUpdateScopes scopeFlags) override
    {
        if (scopeFlags.affectSamples())
            static_cast<HoneyAudioSource*> (audioSource)->state->samplesChanged = true;
    }

    void didEnableAudioSourceSamplesAccess (ARAAudioSource* audioSource, bool enable) override
    {
        auto* src = static_cast<HoneyAudioSource*> (audioSource);
        if (enable && (src->state->samplesChanged || src->state->status != 2))
            listen (src);
    }

    void willDestroyAudioSource (ARAAudioSource* audioSource) override
    {
        audioSource->removeListener (this);
    }

    ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override
    {
        return new HoneyPlaybackRenderer (getDocumentController());
    }

    bool doRestoreObjectsFromStream (ARAInputStream& input, const ARARestoreObjectsFilter* filter) noexcept override
    {
        const auto first = input.readInt64();
        const bool v2 = first == kArchiveV2;
        Settings s;
        s.key = static_cast<int> (v2 ? input.readInt64() : first);
        s.scale = static_cast<int> (input.readInt64());
        s.snap = input.readDouble();
        s.drift = input.readDouble();
        s.vibrato = input.readDouble();
        if (input.failed())
            return false;
        s.key = jlimit (0, honeyui::kAutoKey, s.key);
        s.scale = jlimit (0, vox::kScales - 1, s.scale);
        if (filter == nullptr || filter->shouldRestoreDocumentData())
            setSettings (s);
        if (! v2)
            return true;

        const auto numSources = input.readInt64();
        for (int64 k = 0; k < numSources && ! input.failed(); ++k)
        {
            const auto id = input.readString().toStdString();
            const auto numEdits = input.readInt64();
            std::vector<SavedEdit> saved;
            for (int64 e = 0; e < numEdits && ! input.failed(); ++e)
            {
                SavedEdit se;
                se.start = input.readDouble();
                se.edit.moved = input.readBool();
                se.edit.target = input.readDouble();
                se.edit.drift = input.readDouble();
                se.edit.vibrato = input.readDouble();
                saved.push_back (se);
            }
            HoneyAudioSource* target = nullptr;
            if (filter != nullptr)
                target = filter->getAudioSourceToRestoreStateWithID<HoneyAudioSource> (id.c_str());
            else
                for (auto* src : sources())
                    if (src->getPersistentID() == id) target = src;
            if (target == nullptr)
                continue;
            auto st = target->state;
            {
                const ScopedLock sl (st->dataLock);
                if (st->track != nullptr) st->attach (saved);
                else st->pending = saved;
            }
            requestRender (st);
        }
        return ! input.failed();
    }

    bool doStoreObjectsToStream (ARAOutputStream& output, const ARAStoreObjectsFilter* filter) noexcept override
    {
        const auto s = getSettings();
        bool ok = output.writeInt64 (kArchiveV2) && output.writeInt64 (s.key) && output.writeInt64 (s.scale)
               && output.writeDouble (s.snap) && output.writeDouble (s.drift) && output.writeDouble (s.vibrato);
        std::vector<const HoneyAudioSource*> toStore;
        if (filter != nullptr)
            toStore = filter->getAudioSourcesToStore<HoneyAudioSource>();
        else
            for (auto* src : sources()) toStore.push_back (src);
        ok = ok && output.writeInt64 (static_cast<int64> (toStore.size()));
        for (auto* src : toStore)
        {
            std::vector<SavedEdit> saved;
            {
                const ScopedLock sl (src->state->dataLock);
                saved = src->state->saved();
            }
            ok = ok && output.writeString (String (src->getPersistentID())) && output.writeInt64 (static_cast<int64> (saved.size()));
            for (const auto& se : saved)
                ok = ok && output.writeDouble (se.start) && output.writeBool (se.edit.moved) && output.writeDouble (se.edit.target)
                        && output.writeDouble (se.edit.drift) && output.writeDouble (se.edit.vibrato);
        }
        return ok;
    }

private:
    /** Read the whole recording (now: sample access is enabled for this call), then analyse it in the background. */
    void listen (HoneyAudioSource* src)
    {
        auto state = src->state;
        state->status = 1;
        state->samplesChanged = false;
        state->persistentId = src->getPersistentID();
        state->name = src->getName() != nullptr ? src->getName() : "clip";
        ARAAudioSourceReader reader (src);
        const auto len = static_cast<int> (src->getSampleCount());
        const int nch = std::max (1, static_cast<int> (src->getChannelCount()));
        AudioBuffer<float> buf (nch, std::max (1, len));
        if (! reader.isValid() || len <= 0 || ! reader.read (&buf, 0, len, 0, true, nch > 1))
        {
            state->status = 3;
            changes.sendChangeMessage();
            return;
        }
        std::vector<std::vector<float>> chans (static_cast<size_t> (nch));
        for (int c = 0; c < nch; ++c)
            chans[static_cast<size_t> (c)].assign (buf.getReadPointer (c), buf.getReadPointer (c) + len);
        changes.sendChangeMessage();
        pool.addJob ([this, state, chans = std::move (chans), sr = src->getSampleRate()]() mutable
        {
            state->sampleRate = sr;
            state->original = std::move (chans);
            std::vector<float> mono (state->original.front().size(), 0.0f);
            for (const auto& ch : state->original)
                for (size_t i = 0; i < mono.size(); ++i) mono[i] += ch[i] / static_cast<float> (state->original.size());
            auto track = std::make_shared<vox::honey::Track> (vox::honey::analyse (mono, sr));
            auto notes = vox::honey::findNotes (*track);
            std::vector<double> sung;
            for (double m : track->midi) if (m > 0.0) sung.push_back (m);
            const auto guess = vox::detectKey (sung);
            {
                const ScopedLock sl (state->dataLock);
                const auto keep = state->saved();   // hand edits (or the ones restored from the project)
                state->track = track;
                state->notes = std::move (notes);
                state->guess = guess;
                state->attach (keep);
                state->pending.clear();
            }
            requestRender (state);
        });
    }

    void requestRender (const std::shared_ptr<SourceState>& state)
    {
        ++state->wanted;
        if (state->queued.exchange (true))
            return;   // a job is waiting: it will pick this up
        pool.addJob ([this, state]
        {
            for (;;)
            {
                const int asked = state->wanted;
                if (renderNow (*state))
                    finished (state);
                state->queued = false;
                if (state->wanted == asked || state->queued.exchange (true))
                    return;
            }
        });
    }

    bool renderNow (SourceState& st)
    {
        if (st.original.empty()) return false;
        const auto s = getSettings();
        std::shared_ptr<const vox::honey::Track> track;
        std::vector<vox::honey::Note> notes;
        {
            const ScopedLock sl (st.dataLock);
            if (st.track == nullptr) return false;
            int key = 0, scale = 0;
            honeyui::resolveKey (s, st.guess, key, scale);
            track = st.track;
            notes = honeyui::applyEdits (st.notes, st.edits, s, key, scale);
        }
        auto out = std::make_shared<std::vector<std::vector<float>>>();
        for (const auto& ch : st.original)
            out->push_back (vox::honey::render (ch, st.sampleRate, *track, notes));
        {
            const SpinLock::ScopedLockType sl (st.lock);
            st.rendered = std::move (out);
        }
        st.status = 2;
        return true;
    }

    void finished (std::shared_ptr<SourceState> state)
    {
        // Tell the host the audio changed (message thread), unless the recording has gone meanwhile.
        MessageManager::callAsync ([state, this, token = controllerAlive]
        {
            if (! *token || ! state->alive) return;
            for (auto* src : sources())
                if (src->state == state)
                    for (auto* mod : src->getAudioModifications())
                    {
                        mod->notifyContentChanged (ARAContentUpdateScopes::samplesAreAffected(), true);
                        for (auto* region : mod->getPlaybackRegions())
                            region->notifyContentChanged (ARAContentUpdateScopes::samplesAreAffected(), true);
                    }
            changes.sendChangeMessage();
        });
    }

    std::shared_ptr<std::atomic<bool>> controllerAlive = std::make_shared<std::atomic<bool>> (true);
    mutable CriticalSection settingsLock;
    Settings settings;
    ThreadPool pool { 1 };
};

//==============================================================================
class HoneyProcessor final : public AudioProcessor,
                             public AudioProcessorARAExtension
{
public:
    HoneyProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", AudioChannelSet::stereo(), true)) {}

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
    }
    void releaseResources() override { releaseResourcesForARA(); }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi) override
    {
        ScopedNoDenormals noDenormals;
        // Not applied as an ARA extension (or not ready): pass the audio through.
        if (! processBlockForARA (buffer, isRealtime(), getPlayHead()))
            processBlockBypassed (buffer, midi);
    }
    using AudioProcessor::processBlock;

    const String getName() const override { return "Honey Tune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { double t = 0.0; return getTailLengthSecondsForARA (t) ? t : 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    bool hasEditor() const override { return true; }
    AudioProcessorEditor* createEditor() override;
    AudioProcessorARAExtension* getARAClientExtensions() override { return this; }
};

//==============================================================================
/** The editor shows one clip: the one you selected in the host (else the first). */
class HoneyEditor final : public AudioProcessorEditor,
                          public AudioProcessorEditorARAExtension,
                          private ARAEditorView::Listener,
                          private ChangeListener,
                          private Timer,
                          private honeyui::Model
{
public:
    explicit HoneyEditor (HoneyProcessor& p)
        : AudioProcessorEditor (&p), AudioProcessorEditorARAExtension (&p)
    {
        addAndMakeVisible (panel);
        if (auto* view = getARAEditorView())
        {
            dc = ARADocumentControllerSpecialisation::getSpecialisedDocumentController<HoneyDocumentController> (view->getDocumentController());
            view->addListener (this);
            dc->changes.addChangeListener (this);
            pickSource (view->getViewSelection().getPlaybackRegions<ARAPlaybackRegion>());
            startTimerHz (2);
        }
        panel.setModel (dc != nullptr ? this : nullptr);
        setResizable (true, false);
        setResizeLimits (900, 460, 4000, 2400);
        setSize (1200, 720);
    }

    ~HoneyEditor() override
    {
        if (dc != nullptr) dc->changes.removeChangeListener (this);
        if (auto* view = getARAEditorView()) view->removeListener (this);
    }

    void paint (Graphics& g) override { g.fillAll (Colour (0xfff3ede0)); }
    void resized() override { panel.setBounds (getLocalBounds()); }

    AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }

private:
    // honeyui::Model
    honeyui::Snapshot snapshot() override
    {
        if (dc == nullptr || state == nullptr) return {};
        return dc->snapshot (state);
    }
    Settings getSettings() override { return dc != nullptr ? dc->getSettings() : Settings {}; }
    void setSettings (const Settings& s) override { if (dc != nullptr) dc->setSettings (s); }
    void setEdit (int i, const NoteEdit& e) override { if (dc != nullptr && state != nullptr) dc->setEdit (state, i, e); }
    void resetAllEdits() override { if (dc != nullptr && state != nullptr) dc->resetAllEdits (state); }

    void onNewSelection (const ARAViewSelection& sel) override
    {
        pickSource (sel.getPlaybackRegions<ARAPlaybackRegion>());
    }

    void pickSource (const std::vector<ARAPlaybackRegion*>& regions)
    {
        if (dc == nullptr) return;
        std::shared_ptr<SourceState> next;
        if (! regions.empty())
            next = static_cast<HoneyAudioSource*> (regions.front()->getAudioModification()->getAudioSource())->state;
        else if (state == nullptr || ! state->alive)
            if (const auto all = dc->sources(); ! all.empty())
                next = all.front()->state;
        if (next != nullptr && next != state)
        {
            state = next;
            panel.roll.setSelected (-1);
            panel.refresh();
        }
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        if (state == nullptr || ! state->alive) pickSource ({});
        panel.refresh();
    }

    void timerCallback() override
    {
        // While listening (or if a clip was removed), keep the status fresh.
        if (state == nullptr || ! state->alive) { state = nullptr; pickSource ({}); panel.refresh(); }
        else if (state->status != 2) panel.refresh();
    }

    HoneyDocumentController* dc = nullptr;
    std::shared_ptr<SourceState> state;
    HoneyPanel panel;
};

AudioProcessorEditor* HoneyProcessor::createEditor() { return new HoneyEditor (*this); }

//==============================================================================
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return ARADocumentControllerSpecialisation::createARAFactory<HoneyDocumentController>();
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HoneyProcessor();
}
