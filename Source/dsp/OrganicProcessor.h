#pragma once

#include <juce_core/juce_core.h>

namespace spectral
{
    void applyOrganicSpectralProcessing (float* spectrum, juce::Random& rng,
                                         float organicAmt, float filterAmt) noexcept;
    void applyOrganicSaturation (float* samples, float organicAmt) noexcept;
}
