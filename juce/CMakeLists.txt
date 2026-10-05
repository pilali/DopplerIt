cmake_minimum_required(VERSION 3.22)

# DopplerIt - JUCE version (VST3 on Windows / Linux / macOS, AU on macOS)
#
#   cmake -S juce -B build-juce -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-juce --config Release
#
# macOS universal binary (Intel + Apple Silicon):
#   cmake -S juce -B build-juce -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
#
# JUCE is downloaded automatically, or use -DJUCE_DIR=/path/to/JUCE.

# Lowest macOS supported by JUCE 8 (the arm64 slice of a universal binary
# always starts at macOS 11.0, the first release for Apple Silicon).
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11" CACHE STRING "Minimum macOS version")

# Windows: static MSVC runtime, no Visual C++ redistributable needed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(DopplerIt VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "" CACHE PATH "Local JUCE checkout (empty = download)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(DOPPLERIT_FORMATS VST3)
if(APPLE)
    list(APPEND DOPPLERIT_FORMATS AU)
endif()
option(DOPPLERIT_STANDALONE "Also build a standalone application" OFF)
if(DOPPLERIT_STANDALONE)
    list(APPEND DOPPLERIT_FORMATS Standalone)
endif()

juce_add_plugin(DopplerIt
    COMPANY_NAME                "pilali"
    COMPANY_WEBSITE             "https://github.com/pilali/dopplerit"
    BUNDLE_ID                   "com.pilali.dopplerit"
    PLUGIN_MANUFACTURER_CODE    Pila
    PLUGIN_CODE                 Dplr
    PRODUCT_NAME                "DopplerIt"
    DESCRIPTION                 "Doppler effect: approach, recede or pass-by"
    FORMATS                     ${DOPPLERIT_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Spatial
    AU_MAIN_TYPE                kAudioUnitType_Effect)

juce_generate_juce_header(DopplerIt)

juce_add_binary_data(DopplerItAssets SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../dopplerit.lv2/modgui/knob-dopplerit.png
    ${CMAKE_CURRENT_SOURCE_DIR}/../dopplerit.lv2/modgui/footswitch-dopplerit.png
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/header.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/fonts/CooperHewitt-Book.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/fonts/CooperHewitt-Semibold.ttf)

target_sources(DopplerIt PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(DopplerIt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_compile_definitions(DopplerIt PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DopplerIt
    PRIVATE
        DopplerItAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
