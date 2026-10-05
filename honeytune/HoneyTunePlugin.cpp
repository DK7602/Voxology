// Honey Tune: the note editor, as an ARA plug-in (applied to a clip, like Melodyne).
//
// Each audio source (the clip's recording) is read in full on a background thread, analysed into notes
// (vox::honey), and rendered with the document's settings (key / scale / snap / drift / vibrato). The
// playback renderer plays the rendered audio; until it's ready it plays the clip untouched. Settings
// are saved in the host's project through the ARA archive.

#include <juce_audio_utils/juce_audio_utils.h>

#include "vox/HoneyTune.h"

#include <map>
#include <memory>

using namespace juce;

namespace
{
    constexpr int kAutoKey = 12;   // Key choice "Auto": detected from the clip

    struct Settings
    {
        int key = kAutoKey;        // 0 = C ... 11 = B, 12 = auto
        int scale = 0;             // vox::kScaleNames (0 = chromatic)
        double snap = 1.0;         // 0..1 of the way to the note
        double drift = 1.0;        // keep 0..1
        double vibrato = 1.0;      // keep 0..1
        bool operator== (const Settings& o) const
        {
            auto same = [] (double a, double b) { return std::abs (a - b) < 1.0e-9; };
            return key == o.key && scale == o.scale && same (snap, o.snap) && same (drift, o.drift) && same (vibrato, o.vibrato);
        }
    };

    /** What we know about one recording, shared between the model, the background jobs and the
        audio thread. The rendered audio is swapped in under a spin lock (the audio thread only tries). */
    struct SourceState
    {
        std::atomic<bool> alive { true };
        std::atomic<int> status { 0 };   // 0 waiting, 1 analysing, 2 ready, 3 couldn't read
        double sampleRate = 48000.0;
        int numChannels = 1;
        std::vector<std::vector<float>> original;   // per channel (background thread only)
        vox::honey::Track track;
        std::vector<vox::honey::Note> notes;
        vox::KeyGuess keyGuess;
        int offKey = 0, usedKey = 0, usedScale = 0;

        SpinLock lock;
        std::shared_ptr<const std::vector<std::vector<float>>> rendered;

        std::shared_ptr<const std::vector<std::vector<float>>> getRendered()
        {
            const SpinLock::ScopedTryLockType sl (lock);
            return sl.isLocked() ? rendered : nullptr;
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
        for (auto* src : getDocumentController()->getDocument<ARADocument>()->getAudioSources<HoneyAudioSource>())
            launch (src, false);
    }

    /** One line about each recording, for the editor. */
    StringArray describe()
    {
        StringArray lines;
        for (auto* src : getDocumentController()->getDocument<ARADocument>()->getAudioSources<HoneyAudioSource>())
        {
            const auto& st = *src->state;
            const auto name = String (src->getName() != nullptr ? src->getName() : "clip");
            switch (st.status.load())
            {
                case 0: lines.add (name + ": waiting for the host to share the audio"); break;
                case 1: lines.add (name + ": listening..."); break;
                case 3: lines.add (name + ": couldn't read the audio"); break;
                default:
                    lines.add (name + ": " + String (st.notes.size()) + " notes, key " + vox::kNoteNames[static_cast<size_t> (st.usedKey)]
                               + " " + vox::kScaleNames[static_cast<size_t> (st.usedScale)] + ", " + String (st.offKey)
                               + " were more than 25 cents off (detected " + vox::kNoteNames[static_cast<size_t> (st.keyGuess.key)]
                               + (st.keyGuess.minor ? " minor" : " major") + ", " + String (roundToInt (st.keyGuess.confidence * 100)) + " % sure)");
            }
        }
        if (lines.isEmpty())
            lines.add ("Apply Honey Tune to a vocal clip (in Cubase: select the event, then Audio > Extensions > Honey Tune).");
        return lines;
    }

protected:
    ARAAudioSource* doCreateAudioSource (ARADocument* document, ARA::ARAAudioSourceHostRef hostRef) noexcept override
    {
        auto* src = new HoneyAudioSource (document, hostRef);
        src->addListener (this);
        return src;
    }

    void didEnableAudioSourceSamplesAccess (ARAAudioSource* audioSource, bool enable) override
    {
        if (enable)
            launch (static_cast<HoneyAudioSource*> (audioSource), true);
    }

    void willDestroyAudioSource (ARAAudioSource* audioSource) override
    {
        audioSource->removeListener (this);
    }

    ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override
    {
        return new HoneyPlaybackRenderer (getDocumentController());
    }

    bool doRestoreObjectsFromStream (ARAInputStream& input, const ARARestoreObjectsFilter*) noexcept override
    {
        Settings s;
        s.key = static_cast<int> (input.readInt64());
        s.scale = static_cast<int> (input.readInt64());
        s.snap = input.readDouble();
        s.drift = input.readDouble();
        s.vibrato = input.readDouble();
        if (input.failed())
            return false;
        s.key = jlimit (0, kAutoKey, s.key);
        s.scale = jlimit (0, vox::kScales - 1, s.scale);
        setSettings (s);
        return true;
    }

    bool doStoreObjectsToStream (ARAOutputStream& output, const ARAStoreObjectsFilter*) noexcept override
    {
        const auto s = getSettings();
        return output.writeInt64 (s.key) && output.writeInt64 (s.scale) && output.writeDouble (s.snap)
            && output.writeDouble (s.drift) && output.writeDouble (s.vibrato);
    }

private:
    /** Analyse (first time) and render a recording in the background. */
    void launch (HoneyAudioSource* src, bool reread)
    {
        auto state = src->state;
        const auto s = getSettings();
        if (reread || state->original.empty())
        {
            // Read the whole recording now (sample access is enabled on this thread's call).
            state->status = 1;
            ARAAudioSourceReader reader (src);
            const auto len = static_cast<int> (src->getSampleCount());
            const int nch = std::max (1, static_cast<int> (src->getChannelCount()));
            AudioBuffer<float> buf (nch, std::max (1, len));
            if (! reader.isValid() || len <= 0 || ! reader.read (&buf, 0, len, 0, true, nch > 1))
            {
                state->status = 3;
                return;
            }
            std::vector<std::vector<float>> chans (static_cast<size_t> (nch));
            for (int c = 0; c < nch; ++c)
                chans[static_cast<size_t> (c)].assign (buf.getReadPointer (c), buf.getReadPointer (c) + len);
            pool.addJob ([state, chans = std::move (chans), sr = src->getSampleRate(), s, this]() mutable
            {
                state->sampleRate = sr;
                state->numChannels = static_cast<int> (chans.size());
                state->original = std::move (chans);
                analyse (*state);
                render (*state, s);
                finished (state);
            });
            return;
        }
        pool.addJob ([state, s, this] { render (*state, s); finished (state); });
    }

    static void analyse (SourceState& st)
    {
        std::vector<float> mono (st.original.front().size(), 0.0f);
        for (const auto& ch : st.original)
            for (size_t i = 0; i < mono.size(); ++i) mono[i] += ch[i] / static_cast<float> (st.original.size());
        st.track = vox::honey::analyse (mono, st.sampleRate);
        st.notes = vox::honey::findNotes (st.track);
        std::vector<double> sung;
        for (double m : st.track.midi) if (m > 0.0) sung.push_back (m);
        st.keyGuess = vox::detectKey (sung);
    }

    static void render (SourceState& st, const Settings& s)
    {
        if (st.original.empty()) return;
        auto notes = st.notes;
        st.usedKey = s.key == kAutoKey ? st.keyGuess.key : s.key;
        st.usedScale = s.key == kAutoKey && s.scale == 0 && st.keyGuess.confidence >= 0.45 ? (st.keyGuess.minor ? 2 : 1) : s.scale;
        st.offKey = 0;
        for (const auto& n : notes)
            if (std::abs (vox::honey::centsOff (n, st.usedKey, st.usedScale)) > 25.0) ++st.offKey;
        vox::honey::snapToKey (notes, st.usedKey, st.usedScale, s.snap);
        for (auto& n : notes) { n.drift = s.drift; n.vibrato = s.vibrato; }
        auto out = std::make_shared<std::vector<std::vector<float>>>();
        for (const auto& ch : st.original)
            out->push_back (vox::honey::render (ch, st.sampleRate, st.track, notes));
        {
            const SpinLock::ScopedLockType sl (st.lock);
            st.rendered = std::move (out);
        }
        st.status = 2;
    }

    void finished (std::shared_ptr<SourceState> state)
    {
        // Tell the host the audio changed (message thread), unless the recording has gone meanwhile.
        MessageManager::callAsync ([state, this, token = controllerAlive]
        {
            if (! *token || ! state->alive) return;
            for (auto* src : getDocumentController()->getDocument<ARADocument>()->getAudioSources<HoneyAudioSource>())
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

public:
    ChangeBroadcaster changes;   // the editor listens
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
/** Step 2 editor: plain controls (the honeycomb note editor comes next). */
class HoneyEditor final : public AudioProcessorEditor,
                          public AudioProcessorEditorARAExtension,
                          private ChangeListener,
                          private Timer
{
public:
    explicit HoneyEditor (HoneyProcessor& p)
        : AudioProcessorEditor (&p), AudioProcessorEditorARAExtension (&p)
    {
        if (auto* view = getARAEditorView())
            dc = ARADocumentControllerSpecialisation::getSpecialisedDocumentController<HoneyDocumentController> (view->getDocumentController());

        for (int k = 0; k < 12; ++k) key.addItem (vox::kNoteNames[static_cast<size_t> (k)], k + 1);
        key.addItem ("Auto", kAutoKey + 1);
        for (int k = 0; k < vox::kScales; ++k) scale.addItem (vox::kScaleNames[static_cast<size_t> (k)], k + 1);
        for (auto* s : { &snap, &drift, &vibrato })
        {
            s->setRange (0.0, 100.0, 1.0);
            s->setTextValueSuffix (" %");
            s->setSliderStyle (Slider::LinearHorizontal);
            s->setTextBoxStyle (Slider::TextBoxRight, false, 64, 22);
        }
        auto addLabelled = [this] (Component& c, Label& l, const String& text)
        {
            l.setText (text, dontSendNotification);
            l.attachToComponent (&c, true);
            addAndMakeVisible (c);
        };
        addLabelled (key, keyLabel, "Key");
        addLabelled (scale, scaleLabel, "Scale");
        addLabelled (snap, snapLabel, "Snap to note");
        addLabelled (drift, driftLabel, "Keep drift");
        addLabelled (vibrato, vibratoLabel, "Keep vibrato");
        status.setJustificationType (Justification::topLeft);
        status.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (status);

        if (dc != nullptr)
        {
            const auto s = dc->getSettings();
            key.setSelectedId (s.key + 1, dontSendNotification);
            scale.setSelectedId (s.scale + 1, dontSendNotification);
            snap.setValue (s.snap * 100.0, dontSendNotification);
            drift.setValue (s.drift * 100.0, dontSendNotification);
            vibrato.setValue (s.vibrato * 100.0, dontSendNotification);
            dc->changes.addChangeListener (this);
            auto push = [this] { apply(); };
            key.onChange = push; scale.onChange = push;
            for (auto* sl : { &snap, &drift, &vibrato }) sl->onDragEnd = push;
            refreshStatus();
            startTimerHz (2);
        }
        setResizable (true, false);
        setSize (640, 300);
    }

    ~HoneyEditor() override
    {
        if (dc != nullptr) dc->changes.removeChangeListener (this);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xfff3ede0));
        g.setColour (Colour (0xff8d641f));
        g.setFont (FontOptions (22.0f, Font::bold));
        g.drawText ("HONEY TUNE", 16, 8, 300, 30, Justification::centredLeft);
        g.setFont (FontOptions (13.0f));
        g.setColour (Colour (0xff566170));
        g.drawText ("note editor preview: the honeycomb editor is coming next", 180, 12, 440, 24, Justification::centredLeft);
        if (dc == nullptr)
        {
            g.setColour (Colour (0xff1d2733));
            g.drawFittedText ("Honey Tune works on a clip: in Cubase select the vocal event, then Audio > Extensions > Honey Tune.\n"
                              "(As an insert it just passes the audio through.)", getLocalBounds().reduced (20).withTrimmedTop (40),
                              Justification::topLeft, 4);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16).withTrimmedTop (40);
        auto row = [&r] { auto x = r.removeFromTop (28); r.removeFromTop (6); return x.withTrimmedLeft (110); };
        key.setBounds (row().withWidth (120));
        scale.setBounds (row().withWidth (200));
        snap.setBounds (row());
        drift.setBounds (row());
        vibrato.setBounds (row());
        status.setBounds (r);
    }

    AudioProcessorEditorARAExtension* getARAClientExtensions() override { return this; }

private:
    void apply()
    {
        if (dc == nullptr) return;
        Settings s;
        s.key = key.getSelectedId() - 1;
        s.scale = scale.getSelectedId() - 1;
        s.snap = snap.getValue() / 100.0;
        s.drift = drift.getValue() / 100.0;
        s.vibrato = vibrato.getValue() / 100.0;
        dc->setSettings (s);
        refreshStatus();
    }
    void refreshStatus() { if (dc != nullptr) status.setText (dc->describe().joinIntoString ("\n"), dontSendNotification); }
    void changeListenerCallback (ChangeBroadcaster*) override { refreshStatus(); }
    void timerCallback() override { refreshStatus(); }

    HoneyDocumentController* dc = nullptr;
    ComboBox key, scale;
    Slider snap, drift, vibrato;
    Label keyLabel, scaleLabel, snapLabel, driftLabel, vibratoLabel, status;
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
