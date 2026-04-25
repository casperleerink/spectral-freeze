#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/FreezeProcessor.h"
#include "dsp/OrganicProcessor.h"
#include "dsp/SidechainProcessor.h"
#include "dsp/SpectralFilter.h"
#include "dsp/StftProcessor.h"

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
    organicParam      = apvts.getRawParameterValue (organicParamID);
    jassert (freezeParam != nullptr && filterParam != nullptr
             && scBoostParam != nullptr && scFreqSmoothParam != nullptr
             && organicParam != nullptr);
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

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { organicParamID, 1 },
        "Organic",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.0f));

    return layout;
}

void SpectralFreezeProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
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
    {
        ch.phaseRng.setSeedRandomly();
        for (int b = 0; b < organicAmBands; ++b)
        {
            ch.organicAm.value[(size_t) b]  = 0.0f;
            ch.organicAm.target[(size_t) b] = ch.phaseRng.nextFloat() * 2.0f - 1.0f;
        }
        ch.organicAm.hopCounter = 0;
    }

    for (int k = 0; k < numBins; ++k)
        phaseAdvance[(size_t) k] = juce::MathConstants<float>::twoPi
                                 * (float) k * (float) hopSize / (float) fftSize;

    scLatestMag.fill (0.0f);
    scSmoothedMag.fill (0.0f);

    // Let the sidechain resonance release musically instead of dropping after a
    // few STFT hops. This is an exponential decay measured in real time, so it
    // remains similar at different host sample rates.
    constexpr float sidechainReleaseSeconds = 0.75f;
    const double safeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    scRetentionPerHop = std::exp (-static_cast<float> (hopSize / (safeSampleRate * sidechainReleaseSeconds)));

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
            data[n] = spectral::pushSampleAndPopOutput (st.stft, data[n]);
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
                {
                    auto& sc = scChannels[(size_t) ch];
                    spectral::analyseSidechainHop (sc.inputFifo, sc.fifoPos, sc.fftScratch,
                                                   window, fft, scLatestMag);
                }

                // Time-smooth per-bin magnitude. Keeps held notes "ringing" in the
                // mask after they've stopped, matching the sympathetic-resonance feel.
                for (int k = 0; k < numBins; ++k)
                    scSmoothedMag[(size_t) k]
                        = scRetentionPerHop * scSmoothedMag[(size_t) k]
                        + (1.0f - scRetentionPerHop) * scLatestMag[(size_t) k];
            }

            for (int ch = 0; ch < mainChannels; ++ch)
            {
                auto& st = channels[(size_t) ch];
                processFrame (st, runSidechain);
                spectral::overlapAddScratchToOutput (st.stft);
            }
        }
    }

}

void SpectralFreezeProcessor::processFrame (ChannelState& st, bool applySidechain)
{
    const bool  freezeOn   = freezeParam->load() >= 0.5f;
    const float filterAmt  = juce::jlimit (0.0f, 1.0f, filterParam->load());
    const float organicAmt = juce::jlimit (0.0f, 1.0f, organicParam->load());

    spectral::copyInputFrameToScratch (st.stft);

    // --- Analysis: window + forward FFT ---------------------------------------------------
    // We still analyse even when frozen on the first frame of the edge, so we can
    // capture a fresh magnitude/phase snapshot. After that we skip analysis entirely.
    const bool fifoPrimed = st.stft.samplesSeen >= fftSize;
    const bool captureEdge = spectral::shouldCaptureFreezeEdge (st.freeze, freezeOn, fifoPrimed);
    const bool runAnalysis = spectral::shouldRunFreezeAnalysis (st.freeze, freezeOn, captureEdge);

    if (runAnalysis)
    {
        spectral::applyWindow (st.stft.fftScratch.data(), window);

        fft.performRealOnlyForwardTransform (st.stft.fftScratch.data());

        spectral::recordAnalysisFrame (st.freeze, st.stft.fftScratch.data(), phaseAdvance);
    }

    // --- Freeze memory --------------------------------------------------------------------
    if (captureEdge)
        spectral::captureFreezeFrame (st.freeze, st.stft.fftScratch.data());

    // If the host reconfigures/enables the sidechain while Freeze is already on,
    // prepareToPlay() can reset our STFT state. In that case there is no valid
    // frozen spectrum yet; leave the analysed edge frame intact instead of
    // resynthesising an all-zero/garbage freeze frame, which caused the chopped
    // "no tone" failure when attaching a sidechain to an active freeze.
    // --- Spectral processing --------------------------------------------------------------
    if (freezeOn && st.freeze.hasFrozenFrame)
        spectral::resynthesiseFrozenFrame (st.freeze, st.organicAm, st.phaseRng,
                                           st.stft.fftScratch.data(), organicAmt);

    // --- Magnitude-threshold filter ------------------------------------------------------
    // Gate bins below a per-frame threshold. The threshold is derived from `filterAmt`
    // and the frame's peak magnitude — see applySpectralFilter below.
    spectral::applyMagnitudeThresholdFilter (st.stft.fftScratch.data(), filterAmt);

    // --- Organic macro -------------------------------------------------------------------
    // Adds a little life back after strong spectral filtering: bin drift is handled
    // in the freeze phase path above; here we add spectral softening plus a shaped,
    // decorrelated residual floor so filtered frames don't collapse into pure sines.
    spectral::applyOrganicSpectralProcessing (st.stft.fftScratch.data(), st.phaseRng, organicAmt, filterAmt);

    // --- Sidechain mask ------------------------------------------------------------------
    // Runs AFTER freeze resynthesis so a frozen cloud can be sculpted by a live sidechain.
    if (applySidechain)
    {
        const float scBoostDb    = juce::jlimit (0.0f, 18.0f, scBoostParam->load());
        const float scFreqSmooth = juce::jlimit (0.0f, 1.0f, scFreqSmoothParam->load());
        spectral::applySidechainEnhancement (st.stft.fftScratch.data(), scSmoothedMag,
                                             scBoostDb, scFreqSmooth);
    }

    // --- Rebuild conjugate mirror so the inverse FFT returns a purely real signal --------
    spectral::rebuildConjugateMirror (st.stft.fftScratch.data());

    // --- Synthesis: inverse FFT + window + COLA gain -------------------------------------
    fft.performRealOnlyInverseTransform (st.stft.fftScratch.data());

    spectral::applyOrganicSaturation (st.stft.fftScratch.data(), organicAmt);

    spectral::applySynthesisWindow (st.stft.fftScratch.data(), window, windowGain);

    st.freeze.wasFrozen = freezeOn;
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
