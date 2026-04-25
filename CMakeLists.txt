cmake_minimum_required(VERSION 3.22)

# Must be set BEFORE project() — otherwise Xcode bakes in the SDK's default
# (e.g. macOS 26) and the app refuses to launch on older systems with -10825.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(SpectralFreeze VERSION 0.2.0)

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
    Source/dsp/FreezeProcessor.cpp
    Source/dsp/OrganicProcessor.cpp
    Source/dsp/SidechainProcessor.cpp
    Source/dsp/SpectralFilter.cpp
    Source/dsp/StftProcessor.cpp
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

# Production builds (SPECTRAL_FREEZE_UI_DEV=OFF) embed the Vite bundle into the
# plugin binary via juce_add_binary_data so the VST3 is self-contained. Dev
# builds skip this and load the UI from the Vite dev server instead.
if (NOT SPECTRAL_FREEZE_UI_DEV)
    set(UI_DIR "${CMAKE_SOURCE_DIR}/ui")

    # The UI's package.json declares juce-framework-frontend as
    # `file:../build/_deps/juce-src/...`, which only exists after the JUCE
    # FetchContent above has populated it — so we must run bun install AFTER
    # FetchContent_MakeAvailable, not before.
    message(STATUS "Building Spectral Freeze UI for embedded resources")

    execute_process(
        COMMAND bun install --frozen-lockfile
        WORKING_DIRECTORY "${UI_DIR}"
        RESULT_VARIABLE _ui_install_rc)
    if (NOT _ui_install_rc EQUAL 0)
        message(FATAL_ERROR "ui/: bun install failed (exit ${_ui_install_rc})")
    endif()

    execute_process(
        COMMAND bun run build
        WORKING_DIRECTORY "${UI_DIR}"
        RESULT_VARIABLE _ui_build_rc)
    if (NOT _ui_build_rc EQUAL 0)
        message(FATAL_ERROR "ui/: bun run build failed (exit ${_ui_build_rc})")
    endif()

    file(GLOB_RECURSE UI_DIST_FILES CONFIGURE_DEPENDS "${UI_DIR}/dist/*")
    if (UI_DIST_FILES STREQUAL "")
        message(FATAL_ERROR "ui/dist is empty after vite build")
    endif()

    juce_add_binary_data(SpectralFreezeUIData SOURCES ${UI_DIST_FILES})
    target_link_libraries(SpectralFreeze PRIVATE SpectralFreezeUIData)
endif()
