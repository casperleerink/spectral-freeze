#pragma once

#include "SpectralConstants.h"

#include <juce_core/juce_core.h>
#include <array>

namespace spectral
{
    struct FreezeState
    {
        std::array<float, numBins> frozenMag          {};
        std::array<float, numBins> frozenPhase        {};
        std::array<float, numBins> frozenPhaseAdvance {};

        std::array<float, numBins> lastAnalysisPhase    {};
        std::array<float, numBins> smoothedPhaseAdvance {};
        bool hasLastAnalysisPhase = false;

        std::array<std::array<float, numBins>, magHistorySize> magHistory {};
        int magHistoryWrite = 0;
        int magHistoryCount = 0;

        bool wasFrozen = false;
        bool hasFrozenFrame = false;
    };

    struct OrganicAmState
    {
        std::array<float, organicAmBands> value  {};
        std::array<float, organicAmBands> target {};
        int hopCounter = 0;
    };

    bool shouldCaptureFreezeEdge (const FreezeState& state, bool freezeOn, bool fifoPrimed) noexcept;
    bool shouldRunFreezeAnalysis (const FreezeState& state, bool freezeOn, bool captureEdge) noexcept;

    void recordAnalysisFrame (FreezeState& state,
                              const float* spectrum,
                              const std::array<float, numBins>& phaseAdvance) noexcept;

    void captureFreezeFrame (FreezeState& state, const float* spectrum) noexcept;

    void resynthesiseFrozenFrame (FreezeState& state,
                                  OrganicAmState& organicAm,
                                  juce::Random& rng,
                                  float* spectrum,
                                  float organicAmt) noexcept;
}
