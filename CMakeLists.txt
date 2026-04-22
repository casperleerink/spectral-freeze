cmake_minimum_required(VERSION 3.22)

# Must be set BEFORE project() — otherwise Xcode bakes in the SDK's default
# (e.g. macOS 26) and the app refuses to launch on older systems with -10825.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(SpectralFreeze VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit compile_commands.json so clangd (Zed / VS Code / Neovim) resolves JUCE headers.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pull JUCE directly from GitHub so there's nothing to install manually.
# First configure will take a few minutes while it downloads ~100MB of JUCE.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SpectralFreeze
    COMPANY_NAME            "Learning"
    BUNDLE_ID               "com.learning.spectralfreeze"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Lrng
    PLUGIN_CODE             Spfr
    FORMATS                 AU VST3 Standalone
    PRODUCT_NAME            "Spectral Freeze"
)

target_sources(SpectralFreeze PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# Dev mode loads the UI from the Vite dev server (http://localhost:5173) so
# front-end edits hot-reload inside the plugin window. Turn this OFF for a
# build that serves the UI from ui/dist/ on disk via the resource provider.
option(SPECTRAL_FREEZE_UI_DEV "Load UI from the Vite dev server" ON)

target_compile_definitions(SpectralFreeze PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    SPECTRAL_FREEZE_UI_DEV=$<IF:$<BOOL:${SPECTRAL_FREEZE_UI_DEV}>,1,0>
    SPECTRAL_FREEZE_UI_DEV_URL="http://localhost:5173/"
    SPECTRAL_FREEZE_UI_DIST_DIR="${CMAKE_SOURCE_DIR}/ui/dist"
)

target_link_libraries(SpectralFreeze
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
