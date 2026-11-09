cmake_minimum_required(VERSION 3.22)
project(Zampler VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ZAMPLER_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(ZAMPLER_BUILD_CLAP "Also build a CLAP plugin (clap-juce-extensions)" ON)
set(ZAMPLER_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading")
set(ZAMPLER_CLAP_JUCE_DIR "" CACHE PATH "Use a local clap-juce-extensions checkout instead of downloading")

# ---------------------------------------------------------------- engine (no JUCE)
add_library(zampler_engine STATIC
    engine/Theory.cpp
    engine/Dsp.cpp
    engine/Analysis.cpp
    engine/Poly.cpp
    engine/Drums.cpp
    engine/Tracker.cpp)
target_include_directories(zampler_engine PUBLIC engine)
set_target_properties(zampler_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(zampler_engine PUBLIC _USE_MATH_DEFINES)
endif()

enable_testing()
add_executable(zampler_engine_tests tests/EngineTests.cpp)
target_link_libraries(zampler_engine_tests PRIVATE zampler_engine)
add_test(NAME engine COMMAND zampler_engine_tests)

if(NOT ZAMPLER_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------- JUCE plugin
include(FetchContent)
if(ZAMPLER_JUCE_DIR)
    add_subdirectory(${ZAMPLER_JUCE_DIR} juce)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

set(ZAMPLER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ZAMPLER_FORMATS AU)
endif()

juce_add_plugin(Zampler
    COMPANY_NAME "Zampler"
    BUNDLE_ID "com.zampler.zampler"
    PLUGIN_MANUFACTURER_CODE Zmpl
    PLUGIN_CODE Zmp1
    FORMATS ${ZAMPLER_FORMATS}
    PRODUCT_NAME "Zampler"
    DESCRIPTION "Drop a sample, get tracker notes, build key-locked parts"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Tools
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_binary_data(ZamplerData SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../renoise_tool/com.zampler.Import.xrnt/manifest.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/../renoise_tool/com.zampler.Import.xrnt/main.lua)
set_target_properties(ZamplerData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Zampler PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)

target_compile_definitions(Zampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Zampler
    PRIVATE
        zampler_engine
        ZamplerData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- CLAP
if(ZAMPLER_BUILD_CLAP)
    if(ZAMPLER_CLAP_JUCE_DIR)
        add_subdirectory(${ZAMPLER_CLAP_JUCE_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(clap_juce_extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8
            GIT_SUBMODULES_RECURSE TRUE)
        FetchContent_GetProperties(clap_juce_extensions)
        if(NOT clap_juce_extensions_POPULATED)
            FetchContent_Populate(clap_juce_extensions)
            add_subdirectory(${clap_juce_extensions_SOURCE_DIR} ${clap_juce_extensions_BINARY_DIR} EXCLUDE_FROM_ALL)
        endif()
    endif()
    clap_juce_extensions_plugin(TARGET Zampler
        CLAP_ID "com.zampler.zampler"
        CLAP_FEATURES instrument note-effect utility)
endif()

# ---------------------------------------------------------------- host test
# Loads the built VST3 like a DAW, plays it with a fake transport and checks
# the MIDI it sends; can also render the editor to a PNG.
juce_add_console_app(zampler_host_test PRODUCT_NAME "zampler_host_test")
target_sources(zampler_host_test PRIVATE
    tests/HostTest.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)
target_compile_definitions(zampler_host_test PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_include_directories(zampler_host_test PRIVATE engine source)
target_link_libraries(zampler_host_test PRIVATE
    zampler_engine
    ZamplerData
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)
add_dependencies(zampler_host_test Zampler_VST3)
