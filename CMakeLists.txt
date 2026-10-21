cmake_minimum_required(VERSION 3.25)

# This tells cmake we have goodies in the /cmake folder
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include (PamplejuceVersion)

# Modern concise way to add dependencies to your project
include (CPM)

# Configures universal binaries and decides which version of macOS to support
include(PamplejuceMacOS)

# Couple tweaks that IMO should be JUCE defaults
include(JUCEDefaults)

# This is the internal name of the project and the name of JUCE's shared code target
# Note: This cannot have spaces (it may be 2024, but you can't have it all!)
# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs display)
set(PROJECT_NAME "Lumiere")

# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs will display)
# You can also just have it be the same thing as PROJECT_NAME
# You may want to append the major version on the end of this (and PROJECT_NAME) ala:
#   set(PROJECT_NAME "MyPlugin_v${MAJOR_VERSION}")
# Doing so enables major versions to show up in IDEs and DAWs as separate plugins
# allowing you to change parameters and behavior without breaking existing user projects
set(PRODUCT_NAME "Lumiere Test v0.1")

# Used for the macOS bundle name and installers
set(COMPANY_NAME "Beyond Analog")

# Used for the macOS bundle identifier (and signing)
set(BUNDLE_ID "com.beyondanalog.lumiere")

# Preserve these IDs so hosts continue to recognize the existing plugin
set(PLUGIN_MANUFACTURER_CODE BADA)
set(PLUGIN_CODE NOR1)

# Change me! Set the plugin formats you want built
# Valid choices: AAX Unity VST VST3 AU AUv3 Standalone
set(FORMATS Standalone AU VST3 AUv3)

# Force JUCE to work with sccache
# instruct MSVC to embed debug info (/Z7) instead of emitting a .pdb
set(CMAKE_POLICY_DEFAULT_CMP0141        NEW      CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

# For simplicity, the name of the CMake project is also the name of the target
project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

# Log build environment info (after project() so compiler info is available)
include(PamplejuceLog)

# Defines the pamplejuce_* functions we call below (also fetches Catch2 at include time)
include(PamplejuceFunctions)

# JUCE is setup as a submodule in the /JUCE folder
# Locally, you must run `git submodule update --init --recursive` once
# and later `git submodule update --remote --merge` to keep it up to date
# On Github Actions, this is done as a part of actions/checkout
add_subdirectory(JUCE)

# Add CLAP format
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Add any other modules you want modules here, before the juce_add_plugin call
# juce_add_module(modules/my_module)

# This adds the melatonin inspector module
add_subdirectory (modules/melatonin_inspector)

# See `docs/CMake API.md` in the JUCE repo for all config options
juce_add_plugin("${PROJECT_NAME}"
    # Icons for the standalone app
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"

    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${BUNDLE_ID}"
    IS_SYNTH FALSE

    # JUCE's unconditional copy step lets Debug overwrite the DAW's Release copy.
    # Deployment is gated to Release below.
    COPY_PLUGIN_AFTER_BUILD FALSE

    # A four-character plugin id
    # First character MUST be uppercase for AU formats
    PLUGIN_MANUFACTURER_CODE "${PLUGIN_MANUFACTURER_CODE}"

    # A unique four-character plugin id
    # Note: this must have at least one upper-case character
    PLUGIN_CODE "${PLUGIN_CODE}"
    FORMATS "${FORMATS}"

    # The name of your final executable
    # This is how it's listed in the DAW
    # This can be different from PROJECT_NAME and can have spaces!
    # You might want to use v${MAJOR_VERSION} here once you go to v2...
    PRODUCT_NAME "${PRODUCT_NAME}")

# Keep development builds out of the live plugin folders. Stage and verify
# Release bundles before replacing installed copies, retaining recoverable backups.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    foreach(plugin_target IN ITEMS ${PROJECT_NAME}_VST3 ${PROJECT_NAME}_AU)
        if(TARGET ${plugin_target})
            get_target_property(plugin_source ${plugin_target} JUCE_PLUGIN_ARTEFACT_FILE)
            get_target_property(plugin_destination ${plugin_target} JUCE_PLUGIN_COPY_DIR)
            add_custom_command(TARGET ${plugin_target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    "-Dconfig=$<CONFIG>"
                    "-Dsrc=$<GENEX_EVAL:${plugin_source}>"
                    "-Ddest=$<GENEX_EVAL:${plugin_destination}>"
                    "-Dbackup_root=${CMAKE_BINARY_DIR}/plugin-backups"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyReleasePlugin.cmake"
                VERBATIM)
        endif()
    endforeach()
endif()

# Keep Xcode's AUv3 framework metadata consistent and copy signed bundles intact.
if(CMAKE_GENERATOR STREQUAL "Xcode" AND TARGET ${PROJECT_NAME}_AUv3_Framework)
    set_target_properties(${PROJECT_NAME}_AUv3_Framework PROPERTIES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${BUNDLE_ID}.internal")
    set_target_properties(${PROJECT_NAME}_Standalone PROPERTIES
        XCODE_ATTRIBUTE_COPY_PHASE_STRIP NO)
endif()

# This lets us use our code in both the JUCE targets and our Test target
# Without running into ODR violations
add_library(SharedCode INTERFACE)

clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
    CLAP_ID "${BUNDLE_ID}"
    CLAP_FEATURES audio-effect)

# Enable fast math, C++23 and a few other target defaults
pamplejuce_shared_code_defaults()


# Keep the plugin, editor, meter, and DSP sources explicit for Xcode.
set(SourceFiles
    source/PeakAccumulator.h
    source/MeterBallistics.h
    source/StereoLevelMeter.h
    source/StereoLevelMeter.cpp
    source/BaParameters.h
    source/BaParameters.cpp
    source/GainLink.h
    source/GainLinkController.h
    source/GainLinkController.cpp
    source/CompressorParameters.h
    source/TransformerVoicing.h
    source/TransformerMeasurement.h
    source/TransformerMeasurement.cpp
    source/TransformerMeasurementView.h
    source/TransformerMeasurementView.cpp
    source/TransformerVoicingState.h
    source/TransformerVoicingState.cpp
    source/TransformerVoicingPanel.h
    source/TransformerVoicingPanel.cpp
    source/GainReductionMeter.h
    source/GainReductionMeter.cpp
    source/PluginEditor.h
    source/PluginEditor.cpp
    source/PluginProcessor.h
    source/PluginProcessor.cpp
    source/dsp/DbGainEnvelope.h
    source/dsp/TransformerSettings.h
    source/dsp/MagneticOversampling.h
    source/experimental/MagneticCoreSettings.h
    source/experimental/PlayPreisachCore.h
    source/experimental/PlayPreisachCore.cpp
    source/experimental/ConstitutiveLookup.h
    source/experimental/ConstitutiveLookup.cpp
    source/experimental/TransformerAudioSettings.h
    source/experimental/TransformerAudioCircuit.h
    source/experimental/TransformerAudioCircuit.cpp
    source/dsp/TransformerSaturation.h
    source/dsp/TransformerSaturation.cpp
    source/dsp/Biquad.h
    source/dsp/LowFrequencyMono.h
    source/dsp/LowFrequencyMono.cpp
    source/dsp/BaSignalPath.h
    source/dsp/BaSignalPath.cpp
    source/dsp/SidechainFilterSettings.h
    source/dsp/SidechainFilter.h
    source/dsp/SidechainFilter.cpp
    source/dsp/SoftKneeGainComputer.h
    source/dsp/StereoCompressor.h
    source/dsp/StereoCompressor.cpp)
target_sources(SharedCode INTERFACE ${SourceFiles})

# Adds a BinaryData target for embedding assets into the binary
pamplejuce_add_assets()

# MacOS only: Cleans up folder and target organization on Xcode.
pamplejuce_xcode_prettify()

# This is where you can set preprocessor definitions for JUCE and your plugin
target_compile_definitions(SharedCode
    INTERFACE

    # JUCE_WEB_BROWSER and JUCE_USE_CURL off by default
    JUCE_WEB_BROWSER=0  # If you set this to 1, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
    JUCE_USE_CURL=0     # If you set this to 1, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0

    # Uncomment if you are paying for a an Indie/Pro license or releasing under GPLv3
    # JUCE_DISPLAY_SPLASH_SCREEN=0

    # lets the app known if we're Debug or Release
    CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    VERSION="${CURRENT_VERSION}"

    # JucePlugin_Name is for some reason doesn't use the nicer PRODUCT_NAME
    PRODUCT_NAME_WITHOUT_VERSION="Lumiere Test"
)

# Link to any other modules you added (with juce_add_module) here!
# Usually JUCE modules must have PRIVATE visibility
# See https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md#juce_add_module
# However, with Pamplejuce, you'll link modules to SharedCode with INTERFACE visibility
# This allows the JUCE plugin targets and the Tests target to link against it
target_link_libraries(SharedCode
    INTERFACE
    Assets
    melatonin_inspector
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Link the JUCE plugin targets our SharedCode target
target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# JUCE's disabled ARA/LV2 support leaves empty objects in the shared-code archive.
if(APPLE)
    set_property(TARGET "${PROJECT_NAME}" APPEND PROPERTY
        STATIC_LIBRARY_OPTIONS -no_warning_for_no_symbols)
endif()

# Intel IPP is disabled for local development
# include(PamplejuceIPP)

# Everything related to the tests target
pamplejuce_add_tests()

# The magnetic circuit is shared by plugin, Standalone and offline measurements.

# A separate target for Benchmarks (keeps the Tests target fast)
pamplejuce_add_benchmarks()

# Output some config for CI (like our PRODUCT_NAME)
include(GitHubENV)
