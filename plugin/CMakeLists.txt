set(VOX_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VOX_FORMATS AU)
endif()

juce_add_plugin(Voxology
    PRODUCT_NAME                "Voxology"
    COMPANY_NAME                "Voxology Audio"
    COMPANY_WEBSITE             "https://example.com"
    BUNDLE_ID                   com.voxologyaudio.voxology
    PLUGIN_MANUFACTURER_CODE    Voxa
    PLUGIN_CODE                 Vox1
    FORMATS                     ${VOX_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    NEEDS_WEB_BROWSER           TRUE
    NEEDS_WEBVIEW2              TRUE
    VST3_CATEGORIES             Fx Vocals
    AU_MAIN_TYPE                kAudioUnitType_Effect)

# The web UI, embedded in the binary. File names must be unique (they are served flat).
file(GLOB VOX_UI_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/fonts/*.ttf")

juce_add_binary_data(VoxologyUIData
    HEADER_NAME VoxologyUIData.h
    NAMESPACE   VoxologyUI
    SOURCES
        ${VOX_UI_FILES}
        "${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/index.js"
        "${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/check_native_interop.js")
set_target_properties(VoxologyUIData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Voxology PRIVATE
    PluginProcessor.cpp
    VoxWebEditor.cpp
    AutoEditController.cpp)

target_compile_definitions(Voxology PUBLIC
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(Voxology
    PRIVATE
        vox_dsp
        VoxologyUIData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
