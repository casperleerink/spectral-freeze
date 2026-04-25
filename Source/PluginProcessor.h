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
    static constexpr const char* scBoostParamID      = "scBoost";
    static constexpr const char* scFreqSmoothParamID = "scFreqSmoothing";
    static constexpr const char* organicParamID      = "organic";

    // STFT config — 2048 @ 75% overlap. fftSize/hopSize = 4 → four overlapping windows per output sample.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1; // DC..Nyquist (non-negative frequencies)

    // Freeze captures a short averaged magnitude spectrum over the last N analysis hops
    // (≈ 46 ms @ 44.1k). This softens the freeze edge without washing the sound into a cloud.
    static constexpr int magHistorySize = 4;

    // Very small per-hop phase wander for frozen bins. The main smoothness now comes from
    // measured phase-vocoder bin advances; this only prevents perfectly static locking.
    static constexpr float freezePhaseJitterRadians = 0.004f;

    // Organic AM uses a small number of broad frequency bands so the motion feels
    // like a coupled resonant body, not thousands of unrelated tremolos.
    static constexpr int organicAmBands = 12;

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
        std::array<float, fftSize>     inputFifo {};
        std::array<float, fftSize>     outputFifo {};
        std::array<float, fftSize * 2> fftScratch {}; // juce real-FFT needs 2N floats

        // Frozen spectrum memory. Magnitude is averaged over a short history at the
        // freeze edge; phase is captured from the edge frame and advanced with the
        // measured per-bin phase motion from the live signal.
        std::array<float, numBins> frozenMag          {};
        std::array<float, numBins> frozenPhase        {};
        std::array<float, numBins> frozenPhaseAdvance {};

        // Live phase-vocoder tracking. Bin-centre phase advance is what made the old
        // freeze beat/chop when a partial sat between FFT bins; these measured advances
        // preserve that off-bin motion instead of forcing everything onto bin centres.
        std::array<float, numBins> lastAnalysisPhase       {};
        std::array<float, numBins> smoothedPhaseAdvance    {};
        bool hasLastAnalysisPhase = false;

        // Rolling history of recent analysis-frame magnitudes. Written on every
        // non-frozen hop; averaged on the freeze edge to form `frozenMag`.
        std::array<std::array<float, numBins>, magHistorySize> magHistory {};
        int magHistoryWrite = 0;
        int magHistoryCount = 0; // clamped to magHistorySize once full

        juce::Random phaseRng; // per-channel so stereo noise is decorrelated

        std::array<float, organicAmBands> organicAmValue  {};
        std::array<float, organicAmBands> organicAmTarget {};
        int organicAmHopCounter = 0;

        int  fifoPos     = 0; // next write slot; also "oldest" slot from reader's POV
        int  samplesSeen = 0; // avoids freezing cold FIFO contents after host bus resets
        bool wasFrozen   = false; // previous frame's freeze state — detects rising edge
        bool hasFrozenFrame = false;
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
    void processSidechainHop (SidechainState& sc) noexcept;
    void applySpectralFilter (float* spectrum, float filterAmt) noexcept;
    void applyOrganicSpectralProcessing (ChannelState& st, float* spectrum, float organicAmt, float filterAmt) noexcept;
    void applySidechainEnhancement (float* spectrum, float boostDb, float freqSmoothing) noexcept;
    void applyOrganicSaturation (float* samples, float organicAmt) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFreezeProcessor)
};
