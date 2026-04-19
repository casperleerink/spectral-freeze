#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralFreezeProcessor::SpectralFreezeProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    gainParam   = apvts.getRawParameterValue (gainParamID);
    freezeParam = apvts.getRawParameterValue (freezeParamID);
    filterParam = apvts.getRawParameterValue (filterParamID);
    jassert (gainParam != nullptr && freezeParam != nullptr && filterParam != nullptr);
}

SpectralFreezeProcessor::~SpectralFreezeProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
SpectralFreezeProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { gainParamID, 1 },
        "Gain",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.01f },
        0.0f));

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

    return layout;
}

void SpectralFreezeProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    smoothedGain.reset (sampleRate, 0.02); // 20 ms ramp
    const float initialDb = gainParam != nullptr ? gainParam->load() : 0.0f;
    smoothedGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (initialDb));

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

    // Per-bin phase advance across one hop, wrapped to (-π, π] to stay numerically tight.
    for (int k = 0; k < numBins; ++k)
    {
        const float raw = juce::MathConstants<float>::twoPi * (float) k * (float) hopSize / (float) fftSize;
        phaseAdvance[(size_t) k] = std::remainder (raw, juce::MathConstants<float>::twoPi);
    }

    // One STFT state per input channel. Fresh zeros on each prepareToPlay.
    channels.assign ((size_t) getTotalNumInputChannels(), ChannelState{});

    // Report streaming-STFT latency so hosts align bypass/compare. Impulse-response
    // proves: input at t=0 reaches output at t=fftSize.
    setLatencySamples (fftSize);
}

void SpectralFreezeProcessor::releaseResources() {}

bool SpectralFreezeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return mainOut == layouts.getMainInputChannelSet();
}

void SpectralFreezeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    smoothedGain.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Streaming STFT: push input into ring buffer, pop overlap-added output,
    // fire an FFT round-trip every `hopSize` samples.
    for (int ch = 0; ch < numChannels && ch < (int) channels.size(); ++ch)
    {
        auto& st = channels[(size_t) ch];
        auto* data = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            st.inputFifo[(size_t) st.fifoPos]  = data[n];
            data[n]                            = st.outputFifo[(size_t) st.fifoPos];
            st.outputFifo[(size_t) st.fifoPos] = 0.0f; // consume — frees slot for next OLA
            st.fifoPos = (st.fifoPos + 1) % fftSize;

            if (++st.hopCounter >= hopSize)
            {
                st.hopCounter = 0;
                processFrame (st);
                // Overlap-add the just-synthesised frame into outputFifo starting at
                // the current fifoPos (which is the "oldest" slot from the read side).
                for (int i = 0; i < fftSize; ++i)
                    st.outputFifo[(size_t) ((st.fifoPos + i) % fftSize)]
                        += st.fftScratch[(size_t) i];
            }
        }
    }

    // Apply post-STFT gain. Smoother ramps per-sample to prevent zipper noise.
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = smoothedGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[n] *= g;
    }
}

void SpectralFreezeProcessor::processFrame (ChannelState& st)
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
    }

    // --- Freeze memory --------------------------------------------------------------------
    if (captureEdge)
    {
        // Snapshot magnitude and phase for every non-negative bin; these drive resynthesis
        // for as long as freeze stays engaged.
        for (int k = 0; k < numBins; ++k)
        {
            const float re = st.fftScratch[(size_t) (2 * k)];
            const float im = st.fftScratch[(size_t) (2 * k + 1)];
            st.frozenMag  [(size_t) k] = std::sqrt (re * re + im * im);
            st.frozenPhase[(size_t) k] = std::atan2 (im, re);
        }
    }

    // --- Spectral processing --------------------------------------------------------------
    if (freezeOn)
    {
        // Rebuild the spectrum from stored magnitudes, advancing each bin's phase by its
        // natural hop increment. This keeps partials rotating like they would in the
        // original signal, so sustained tones stay musical instead of buzzing.
        for (int k = 0; k < numBins; ++k)
        {
            st.frozenPhase[(size_t) k] += phaseAdvance[(size_t) k];
            // Wrap to stay in a comfortable range for std::cos/std::sin accuracy.
            if (st.frozenPhase[(size_t) k] > juce::MathConstants<float>::pi)
                st.frozenPhase[(size_t) k] -= juce::MathConstants<float>::twoPi;
            else if (st.frozenPhase[(size_t) k] < -juce::MathConstants<float>::pi)
                st.frozenPhase[(size_t) k] += juce::MathConstants<float>::twoPi;

            const float mag   = st.frozenMag  [(size_t) k];
            const float phase = st.frozenPhase[(size_t) k];
            st.fftScratch[(size_t) (2 * k)]     = mag * std::cos (phase);
            st.fftScratch[(size_t) (2 * k + 1)] = mag * std::sin (phase);
        }
    }

    // --- Magnitude-threshold filter ------------------------------------------------------
    // Gate bins below a per-frame threshold. The threshold is derived from `filterAmt`
    // and the frame's peak magnitude — see applySpectralFilter below.
    applySpectralFilter (st.fftScratch.data(), filterAmt);

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
