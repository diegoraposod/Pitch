cmake_minimum_required(VERSION 3.22)
# Mac: universal binary (Apple Silicon + Intel), macOS 11+. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(CMAKE_HOST_APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
project(PITCH VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local copy if present (./JUCE), otherwise download it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
  add_subdirectory(JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
  set(PITCH_FORMATS AU VST3)
else()
  set(PITCH_FORMATS VST3)
endif()

juce_add_plugin(PITCH
  COMPANY_NAME "Diego"
  BUNDLE_ID "com.diego.pitch"
  PLUGIN_MANUFACTURER_CODE Dieg
  PLUGIN_CODE Ptch
  FORMATS ${PITCH_FORMATS}
  PRODUCT_NAME "PITCH"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Reverb Spatial
  AU_MAIN_TYPE kAudioUnitType_Effect
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "PITCH listens to the audio that reaches it.")

target_sources(PITCH PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_include_directories(PITCH PRIVATE Source)

target_compile_definitions(PITCH PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

if(NOT APPLE)
  # Linux build (used only to verify the code): no curl, no optional X11 extensions
  target_compile_definitions(PITCH PUBLIC JUCE_USE_CURL=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

target_link_libraries(PITCH
  PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Offline test: renders the processor to a WAV and snapshots the editor to a PNG (no DAW needed).
option(PITCH_BUILD_TESTS "Build the offline render test" OFF)
if(PITCH_BUILD_TESTS)
  juce_add_console_app(PitchRenderTest PRODUCT_NAME "PitchRenderTest")
  target_sources(PitchRenderTest PRIVATE tests/RenderTest.cpp)
  target_include_directories(PitchRenderTest PRIVATE Source)
  target_compile_definitions(PitchRenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
  target_link_libraries(PitchRenderTest PRIVATE PITCH juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics)
endif()
