cmake_minimum_required(VERSION 3.22)
project(Prophet6Control VERSION 0.2.2 LANGUAGES C CXX)

if(NOT DEFINED JUCE_DIR OR NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set JUCE_DIR to a JUCE source checkout. Protocol tests can be built separately from Tests/.")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(Prophet6Control
    COMPANY_NAME "Studio Tools"
    PRODUCT_NAME "Prophet 6 Control"
    BUNDLE_ID "audio.studiotools.prophet6control"
    PLUGIN_MANUFACTURER_CODE StTl
    PLUGIN_CODE P6Ct
    FORMATS AU VST3 Standalone
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_SYNTH FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(Prophet6Control)

target_sources(Prophet6Control PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ParameterMap.h
    Source/ParameterMap.cpp
    Source/ProtocolCore.h
    Source/ProtocolCore.cpp
)

target_compile_features(Prophet6Control PRIVATE cxx_std_17)
target_compile_definitions(Prophet6Control PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(Prophet6Control PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
)
