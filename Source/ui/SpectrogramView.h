#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include "ViewState.h"
#include <memory>

// Card showing an STFT spectrogram (Magma colours, -90..0 dB). The image is computed on a background
// thread; setAudio returns immediately.
class SpectrogramView : public juce::Component, private juce::ChangeListener
{
public:
    SpectrogramView (ViewState& viewState, juce::String title, juce::String placeholder);
    ~SpectrogramView() override;

    void setAudio (const juce::AudioBuffer<float>& audio, double sampleRate);
    void clear();
    void setPlayheadSeconds (double seconds);

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    juce::Rectangle<int> getPlotBounds() const;

    ViewState& viewState;
    int frameStride = 1;   // STFT frames skipped per image column (long files are decimated)

    juce::String title, placeholder;
    juce::Image image;
    double durationSeconds = 0.0, sampleRate = 0.0, playheadSeconds = 0.0;
    bool computing = false;

    std::shared_ptr<std::atomic<int>> generation = std::make_shared<std::atomic<int>> (0);
    juce::ThreadPool pool { 1 };   // declared last so it is destroyed (and joined) first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramView)
};
