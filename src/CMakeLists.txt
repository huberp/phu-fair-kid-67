# =============================================================================
# Top-Level Guard
# =============================================================================
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
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
    DSP/Models/Fairchild/Fairchild670Core.h
    DSP/Models/Fairchild/VariableMuPushPullStage.h
    DSP/Models/Transformer/TransformerSecondOrder.h
    DSP/Models/Sidechain/TimingNetwork.h
    DSP/Models/Sidechain/TimingNetworkAdapter.h
    DSP/Utils/OversamplingChain.cpp
    DSP/Utils/OversamplingChain.h
)

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

# Install rules for the plugin
install(TARGETS phu-fair-kid-67
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)
