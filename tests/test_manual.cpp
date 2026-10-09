// The built-in manual (docs/MANUAL.md, shown by the ? button in both plug-ins) must keep up with the plug-in:
// same version as the build, and it names every style and every built-in reference.
#include "vox/AutoEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
std::string readFile (const char* path)
{
    std::ifstream in (path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
}

TEST_CASE ("Manual: same version as the plug-in, names every style and built-in reference", "[manual]")
{
    const auto manual = readFile (VOX_MANUAL_PATH);
    REQUIRE (! manual.empty());
    const std::string version = VOX_VERSION;
    INFO ("Update docs/MANUAL.md for this release (version line + what changed)");
    CHECK (manual.find ("Manual for version " + version + ".") != std::string::npos);
    CHECK (manual.find ("VOXOLOGY v" + version) != std::string::npos);
    CHECK (readFile (VOX_UI_INDEX).find ("VOXOLOGY v" + version) != std::string::npos);
    for (const char* style : vox::kStyleNames)
    {
        INFO (style);
        CHECK (manual.find (style) != std::string::npos);
    }
    for (const auto& ref : vox::builtinReferences())
    {
        INFO (ref.name);
        CHECK (manual.find (ref.name) != std::string::npos);
    }
}
