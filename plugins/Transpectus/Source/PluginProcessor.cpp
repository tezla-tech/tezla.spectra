// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"

#include <tezla/ui/StateIds.hpp>
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tezla::transpectus
{

namespace
{
constexpr int kSchemaV1 = 1;
constexpr int kSchemaV2 = 2;   // added the spectrum Resolution choice
constexpr int kStateSchemaVersion = kSchemaV2;
constexpr auto kStateTypeName = "TranspectusState";

/// Where the captured reference is kept inside the state tree.
constexpr auto kReferenceChild = "reference";
constexpr auto kReferenceProperty = "curve";

/// Where an untouched peak-hold bin sits. Below the display's own floor, so an
/// unvisited bin draws off the bottom rather than as a flat line that looks
/// like a measurement.
constexpr float kSpectrumHoldFloorDb = -140.0f;
} // namespace

juce::StringArray choices::targetNames()
{
    // Generated from the engine's own table rather than written out again. Two
    // copies of an append-only list would have to be kept in step by eye, and
    // the failure -- a name pointing at the wrong number -- would look like a
    // measurement error rather than a typo.
    juce::StringArray names;

    for (const auto& target : kLoudnessTargets)
        names.add (juce::String (target.name) + "  " + juce::String (target.lufs, 0) + " LUFS");

    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout
TranspectusProcessor::createParameterLayout()
{
    using Choice     = juce::AudioParameterChoice;
    using Boolean    = juce::AudioParameterBool;
    using Parameter  = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::target, kSchemaV1 }, "Target",
        choices::targetNames(), 0));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::truePeak, kSchemaV1 }, "True Peak",
        choices::truePeak, static_cast<int> (dsp::TruePeakMode::Standard)));

    {
        juce::NormalisableRange<float> range { 60.0f, 300.0f };
        range.setSkewForCentre (120.0f);

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { ids::monoCheckHz, kSchemaV1 }, "Mono Check",
            range, 120.0f,
            Attributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float value, int)
                {
                    return juce::String (juce::roundToInt (value)) + " Hz";
                })
                .withValueFromStringFunction ([] (const juce::String& text)
                {
                    return text.getFloatValue();
                })));
    }

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::bypass, kSchemaV1 }, "Bypass", false));

    // Appended at v2, defaulting to the new Fine mode: this is an analyser --
    // the parameter changes what the screen resolves, never the audio, so an
    // old project reopens sounding identical either way and simply gains the
    // sharper bass. The Fast entry IS the original single-transform display
    // for anyone who wants it back.
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::resolution, kSchemaV2 }, "Resolution",
        choices::resolution, 2));

    return layout;
}

TranspectusProcessor::TranspectusProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
}

bool TranspectusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& main = layouts.getMainOutputChannelSet();

    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == main;
}

void TranspectusProcessor::resetSpectrumPeakHold() noexcept
{
    spectrumPeakHold_.assign (static_cast<std::size_t> (Engine::kNumBins), kSpectrumHoldFloorDb);
}

void TranspectusProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

    const int maximumBlock = std::max (maximumExpectedSamplesPerBlock, 1);
    const int channels = juce::jlimit (1, Engine::kMaxChannels,
                                       std::max (getTotalNumInputChannels(),
                                                 getTotalNumOutputChannels()));

    engine_.prepare (sampleRate_, maximumBlock, channels);
    scratch_.setSize (Engine::kMaxChannels, maximumBlock, false, true, true);

    reference_.prepare (static_cast<std::size_t> (Engine::kNumBins), sampleRate_);

    // Sized here rather than in the constructor so the bin count has exactly
    // one source. Not cleared on a later prepare: a transport stop is not a
    // reason to forget what the loudest moment was, any more than it is for the
    // true-peak hold.
    if (spectrumPeakHold_.size() != static_cast<std::size_t> (Engine::kNumBins))
        resetSpectrumPeakHold();

    // prepare() runs before any parameter is known, so this is what makes the
    // very first push take effect rather than being swallowed.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.reset();

    // Measurement only: nothing is delayed, so nothing is reported.
    setLatencySamples (0);

    prepared_ = true;
}

void TranspectusProcessor::pullParameters()
{
    const auto value = [this] (const char* id)
    {
        return static_cast<double> (state_.getRawParameterValue (id)->load());
    };

    parameters_.targetIndex = juce::jlimit (0, kNumLoudnessTargets - 1,
                                            static_cast<int> (value (ids::target)));

    parameters_.truePeak = static_cast<dsp::TruePeakMode> (
        juce::jlimit (0, 2, static_cast<int> (value (ids::truePeak))));

    parameters_.monoCheckHz = value (ids::monoCheckHz);
}

template <typename FloatType>
void TranspectusProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();

    for (int channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0)
        return;

    pullParameters();
    engine_.setParameters (parameters_);

    // Bypass on an analyser means "stop measuring", not "stop processing" --
    // there is no processing to stop. The audio is untouched either way.
    if (state_.getRawParameterValue (ids::bypass)->load() > 0.5f)
        return;

    const int channels = juce::jlimit (1, Engine::kMaxChannels,
                                       std::min (inputChannels, outputChannels));

    if (numSamples > scratch_.getNumSamples())
        scratch_.setSize (Engine::kMaxChannels, numSamples, false, true, true);

    // Into double, and into a copy: the engine takes const pointers, so it
    // could read the host buffer directly at single precision -- but the
    // measurement runs in double throughout and converting once here is what
    // makes that true.
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = scratch_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<double> (source[i]);

        channelPointers_[static_cast<std::size_t> (channel)] = destination;
    }

    engine_.process (channelPointers_.data(), channels, numSamples);

    // And the audio leaves exactly as it arrived. Nothing is written back.
}

void TranspectusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void TranspectusProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void TranspectusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    state.appendChild (abCompare_.toValueTree(), nullptr);

    // A panel preference rather than a parameter, so it rides in the state tree
    // rather than in the parameter layout -- see getTooltipsEnabled.
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    // The captured reference travels with the project. Text rather than a blob,
    // so it stays readable in a saved session file.
    if (reference_.hasCurve())
    {
        juce::ValueTree curve { kReferenceChild };
        curve.setProperty (kReferenceProperty, juce::String (reference_.toText()), nullptr);
        state.appendChild (curve, nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TranspectusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return;

    const int version = tree.getProperty ("schemaVersion", 1);

    if (version > kStateSchemaVersion)
        return;

    state_.replaceState (tree);
    abCompare_.restoreFromValueTree (tree.getChildWithName ("abCompare"));
    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);

    // A reference that fails to load leaves the plugin with none rather than
    // with half of one -- fromText refuses anything it cannot trust.
    if (const auto curve = tree.getChildWithName (kReferenceChild); curve.isValid())
        reference_.fromText (curve.getProperty (kReferenceProperty).toString().toStdString());
}

juce::AudioProcessorEditor* TranspectusProcessor::createEditor()
{
    return new TranspectusEditor (*this);
}

} // namespace tezla::transpectus

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::transpectus::TranspectusProcessor();
}
