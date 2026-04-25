#pragma once

#include "SpectralConstants.h"

#include <array>

namespace spectral
{
    struct StftChannelState
    {
        std::array<float, fftSize>     inputFifo {};
        std::array<float, fftSize>     outputFifo {};
        std::array<float, fftSize * 2> fftScratch {}; // juce real-FFT needs 2N floats

        int fifoPos = 0;     // next write slot; also "oldest" slot from reader's POV
        int samplesSeen = 0; // avoids freezing cold FIFO contents after host bus resets
    };

    float pushSampleAndPopOutput (StftChannelState& state, float input) noexcept;
    void copyInputFrameToScratch (StftChannelState& state) noexcept;
    void applyWindow (float* samples, const std::array<float, fftSize>& window) noexcept;
    void applySynthesisWindow (float* samples, const std::array<float, fftSize>& window,
                               float windowGain) noexcept;
    void overlapAddScratchToOutput (StftChannelState& state) noexcept;
}
