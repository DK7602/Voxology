// Key benchmark: the voice's key as Voxology hears it vs the sung keys in test-audio/cymatics/labels.csv (54 vocals,
// labelled from their sung notes by an independent FFT pitch method; many pack file names are a semitone off).
// Two detectors: Auto-Edit's guess (analyseVocal -> KeyGuess: sets Key / Scale when >= 60 % and not ambiguous; also
// Honey Tune's voice fallback) and the live Pitch AUTO key (VoiceKey, fed the pitch readings in order, file played twice).
// Right = the same 7 notes as one of the label's fitting sets (relative major / minor share them). Wrong only counts
// when the detector is sure: an unsure guess falls back to Chromatic (safe).
// Build: g++ -std=c++20 -O2 -Idsp/include tools/audit/keybench.cpp build-t/dsp/libvox_dsp.a -lpthread -o keybench
// Decode: for f in test-audio/cymatics/*.mp3; do ffmpeg -i "$f" -ac 1 -ar 48000 -f f64le "dir/$(basename "${f%.mp3}").f64"; done
// Run:   keybench dir test-audio/cymatics/labels.csv
#include "vox/AutoEdit.h"
#include "vox/BeatKey.h"
#include "vox/HoneyTune.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

int noteIndex (const std::string& s)
{
    for (int k = 0; k < 12; ++k) if (s == kNames[k]) return k;
    return -1;
}

std::vector<std::string> splitCsv (const std::string& line)
{
    std::vector<std::string> out;
    std::string cell;
    std::istringstream ss (line);
    while (std::getline (ss, cell, ',')) out.push_back (cell);
    return out;
}

/** "D# major or A# major" -> the major-scale roots of the fitting note sets; empty = too few notes to tell. */
std::vector<int> fittingSets (const std::string& text)
{
    std::vector<int> roots;
    std::istringstream ss (text);
    std::string word;
    while (ss >> word) if (const int k = noteIndex (word); k >= 0) roots.push_back (k);
    return roots;
}
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: keybench f64dir labels.csv\n"); return 1; }
    std::ifstream csv (argv[2]);
    std::string line;
    std::getline (csv, line);
    int files = 0, aeRight = 0, aeWrong = 0, liveRight = 0, liveWrong = 0;
    while (std::getline (csv, line))
    {
        const auto c = splitCsv (line);
        if (c.size() < 6 || c[4].rfind ("unclear", 0) == 0) continue;   // no trustworthy label
        const auto sets = fittingSets (c[5]);
        if (sets.empty()) continue;
        const std::string path = std::string (argv[1]) + "/" + c[0].substr (0, c[0].size() - 4) + ".f64";
        std::ifstream in (path, std::ios::binary);
        std::vector<float> x;
        double v;
        while (in.read (reinterpret_cast<char*> (&v), sizeof v)) x.push_back (static_cast<float> (v));
        if (x.empty()) { std::printf ("missing %s\n", path.c_str()); continue; }
        auto fits = [&] (int root) { for (int s : sets) if (s == root) return true; return false; };

        const auto a = vox::analyseVocal ({ x }, 48000.0);
        const bool aeSure = a.key.confidence >= 0.6 && ! a.key.ambiguous;
        const bool aeSame = fits (a.key.minor ? (a.key.key + 3) % 12 : a.key.key);

        const auto t = vox::honey::analyse (x, 48000.0, false);
        vox::VoiceKey live;
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < t.midi.size(); ++i)
                live.add (t.midi[i] > 0.0, t.midi[i], t.clarity[i], (i + 1 < t.time.size() ? t.time[i + 1] - t.time[i] : 128.0) / 48000.0);
        const auto r = live.result();
        const bool liveSure = r.ready && r.confidence >= 0.5;
        const bool liveSame = fits (r.setRoot);

        ++files;
        if (aeSure) (aeSame ? aeRight : aeWrong)++;
        if (liveSure) (liveSame ? liveRight : liveWrong)++;
        std::printf ("%-60s sung %-14s Auto-Edit %s%s %3.0f%% %-7s  live %s major notes %3.0f%% %s\n", c[0].c_str(), c[4].c_str(),
                     kNames[a.key.key], a.key.minor ? "m" : " ", 100.0 * a.key.confidence, ! aeSure ? "unsure" : aeSame ? "RIGHT" : "WRONG",
                     kNames[r.setRoot], 100.0 * r.confidence, ! liveSure ? "unsure" : liveSame ? "RIGHT" : "WRONG");
    }
    std::printf ("\n%d labelled vocals\nAuto-Edit key: %d right, %d WRONG, %d unsure (Chromatic)\nLive AUTO key: %d right, %d WRONG, %d unsure\n",
                 files, aeRight, aeWrong, files - aeRight - aeWrong, liveRight, liveWrong, files - liveRight - liveWrong);
    return 0;
}
