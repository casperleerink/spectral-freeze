#pragma once

namespace spectral
{
    void applyMagnitudeThresholdFilter (float* spectrum, float filterAmt) noexcept;
    void rebuildConjugateMirror (float* spectrum) noexcept;
}
