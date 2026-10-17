# Real Voxology processors in a console app. Local checks, not built by default.
# The plug-in's own sources are compiled in (a plug-in target can't be linked into an app).
function(vox_processor_check name source)
    juce_add_console_app(${name} PRODUCT_NAME "${name}" NEEDS_WEB_BROWSER TRUE)
    target_sources(${name} PRIVATE
        ${source}
        ${CMAKE_SOURCE_DIR}/plugin/PluginProcessor.cpp
        ${CMAKE_SOURCE_DIR}/plugin/AutoEditController.cpp
        ${CMAKE_SOURCE_DIR}/plugin/ChainLibrary.cpp
        ${CMAKE_SOURCE_DIR}/plugin/VoxWebEditor.cpp)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/plugin)
    target_compile_definitions(${name} PRIVATE
        JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Voxology" JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_VersionCode=${VOX_VERSION_CODE} JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(${name} PRIVATE vox_dsp VoxologyUIData
        juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra)
endfunction()
math(EXPR VOX_VERSION_CODE "(${PROJECT_VERSION_MAJOR} << 16) + (${PROJECT_VERSION_MINOR} << 8) + ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT HEXADECIMAL)

vox_processor_check(vox_linkcheck linkcheck.cpp)     # two processors linked through Unmask
vox_processor_check(vox_sidecheck sidecheck.cpp)     # the side-chain key
vox_processor_check(vox_hostcheck hostcheck.cpp)     # the DAW's bypass, NaN input, Record mode switch, MIDI in BEAT

# The side-chain through the real VST3 wrapper (see vst3sidecheck.cpp).
juce_add_console_app(vox_vst3sidecheck PRODUCT_NAME "vox_vst3sidecheck")
target_sources(vox_vst3sidecheck PRIVATE vst3sidecheck.cpp)
target_compile_definitions(vox_vst3sidecheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(vox_vst3sidecheck PRIVATE juce::juce_audio_processors juce::juce_audio_utils)
