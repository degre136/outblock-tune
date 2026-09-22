cmake_minimum_required(VERSION 3.22)
project(OutblockTune VERSION 0.1.0)

add_subdirectory(JUCE)

juce_add_plugin(OutblockTune
    COMPANY_NAME "Outblock"
    BUNDLE_ID "com.outblock.tune"
    PLUGIN_MANUFACTURER_CODE Otbk
    PLUGIN_CODE Obtu
    FORMATS VST3
    PRODUCT_NAME "OUTBLOCK TUNE"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(OutblockTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
)

target_compile_features(OutblockTune PRIVATE cxx_std_17)

target_compile_definitions(OutblockTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(OutblockTune PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)
