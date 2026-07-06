# TURBO TUBES — tube saturation plugin
cmake_minimum_required(VERSION 3.22)

# Apple Silicon + Intel universal binaries by default (native everywhere).
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(TurboTubes VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(TT_BUILD_PLUGIN "Build the JUCE plugin targets" ON)
option(TT_BUILD_TESTS  "Build the offline DSP test suite" ON)
set(TT_AAX_SDK_PATH "" CACHE PATH "Path to the AAX SDK (enables the AAX format)")

# ------------------------------------------------------------------ DSP tests
if(TT_BUILD_TESTS)
    enable_testing()
    add_executable(turbo_dsp_tests tests/dsp_tests.cpp)
    if(MSVC)
        target_compile_options(turbo_dsp_tests PRIVATE /O2 /W3)
    else()
        target_compile_options(turbo_dsp_tests PRIVATE -O2 -Wall -Wextra)
    endif()
    add_test(NAME dsp COMMAND turbo_dsp_tests)
endif()

# ---------------------------------------------------------------- JUCE plugin
if(TT_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.8
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    # AU is macOS-only; everything else is cross-platform.
    set(TT_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND TT_FORMATS AU)
    endif()
    if(TT_AAX_SDK_PATH)
        juce_set_aax_sdk_path(${TT_AAX_SDK_PATH})
        list(APPEND TT_FORMATS AAX)
    endif()

    juce_add_plugin(TurboTubes
        COMPANY_NAME              "JTEK Audio"
        BUNDLE_ID                 com.jtekaudio.turbotubes
        PLUGIN_MANUFACTURER_CODE  Jtek
        PLUGIN_CODE               Ttbs
        FORMATS                   ${TT_FORMATS}
        PRODUCT_NAME              "Turbo Tubes"
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          FALSE
        NEEDS_MIDI_OUTPUT         FALSE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   FALSE
        VST3_CATEGORIES           "Fx Distortion"
        AU_MAIN_TYPE              "kAudioUnitType_Effect")

    target_sources(TurboTubes PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp
        plugin/ui/TurboLookAndFeel.cpp
        plugin/ui/Background.cpp
        plugin/ui/TubeBank.cpp
        plugin/ui/Meters.cpp)

    target_include_directories(TurboTubes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    target_compile_definitions(TurboTubes PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)

    target_link_libraries(TurboTubes
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
