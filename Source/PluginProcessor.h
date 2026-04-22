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

    static constexpr const char* freezeParamID   = "freeze";
    static constexpr const char* filterParamID   = "filter";
    static constexpr const char* scMixParamID    = "scMix";
    static constexpr const char* scSelectParamID = "scSelectivity";
    static constexpr const char* scSmoothParamID = "scSmoothing";

    // STFT config — 2048 @ 75% overlap. fftSize/hopSize = 4 → four overlapping windows per output sample.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1; // DC..Nyquist (non-negative frequencies)

    // Freeze captures an averaged magnitude spectrum over the last N analysis hops
    // (≈ 93 ms @ 44.1k), which smears out transients and vibrato in the snapshot.
    static constexpr int magHistorySize = 8;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* freezeParam   { nullptr };
    std::atomic<float>* filterParam   { nullptr };
    std::atomic<float>* scMixParam    { nullptr };
    std::atomic<float>* scSelectParam { nullptr };
    std::atomic<float>* scSmoothParam { nullptr };

    struct ChannelState
    {
        std::array<float, fftSize>     inputFifo {};
        std::array<float, fftSize>     outputFifo {};
        std::array<float, fftSize * 2> fftScratch {}; // juce real-FFT needs 2N floats

        // Frozen magnitude spectrum — averaged over the last magHistorySize hops
        // at the freeze edge. Phase is redrawn at random every hop during freeze
        // (no accumulator needed), which kills hop-rate beating.
        std::array<float, numBins> frozenMag {};

        // Rolling history of recent analysis-frame magnitudes. Written on every
        // non-frozen hop; averaged on the freeze edge to form `frozenMag`.
        std::array<std::array<float, numBins>, magHistorySize> magHistory {};
        int magHistoryWrite = 0;
        int magHistoryCount = 0; // clamped to magHistorySize once full

        juce::Random phaseRng; // per-channel so stereo noise is decorrelated

        int  fifoPos   = 0;     // next write slot; also "oldest" slot from reader's POV
        bool wasFrozen = false; // previous frame's freeze state — detects rising edge
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

    // Shared hop counter — main and sidechain FFTs must fire on the same sample.
    int masterHopCounter = 0;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {}; // Hann — used as both analysis and synthesis window
    float windowGain = 1.0f;              // 1 / Σ window[k·hop]² → forces unity OLA

    void processFrame (ChannelState& st, bool applySidechain);
    void processSidechainHop (SidechainState& sc) noexcept;
    void applySpectralFilter (float* spectrum, float filterAmt) noexcept;
    void applySidechainMask (float* spectrum, float mix, float selectivity) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFreezeProcessor)
};
