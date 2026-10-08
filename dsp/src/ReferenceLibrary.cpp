#include "vox/AutoEdit.h"

// Built-in references: finished pro vocals measured with analyseVocal and grouped by voice (median pitch under 190 Hz
// "male", over 230 Hz "female") and delivery (held notes >= 30 % "singer", else "rap / rhythmic"); each entry is the
// median of its group. Source: the MUSDB18 7 s preview set (143 vocal stems of released songs, mostly pop / rock /
// indie; research use: only these measurements are kept, no audio). Space after phrases can't be measured on 7 s
// clips, so it's left to the style (tailDb -120 = unknown). Made with tools/audit/tone_profile.cpp + a script.
namespace vox {

namespace {
ReferenceProfile make (const char* name, const char* about, int clips, double f0, double sib, double punch, std::vector<double> bands)
{
    ReferenceProfile r;
    r.ok = true;
    r.name = name;
    r.about = about;
    r.builtin = true;
    r.voicedSeconds = 5.0 * clips;
    r.f0Median = f0;
    r.sibilanceDb = sib;
    r.microDynDb = punch;
    r.tailDb = -120.0;
    r.bandDb = std::move (bands);
    return r;
}
}

const std::vector<ReferenceProfile>& builtinReferences()
{
    static const std::vector<ReferenceProfile> refs {
        make ("Pro male singer", "Low voices that mostly sing, from finished pop / rock / indie mixes: warm, full low mids, smooth top.", 18, 156, -4.3, 3.2, { -19.8, -8.0, -1.7, 0.4, -2.7, 2.4, 4.9, 2.5, 2.3, -1.1, -3.0, -2.4, -2.1, -9.4, -8.1, -9.4, -11.9, -15.1, -13.2, -13.6, -14.7, -19.7, -28.8 }),
        make ("Pro male rap / rhythmic", "Low voices that rap or deliver rhythmically: forward mids, a brighter top, tight s sounds.", 12, 158, -5.3, 3.2, { -31.1, -26.0, -12.4, -6.0, -7.4, 2.3, 0.6, 0.5, 3.8, 0.5, -1.8, -1.9, -3.0, -6.6, -7.6, -8.1, -9.4, -14.6, -13.6, -13.7, -12.2, -16.4, -26.3 }),
        make ("Pro female singer, bright", "Higher voices that mostly sing, the brighter half: airy, open top end (modern pop).", 26, 308, -5.0, 4.2, { -36.2, -33.9, -28.9, -12.5, -5.1, 0.7, -4.8, -2.6, 1.7, 0.7, 1.5, -2.5, -1.9, -6.1, -6.3, -6.8, -9.3, -12.7, -11.6, -10.6, -13.1, -17.2, -25.7 }),
        make ("Pro female singer, warm", "Higher voices that mostly sing, the warmer half: smooth, soft top, gentle s sounds (soul / ballad).", 25, 343, -10.2, 3.5, { -41.3, -40.0, -35.3, -24.6, -11.8, -4.8, -4.2, -3.8, 0.2, 2.3, -0.7, -1.6, -3.7, -9.8, -12.0, -8.9, -13.2, -20.1, -20.8, -20.9, -21.5, -25.7, -33.8 }),
        make ("Pro female rap / rhythmic", "Higher voices that rap or deliver rhythmically: present mids, clear top, crisp s sounds.", 35, 280, -4.3, 3.8, { -32.6, -32.0, -23.7, -13.5, -6.2, -3.6, -2.3, -0.2, 1.9, 0.3, -0.8, -1.7, -2.4, -4.6, -4.1, -5.3, -9.1, -13.5, -13.0, -11.9, -13.1, -18.1, -27.7 }),
        make ("Pro average (all voices)", "The middle of all 143 finished pro vocals: a neutral, balanced target.", 143, 262, -5.1, 3.5, { -33.8, -31.4, -18.0, -7.9, -5.1, -1.0, -0.4, -0.2, 2.0, 0.6, -1.1, -2.3, -2.9, -6.2, -6.6, -7.0, -9.8, -14.1, -13.2, -13.4, -14.8, -19.4, -28.2 }),
    };
    return refs;
}

} // namespace vox
