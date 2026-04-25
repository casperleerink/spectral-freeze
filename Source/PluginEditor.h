#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class SpectralFreezeEditor : public juce::AudioProcessorEditor
{
public:
    explicit SpectralFreezeEditor (SpectralFreezeProcessor&);
    ~SpectralFreezeEditor() override;

    void resized() override;

private:
    SpectralFreezeProcessor& processorRef;

    // Relays must be declared BEFORE the attachments that reference them and
    // BEFORE the WebBrowserComponent whose Options pull them in via
    // withOptionsFrom(). Members destruct in reverse declaration order, so
    // the webview and attachments die first — safely.
    juce::WebSliderRelay       filterRelay    { "filter" };
    juce::WebToggleButtonRelay freezeRelay    { "freeze" };
    juce::WebSliderRelay       scBoostRelay      { "scBoost" };
    juce::WebSliderRelay       scFreqSmoothRelay { "scFreqSmoothing" };
    juce::WebSliderRelay       organicRelay      { "organic" };

    juce::WebSliderParameterAttachment filterAttachment {
        *processorRef.apvts.getParameter (SpectralFreezeProcessor::filterParamID),
        filterRelay };
    juce::WebToggleButtonParameterAttachment freezeAttachment {
        *processorRef.apvts.getParameter (SpectralFreezeProcessor::freezeParamID),
        freezeRelay };
    juce::WebSliderParameterAttachment scBoostAttachment {
        *processorRef.apvts.getParameter (SpectralFreezeProcessor::scBoostParamID),
        scBoostRelay };
    juce::WebSliderParameterAttachment scFreqSmoothAttachment {
        *processorRef.apvts.getParameter (SpectralFreezeProcessor::scFreqSmoothParamID),
        scFreqSmoothRelay };
    juce::WebSliderParameterAttachment organicAttachment {
        *processorRef.apvts.getParameter (SpectralFreezeProcessor::organicParamID),
        organicRelay };

    juce::WebBrowserComponent webView { buildOptions() };

    juce::WebBrowserComponent::Options buildOptions();

    // Resource provider: reads files from ui/dist on disk. Used in the
    // non-dev build; the dev build points at the Vite dev server directly.
    std::optional<juce::WebBrowserComponent::Resource>
        provideResource (const juce::String& urlPath);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFreezeEditor)
};
