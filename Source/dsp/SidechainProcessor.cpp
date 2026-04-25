#include "SidechainProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace spectral
{
void analyseSidechainHop (const std::array<float, fftSize>& inputFifo,
                          int fifoPos,
                          std::array<float, fftSize * 2>& fftScratch,
                          const std::array<float, fftSize>& window,
                          juce::dsp::FFT& fft,
                          std::array<float, numBins>& latestMag) noexcept
{
    // Unroll ring buffer into fftScratch in temporal order (same convention as main path).
    for (int i = 0; i < fftSize; ++i)
        fftScratch[(size_t) i] = inputFifo[(size_t) ((fifoPos + i) % fftSize)];

    // Window + forward FFT. Using the same Hann window as the main path keeps the
    // two spectra on the same footing — their magnitudes are directly comparable.
    for (int i = 0; i < fftSize; ++i)
        fftScratch[(size_t) i] *= window[(size_t) i];

    fft.performRealOnlyForwardTransform (fftScratch.data());

    // Sum magnitudes across SC channels into latestMag. Caller zeroed it for
    // this hop; each SC channel adds its own contribution.
    for (int k = 0; k < numBins; ++k)
    {
        const float re = fftScratch[(size_t) (2 * k)];
        const float im = fftScratch[(size_t) (2 * k + 1)];
        latestMag[(size_t) k] += std::sqrt (re * re + im * im);
    }
}

void applySidechainEnhancement (float* spectrum,
                                const std::array<float, numBins>& smoothedMag,
                                float boostDb,
                                float freqSmoothing) noexcept
{
    if (boostDb <= 0.0f)
        return;

    auto smoothstep = [] (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    };

    float scPeak = 0.0f;
    float mainPeak = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        scPeak = juce::jmax (scPeak, smoothedMag[(size_t) k]);

        const float re = spectrum[2 * k];
        const float im = spectrum[2 * k + 1];
        mainPeak = juce::jmax (mainPeak, std::sqrt (re * re + im * im));
    }

    // Silent sidechain or silent main: boost mode leaves the main signal unchanged.
    if (scPeak <= 1.0e-9f || mainPeak <= 1.0e-9f)
        return;

    // Build a boost mask from BOTH spectra. The sidechain says which frequencies
    // should be emphasised; mainPresence prevents boosting bins that are only FFT
    // leakage/noise in the main signal.
    std::array<float, numBins> rawMask {};
    std::array<float, numBins> mask {};

    // Fixed broad-ish selectivity. User-facing focus is boost amount + smoothing.
    constexpr float gamma = 1.25f;
    const float invScPeak = 1.0f / scPeak;
    const float invMainPeak = 1.0f / mainPeak;

    for (int k = 0; k < numBins; ++k)
    {
        const float re = spectrum[2 * k];
        const float im = spectrum[2 * k + 1];
        const float mainNorm = std::sqrt (re * re + im * im) * invMainPeak;
        const float scNorm = smoothedMag[(size_t) k] * invScPeak;

        const float scMatch = std::pow (juce::jlimit (0.0f, 1.0f, scNorm), gamma);

        // Main presence is an eligibility curve, not a second heavy gain shape:
        // below about -48 dB relative to the frame peak, don't boost; by about
        // -26 dB, allow the sidechain match through fully.
        constexpr float presenceThreshold = 0.004f;
        constexpr float presenceFull = 0.05f;
        const float mainPresence = smoothstep ((mainNorm - presenceThreshold)
                                             / (presenceFull - presenceThreshold));
        rawMask[(size_t) k] = scMatch * mainPresence;
    }

    // Optional one-bin frequency smoothing. This blends each bin with its neighbours
    // to reduce isolated, chirpy boosts while preserving the original mask at 0%.
    const float a = juce::jlimit (0.0f, 1.0f, freqSmoothing);
    for (int k = 0; k < numBins; ++k)
    {
        const float left  = rawMask[(size_t) juce::jmax (0, k - 1)];
        const float mid   = rawMask[(size_t) k];
        const float right = rawMask[(size_t) juce::jmin (numBins - 1, k + 1)];
        mask[(size_t) k] = (1.0f - a) * mid + a * (0.25f * left + 0.5f * mid + 0.25f * right);
    }

    const float maxBoost = juce::Decibels::decibelsToGain (juce::jlimit (0.0f, 18.0f, boostDb));
    for (int k = 0; k < numBins; ++k)
    {
        const float shaped = smoothstep (mask[(size_t) k]);
        const float boostGain = 1.0f + (maxBoost - 1.0f) * shaped;

        spectrum[2 * k]     *= boostGain;
        spectrum[2 * k + 1] *= boostGain;
    }
}
} // namespace spectral
