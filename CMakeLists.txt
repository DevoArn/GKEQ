cmake_minimum_required(VERSION 3.20)

project(ModernEQ VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static MSVC runtime: the plugin does not depend on an installed VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# PFFFT (fast SIMD FFT, BSD-style licence) used for linear-phase convolution
# ---------------------------------------------------------------------------
add_library(pffft STATIC
    external/pffft/src/pffft.c
    external/pffft/src/pffft_common.c)
target_include_directories(pffft
    PUBLIC  external/pffft/include/pffft
    PRIVATE external/pffft/src)
target_compile_definitions(pffft
    PUBLIC  PFFFT_STATIC_DEFINE
    PRIVATE _USE_MATH_DEFINES)
if(MSVC)
    target_compile_options(pffft PRIVATE /O2 /fp:fast /W0)
endif()

# ---------------------------------------------------------------------------
# DSP core (shared by the plugin and the test runner)
# ---------------------------------------------------------------------------
set(MEQ_DSP_SOURCES
    Source/Params.cpp
    Source/dsp/FilterDesign.cpp
    Source/dsp/IIREngine.cpp
    Source/dsp/LinearPhaseEngine.cpp
    Source/dsp/Character.cpp
    Source/dsp/Oversampler.cpp)

option(MEQ_BUILD_PLUGIN "Build the plugin (disable to build only the DSP tests)" ON)

set(MEQ_UI_SOURCES
    Source/ui/Theme.cpp
    Source/ui/Spectrum.cpp
    Source/ui/EQDisplay.cpp
    Source/ui/BandPanel.cpp
    Source/ui/Bars.cpp
    Source/ui/EQMatch.cpp
    Source/ui/CurveFit.cpp)

if(MEQ_BUILD_PLUGIN)
juce_add_plugin(ModernEQ
    COMPANY_NAME "GK"
    COMPANY_WEBSITE ""
    PLUGIN_MANUFACTURER_CODE MdAu
    PLUGIN_CODE MdEq
    FORMATS VST3 Standalone
    PRODUCT_NAME "GKEQ"
    BUNDLE_ID "com.gk.gkeq"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx EQ
    ICON_BIG Resources/icon.png
    VERSION ${PROJECT_VERSION}
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ModernEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/InstanceRegistry.cpp
    Source/Presets.cpp
    ${MEQ_DSP_SOURCES}
    ${MEQ_UI_SOURCES})

target_include_directories(ModernEQ PRIVATE Source)

target_compile_definitions(ModernEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(ModernEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
        pffft
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(ModernEQ PRIVATE /fp:precise /bigobj)
endif()

# Host-side test: loads the built VST3 like a DAW and renders the editor.
juce_add_console_app(ModernEQHostTest PRODUCT_NAME "ModernEQHostTest")
target_sources(ModernEQHostTest PRIVATE Tests/HostTest.cpp)
target_compile_definitions(ModernEQHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(ModernEQHostTest
    PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags)

# In-process UI snapshot tool (renders the editor to PNG for inspection).
juce_add_console_app(ModernEQUISnapshot PRODUCT_NAME "ModernEQUISnapshot")
target_sources(ModernEQUISnapshot PRIVATE
    Tests/UISnapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/InstanceRegistry.cpp
    Source/Presets.cpp
    ${MEQ_DSP_SOURCES}
    ${MEQ_UI_SOURCES})
target_include_directories(ModernEQUISnapshot PRIVATE Source)
target_compile_definitions(ModernEQUISnapshot PRIVATE
    JucePlugin_Name="GKEQ" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(ModernEQUISnapshot
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_opengl pffft
    PUBLIC juce::juce_recommended_config_flags)
endif() # MEQ_BUILD_PLUGIN

# ---------------------------------------------------------------------------
# Numerical test runner for the DSP core
# ---------------------------------------------------------------------------
juce_add_console_app(ModernEQTests PRODUCT_NAME "ModernEQTests")
target_sources(ModernEQTests PRIVATE Tests/Tests.cpp ${MEQ_DSP_SOURCES})
target_include_directories(ModernEQTests PRIVATE Source)
target_compile_definitions(ModernEQTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(ModernEQTests
    PRIVATE juce::juce_audio_processors juce::juce_dsp pffft
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Windows installer (build.bat installer). Embeds build/installer/payload.zip.
# ---------------------------------------------------------------------------
option(MEQ_BUILD_INSTALLER "Build the Windows installer" OFF)
if(MEQ_BUILD_INSTALLER)
    set(MEQ_PAYLOAD "" CACHE FILEPATH "Installer payload (zip made by Tools/make_payload.py)")
    if(NOT EXISTS "${MEQ_PAYLOAD}")
        message(FATAL_ERROR "Installer payload not found: '${MEQ_PAYLOAD}'")
    endif()

    juce_add_binary_data(ModernEQPayload
        HEADER_NAME PayloadData.h
        NAMESPACE Payload
        SOURCES "${MEQ_PAYLOAD}")

    # ModernEQSetup: the real installer (asks for administrator rights).
    # ModernEQSetupTest: identical but without the UAC requirement, used by the
    # automated install/uninstall test into a temporary folder.
    foreach(setup_target ModernEQSetup ModernEQSetupTest)
        juce_add_gui_app(${setup_target}
            PRODUCT_NAME "GKEQ Setup"
            COMPANY_NAME "GK"
            VERSION ${PROJECT_VERSION}
            ICON_BIG Resources/icon.png)
        target_sources(${setup_target} PRIVATE Installer/SetupApp.cpp Installer/Win32.cpp)
        target_compile_definitions(${setup_target} PRIVATE
            MEQ_VERSION="${PROJECT_VERSION}"
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
        target_link_libraries(${setup_target}
            PRIVATE ModernEQPayload juce::juce_gui_basics shell32 ole32 uuid advapi32
            PUBLIC juce::juce_recommended_config_flags)
    endforeach()

    if(MSVC)
        target_link_options(ModernEQSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
    endif()
endif()
