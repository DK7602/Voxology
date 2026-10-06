cmake_minimum_required(VERSION 3.25)

# Universal binaries on macOS (Apple silicon + Intel). Must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

project(Voxology VERSION 0.15.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Static MSVC runtime so users don't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(VOX_BUILD_PLUGIN "Build the JUCE plug-in (VST3 / AU / Standalone)" ON)
option(VOX_BUILD_TESTS  "Build the DSP unit tests" ON)
option(VOX_BUILD_LINKCHECK "Build the two-instance Unmask check (local)" OFF)

# Framework-free DSP engine: the vocal chain and Auto-Edit.
add_subdirectory(dsp)

if(VOX_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
    if(VOX_BUILD_LINKCHECK)
        add_subdirectory(tools/linkcheck)
    endif()

    # Honey Tune (the note editor) is an ARA plug-in: Celemony's ARA SDK (Apache 2.0), a tagged release.
    FetchContent_Declare(ara_sdk
        GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
        GIT_TAG        releases/2.3.0
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES ARA_API ARA_Library)
    FetchContent_MakeAvailable(ara_sdk)
    juce_set_ara_sdk_path("${ara_sdk_SOURCE_DIR}")
    add_subdirectory(honeytune)
endif()

if(VOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
