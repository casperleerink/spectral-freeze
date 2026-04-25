#include "StftProcessor.h"

namespace spectral
{
float pushSampleAndPopOutput (StftChannelState& state, float input) noexcept
{
    state.inputFifo[(size_t) state.fifoPos] = input;

    const float output = state.outputFifo[(size_t) state.fifoPos];
    state.outputFifo[(size_t) state.fifoPos] = 0.0f;

    state.fifoPos = (state.fifoPos + 1) % fftSize;
    if (state.samplesSeen < fftSize)
        ++state.samplesSeen;

    return output;
}

void copyInputFrameToScratch (StftChannelState& state) noexcept
{
    // Unroll the ring buffer into fftScratch[0..fftSize-1] in TEMPORAL order:
    // fftScratch[0] = oldest sample in the window, fftScratch[fftSize-1] = newest.
    for (int i = 0; i < fftSize; ++i)
        state.fftScratch[(size_t) i] = state.inputFifo[(size_t) ((state.fifoPos + i) % fftSize)];
}

void applyWindow (float* samples, const std::array<float, fftSize>& window) noexcept
{
    for (int i = 0; i < fftSize; ++i)
        samples[(size_t) i] *= window[(size_t) i];
}

void applySynthesisWindow (float* samples, const std::array<float, fftSize>& window,
                           float windowGain) noexcept
{
    for (int i = 0; i < fftSize; ++i)
        samples[(size_t) i] *= window[(size_t) i] * windowGain;
}

void overlapAddScratchToOutput (StftChannelState& state) noexcept
{
    for (int i = 0; i < fftSize; ++i)
        state.outputFifo[(size_t) ((state.fifoPos + i) % fftSize)]
            += state.fftScratch[(size_t) i];
}
} // namespace spectral
