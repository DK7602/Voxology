// Draws the Honey Tune editor with a real vocal into a PNG (developer tool, no host needed).
// HoneyPreview <mono float32 raw> <sample rate> <out.png> [key 0-11|12=auto] [scale]

#include <juce_gui_basics/juce_gui_basics.h>

#include "../HoneyPanel.h"

#include <fstream>

using namespace juce;

struct MemoryModel final : honeyui::Model
{
    std::shared_ptr<const vox::honey::Track> track;
    std::vector<vox::honey::Note> notes;
    std::vector<honeyui::NoteEdit> edits;
    vox::KeyGuess guess;
    honeyui::Settings settings;

    honeyui::Snapshot snapshot() override
    {
        auto s = honeyui::makeSnapshot (track, notes, edits, guess, settings);
        s.status = 2;
        return s;
    }
    honeyui::Settings getSettings() override { return settings; }
    void setSettings (const honeyui::Settings& s) override { settings = s; }
    void setEdit (int i, const honeyui::NoteEdit& e) override { edits[static_cast<size_t> (i)] = e; }
    void resetAllEdits() override { edits.assign (notes.size(), {}); }
    bool orig = false;
    void setOriginal (bool o) override { orig = o; }
    bool isOriginal() override { return orig; }
};

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage: HoneyPreview in.raw sr out.png [key] [scale]\n"); return 1; }
    ScopedJuceInitialiser_GUI gui;
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> mono;
    float x;
    while (in.read (reinterpret_cast<char*> (&x), sizeof x)) mono.push_back (x);
    const double sr = std::atof (argv[2]);

    MemoryModel m;
    auto t = std::make_shared<vox::honey::Track> (vox::honey::analyse (mono, sr));
    m.notes = vox::honey::findNotes (*t);
    std::vector<double> sung;
    for (double v : t->midi) if (v > 0.0) sung.push_back (v);
    m.guess = vox::detectKey (sung);
    m.track = t;
    m.edits.assign (m.notes.size(), {});
    if (argc > 4) m.settings.key = std::atoi (argv[4]);
    if (argc > 5) m.settings.scale = std::atoi (argv[5]);
    m.settings.snap = argc > 6 ? std::atof (argv[6]) : 0.0;   // show the notes as sung (glow), unless asked

    HoneyPanel panel;
    panel.setSize (500, 300);   // hosts open small, then enlarge
    panel.setModel (&m);
    panel.setSize (1200, std::getenv ("HONEY_H") ? std::atoi (std::getenv ("HONEY_H")) : 720);
    panel.setModel (&m);
    // A few hand edits so every look shows: fixed by hand, moved a whole note, and a selection.
    int shown = 0;
    for (size_t i = 0; i < m.notes.size() && shown < 3; ++i)
    {
        auto snap = m.snapshot();
        if (snap.notes[i].wasOff && m.notes[i].start / sr > 2.0)
        {
            m.edits[i].moved = true;
            m.edits[i].target = honeyui::keyNote (m.notes[i].pitch, snap.key, snap.scale);
            ++shown;
        }
    }
    panel.refresh();
    panel.roll.setSelected (static_cast<int> (m.notes.size() / 3));
    panel.roll.setPlayhead (2.5);

    if (std::getenv ("HONEY_DEBUG"))
        for (auto* c : panel.getChildren())
            if (auto* sl = dynamic_cast<Slider*> (c))
                for (auto* k : sl->getChildren())
                    if (auto* l = dynamic_cast<Label*> (k))
                        std::printf ("label '%s' text %s bg %s\n", l->getText().toRawUTF8(), l->findColour (Label::textColourId).toString().toRawUTF8(),
                                     l->findColour (Label::backgroundColourId).toString().toRawUTF8());
    const auto img = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
    File out (File::getCurrentWorkingDirectory().getChildFile (argv[3]));
    out.deleteFile();
    FileOutputStream os (out);
    PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%zu notes, wrote %s\n", m.notes.size(), out.getFullPathName().toRawUTF8());
    return 0;
}
