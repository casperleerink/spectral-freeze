#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
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

    static constexpr const char* gainParamID   = "gain";
    static constexpr const char* freezeParamID = "freeze";
    static constexpr const char* filterParamID = "filter";

    // STFT config — 2048 @ 75% overlap. fftSize/hopSize = 4 → four overlapping windows per output sample.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1; // DC..Nyquist (non-negative frequencies)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* gainParam   { nullptr };
    std::atomic<float>* freezeParam { nullptr };
    std::atomic<float>* filterParam { nullptr };
    juce::LinearSmoothedValue<float> smoothedGain;

    // Per-bin natural phase advance across one hop: 2π·k·hopSize/fftSize.
    // Precomputed so freeze resynthesis stays cheap inside the audio thread.
    std::array<float, numBins> phaseAdvance {};

    struct ChannelState
    {
        std::array<float, fftSize>     inputFifo {};
        std::array<float, fftSize>     outputFifo {};
        std::array<float, fftSize * 2> fftScratch {}; // juce real-FFT needs 2N floats

        // Freeze memory: magnitude snapshot taken at the freeze edge, plus a running
        // phase accumulator advanced each hop so sinusoids keep rotating naturally.
        std::array<float, numBins> frozenMag   {};
        std::array<float, numBins> frozenPhase {};

        int  fifoPos    = 0;     // next write slot; also "oldest" slot from reader's POV
        int  hopCounter = 0;     // samples since last hop fired
        bool wasFrozen  = false; // previous frame's freeze state — detects rising edge
    };

    std::vector<ChannelState> channels;
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {}; // Hann — used as both analysis and synthesis window
    float windowGain = 1.0f;              // 1 / Σ window[k·hop]² → forces unity OLA

    void processFrame (ChannelState& st);
    void applySpectralFilter (float* spectrum, float filterAmt) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFreezeProcessor)
};
