add_library(vox_dsp STATIC
    src/KWeighting.cpp
    src/TruePeakDetector.cpp
    src/LoudnessMeter.cpp
    src/SoftClipper.cpp
    src/Saturation.cpp
    src/Modules.cpp
    src/DynamicEq.cpp
    src/PopBreath.cpp
    src/Unmask.cpp
    src/Voices.cpp
    src/Space.cpp
    src/VocalChain.cpp
    src/AutoEdit.cpp
    src/PitchCorrector.cpp
    src/HoneyTune.cpp
    src/BeatKey.cpp
    src/ClipWatch.cpp
    src/Crepe.cpp
    src/CrepeWeights.cpp)

target_include_directories(vox_dsp PUBLIC include)
target_compile_features(vox_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(vox_dsp PRIVATE /W4 /fp:precise)
else()
    target_compile_options(vox_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# The AI pitch check (CREPE) is the heaviest loop: full optimisation even in Release builds that use -O2.
if(NOT MSVC)
    set_source_files_properties(src/Crepe.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-O3>")
endif()
find_package(Threads REQUIRED)
target_link_libraries(vox_dsp PUBLIC Threads::Threads)

set_target_properties(vox_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Voxology")
