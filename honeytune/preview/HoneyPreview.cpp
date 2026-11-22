// Draws the Honey Tune editor with a real vocal into a PNG (developer tool, no host needed).
// HoneyPreview <mono float32 raw> <sample rate> <out.png> [key 0-11|12=auto] [scale] [snap]
// Env: HONEY_W / HONEY_H size, HONEY_BPM tempo map, HONEY_MANUAL the manual, HONEY_BEAT=key,scale a key from Voxology's
// beat, HONEY_VOXOLOGY=1|2|3 the Voxology on this track (Pitch resting / tuning anyway / tuning), HONEY_STEADY=n note n
// selected and steadied (NOTE DRIFT + NOTE VIBRATO 0), HONEY_PLAYHEAD=seconds, HONEY_STOPPED=1 (playback stopped),
// HONEY_SPLIT=n note n cut in two in its middle (and selected), HONEY_JOIN=n note n joined with the next,
// HONEY_GAIN=n,dB note n louder / quieter, HONEY_MUTE=n note n muted, HONEY_ADD=mask / HONEY_REMOVE=mask notes switched into /
// out of the key (bit 0 = C), HONEY_A4=Hz the tuning, HONEY_AUTOTUNE=cents the beat's tuning from Voxology (Auto),
// HONEY_NEXT=1 N pressed (the next note to check selected).

#include <juce_gui_basics/juce_gui_basics.h>

#include "../HoneyPanel.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

using namespace juce;

struct MemoryModel final : honeyui::Model
{
    std::shared_ptr<const vox::honey::Track> track;
    std::vector<vox::honey::Note> found, notes;
    vox::honey::Layout layout;
    std::vector<honeyui::NoteEdit> edits;
    vox::KeyGuess guess;
    honeyui::Settings settings;

    honeyui::Snapshot snapshot() override
    {
        honeyui::HostKey beat;
        if (const char* b = std::getenv ("HONEY_BEAT"))
        {
            beat.valid = beat.fromBeat = true;
            std::sscanf (b, "%d,%d", &beat.key, &beat.scale);
        }
        auto s = honeyui::makeSnapshot (track, notes, edits, guess, settings, beat,
                                        std::getenv ("HONEY_AUTOTUNE") ? std::atoi (std::getenv ("HONEY_AUTOTUNE")) : 0);
        s.status = 2;
        if (const char* v = std::getenv ("HONEY_VOXOLOGY"))
        {
            s.voxologyHere = true;
            s.voxologyPitch = static_cast<vox::honeylink::Pitch> (std::clamp (std::atoi (v) == 1 ? 2 : std::atoi (v) == 2 ? 3 : 1, 0, 3));
        }
        if (const char* bpm = std::getenv ("HONEY_BPM"))   // a fake host tempo map (4/4)
            for (int b = 0; b < 400; ++b)
                for (int k = 0; k < 4; ++k)
                    s.timeline.lines.push_back ({ (b * 4 + k) * 60.0 / std::atof (bpm), b + 1, k + 1 });
        return s;
    }
    honeyui::Settings getSettings() override { return settings; }
    void setSettings (const honeyui::Settings& s) override { settings = s; }
    void setEdit (int i, const honeyui::NoteEdit& e) override { edits[static_cast<size_t> (i)] = e; }
    void resetAllEdits() override { edits.assign (notes.size(), {}); }
    bool reshapeWith (const vox::honey::Layout& next)
    {
        auto now = vox::honey::reshape (*track, found, next);
        if (now.size() == notes.size()) return false;
        edits = honeyui::carryEdits (notes, edits, now, track->sampleRate);
        notes = std::move (now);
        layout = next;
        return true;
    }
    bool splitNote (int i, double seconds) override
    {
        auto next = layout;
        return vox::honey::addSplit (next, *track, notes, i, seconds) && reshapeWith (next);
    }
    bool joinNotes (int first, int last) override
    {
        auto next = layout;
        return vox::honey::addJoin (next, *track, notes, first, last) && reshapeWith (next);
    }
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
    m.found = m.notes = vox::honey::findNotes (*t);
    std::vector<double> sung;
    for (double v : t->midi) if (v > 0.0) sung.push_back (v);
    m.guess = vox::detectKey (sung);
    m.track = t;
    m.edits.assign (m.notes.size(), {});
    if (argc > 4) m.settings.key = std::atoi (argv[4]);
    if (argc > 5) m.settings.scale = std::atoi (argv[5]);
    m.settings.snap = argc > 6 ? std::atof (argv[6]) : 0.0;   // show the notes as sung (glow), unless asked
    if (const char* a = std::getenv ("HONEY_ADD")) m.settings.added = std::atoi (a);
    if (const char* r = std::getenv ("HONEY_REMOVE")) m.settings.removed = std::atoi (r);
    if (const char* a4 = std::getenv ("HONEY_A4")) m.settings.a4 = std::atof (a4);
    m.settings = honeyui::sanitised (m.settings);

    HoneyPanel panel;
    panel.setSize (500, 300);   // hosts open small, then enlarge
    panel.setModel (&m);
    panel.setSize (std::getenv ("HONEY_W") ? std::atoi (std::getenv ("HONEY_W")) : 1280, std::getenv ("HONEY_H") ? std::atoi (std::getenv ("HONEY_H")) : 720);
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
    // The selected note (HONEY_STEADY=<note number> picks it, and steadies it).
    int pick = static_cast<int> (m.notes.size() / 3);
    if (const char* st = std::getenv ("HONEY_STEADY"); st != nullptr && ! m.edits.empty())
    {
        if (std::atoi (st) > 0) pick = std::min (std::atoi (st), static_cast<int> (m.notes.size())) - 1;
        auto& e = m.edits[static_cast<size_t> (pick)];
        e.drift = e.vibrato = 0.0;
    }
    if (const char* g = std::getenv ("HONEY_GAIN"))
    {
        int n = 0;
        double db = 0.0;
        if (std::sscanf (g, "%d,%lf", &n, &db) == 2 && n > 0 && n <= static_cast<int> (m.edits.size())) m.edits[static_cast<size_t> (n - 1)].gainDb = db;
    }
    if (const char* mu = std::getenv ("HONEY_MUTE"); mu != nullptr && std::atoi (mu) > 0 && std::atoi (mu) <= static_cast<int> (m.edits.size()))
        m.edits[static_cast<size_t> (std::atoi (mu) - 1)].muted = true;
    panel.refresh();
    panel.roll.setSelected (pick);
    if (std::getenv ("HONEY_NEXT")) { panel.roll.setSelected (-1); panel.roll.nextCheck(); }
    if (const char* sp = std::getenv ("HONEY_SPLIT"); sp != nullptr && std::atoi (sp) > 0)
    {
        const int n = std::min (std::atoi (sp), static_cast<int> (m.notes.size())) - 1;
        const auto& note = m.notes[static_cast<size_t> (n)];
        panel.roll.splitAt (n, 0.5 * (note.soundStart() + note.soundEnd()) / sr);
    }
    if (const char* jn = std::getenv ("HONEY_JOIN"); jn != nullptr && std::atoi (jn) > 0)
    {
        panel.roll.setSelected (std::min (std::atoi (jn), static_cast<int> (m.notes.size())) - 1);
        panel.roll.join (panel.roll.getSelected());
    }
    panel.setPlayhead (std::getenv ("HONEY_PLAYHEAD") ? std::atof (std::getenv ("HONEY_PLAYHEAD")) : 2.5, std::getenv ("HONEY_STOPPED") == nullptr);

    if (std::getenv ("HONEY_MANUAL"))   // show the manual (the ? button)
        for (auto* c : panel.getChildren())
            if (auto* b = dynamic_cast<TextButton*> (c); b != nullptr && b->getButtonText() == "?" && b->onClick)
                b->onClick();

    if (std::getenv ("HONEY_DEBUG"))
        for (auto* c : panel.getChildren())
            if (auto* sl = dynamic_cast<Slider*> (c))
                for (auto* k : sl->getChildren())
                    if (auto* l = dynamic_cast<Label*> (k))
                        std::printf ("label '%s' text %s bg %s\n", l->getText().toRawUTF8(), l->findColour (Label::textColourId).toString().toRawUTF8(),
                                     l->findColour (Label::backgroundColourId).toString().toRawUTF8());
    if (std::getenv ("HONEY_NOTES"))
        for (const auto& v : panel.roll.getSnapshot().notes)
            std::printf ("note %.3f-%.3f s  sung %.2f  plays %.2f  check %d (other %.2f, join %d)\n", v.note.start / sr, v.note.end / sr,
                         v.note.pitch, v.note.target, static_cast<int> (v.check.why), v.check.other, v.check.joinWith);
    const auto img = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
    File out (File::getCurrentWorkingDirectory().getChildFile (argv[3]));
    out.deleteFile();
    FileOutputStream os (out);
    PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%zu notes, wrote %s\n", m.notes.size(), out.getFullPathName().toRawUTF8());
    return 0;
}
