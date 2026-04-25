#include "FreezeProcessor.h"

#include <cmath>

namespace spectral
{
bool shouldCaptureFreezeEdge (const FreezeState& state, bool freezeOn, bool fifoPrimed) noexcept
{
    return freezeOn && fifoPrimed && (! state.wasFrozen || ! state.hasFrozenFrame);
}

bool shouldRunFreezeAnalysis (const FreezeState& state, bool freezeOn, bool captureEdge) noexcept
{
    return ! freezeOn || captureEdge || ! state.hasFrozenFrame;
}

void recordAnalysisFrame (FreezeState& state,
                          const float* spectrum,
                          const std::array<float, numBins>& phaseAdvance) noexcept
{
    // Push this frame's magnitude spectrum into the rolling history, and track
    // the actual phase movement of each bin. The latter is the important middle
    // ground: deterministic bin-centre phase was choppy, fully random phase was
    // reverb-like, measured phase advance follows the captured source motion.
    auto& slot = state.magHistory[(size_t) state.magHistoryWrite];
    for (int k = 0; k < numBins; ++k)
    {
        const float re = spectrum[2 * k];
        const float im = spectrum[2 * k + 1];
        const float phase = std::atan2 (im, re);
        slot[(size_t) k] = std::sqrt (re * re + im * im);

        if (state.hasLastAnalysisPhase)
        {
            float deviation = phase - state.lastAnalysisPhase[(size_t) k] - phaseAdvance[(size_t) k];
            while (deviation > juce::MathConstants<float>::pi)
                deviation -= juce::MathConstants<float>::twoPi;
            while (deviation < -juce::MathConstants<float>::pi)
                deviation += juce::MathConstants<float>::twoPi;

            const float measuredAdvance = phaseAdvance[(size_t) k] + deviation;
            state.smoothedPhaseAdvance[(size_t) k]
                = 0.65f * state.smoothedPhaseAdvance[(size_t) k]
                + 0.35f * measuredAdvance;
        }
        else
        {
            state.smoothedPhaseAdvance[(size_t) k] = phaseAdvance[(size_t) k];
        }

        state.lastAnalysisPhase[(size_t) k] = phase;
    }

    state.hasLastAnalysisPhase = true;
    state.magHistoryWrite = (state.magHistoryWrite + 1) % magHistorySize;
    if (state.magHistoryCount < magHistorySize)
        ++state.magHistoryCount;
}

void captureFreezeFrame (FreezeState& state, const float* spectrum) noexcept
{
    // Average magnitudes across a short history so the freeze edge is smooth,
    // but keep the actual edge phase and measured phase advance. Fully random
    // phase was too diffuse/reverb-like; bin-centre coherent phase was choppy.
    const int   count    = juce::jmax (1, state.magHistoryCount);
    const float invCount = 1.0f / (float) count;
    for (int k = 0; k < numBins; ++k)
    {
        float sum = 0.0f;
        for (int h = 0; h < count; ++h)
            sum += state.magHistory[(size_t) h][(size_t) k];

        const float re = spectrum[2 * k];
        const float im = spectrum[2 * k + 1];
        state.frozenMag         [(size_t) k] = sum * invCount;
        state.frozenPhase       [(size_t) k] = std::atan2 (im, re);
        state.frozenPhaseAdvance[(size_t) k] = state.smoothedPhaseAdvance[(size_t) k];
    }

    state.hasFrozenFrame = true;
}

void resynthesiseFrozenFrame (FreezeState& state,
                              OrganicAmState& organicAm,
                              juce::Random& rng,
                              float* spectrum,
                              float organicAmt) noexcept
{
    // Advance captured phase by the live signal's measured per-bin motion, with
    // only a tiny random walk. This avoids both extremes: no fresh random phase
    // cloud, and no rigid bin-centre loop that chops/beats at the hop rate.
    if (organicAmt > 0.0f)
    {
        if (++organicAm.hopCounter >= 8)
        {
            organicAm.hopCounter = 0;
            for (int b = 0; b < organicAmBands; ++b)
                organicAm.target[(size_t) b] = rng.nextFloat() * 2.0f - 1.0f;
        }

        for (int b = 0; b < organicAmBands; ++b)
            organicAm.value[(size_t) b] += 0.08f * (organicAm.target[(size_t) b]
                                                   - organicAm.value[(size_t) b]);
    }

    for (int k = 0; k < numBins; ++k)
    {
        float phase = state.frozenPhase[(size_t) k]
                    + state.frozenPhaseAdvance[(size_t) k] * (1.0f + (rng.nextFloat() * 2.0f - 1.0f) * organicAmt * 0.035f)
                    + (rng.nextFloat() * 2.0f - 1.0f) * (freezePhaseJitterRadians + organicAmt * 0.18f);

        if (phase > juce::MathConstants<float>::pi)
            phase -= juce::MathConstants<float>::twoPi;
        else if (phase < -juce::MathConstants<float>::pi)
            phase += juce::MathConstants<float>::twoPi;

        state.frozenPhase[(size_t) k] = phase;
        const float bandPos = (float) k * (float) organicAmBands / (float) numBins;
        const int band0 = juce::jlimit (0, organicAmBands - 1, (int) bandPos);
        const int band1 = juce::jmin (organicAmBands - 1, band0 + 1);
        const float frac = bandPos - (float) band0;
        const float bandAm = organicAm.value[(size_t) band0] * (1.0f - frac)
                           + organicAm.value[(size_t) band1] * frac;

        const float mag = state.frozenMag[(size_t) k]
                        * (1.0f + bandAm * organicAmt * 0.28f)
                        * (1.0f + (rng.nextFloat() * 2.0f - 1.0f) * organicAmt * 0.06f);
        spectrum[2 * k]     = mag * std::cos (phase);
        spectrum[2 * k + 1] = mag * std::sin (phase);
    }
}
} // namespace spectral
