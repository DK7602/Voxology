// The song's style (shared with Mixology and Masterology) suggests the vocal style (vox/SongStyle.h).

#include "vox/SongStyle.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

using namespace vox::songstyle;

TEST_CASE ("Song style: suggests the vocal style that usually fits", "[songstyle]")
{
    CHECK (vocalStyleFor ("hiphop.modern") == rap);
    CHECK (vocalStyleFor ("hiphop.gfunk") == rap);
    CHECK (vocalStyleFor ("trap.hard808") == trapLead);
    CHECK (vocalStyleFor ("trap.melodic") == melodic);
    CHECK (vocalStyleFor ("rnb.smooth") == rnb);
    CHECK (vocalStyleFor ("pop.modern") == pop);
    CHECK (vocalStyleFor ("pop.hyperpop") == robot);
    CHECK (vocalStyleFor ("rock.metal") == naturalSinger);
    CHECK (vocalStyleFor ("folk.songwriter") == folk);
    CHECK (vocalStyleFor ("latin.reggaeton") == melodic);
    CHECK (vocalStyleFor ("") == -1);
    CHECK (vocalStyleFor ("hiphop") == -1);
    CHECK (vocalStyleFor ("polka.fast") == -1);
}

TEST_CASE ("Song style: every style in the shared list has a vocal style", "[songstyle]")
{
    // plugin/ui/mixstyles.js is the same file in all three plug-ins: g("genre", ...) then ["style", ...] rows.
    std::ifstream file (VOX_UI_STYLES);
    REQUIRE (file.good());
    std::stringstream all;
    all << file.rdbuf();
    const auto text = all.str();
    const std::regex genreRe (R"re(g\("([a-z]+)")re"), styleRe (R"re(\[\s*"([a-z0-9]+)",\s*")re");
    int checked = 0;
    std::string genre;
    std::istringstream lines (text);
    for (std::string line; std::getline (lines, line);)
    {
        std::smatch m;
        if (std::regex_search (line, m, genreRe)) { genre = m[1]; continue; }
        if (! genre.empty() && std::regex_search (line, m, styleRe))
        {
            const auto key = genre + "." + m[1].str();
            INFO (key);
            const int v = vocalStyleFor (key);
            CHECK (v >= trapLead);
            CHECK (v <= naturalSinger);
            ++checked;
        }
    }
    CHECK (checked == 75);
}
