if(APPLE)
    set(PHU_PLUGIN_FORMATS VST3 AU)
else()
    set(PHU_PLUGIN_FORMATS VST3)
endif()

juce_add_plugin(phu-fair-kid-67
    PLUGIN_MANUFACTURER_CODE Phub
    PLUGIN_CODE Pfk6
    FORMATS ${PHU_PLUGIN_FORMATS}
    PRODUCT_NAME "PHU FAIR KID 67"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

target_sources(phu-fair-kid-67 PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
    DSP/UnitScaling.h
)

target_compile_definitions(phu-fair-kid-67 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CAMERA=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_FLAC=0
)

# Prevent Windows.h from defining min/max macros
if(WIN32)
    target_compile_definitions(phu-fair-kid-67 PUBLIC NOMINMAX)
endif()

target_link_libraries(phu-fair-kid-67
    PRIVATE
        PhuFairKidLib
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
