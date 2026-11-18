#pragma once

#include "AutoEdit.h"

#include <string>
#include <vector>

namespace vox::coach {

/** The Sonicology Rack's Coach (stage 3), for a vocal: what Auto-Edit heard and would do, as the Rack's COACH page shows
    it. Everything here is measured on the take Auto-Edit heard, never made up: the issues against what Auto-Edit aims at
    (the style's balance of finished pro vocals, or the reference), and the results by running the take through the
    proposed settings and measuring it again ("Mud +4.1 -> +1.2 dB"). */

/** One thing that stands out in the take, against the target. */
struct Issue
{
    std::string key;           // "mud", "harsh", "sibilance", "air", ...
    std::string name;          // "Mud", "Harshness", ...
    double fromHz = 0.0, toHz = 0.0;   // where on the spectrum (0 = not a place: level, noise)
    double db = 0.0;           // how far off (dB; the sign says over / under)
    bool warn = false;         // red (worth fixing) or amber / blue (good to know)
    std::string text;          // one line ("200 - 400 Hz is 4.1 dB over Trap Lead's balance")
};

/** One measured change, before -> after (both on the take: before as it came in, after through the proposed settings). */
struct Result
{
    std::string key, name;     // "mud", "Mud (200 - 400 Hz)"
    double before = 0.0, after = 0.0;
    std::string unit;          // "dB vs target", "dB swing" ...
    bool better = false;       // after is closer to where it should be
};

/** Tone regions (inclusive band centres) the Coach names. */
struct Region { const char* key; const char* name; double fromHz, toHz; double overDb, underDb; };
const std::vector<Region>& regions();

/** Mean of (take - target) over the analysis bands in [fromHz, toHz] (0 when there are none). */
double regionOffset (const std::vector<double>& bandDb, const std::vector<double>& target, double fromHz, double toHz);

std::vector<Issue> issues (const VocalAnalysis& a, const std::vector<double>& target, double sibTargetDb, const std::string& aimName);
std::vector<Result> results (const VocalAnalysis& before, const VocalAnalysis& after, const std::vector<double>& target, double sibTargetDb);

/** Where Voxology stands now (Beta 30). Auto-Edit always decides from the take as it comes in, so after an APPLY the
    same take gives the same plan; the Coach measures what the settings Voxology has now already do to it, and says
    which settings the plan would still change. */
struct Now
{
    const VocalAnalysis* sound = nullptr;   // the take through the settings Voxology has now (nullptr: not measured)
    bool compared = false;                  // the plan was compared with the settings now
    std::vector<std::string> changedIds;    // the plug-in parameter ids the plan would change (when compared)
};

/** The Rack's module key for one of Voxology's parameter ids ("eqGain2" -> "vox.eq"; "" for none). */
const char* rackKeyOfParam (const std::string& id);

/** The plan as the Rack reads it (a JSON object): heard, issues, the recommended chain with each module's reasons,
    bands (before / after / target), results, what to adjust first. At most `maxBytes` (texts shortened, then dropped,
    to fit; always valid JSON). `after` may be nullptr (not measured: no results, no after curve). With `now`: what
    stands out and "before" are measured through the settings now, each module says whether it is already set as
    planned ("same"), "changes" counts the settings the plan would change, and "upToDate" is true when there are none. */
std::string planJson (const AutoEditResult& r, const VocalAnalysis* after, const std::vector<double>& target, double sibTargetDb,
                      const AutoEditSettings& s, const std::string& reference, std::size_t maxBytes, const Now* now = nullptr);

} // namespace vox::coach
