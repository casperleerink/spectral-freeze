#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralFreezeProcessor::SpectralFreezeProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    freezeParam   = apvts.getRawParameterValue (freezeParamID);
    filterParam   = apvts.getRawParameterValue (filterParamID);
    scBoostParam      = apvts.getRawParameterValue (scBoostParamID);
    scFreqSmoothParam = apvts.getRawParameterValue (scFreqSmoothParamID);
    jassert (freezeParam != nullptr && filterParam != nullptr
             && scBoostParam != nullptr && scFreqSmoothParam != nullptr);
}

SpectralFreezeProcessor::~SpectralFreezeProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
SpectralFreezeProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { freezeParamID, 1 },
        "Freeze",
        false));

    // 0 = pass everything, 1 = only bins at the frame's peak magnitude survive.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { filterParamID, 1 },
        "Filter",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f));

    // Sidechain enhancement controls. The sidechain path is intentionally simple:
    // Boost sets the maximum matched-bin lift; Freq Smooth widens/softens the boost
    // mask across neighbouring FFT bins. Other shaping uses fixed musical defaults.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { scBoostParamID, 1 },
        "SC Boost",
        juce::NormalisableRange<float> { 0.0f, 18.0f, 0.01f },
        9.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { scFreqSmoothParamID, 1 },
        "SC Freq Smooth",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.25f));

    return layout;
}

void SpectralFreezeProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Hann window — applied for BOTH analysis and synthesis (i.e. Hann²).
    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                    * (float) n / (float) fftSize);

    // COLA scale: Σ window[k·hop]² at any output sample. For Hann² at 75% overlap this = 1.5.
    // Dividing each synthesised frame by this makes overlap-add sum to unity (pass-through).
    float colaSum = 0.0f;
    for (int k = 0; k * hopSize < fftSize; ++k)
    {
        const float w = window[(size_t) (k * hopSize)];
        colaSum += w * w;
    }
    windowGain = 1.0f / colaSum;

    // Size STFT state by bus, not by total input count — the sidechain bus
    // has its own leaner state and shouldn't inflate the main vector.
    channels  .assign ((size_t) getChannelCountOfBus (true, 0), ChannelState{});
    scChannels.assign ((size_t) getChannelCountOfBus (true, 1), SidechainState{});

    // Seed each channel's tiny freeze phase wander independently so L/R do not
    // collapse into exactly the same frozen texture.
    for (auto& ch : channels)
        ch.phaseRng.setSeedRandomly();

    for (int k = 0; k < numBins; ++k)
        phaseAdvance[(size_t) k] = juce::MathConstants<float>::twoPi
                                 * (float) k * (float) hopSize / (float) fftSize;

    scLatestMag.fill (0.0f);
    scSmoothedMag.fill (0.0f);
    masterHopCounter = 0;

    // Report streaming-STFT latency so hosts align bypass/compare. Impulse-response
    // proves: input at t=0 reaches output at t=fftSize.
    setLatencySamples (fftSize);
}

void SpectralFreezeProcessor::releaseResources() {}

bool SpectralFreezeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (mainIn != mainOut)
        return false;

    // Sidechain is optional: hosts may leave it disabled, or attach mono/stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.inputBuses.getReference (1);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SpectralFreezeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Alias the main I/O slice of `buffer`. getBusBuffer returns a view that
    // writes back into the same memory, so writes here ARE the plugin output.
    auto  mainBuf = getBusBuffer (buffer, true, 0);
    const int numSamples   = mainBuf.getNumSamples();
    const int mainChannels = juce::jmin (mainBuf.getNumChannels(), (int) channels.size());

    // Sidechain bus may be absent (host didn't wire it) or disabled (host wired
    // it but user hasn't attached audio). Both cases route through the same guard.
    const auto* scBus      = getBus (true, 1);
    const bool  scBusLive  = scBus != nullptr && scBus->isEnabled() && ! scChannels.empty();
    juce::AudioBuffer<float> scBuf;
    int scChNum = 0;
    if (scBusLive)
    {
        scBuf   = getBusBuffer (buffer, true, 1);
        scChNum = juce::jmin (scBuf.getNumChannels(), (int) scChannels.size());
    }

    const float scBoostDb = juce::jlimit (0.0f, 18.0f, scBoostParam->load());
    const bool  runSidechain = scBusLive && scChNum > 0 && scBoostDb > 0.0f;

    // Fixed, gentle time smoothing for the sidechain spectrum. The user-facing
    // smoothing control is reserved for frequency width/blur, not envelope timing.
    const float scRetention = 0.65f;

    // Sample-level outer loop so MAIN + SIDECHAIN hit their hop boundary in
    // perfect lockstep. Within a hop we pull the SC magnitude spectrum first,
    // then feed it to every main channel's frame.
    for (int n = 0; n < numSamples; ++n)
    {
        // Main: push input, pop output, advance fifoPos.
        for (int ch = 0; ch < mainChannels; ++ch)
        {
            auto& st   = channels[(size_t) ch];
            auto* data = mainBuf.getWritePointer (ch);
            st.inputFifo[(size_t) st.fifoPos]  = data[n];
            data[n]                            = st.outputFifo[(size_t) st.fifoPos];
            st.outputFifo[(size_t) st.fifoPos] = 0.0f;
            st.fifoPos = (st.fifoPos + 1) % fftSize;
        }

        // Sidechain: push only — no output to reconstruct.
        if (runSidechain)
        {
            for (int ch = 0; ch < scChNum; ++ch)
            {
                auto& sc = scChannels[(size_t) ch];
                sc.inputFifo[(size_t) sc.fifoPos] = scBuf.getReadPointer (ch)[n];
                sc.fifoPos = (sc.fifoPos + 1) % fftSize;
            }
        }

        if (++masterHopCounter >= hopSize)
        {
            masterHopCounter = 0;

            if (runSidechain)
            {
                scLatestMag.fill (0.0f);
                for (int ch = 0; ch < scChNum; ++ch)
                    processSidechainHop (scChannels[(size_t) ch]);

                // Time-smooth per-bin magnitude. Keeps held notes "ringing" in the
                // mask after they've stopped, matching the sympathetic-resonance feel.
                for (int k = 0; k < numBins; ++k)
                    scSmoothedMag[(size_t) k]
                        = scRetention * scSmoothedMag[(size_t) k]
                        + (1.0f - scRetention) * scLatestMag[(size_t) k];
            }

            for (int ch = 0; ch < mainChannels; ++ch)
            {
                auto& st = channels[(size_t) ch];
                processFrame (st, runSidechain);
                for (int i = 0; i < fftSize; ++i)
                    st.outputFifo[(size_t) ((st.fifoPos + i) % fftSize)]
                        += st.fftScratch[(size_t) i];
            }
        }
    }

}

void SpectralFreezeProcessor::processFrame (ChannelState& st, bool applySidechain)
{
    const bool  freezeOn  = freezeParam->load() >= 0.5f;
    const float filterAmt = juce::jlimit (0.0f, 1.0f, filterParam->load());

    // Unroll the ring buffer into fftScratch[0..fftSize-1] in TEMPORAL order:
    // fftScratch[0] = oldest sample in the window, fftScratch[fftSize-1] = newest.
    for (int i = 0; i < fftSize; ++i)
        st.fftScratch[(size_t) i] = st.inputFifo[(size_t) ((st.fifoPos + i) % fftSize)];

    // --- Analysis: window + forward FFT ---------------------------------------------------
    // We still analyse even when frozen on the first frame of the edge, so we can
    // capture a fresh magnitude/phase snapshot. After that we skip analysis entirely.
    const bool captureEdge = freezeOn && ! st.wasFrozen;
    const bool runAnalysis = ! freezeOn || captureEdge;

    if (runAnalysis)
    {
        for (int i = 0; i < fftSize; ++i)
            st.fftScratch[(size_t) i] *= window[(size_t) i];

        fft.performRealOnlyForwardTransform (st.fftScratch.data());

        // Push this frame's magnitude spectrum into the rolling history, and track
        // the actual phase movement of each bin. The latter is the important middle
        // ground: deterministic bin-centre phase was choppy, fully random phase was
        // reverb-like, measured phase advance follows the captured source motion.
        auto& slot = st.magHistory[(size_t) st.magHistoryWrite];
        for (int k = 0; k < numBins; ++k)
        {
            const float re = st.fftScratch[(size_t) (2 * k)];
            const float im = st.fftScratch[(size_t) (2 * k + 1)];
            const float phase = std::atan2 (im, re);
            slot[(size_t) k] = std::sqrt (re * re + im * im);

            if (st.hasLastAnalysisPhase)
            {
                float deviation = phase - st.lastAnalysisPhase[(size_t) k] - phaseAdvance[(size_t) k];
                while (deviation > juce::MathConstants<float>::pi)
                    deviation -= juce::MathConstants<float>::twoPi;
                while (deviation < -juce::MathConstants<float>::pi)
                    deviation += juce::MathConstants<float>::twoPi;

                const float measuredAdvance = phaseAdvance[(size_t) k] + deviation;
                st.smoothedPhaseAdvance[(size_t) k]
                    = 0.65f * st.smoothedPhaseAdvance[(size_t) k]
                    + 0.35f * measuredAdvance;
            }
            else
            {
                st.smoothedPhaseAdvance[(size_t) k] = phaseAdvance[(size_t) k];
            }

            st.lastAnalysisPhase[(size_t) k] = phase;
        }
        st.hasLastAnalysisPhase = true;
        st.magHistoryWrite = (st.magHistoryWrite + 1) % magHistorySize;
        if (st.magHistoryCount < magHistorySize)
            ++st.magHistoryCount;
    }

    // --- Freeze memory --------------------------------------------------------------------
    if (captureEdge)
    {
        // Average magnitudes across a short history so the freeze edge is smooth,
        // but keep the actual edge phase and measured phase advance. Fully random
        // phase was too diffuse/reverb-like; bin-centre coherent phase was choppy.
        const int   count    = juce::jmax (1, st.magHistoryCount);
        const float invCount = 1.0f / (float) count;
        for (int k = 0; k < numBins; ++k)
        {
            float sum = 0.0f;
            for (int h = 0; h < count; ++h)
                sum += st.magHistory[(size_t) h][(size_t) k];

            const float re = st.fftScratch[(size_t) (2 * k)];
            const float im = st.fftScratch[(size_t) (2 * k + 1)];
            st.frozenMag         [(size_t) k] = sum * invCount;
            st.frozenPhase       [(size_t) k] = std::atan2 (im, re);
            st.frozenPhaseAdvance[(size_t) k] = st.smoothedPhaseAdvance[(size_t) k];
        }
    }

    // --- Spectral processing --------------------------------------------------------------
    if (freezeOn)
    {
        // Advance captured phase by the live signal's measured per-bin motion, with
        // only a tiny random walk. This avoids both extremes: no fresh random phase
        // cloud, and no rigid bin-centre loop that chops/beats at the hop rate.
        for (int k = 0; k < numBins; ++k)
        {
            float phase = st.frozenPhase[(size_t) k]
                        + st.frozenPhaseAdvance[(size_t) k]
                        + (st.phaseRng.nextFloat() * 2.0f - 1.0f) * freezePhaseJitterRadians;

            if (phase > juce::MathConstants<float>::pi)
                phase -= juce::MathConstants<float>::twoPi;
            else if (phase < -juce::MathConstants<float>::pi)
                phase += juce::MathConstants<float>::twoPi;

            st.frozenPhase[(size_t) k] = phase;
            const float mag = st.frozenMag[(size_t) k];
            st.fftScratch[(size_t) (2 * k)]     = mag * std::cos (phase);
            st.fftScratch[(size_t) (2 * k + 1)] = mag * std::sin (phase);
        }
    }

    // --- Magnitude-threshold filter ------------------------------------------------------
    // Gate bins below a per-frame threshold. The threshold is derived from `filterAmt`
    // and the frame's peak magnitude — see applySpectralFilter below.
    applySpectralFilter (st.fftScratch.data(), filterAmt);

    // --- Sidechain mask ------------------------------------------------------------------
    // Runs AFTER freeze resynthesis so a frozen cloud can be sculpted by a live sidechain.
    if (applySidechain)
    {
        const float scBoostDb    = juce::jlimit (0.0f, 18.0f, scBoostParam->load());
        const float scFreqSmooth = juce::jlimit (0.0f, 1.0f, scFreqSmoothParam->load());
        applySidechainEnhancement (st.fftScratch.data(), scBoostDb, scFreqSmooth);
    }

    // --- Rebuild conjugate mirror so the inverse FFT returns a purely real signal --------
    // Bins fftSize/2+1 .. fftSize-1 must be conj of bins fftSize/2-1 .. 1.
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const int mirror = fftSize - k;
        st.fftScratch[(size_t) (2 * mirror)]     =  st.fftScratch[(size_t) (2 * k)];
        st.fftScratch[(size_t) (2 * mirror + 1)] = -st.fftScratch[(size_t) (2 * k + 1)];
    }
    // DC and Nyquist must be purely real.
    st.fftScratch[1] = 0.0f;
    st.fftScratch[(size_t) (fftSize + 1)] = 0.0f;

    // --- Synthesis: inverse FFT + window + COLA gain -------------------------------------
    fft.performRealOnlyInverseTransform (st.fftScratch.data());

    for (int i = 0; i < fftSize; ++i)
        st.fftScratch[(size_t) i] *= window[(size_t) i] * windowGain;

    st.wasFrozen = freezeOn;
}

void SpectralFreezeProcessor::applySpectralFilter (float* spectrum, float filterAmt) noexcept
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

void SpectralFreezeProcessor::processSidechainHop (SidechainState& sc) noexcept
{
    // Unroll ring buffer into fftScratch in temporal order (same convention as main path).
    for (int i = 0; i < fftSize; ++i)
        sc.fftScratch[(size_t) i] = sc.inputFifo[(size_t) ((sc.fifoPos + i) % fftSize)];

    // Window + forward FFT. Using the same Hann window as the main path keeps the
    // two spectra on the same footing — their magnitudes are directly comparable.
    for (int i = 0; i < fftSize; ++i)
        sc.fftScratch[(size_t) i] *= window[(size_t) i];

    fft.performRealOnlyForwardTransform (sc.fftScratch.data());

    // Sum magnitudes across SC channels into scLatestMag. Caller zeroed it for
    // this hop; each SC channel adds its own contribution.
    for (int k = 0; k < numBins; ++k)
    {
        const float re = sc.fftScratch[(size_t) (2 * k)];
        const float im = sc.fftScratch[(size_t) (2 * k + 1)];
        scLatestMag[(size_t) k] += std::sqrt (re * re + im * im);
    }
}

void SpectralFreezeProcessor::applySidechainEnhancement (float* spectrum, float boostDb,
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
        scPeak = juce::jmax (scPeak, scSmoothedMag[(size_t) k]);

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
        const float scNorm = scSmoothedMag[(size_t) k] * invScPeak;

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

juce::AudioProcessorEditor* SpectralFreezeProcessor::createEditor()
{
    return new SpectralFreezeEditor (*this);
}

void SpectralFreezeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectralFreezeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralFreezeProcessor();
}
