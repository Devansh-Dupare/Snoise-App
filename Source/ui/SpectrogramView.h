#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <memory>

// Card showing an STFT spectrogram (Magma colours, -90..0 dB). The image is computed on a background
// thread; setAudio returns immediately.
class SpectrogramView : public juce::Component
{
public:
    SpectrogramView (juce::String title, juce::String placeholder);
    ~SpectrogramView() override;

    void setAudio (const juce::AudioBuffer<float>& audio, double sampleRate);
    void clear();
    void setPlayheadSeconds (double seconds);

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> getPlotBounds() const;

    juce::String title, placeholder;
    juce::Image image;
    double durationSeconds = 0.0, sampleRate = 0.0, playheadSeconds = 0.0;
    bool computing = false;

    std::shared_ptr<std::atomic<int>> generation = std::make_shared<std::atomic<int>> (0);
    juce::ThreadPool pool { 1 };   // declared last so it is destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramView)
};
