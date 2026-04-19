#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SpectralFreezeEditor : public juce::AudioProcessorEditor
{
public:
    explicit SpectralFreezeEditor (SpectralFreezeProcessor&);
    ~SpectralFreezeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    SpectralFreezeProcessor& processorRef;

    juce::Slider gainSlider;
    juce::Label  gainLabel;
    std::unique_ptr<SliderAttachment> gainAttachment;

    juce::Slider filterSlider;
    juce::Label  filterLabel;
    std::unique_ptr<SliderAttachment> filterAttachment;

    juce::TextButton freezeButton { "Freeze" };
    std::unique_ptr<ButtonAttachment> freezeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFreezeEditor)
};
