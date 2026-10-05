include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(vox_tests
    test_modules.cpp
    test_space.cpp
    test_chain.cpp
    test_autoedit.cpp
    test_pitch.cpp
    test_honey.cpp)

target_link_libraries(vox_tests PRIVATE vox_dsp Catch2::Catch2WithMain)
set_target_properties(vox_tests PROPERTIES FOLDER "Voxology")

include(Catch)
catch_discover_tests(vox_tests)
