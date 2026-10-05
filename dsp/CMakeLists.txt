add_library(vox_dsp STATIC
    src/KWeighting.cpp
    src/TruePeakDetector.cpp
    src/LoudnessMeter.cpp
    src/SoftClipper.cpp
    src/Saturation.cpp
    src/Modules.cpp
    src/Space.cpp
    src/VocalChain.cpp
    src/AutoEdit.cpp
    src/PitchCorrector.cpp
    src/HoneyTune.cpp)

target_include_directories(vox_dsp PUBLIC include)
target_compile_features(vox_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(vox_dsp PRIVATE /W4 /fp:precise)
else()
    target_compile_options(vox_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

set_target_properties(vox_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Voxology")
