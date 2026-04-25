#pragma once

namespace spectral
{
    // STFT config — 2048 @ 75% overlap. fftSize/hopSize = 4 → four overlapping windows per output sample.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1; // DC..Nyquist (non-negative frequencies)

    // Freeze captures a short averaged magnitude spectrum over the last N analysis hops
    // (≈ 46 ms @ 44.1k). This softens the freeze edge without washing the sound into a cloud.
    static constexpr int magHistorySize = 4;

    // Very small per-hop phase wander for frozen bins. The main smoothness now comes from
    // measured phase-vocoder bin advances; this only prevents perfectly static locking.
    static constexpr float freezePhaseJitterRadians = 0.004f;

    // Organic AM uses a small number of broad frequency bands so the motion feels
    // like a coupled resonant body, not thousands of unrelated tremolos.
    static constexpr int organicAmBands = 12;
}
