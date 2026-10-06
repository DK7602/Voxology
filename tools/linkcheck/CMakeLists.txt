# Two real Voxology processors linked through Unmask (see linkcheck.cpp). Local check, not built by default.
# The plug-in's own sources are compiled in (a plug-in target can't be linked into an app).
juce_add_console_app(vox_linkcheck PRODUCT_NAME "vox_linkcheck" NEEDS_WEB_BROWSER TRUE)
target_sources(vox_linkcheck PRIVATE
    linkcheck.cpp
    ${CMAKE_SOURCE_DIR}/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugin/AutoEditController.cpp
    ${CMAKE_SOURCE_DIR}/plugin/VoxWebEditor.cpp)
target_include_directories(vox_linkcheck PRIVATE ${CMAKE_SOURCE_DIR}/plugin)
target_compile_definitions(vox_linkcheck PRIVATE
    JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
    JucePlugin_Name="Voxology" JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(vox_linkcheck PRIVATE vox_dsp VoxologyUIData
    juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra)
