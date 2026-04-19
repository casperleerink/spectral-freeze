#include "PluginEditor.h"

SpectralFreezeEditor::SpectralFreezeEditor (SpectralFreezeProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    addAndMakeVisible (gainSlider);

    gainLabel.setText ("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    gainLabel.attachToComponent (&gainSlider, false);
    addAndMakeVisible (gainLabel);

    gainAttachment = std::make_unique<SliderAttachment> (
        processorRef.apvts, SpectralFreezeProcessor::gainParamID, gainSlider);

    filterSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    filterSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    addAndMakeVisible (filterSlider);

    filterLabel.setText ("Filter", juce::dontSendNotification);
    filterLabel.setJustificationType (juce::Justification::centred);
    filterLabel.attachToComponent (&filterSlider, false);
    addAndMakeVisible (filterLabel);

    filterAttachment = std::make_unique<SliderAttachment> (
        processorRef.apvts, SpectralFreezeProcessor::filterParamID, filterSlider);

    freezeButton.setClickingTogglesState (true);
    addAndMakeVisible (freezeButton);
    freezeAttachment = std::make_unique<ButtonAttachment> (
        processorRef.apvts, SpectralFreezeProcessor::freezeParamID, freezeButton);

    setSize (480, 320);
}

SpectralFreezeEditor::~SpectralFreezeEditor() = default;

void SpectralFreezeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (18, 18, 22));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    g.drawFittedText ("Spectral Freeze",
                      getLocalBounds().removeFromTop (60),
                      juce::Justification::centred, 1);
}

void SpectralFreezeEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (60); // title

    auto knobRow = area.removeFromTop (200);
    knobRow.removeFromTop (20); // label space for the rotary labels

    const int knobWidth = knobRow.getWidth() / 2;
    gainSlider.setBounds   (knobRow.removeFromLeft (knobWidth).withSizeKeepingCentre (140, 140));
    filterSlider.setBounds (knobRow.withSizeKeepingCentre (140, 140));

    freezeButton.setBounds (area.removeFromTop (40).withSizeKeepingCentre (140, 32));
}
