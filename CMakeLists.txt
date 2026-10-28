cmake_minimum_required(VERSION 3.22)
project(SpectralNoiseCancellation VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4            # pin to the JUCE 8.x tag you install
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- Shared DSP + IO code (used by app and tests); grows phase by phase ----
set(CORE_SOURCES
    Source/dsp/StftConfig.cpp
    Source/dsp/FrameProcessor.cpp
    Source/dsp/AudioReconstructor.cpp
    Source/dsp/NoiseProfile.cpp
    Source/dsp/SpectralSubtractor.cpp
    Source/dsp/Spectrogram.cpp
    Source/dsp/NoiseReductionEngine.cpp
    Source/io/AudioFileManager.cpp)

# ---- Standalone GUI app (no plugin targets) ----
juce_add_gui_app(SpectralNoiseCancellation
    PRODUCT_NAME "Spectral Noise Cancellation"
    COMPANY_NAME "YourName")

target_sources(SpectralNoiseCancellation PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/audio/PlaybackController.cpp
    Source/ui/WaveformView.cpp
    Source/ui/NoiseProfileView.cpp
    Source/ui/SpectrogramView.cpp
    Source/ui/AppLookAndFeel.cpp
    Source/ui/NoiseReductionPanel.cpp
    Source/ui/ActionsPanel.cpp
    ${CORE_SOURCES})

target_include_directories(SpectralNoiseCancellation PRIVATE Source)
target_compile_features(SpectralNoiseCancellation PRIVATE cxx_std_17)
target_compile_definitions(SpectralNoiseCancellation PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SpectralNoiseCancellation,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SpectralNoiseCancellation,JUCE_VERSION>")
target_link_libraries(SpectralNoiseCancellation
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ---- Unit tests (console app) ----
juce_add_console_app(SNC_Tests PRODUCT_NAME "SNC Tests")
target_sources(SNC_Tests PRIVATE Tests/DspTests.cpp ${CORE_SOURCES})
target_include_directories(SNC_Tests PRIVATE Source)
target_compile_features(SNC_Tests PRIVATE cxx_std_17)
target_compile_definitions(SNC_Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SNC_Tests PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
