#include "vox/AutoAnalyze.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vox {

namespace {
bool known (double db) { return db > -119.0; }

std::string num (double v, int decimals = 0)
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%.*f", decimals, std::abs (v) < 0.5 * std::pow (10.0, -decimals) ? 0.0 : v);
    return buf;
}

std::string hz (double f) { return f >= 1000.0 ? num (f / 1000.0, 1) + " kHz" : num (f) + " Hz"; }

const char* region (double f)
{
    if (f < 250.0) return "the low end (body and boom)";
    if (f < 500.0) return "the low mids (where it gets muddy)";
    if (f < 2000.0) return "the mids (where it gets honky)";
    if (f < 6000.0) return "the presence (where words cut through)";
    return "the top (air and s sounds)";
}

// Changes smaller than these are the normal difference between two parts of the same take.
constexpr double kLevelDb = 4.0;       // voice level (RMS while singing)
constexpr double kNoiseDb = 6.0;       // noise floor in the gaps
constexpr double kNoiseAudibleDb = -70.0;
constexpr double kRumbleDb = 6.0;
constexpr double kRumbleAudibleDb = -30.0;
constexpr double kLowNoteSemis = 3.0;  // lowest note
constexpr double kSibUpDb = 3.0, kSibDownDb = 4.0;
constexpr double kToneDb = 3.0;        // third-octave balance, smoothed over three bands
constexpr double kPunchDb = 2.5;       // word-to-word punch
constexpr double kRangeDb = 4.0;       // loud vs quiet phrases
constexpr double kBeatLevelDb = 1.5;   // vocal against the beat
}

std::vector<AnalyzeNeed> findNewNeeds (const VocalAnalysis& was, const BeatAnalysis& wasBeat, const VocalAnalysis& a, const BeatAnalysis& b)
{
    std::vector<AnalyzeNeed> out;
    if (a.voicedSeconds <= 0.0 || ! known (a.voiceRmsDb))
        return out;
    auto add = [&out] (const char* key, std::string note, std::vector<Module> retune, bool fitLevel = false)
    { out.push_back ({ key, std::move (note), std::move (retune), fitLevel }); };

    // Clipping can't be fixed afterwards: advice only.
    if (a.clippedRuns > 0 && was.clippedRuns == 0)
        add ("clip", "CLIPPED HERE: this part of your vocal arrives clipped (" + num (a.clippedRuns) + (a.clippedRuns == 1 ? " spot" : " spots") +
                     " where the waveform hit the top), which wasn't in the part Auto-Edit heard. No plug-in can fully undo clipping: it sounds like crackle or fizz on loud words."
                     "\nNEED: Yes, if you hear crackle on the loud words. If it's only a spot or two you can't hear, no."
                     "\nSTEP: Re-record this part with the mic input (preamp) gain 6 dB lower: aim for peaks around -10 dB on {daw}'s meter."
                     "\nSTEP: Or, if it was a printed / bounced vocal, bounce it again with the level lower."
                     "\nSTEP: Then press AUTO-EDIT again: the gate, Rider and Compressor are set for the level it heard, so they need setting for the new one.",
             {});

    // Level: a new take, or the singer closer / farther from the mic.
    if (known (a.voiceRmsDb) && known (was.voiceRmsDb) && std::abs (a.voiceRmsDb - was.voiceRmsDb) >= kLevelDb)
    {
        const double d = a.voiceRmsDb - was.voiceRmsDb;
        const bool up = d > 0.0;
        add ("level", std::string (up ? "LOUDER HERE" : "QUIETER HERE") + ": your vocal comes in " + num (std::abs (d)) + " dB " + (up ? "louder" : "quieter") +
                          " in this part than where Auto-Edit listened (a new take, or closer to / farther from the mic). The gate and the compressor were set for the old level."
                          "\nNEED: Yes, if the whole song is like this part. If only this part is " + (up ? "louder" : "quieter") +
                          ", no: the Rider already evens out parts of a song."
                          "\nSTEP: Press APPLY below: the gate, Rider and compressor settings Auto-Edit would choose for this part."
                          "\nSTEP: Then play a part you checked before and compare with A / B. UNDO puts everything back.",
             { Module::cleanup, Module::rider, Module::comp });
    }

    // Noise: a fan, AC or a different room.
    if (a.heardGaps && was.heardGaps && known (a.noiseFloorDb) && known (was.noiseFloorDb) && a.noiseFloorDb - was.noiseFloorDb >= kNoiseDb && a.noiseFloorDb > kNoiseAudibleDb)
        add ("noise", "MORE NOISE HERE: the gaps between your phrases are " + num (a.noiseFloorDb - was.noiseFloorDb) + " dB noisier in this part (now " + num (a.noiseFloorDb) +
                          " dB): a fan, AC, a computer, or another room or take."
                          "\nNEED: Yes, if you can hear hiss or hum between your words with the beat playing. If the beat covers it, optional."
                          "\nSTEP: Press APPLY below: Cleanup's Noise and gate, set for this part."
                          "\nSTEP: Best of all, for the next recording: switch the noise off or move away from it.",
             { Module::cleanup });

    // Low end: rumble, or lower notes than the Low Cut allows for.
    {
        const bool rumble = known (a.rumbleDb) && known (was.rumbleDb) && a.rumbleDb - was.rumbleDb >= kRumbleDb && a.rumbleDb > kRumbleAudibleDb;
        const double semis = a.f0Low > 0.0 && was.f0Low > 0.0 ? 12.0 * std::log2 (was.f0Low / a.f0Low) : 0.0;
        if (rumble || semis >= kLowNoteSemis)
            add ("low", rumble ? "MORE RUMBLE HERE: there's " + num (a.rumbleDb - was.rumbleDb) + " dB more energy under 60 Hz in this part (mic-stand bumps, traffic, AC or pops)."
                                     "\nNEED: Yes, if you're mixing on speakers or the low end sounds muddy. On the 808 it can hide, but it still eats headroom."
                                     "\nSTEP: Press APPLY below: Cleanup's Low Cut (and Pops) set for this part."
                               : "LOWER NOTES HERE: you sing down to " + hz (a.f0Low) + " in this part, " + num (semis) + " semitones lower than where Auto-Edit listened."
                                     " The Low Cut was set just under your lowest note, so it may now thin these notes."
                                     "\nNEED: Yes, if these low notes sound thin. Otherwise optional."
                                     "\nSTEP: Press APPLY below: the Low Cut moves under your new lowest note.",
                 { Module::cleanup });
    }

    // S sounds.
    if (known (a.sibilanceDb) && known (was.sibilanceDb))
    {
        const double d = a.sibilanceDb - was.sibilanceDb;
        if (d >= kSibUpDb)
            add ("sib", "MORE S HERE: the s and t sounds are " + num (d) + " dB sharper in this part than where Auto-Edit listened (another take, or the mic angle changed)."
                            "\nNEED: Yes, if the s sounds hurt on headphones. Otherwise optional."
                            "\nSTEP: Press APPLY below: the De-Esser set for this part.",
                 { Module::deEsser });
        else if (d <= -kSibDownDb)
            add ("sib", "SOFTER S HERE: the s sounds are " + num (-d) + " dB softer in this part, so the De-Esser may now take too much (a lispy, dull s)."
                            "\nNEED: Optional: only if the s sounds dull or lispy here."
                            "\nSTEP: Press APPLY below: the De-Esser set for this part.",
                 { Module::deEsser });
    }

    // Tone: the balance across the voice, smoothed so one band alone doesn't count.
    if (a.bandDb.size() == was.bandDb.size() && ! a.bandDb.empty())
    {
        const auto& bands = analysisBands();
        const size_t n = std::min (bands.size(), a.bandDb.size());
        std::vector<double> d (n, 0.0);
        for (size_t k = 0; k < n; ++k)
            d[k] = a.bandDb[k] - was.bandDb[k];
        double worst = 0.0, worstHz = 0.0;
        for (size_t k = 1; k + 1 < n; ++k)
        {
            if (bands[k] < 120.0 || bands[k] > 10000.0)
                continue;
            const double s = (d[k - 1] + d[k] + d[k + 1]) / 3.0;
            if (std::abs (s) > std::abs (worst)) { worst = s; worstHz = bands[k]; }
        }
        if (std::abs (worst) >= kToneDb)
            add ("tone", "NEW TONE HERE: this part has " + num (std::abs (worst)) + " dB " + (worst > 0.0 ? "more" : "less") + " " + region (worstHz) + ", around " + hz (worstHz) +
                             ", than where Auto-Edit listened (another mic position, another take, or another voice on this track)."
                             "\nNEED: Yes, if this part sounds " + (worst > 0.0 ? (worstHz < 500.0 ? "boomy or muddy" : "harsh or thin") : (worstHz < 500.0 ? "thin" : "dull")) +
                             " next to the rest. If this part is meant to sound different, no."
                             "\nSTEP: Press APPLY below: the Tone EQ and Dynamic EQ set for this part."
                             "\nSTEP: If another singer is on this part, give them their own track and Voxology instead.",
                 { Module::eq, Module::dynEq });
    }

    // Dynamics: punchier or flatter delivery, louder and quieter phrases.
    {
        const double punch = a.microDynDb > 0.0 && was.microDynDb > 0.0 ? a.microDynDb - was.microDynDb : 0.0;
        const double range = a.rangeDb > 0.0 && was.rangeDb > 0.0 ? a.rangeDb - was.rangeDb : 0.0;
        if (std::abs (punch) >= kPunchDb || std::abs (range) >= kRangeDb)
        {
            const bool more = std::abs (punch) >= kPunchDb ? punch > 0.0 : range > 0.0;
            add ("dynamics", std::string (more ? "MORE MOVEMENT HERE" : "STEADIER HERE") + ": your delivery " +
                                 (more ? "jumps more between loud and soft words" : "stays more even") + " in this part (" +
                                 (std::abs (punch) >= kPunchDb ? "punch " + std::string (punch > 0.0 ? "+" : "-") + num (std::abs (punch), 1) + " dB"
                                                               : "loud vs quiet phrases " + std::string (range > 0.0 ? "+" : "-") + num (std::abs (range)) + " dB") +
                                 "), so the compressor works " + (more ? "harder" : "less") + " than it was set for."
                                 "\nNEED: Optional: only if words " + (more ? "jump out or get lost" : "sound squashed") + " in this part."
                                 "\nSTEP: Press APPLY below: the Rider and compressor set for this part.",
                 { Module::rider, Module::comp });
        }
    }

    // The beat (Vocal Fit).
    if (b.hasYourVocal && ! wasBeat.hasYourVocal)
        add ("yourvocal", "YOUR VOCAL IS IN THE BEAT: what comes into the side-chain now has your own vocal in it (a full mix or a bounce with vocals), so Vocal Fit can't judge this part."
                              "\nNEED: Yes, for Vocal Fit."
                              "\nSTEP: Send only the beat to \"Voxology - Side-Chain\": the instrumental track, or a group with just the music on it.",
             {});
    else if (b.heard && ! b.hasYourVocal && ! wasBeat.heard)
        add ("beatnew", "THE BEAT IS HERE NOW: Auto-Edit set your chain without a beat, and now Voxology hears one on the side-chain. Vocal Fit can fit your tone and level to it."
                            "\nNEED: Yes: it's the best way to make the vocal sit in this beat."
                            "\nSTEP: Press APPLY below: the Tone EQ and compressor nudged for this beat, and Output to where pros put the vocal against it (if it isn't there already)."
                            "\nSTEP: Judge it with the beat playing; UNDO puts everything back.",
             { Module::eq, Module::comp }, true);
    else if (b.heard && wasBeat.heard && ! b.hasYourVocal && ! wasBeat.hasYourVocal &&
             std::abs (b.vocalOverBeatDb - wasBeat.vocalOverBeatDb) >= kBeatLevelDb)
    {
        const double d = b.vocalOverBeatDb - wasBeat.vocalOverBeatDb;
        add ("beatlevel", std::string (d < 0.0 ? "THE BEAT IS LOUDER HERE" : "THE BEAT IS QUIETER HERE") + ": against your vocal, the beat is " + num (std::abs (d), 1) +
                              " dB " + (d < 0.0 ? "louder" : "quieter") + " in this part than where Auto-Edit listened, so the vocal sits " + (d < 0.0 ? "lower" : "higher") + " in the mix here."
                              "\nNEED: Optional: if this part of the song is meant to hit harder, the difference is the producer's choice."
                              "\nSTEP: Press APPLY below: Output moves so the vocal sits where pro mixes put it against this part."
                              "\nSTEP: For different levels per part, automate the vocal fader in {daw} instead ({daw:automate}).",
             {}, true);
    }
    return out;
}

} // namespace vox
