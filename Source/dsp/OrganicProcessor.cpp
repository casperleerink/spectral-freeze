#include "OrganicProcessor.h"
#include "SpectralConstants.h"

#include <array>
#include <cmath>

namespace spectral
{
void applyOrganicSpectralProcessing (float* spectrum, juce::Random& rng,
                                     float organicAmt, float filterAmt) noexcept
{
    if (organicAmt <= 0.0f)
        return;

    auto binMag = [] (const float* s, int k) noexcept
    {
        const float re = s[2 * k];
        const float im = s[2 * k + 1];
        return std::sqrt (re * re + im * im);
    };

    const float smoothAmt = organicAmt * (0.30f + 0.60f * filterAmt);
    std::array<float, numBins> mag {};
    std::array<float, numBins> phase {};
    std::array<float, numBins> shapedMag {};

    float peak = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        mag[(size_t) k] = binMag (spectrum, k);
        phase[(size_t) k] = std::atan2 (spectrum[2 * k + 1], spectrum[2 * k]);
        peak = juce::jmax (peak, mag[(size_t) k]);
    }

    if (peak <= 1.0e-9f)
        return;

    // Frequency-domain softening: gently leak energy to neighbouring bins, with
    // stronger effect when the magnitude filter is high and isolated bins dominate.
    for (int k = 0; k < numBins; ++k)
    {
        const float farLeft  = mag[(size_t) juce::jmax (0, k - 2)];
        const float left     = mag[(size_t) juce::jmax (0, k - 1)];
        const float mid      = mag[(size_t) k];
        const float right    = mag[(size_t) juce::jmin (numBins - 1, k + 1)];
        const float farRight = mag[(size_t) juce::jmin (numBins - 1, k + 2)];
        shapedMag[(size_t) k] = (1.0f - smoothAmt) * mid
                              + smoothAmt * (0.08f * farLeft + 0.22f * left + 0.40f * mid
                                           + 0.22f * right + 0.08f * farRight);
    }

    const float residualLevel = organicAmt * organicAmt * (0.0007f + 0.0025f * filterAmt) * peak;
    for (int k = 0; k < numBins; ++k)
    {
        const float localEnv = 0.5f * shapedMag[(size_t) k] / peak + 0.5f;
        const float noiseMag = residualLevel * localEnv * (0.4f + 0.6f * rng.nextFloat());
        const float noisePhase = rng.nextFloat() * juce::MathConstants<float>::twoPi;

        const float re = shapedMag[(size_t) k] * std::cos (phase[(size_t) k])
                       + noiseMag * std::cos (noisePhase);
        const float im = shapedMag[(size_t) k] * std::sin (phase[(size_t) k])
                       + noiseMag * std::sin (noisePhase);

        spectrum[2 * k]     = re;
        spectrum[2 * k + 1] = im;
    }
}

void applyOrganicSaturation (float* samples, float organicAmt) noexcept
{
    if (organicAmt <= 0.0f)
        return;

    const float drive = 1.0f + organicAmt * 4.0f;
    const float makeup = 1.0f / std::tanh (drive);
    const float wet = organicAmt * 0.60f;

    for (int i = 0; i < fftSize; ++i)
    {
        const float dry = samples[(size_t) i];
        const float sat = std::tanh (dry * drive) * makeup;
        samples[(size_t) i] = dry + wet * (sat - dry);
    }
}
} // namespace spectral
