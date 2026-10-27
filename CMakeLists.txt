# MIDI Forge - build configuration
#
# Targets:
#   midi_forge_engine        the JUCE-free MIDI engine (static library)
#   midi_forge_engine_tests  engine tests, no JUCE needed           (MIDI_FORGE_BUILD_ENGINE_TESTS)
#   MidiForge_VST3           the plug-in                             (MIDI_FORGE_BUILD_PLUGIN)
#   MidiForge_CLAP           the plug-in as CLAP, currently not built (MIDI_FORGE_BUILD_CLAP)
#   midi_forge_plugin_tests  tests of the JUCE layer and the editor  (MIDI_FORGE_BUILD_PLUGIN_TESTS)
#
# See CONTRIBUTING.md for the commands CI runs.

cmake_minimum_required(VERSION 3.28)

project(MidiForge VERSION 0.15.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clang-tidy and IDEs

option(MIDI_FORGE_BUILD_PLUGIN "Build the VST3 plug-in (downloads JUCE)" ON)
# CLAP is switched off for now (the owner only needs the VST3); turning this on brings it back.
option(MIDI_FORGE_BUILD_CLAP "Also build the CLAP plug-in (downloads clap-juce-extensions)" OFF)
option(MIDI_FORGE_BUILD_ENGINE_TESTS "Build the engine tests" ON)
option(MIDI_FORGE_BUILD_PLUGIN_TESTS "Build the plug-in layer tests (needs MIDI_FORGE_BUILD_PLUGIN)" OFF)
option(MIDI_FORGE_WARNINGS_AS_ERRORS "Treat compiler warnings in our own code as errors" OFF)
option(MIDI_FORGE_SANITIZERS "Build the engine and its tests with address and undefined-behaviour sanitizers" OFF)

# Universal binary on macOS unless the caller decides otherwise. CI builds Windows only for now; this stays so
# a Mac build can come back by adding a CI job.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Static MSVC runtime, so the plug-in does not depend on an installed VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#===============================================================================
# Shared compiler settings for our own code (not for JUCE or other dependencies)

add_library(midi_forge_warnings INTERFACE)
if(MSVC)
    target_compile_options(midi_forge_warnings INTERFACE /W4 $<$<BOOL:${MIDI_FORGE_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(midi_forge_warnings INTERFACE
        -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
        $<$<BOOL:${MIDI_FORGE_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_library(midi_forge_sanitizers INTERFACE)
if(MIDI_FORGE_SANITIZERS AND NOT MSVC)
    target_compile_options(midi_forge_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(midi_forge_sanitizers INTERFACE -fsanitize=address,undefined)
endif()

#===============================================================================
# Engine: plain C++17, no JUCE. Shared by the plug-in and the tests.

add_library(midi_forge_engine STATIC
    Source/Engine/MidiEvent.h
    Source/Engine/ModuleDescriptions.h
    Source/Engine/ModuleDescriptions.cpp
    Source/Engine/MidiModule.h
    Source/Engine/MidiModule.cpp
    Source/Engine/ModuleChain.h
    Source/Engine/ModuleChain.cpp
    Source/Engine/MidiForgeEngine.h
    Source/Engine/MidiForgeEngine.cpp
    Source/Engine/Modulation/Lfo.h
    Source/Engine/Modulation/Lfo.cpp
    Source/Engine/Modulation/ModulationRoutes.h
    Source/Engine/Modulation/NoteRandom.h
    Source/Engine/Modulation/NoteRandom.cpp
    Source/Engine/Modulation/StepModulator.h
    Source/Engine/Modulation/StepModulator.cpp
    Source/Engine/Modules/FilterModules.h
    Source/Engine/Modules/FilterModules.cpp
    Source/Engine/Modules/TransformModules.h
    Source/Engine/Modules/TransformModules.cpp
    Source/Engine/Modules/GeneratorModules.h
    Source/Engine/Modules/GeneratorModules.cpp
    Source/Engine/Modules/RandomizerModules.h
    Source/Engine/Modules/RandomizerModules.cpp
    Source/Plugin/FactoryPresets.h
    Source/Plugin/FactoryPresets.cpp)
target_include_directories(midi_forge_engine PUBLIC Source)
target_link_libraries(midi_forge_engine PRIVATE midi_forge_warnings PUBLIC midi_forge_sanitizers)
set_target_properties(midi_forge_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MIDI_FORGE_BUILD_ENGINE_TESTS)
    enable_testing()
    add_executable(midi_forge_engine_tests
        Tests/TestFramework.h
        Tests/Engine/ChainTestRig.h
        Tests/Engine/EngineTestMain.cpp
        Tests/Engine/ModuleDescriptionTests.cpp
        Tests/Engine/FilterAndTransformTests.cpp
        Tests/Engine/GeneratorAndRandomizerTests.cpp
        Tests/Engine/ModuleChainTests.cpp
        Tests/Engine/ModulationTests.cpp
        Tests/Engine/RepeaterTests.cpp
        Tests/Engine/NoteSplitTests.cpp
        Tests/Engine/NoteRandomAndPolarityTests.cpp
        Tests/Engine/PitchSlideTests.cpp
        Tests/Engine/NoteDispatcherTests.cpp
        Tests/Engine/StepModulatorTests.cpp
        Tests/Engine/MutateTests.cpp)
    target_link_libraries(midi_forge_engine_tests PRIVATE midi_forge_engine midi_forge_warnings)
    add_test(NAME engine COMMAND midi_forge_engine_tests)
endif()

#===============================================================================
# Plug-in. Dependencies are pinned so every build is reproducible.

if(MIDI_FORGE_BUILD_PLUGIN)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)

    # JUCE 9.0.4 (the tag points at commit 3fb5cbc0340758596dac39f7c8de47d562b4896e)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable(JUCE)

    if(MIDI_FORGE_BUILD_CLAP)
        # CLAP support for JUCE plug-ins, pinned to a commit
        FetchContent_Declare(clap_juce_extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
            GIT_SUBMODULES_RECURSE TRUE
            EXCLUDE_FROM_ALL)
        FetchContent_MakeAvailable(clap_juce_extensions)
    endif()

    set(MIDI_FORGE_PLUGIN_SOURCES
        Source/Plugin/ParameterIds.h
        Source/Plugin/PluginProcessor.h
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/UserPresetLibrary.h
        Source/Plugin/UserPresetLibrary.cpp
        Source/Plugin/RecentModuleList.h
        Source/Plugin/RecentModuleList.cpp
        Source/UI/Theme.h
        Source/UI/ForgeLookAndFeel.h
        Source/UI/ForgeLookAndFeel.cpp
        Source/UI/SlotRow.h
        Source/UI/SlotRow.cpp
        Source/UI/ModuleBrowser.h
        Source/UI/ModuleBrowser.cpp
        Source/UI/ModulationPage.h
        Source/UI/ModulationPage.cpp
        Source/UI/StepModulatorPanel.h
        Source/UI/StepModulatorPanel.cpp
        Source/UI/PresetBar.h
        Source/UI/PresetBar.cpp
        Source/UI/PluginEditor.h
        Source/UI/PluginEditor.cpp)

    set(MIDI_FORGE_JUCE_DEFINITIONS
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    juce_add_plugin(MidiForge
        PRODUCT_NAME "MIDI Forge"
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "ugerhard"
        BUNDLE_ID com.ugerhard.midiforge
        PLUGIN_MANUFACTURER_CODE Ugrh
        PLUGIN_CODE MFrg
        FORMATS VST3
        IS_SYNTH FALSE
        IS_MIDI_EFFECT TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(MidiForge PRIVATE ${MIDI_FORGE_PLUGIN_SOURCES})
    target_compile_definitions(MidiForge PUBLIC ${MIDI_FORGE_JUCE_DEFINITIONS})
    target_link_libraries(MidiForge
        PRIVATE
            midi_forge_engine
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(MIDI_FORGE_BUILD_CLAP)
        clap_juce_extensions_plugin(TARGET MidiForge
            CLAP_ID "com.ugerhard.midiforge"
            CLAP_FEATURES note-effect utility)
    endif()

    if(MIDI_FORGE_BUILD_PLUGIN_TESTS)
        enable_testing()
        juce_add_console_app(midi_forge_plugin_tests PRODUCT_NAME "MIDI Forge Plugin Tests")
        target_sources(midi_forge_plugin_tests PRIVATE ${MIDI_FORGE_PLUGIN_SOURCES} Tests/Plugin/PluginTests.cpp)
        target_include_directories(midi_forge_plugin_tests PRIVATE Source)
        target_compile_definitions(midi_forge_plugin_tests PRIVATE
            ${MIDI_FORGE_JUCE_DEFINITIONS}
            JucePlugin_Name="MIDI Forge"
            JucePlugin_VersionString="${PROJECT_VERSION}")
        target_link_libraries(midi_forge_plugin_tests PRIVATE
            midi_forge_engine
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
        add_test(NAME plugin COMMAND midi_forge_plugin_tests)
    endif()
endif()
