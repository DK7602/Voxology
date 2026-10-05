set(HONEY_FORMATS VST3)
if(APPLE)
    list(APPEND HONEY_FORMATS AU)
endif()

# Honey Tune: the note editor. ARA extension on a clip (Cubase, Studio One, Logic, Reaper, Cakewalk...).
juce_add_plugin(HoneyTune
    PRODUCT_NAME                "Honey Tune"
    COMPANY_NAME                "Voxology Audio"
    BUNDLE_ID                   com.voxologyaudio.honeytune
    PLUGIN_MANUFACTURER_CODE    Voxa
    PLUGIN_CODE                 Hny1
    FORMATS                     ${HONEY_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    IS_ARA_EFFECT               TRUE
    VST3_CATEGORIES             Fx "Pitch Shift"
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(HoneyTune PRIVATE HoneyTunePlugin.cpp HoneyRoll.cpp HoneyPanel.cpp)

target_compile_definitions(HoneyTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HoneyTune
    PRIVATE
        vox_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Developer tool (off by default): draws the editor with a real vocal into a PNG, no host needed.
option(VOX_HONEY_PREVIEW "Build the Honey Tune editor preview tool" OFF)
if(VOX_HONEY_PREVIEW)
    juce_add_console_app(HoneyPreview PRODUCT_NAME "HoneyPreview")
    target_sources(HoneyPreview PRIVATE preview/HoneyPreview.cpp HoneyRoll.cpp HoneyPanel.cpp)
    target_compile_definitions(HoneyPreview PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(HoneyPreview PRIVATE vox_dsp juce::juce_gui_basics PUBLIC juce::juce_recommended_config_flags)
endif()
