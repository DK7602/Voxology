#include "VoxWebEditor.h"

#include "VoxologyUIData.h"

#include <cstring>

namespace
{
    juce::String mimeTypeFor (const juce::String& file)
    {
        const auto ext = file.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "html") return "text/html";
        if (ext == "js")   return "text/javascript";
        if (ext == "css")  return "text/css";
        if (ext == "png")  return "image/png";
        if (ext == "webp") return "image/webp";
        if (ext == "jpg")  return "image/jpeg";
        if (ext == "svg")  return "image/svg+xml";
        if (ext == "ttf")  return "font/ttf";
        if (ext == "json") return "application/json";
        return "text/plain";
    }

    juce::RangedAudioParameter& param (VoxologyAudioProcessor& p, const juce::String& id)
    {
        auto* rp = p.parameters.getParameter (id);
        jassert (rp != nullptr);
        return *rp;
    }

    float roundTo (float v, float step) { return std::round (v / step) * step; }
    constexpr juce::uint32 kBackground = 0xfff3ede0;
}

juce::WebBrowserComponent::Options VoxWebEditor::makeBaseOptions()
{
    using Options = juce::WebBrowserComponent::Options;
    return Options{}
       #if JUCE_WINDOWS
        .withBackend (Options::Backend::webview2)
       #endif
        .withWinWebView2Options (Options::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                              .getChildFile ("VoxologyWebView"))
                                     .withBackgroundColour (juce::Colour (kBackground))
                                     .withStatusBarDisabled())
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled();
}

bool VoxWebEditor::isSupported()
{
    return juce::WebBrowserComponent::areOptionsSupported (makeBaseOptions());
}

juce::WebBrowserComponent::Options VoxWebEditor::makeEditorOptions()
{
    auto options = makeBaseOptions();
    for (auto& r : sliderRelays) options = options.withOptionsFrom (*r);
    for (auto& r : toggleRelays) options = options.withOptionsFrom (*r);
    for (auto& r : comboRelays)  options = options.withOptionsFrom (*r);

    return options
        .withNativeFunction ("startAutoEdit", [this] (const juce::Array<juce::var>&, auto complete) { complete (audioProcessor.autoEdit.start()); })
        .withNativeFunction ("cancelAutoEdit", [this] (const juce::Array<juce::var>&, auto complete) { audioProcessor.autoEdit.cancel(); complete (true); })
        .withNativeFunction ("undoAutoEdit", [this] (const juce::Array<juce::var>&, auto complete) { complete (audioProcessor.autoEdit.undo()); })
        .withNativeFunction ("getAutoEditReport", [this] (const juce::Array<juce::var>&, auto complete) { complete (audioProcessor.autoEdit.getReportJson()); })
        .withNativeFunction ("chooseReference", [this] (const juce::Array<juce::var>&, auto complete)
        {
            refChooser = std::make_unique<juce::FileChooser> ("Choose a reference vocal (an acapella you like)", juce::File(),
                                                              "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            refChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                     [this] (const juce::FileChooser& fc)
                                     {
                                         const auto f = fc.getResult();
                                         if (f.existsAsFile()) audioProcessor.autoEdit.loadReference (f);
                                     });
            complete (true);
        })
        .withNativeFunction ("clearReference", [this] (const juce::Array<juce::var>&, auto complete) { audioProcessor.autoEdit.clearReference(); complete (true); })
        .withNativeFunction ("getUnmaskSources", [this] (const juce::Array<juce::var>&, auto complete)
        {
            juce::Array<juce::var> list;
            for (const auto& s : vox::UnmaskLink::instance().sources())
            {
                if (s.slot == audioProcessor.getLinkSlot() || ! s.live) continue;   // live = a Voxology in VOCAL mode, running
                auto* o = new juce::DynamicObject();
                o->setProperty ("name", juce::String (s.name));
                list.add (juce::var (o));
            }
            auto* r = new juce::DynamicObject();
            r->setProperty ("sources", list);
            r->setProperty ("selected", audioProcessor.getUnmaskSourceName());
            complete (juce::JSON::toString (juce::var (r), true));
        })
        .withNativeFunction ("setUnmaskSource", [this] (const juce::Array<juce::var>& args, auto complete)
        {
            audioProcessor.setUnmaskSourceByName (args.isEmpty() ? juce::String() : args[0].toString());
            complete (true);
        })
        .withNativeFunction ("getReference", [this] (const juce::Array<juce::var>&, auto complete) { complete (audioProcessor.autoEdit.getReferenceJson()); })
        .withResourceProvider ([] (const auto& url) { return getResource (url); });
}

VoxWebEditor::VoxWebEditor (VoxologyAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      webView (makeEditorOptions())
{
    const auto sliders = VoxParams::sliderIds(), toggles = VoxParams::toggleIds(), combos = VoxParams::comboIds();
    for (int i = 0; i < sliders.size(); ++i)
        sliderAttachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (param (p, sliders[i]), *sliderRelays[static_cast<size_t> (i)], nullptr));
    for (int i = 0; i < toggles.size(); ++i)
        toggleAttachments.push_back (std::make_unique<juce::WebToggleButtonParameterAttachment> (param (p, toggles[i]), *toggleRelays[static_cast<size_t> (i)], nullptr));
    for (int i = 0; i < combos.size(); ++i)
        comboAttachments.push_back (std::make_unique<juce::WebComboBoxParameterAttachment> (param (p, combos[i]), *comboRelays[static_cast<size_t> (i)], nullptr));

    addAndMakeVisible (webView);
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (800, 450, 2400, 1350);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kDesignWidth) / kDesignHeight);
    setSize (1280, 720);

    audioProcessor.meters.satResidual.store (0.0f);
    audioProcessor.meters.satSignal.store (0.0f);
    startTimerHz (30);
}

VoxWebEditor::~VoxWebEditor()
{
    stopTimer();
    // A/B and MATCH are listening aids: switch them off when the window closes so they can't end up in a bounce.
    for (const char* id : { "levelMatch", "listenA" })
        if (auto* prm = audioProcessor.parameters.getParameter (id))
            if (prm->getValue() > 0.5f)
                prm->setValueNotifyingHost (0.0f);
}

void VoxWebEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (kBackground)); }
void VoxWebEditor::resized() { webView.setBounds (getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> VoxWebEditor::getResource (const juce::String& url)
{
    auto path = url == "/" ? juce::String ("index.html") : url.fromFirstOccurrenceOf ("/", false, false);
    path = path.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    if (path.isEmpty())
        path = "index.html";
    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < VoxologyUI::namedResourceListSize; ++i)
    {
        if (fileName == VoxologyUI::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = VoxologyUI::getNamedResource (VoxologyUI::namedResourceList[i], size))
            {
                std::vector<std::byte> bytes (static_cast<size_t> (size));
                std::memcpy (bytes.data(), data, bytes.size());
                return juce::WebBrowserComponent::Resource { std::move (bytes), mimeTypeFor (fileName) };
            }
        }
    }
    return std::nullopt;
}

juce::var VoxWebEditor::makeSpectrum (const SpectrumTap& tap, Analyser& state)
{
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    tap.copyLatest (fftBuffer.data(), kFftSize);
    window.multiplyWithWindowingTable (fftBuffer.data(), static_cast<size_t> (kFftSize));
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const double sampleRate = juce::jmax (8000.0, audioProcessor.getSampleRate());
    const double binHz = sampleRate / kFftSize;
    const float fullScale = kFftSize * 0.25f;
    constexpr int lastBin = kFftSize / 2 - 1;

    juce::Array<juce::var> out;
    out.ensureStorageAllocated (kSpectrumPoints);
    for (int i = 0; i < kSpectrumPoints; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, static_cast<double> (i) / (kSpectrumPoints - 1));
        const int lo = juce::jlimit (1, lastBin, static_cast<int> (std::floor (f * std::pow (2.0, -1.0 / 12.0) / binHz)));
        const int hi = juce::jlimit (lo, lastBin, static_cast<int> (std::ceil (f * std::pow (2.0, 1.0 / 12.0) / binHz)));
        double power = 0.0;
        for (int b = lo; b <= hi; ++b)
            power += static_cast<double> (fftBuffer[static_cast<size_t> (b)]) * fftBuffer[static_cast<size_t> (b)];
        const double mag = std::sqrt (power / (hi - lo + 1)) / fullScale;
        // 4.5 dB/octave tilt around 1 kHz so a balanced vocal reads roughly flat.
        float db = static_cast<float> (20.0 * std::log10 (mag + 1.0e-9) + 4.5 * std::log2 (f / 1000.0));
        db = juce::jlimit (-120.0f, 12.0f, db);
        auto& s = state.smoothed[static_cast<size_t> (i)];
        if (! state.primed)  s = db;
        else if (db > s)     s = 0.55f * db + 0.45f * s;
        else                 s = 0.88f * s + 0.12f * db;
        out.add (roundTo (s, 0.1f));
    }
    state.primed = true;
    return out;
}

void VoxWebEditor::timerCallback()
{
    auto& m = audioProcessor.meters;
    auto holdDown = [] (float& hold, float now) { hold = now < hold ? now : hold * 0.8f + now * 0.2f; };   // deepest, then glide back
    holdDown (gateHold, m.gate.exchange (0.0f));
    holdDown (popHold, m.pops.exchange (0.0f));
    holdDown (breathHold, m.breath.exchange (0.0f));
    holdDown (essHold, m.deEss.exchange (0.0f));
    juce::Array<juce::var> dyn;
    for (size_t b = 0; b < dynHold.size(); ++b)
    {
        holdDown (dynHold[b], m.dynEq[b].exchange (0.0f));
        dyn.add (roundTo (dynHold[b], 0.1f));
    }
    holdDown (peakHold, m.peakGr.exchange (0.0f));
    holdDown (levelHold, m.levelGr.exchange (0.0f));
    const float inPk = m.inPeak.exchange (-100.0f), outPk = m.outPeak.exchange (-100.0f);
    inPkHold = inPk > inPkHold ? inPk : juce::jmax (-100.0f, inPkHold - 1.2f);
    outPkHold = outPk > outPkHold ? outPk : juce::jmax (-100.0f, outPkHold - 1.2f);
    satResHold = satResHold * 0.9 + m.satResidual.exchange (0.0f);
    satSigHold = satSigHold * 0.9 + m.satSignal.exchange (0.0f);
    const double ratio = satSigHold > 1.0e-12 ? satResHold / satSigHold : 0.0;

    auto* frame = new juce::DynamicObject();
    frame->setProperty ("inShort", roundTo (m.inShort.load(), 0.1f));
    frame->setProperty ("outShort", roundTo (m.outShort.load(), 0.1f));
    frame->setProperty ("inPeak", roundTo (inPkHold, 0.1f));
    frame->setProperty ("outPeak", roundTo (outPkHold, 0.1f));
    frame->setProperty ("gate", roundTo (gateHold, 0.1f));
    frame->setProperty ("pops", roundTo (popHold, 0.1f));
    frame->setProperty ("breath", roundTo (breathHold, 0.1f));
    frame->setProperty ("deEss", roundTo (essHold, 0.1f));
    frame->setProperty ("dyn", dyn);
    frame->setProperty ("rider", roundTo (m.rider.load(), 0.1f));
    frame->setProperty ("peakGr", roundTo (peakHold, 0.1f));
    frame->setProperty ("levelGr", roundTo (levelHold, 0.1f));
    frame->setProperty ("satHarm", roundTo (ratio > 1.0e-10 ? static_cast<float> (10.0 * std::log10 (ratio)) : -100.0f, 0.1f));
    frame->setProperty ("matchDb", roundTo (m.matchDb.load(), 0.1f));
    frame->setProperty ("bpm", roundTo (m.bpm.load(), 0.1f));
    frame->setProperty ("pitchSung", roundTo (m.pitchSung.load(), 0.01f));
    frame->setProperty ("pitchTarget", m.pitchTarget.load());
    frame->setProperty ("pitchCorr", roundTo (m.pitchCorr.load(), 0.01f));
    {
        juce::Array<juce::var> hv;
        hv.add (m.hvNotes[0].load());
        hv.add (m.hvNotes[1].load());
        frame->setProperty ("hvNotes", hv);
    }
    frame->setProperty ("bkState", m.bkState.load());
    frame->setProperty ("bkKey", m.bkKey.load());
    frame->setProperty ("bkMinor", m.bkMinor.load());
    frame->setProperty ("bkConf", roundTo (m.bkConf.load(), 0.01f));
    frame->setProperty ("bkTune", roundTo (m.bkTune.load(), 0.1f));
    frame->setProperty ("bkHeard", roundTo (m.bkHeard.load(), 0.1f));
    frame->setProperty ("keyUsed", m.keyUsed.load());
    frame->setProperty ("scaleUsed", m.scaleUsed.load());
    frame->setProperty ("sr", audioProcessor.getSampleRate());
    auto& ae = audioProcessor.autoEdit;
    frame->setProperty ("aeState", static_cast<int> (ae.getState()));
    frame->setProperty ("aeProgress", roundTo (ae.getProgress(), 0.01f));
    frame->setProperty ("aeHearing", ae.isHearingAudio());
    frame->setProperty ("aeUndo", ae.canUndo());
    frame->setProperty ("aeReport", ae.getReportVersion());
    frame->setProperty ("refVersion", ae.getReferenceVersion());
    if (audioProcessor.isBeatMode())
    {
        juce::Array<juce::var> dips, vocal;
        for (size_t b = 0; b < umHold.size(); ++b)
        {
            holdDown (umHold[b], m.umDip[b].exchange (0.0f));
            dips.add (roundTo (umHold[b], 0.1f));
            vocal.add (roundTo (m.umVocal[b].load(), 0.1f));
        }
        frame->setProperty ("umDip", dips);
        frame->setProperty ("umVocal", vocal);
        frame->setProperty ("umLink", m.umLink.load());
    }
    frame->setProperty ("in", makeSpectrum (audioProcessor.inputTap, inAnalyser));
    frame->setProperty ("out", makeSpectrum (audioProcessor.outputTap, outAnalyser));
    webView.emitEventIfBrowserIsVisible ("voxMeters", juce::var (frame));
}
