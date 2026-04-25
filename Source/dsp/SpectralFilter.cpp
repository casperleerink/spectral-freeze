#include "SpectralFilter.h"
#include "SpectralConstants.h"

#include <juce_core/juce_core.h>
#include <cmath>

namespace spectral
{
void applyMagnitudeThresholdFilter (float* spectrum, float filterAmt) noexcept
{
    if (filterAmt <= 0.0f)
        return; // pass-through — every bin keeps its amplitude

    // Find the frame's peak magnitude so the threshold scales with program loudness.
    float maxMag = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        const float re = spectrum[2 * k];
        const float im = spectrum[2 * k + 1];
        const float mag = std::sqrt (re * re + im * im);
        if (mag > maxMag) maxMag = mag;
    }

    if (maxMag <= 0.0f)
        return;

    // Squared taper — the knob feels gentle in its lower half and bites hard near the top,
    // which matches how you tend to use a "keep only the loudest bins" control in practice.
    const float threshold = maxMag * filterAmt * filterAmt;

    for (int k = 0; k < numBins; ++k)
    {
        const float re = spectrum[2 * k];
        const float im = spectrum[2 * k + 1];
        const float mag = std::sqrt (re * re + im * im);

        if (mag < threshold)
        {
            spectrum[2 * k]     = 0.0f;
            spectrum[2 * k + 1] = 0.0f;
        }
    }
}

void rebuildConjugateMirror (float* spectrum) noexcept
{
    // Bins fftSize/2+1 .. fftSize-1 must be conj of bins fftSize/2-1 .. 1.
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const int mirror = fftSize - k;
        spectrum[2 * mirror]     =  spectrum[2 * k];
        spectrum[2 * mirror + 1] = -spectrum[2 * k + 1];
    }

    // DC and Nyquist must be purely real.
    spectrum[1] = 0.0f;
    spectrum[fftSize + 1] = 0.0f;
}
} // namespace spectral
