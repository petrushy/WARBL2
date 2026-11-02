cmake_minimum_required(VERSION 3.22)

project(WarblStream VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# Sensor stream test app: receives the WARBL2 sensor stream, plots it, measures timing,
# and has a click output for measuring end-to-end latency.
juce_add_gui_app(WarblStream
    PRODUCT_NAME "WARBL Stream"
    COMPANY_NAME "WARBL")

target_sources(WarblStream PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/StreamReceiver.cpp
    Source/AudioEngine.cpp
    Source/RoomReverb.cpp
    Source/TestPhrase.cpp
    Source/ImpulseResponse.cpp)

target_include_directories(WarblStream PRIVATE Source)

target_compile_definitions(WarblStream PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:WarblStream,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:WarblStream,JUCE_VERSION>")

target_link_libraries(WarblStream
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Protocol unit test (no JUCE).
enable_testing()
add_executable(StreamProtocolTest Tests/StreamProtocolTest.cpp)
target_include_directories(StreamProtocolTest PRIVATE Source)
add_test(NAME StreamProtocolTest COMMAND StreamProtocolTest)

# Virtual WARBL for trying the app or a synth without the instrument.
juce_add_console_app(WarblSimulator PRODUCT_NAME "WARBL Simulator")
target_sources(WarblSimulator PRIVATE Tools/WarblSimulator.cpp)
target_include_directories(WarblSimulator PRIVATE Source)
target_compile_definitions(WarblSimulator PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(WarblSimulator
    PRIVATE juce::juce_audio_devices juce::juce_events
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Measures the generated room reverbs (reverberation time, direct/reverberant ratio) and renders WAVs.
juce_add_console_app(ReverbCheck PRODUCT_NAME "Reverb Check")
target_sources(ReverbCheck PRIVATE Tools/ReverbCheck.cpp Source/ImpulseResponse.cpp Source/TestPhrase.cpp)
target_include_directories(ReverbCheck PRIVATE Source)
target_compile_definitions(ReverbCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ReverbCheck
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
