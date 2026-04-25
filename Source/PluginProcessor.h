#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/FreezeProcessor.h"
#include "dsp/SpectralConstants.h"
#include "dsp/StftProcessor.h"
#include <array>
#include <vector>

class SpectralFreezeProcessor : public juce::AudioProcessor
{
public:
    SpectralFreezeProcessor();
    ~SpectralFreezeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spectral Freeze"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Exposed so the editor can attach its slider.
    juce::AudioProcessorValueTreeState apvts;

    static constexpr const char* freezeParamID   = "freeze";
    static constexpr const char* filterParamID   = "filter";
    static constexpr const char* scBoostParamID      = "scBoost";
    static constexpr const char* scFreqSmoothParamID = "scFreqSmoothing";
    static constexpr const char* organicParamID      = "organic";

    static constexpr int fftOrder = spectral::fftOrder;
    static constexpr int fftSize  = spectral::fftSize;
    static constexpr int hopSize  = spectral::hopSize;
    static constexpr int numBins  = spectral::numBins;
    static constexpr int magHistorySize = spectral::magHistorySize;
    static constexpr float freezePhaseJitterRadians = spectral::freezePhaseJitterRadians;
    static constexpr int organicAmBands = spectral::organicAmBands;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* freezeParam   { nullptr };
    std::atomic<float>* filterParam   { nullptr };
    std::atomic<float>* scBoostParam      { nullptr };
    std::atomic<float>* scFreqSmoothParam { nullptr };
    std::atomic<float>* organicParam      { nullptr };

    // Per-bin natural phase advance across one hop: 2π·k·hopSize/fftSize.
    // Frozen bins use this coherent phase motion plus a tiny random walk.
    std::array<float, numBins> phaseAdvance {};

    struct ChannelState
    {
        spectral::StftChannelState stft;
        spectral::FreezeState freeze;
        spectral::OrganicAmState organicAm;
        juce::Random phaseRng; // per-channel so stereo noise is decorrelated
    };

    // Sidechain needs FIFO + FFT scratch only — no output OLA and no freeze memory.
    // Its output is a magnitude spectrum consumed by the mask, not an audio stream.
    struct SidechainState
    {
        std::array<float, fftSize>     inputFifo {};
        std::array<float, fftSize * 2> fftScratch {};
        int fifoPos = 0;
    };

    std::vector<ChannelState>   channels;
    std::vector<SidechainState> scChannels;

    // Sidechain magnitude spectra — summed across SC channels so one mask drives
    // every main channel. `scLatestMag` is the raw per-hop spectrum; `scSmoothedMag`
    // is the time-smoothed version consumed by the mask (and, later, the UI).
    std::array<float, numBins> scLatestMag   {};
    std::array<float, numBins> scSmoothedMag {};
    float scRetentionPerHop = 0.65f;

    // Shared hop counter — main and sidechain FFTs must fire on the same sample.
    int masterHopCounter = 0;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {}; // Hann — used as both analysis and synthesis window
    float windowGain = 1.0f;              // 1 / Σ window[k·hop]² → forces unity OLA

    void processFrame (ChannelState& st, bool applySidechain);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFreezeProcessor)
};
