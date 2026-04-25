#pragma once

#include "SpectralConstants.h"

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace spectral
{
    void analyseSidechainHop (const std::array<float, fftSize>& inputFifo,
                              int fifoPos,
                              std::array<float, fftSize * 2>& fftScratch,
                              const std::array<float, fftSize>& window,
                              juce::dsp::FFT& fft,
                              std::array<float, numBins>& latestMag) noexcept;

    void applySidechainEnhancement (float* spectrum,
                                    const std::array<float, numBins>& smoothedMag,
                                    float boostDb,
                                    float freqSmoothing) noexcept;
}
