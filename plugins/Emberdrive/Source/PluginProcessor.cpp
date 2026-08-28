// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

#include <tezla/ui/StateIds.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Exact.hpp>

namespace tezla::emberdrive
{

namespace
{
// Parameters carry the schema version they were introduced at. Existing ones
// must keep theirs forever: the version hint feeds the VST3 parameter ID, so
// bumping it on a live parameter is indistinguishable from renaming it.
//
// The original parameters were written against kStateSchemaVersion, which is
// exactly the mistake that rule exists to prevent -- every bump of the state
// version moved their IDs with it, and their hint has already travelled 1 -> 2
// -> 3 across two releases. kSchemaV1Frozen pins them where they currently sit
// so it cannot happen again.
//
// It is deliberately 3 and not 1. Setting it back to 1 would be the
// theoretically correct value and would move every one of those IDs a third
// time, breaking the projects saved against the released build. The damage is
// done; freezing where they are costs nothing and stops the bleeding. Anything
// added from here on gets its own constant, and none of them is
// kStateSchemaVersion.
constexpr int kSchemaV1Frozen = 3;
constexpr int kSchemaV2 = 2;
constexpr int kSchemaV3 = 3;
constexpr int kSchemaV4 = 4;
constexpr int kStateSchemaVersion = kSchemaV4;
constexpr auto kStateTypeName = "EmberdriveState";

/// A skew that puts the useful part of a range in the middle of the travel.
/// A drive control that does everything interesting in its first 15% is a
/// broken control, however correct its maths.
juce::NormalisableRange<float> skewedRange (float minimum, float maximum, float centre)
{
    juce::NormalisableRange<float> range { minimum, maximum };
    range.setSkewForCentre (centre);
    return range;
}

/// Formats a value the way the user thinks about it, with a unit and a sensible
/// number of decimals. Without this JUCE prints the raw float and a release
/// time reads "199.9999847", which looks broken because it is.
juce::AudioParameterFloatAttributes formatted (const juce::String& unit, int decimals)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel (unit)
        .withStringFromValueFunction ([unit, decimals] (float value, int)
        {
            return juce::String (value, decimals) + " " + unit;
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            return text.getFloatValue();
        });
}

/// Milliseconds below 10 want a decimal; above that they do not.
juce::AudioParameterFloatAttributes millisecondAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("ms")
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value < 1.0f)  return juce::String (value, 2) + " ms";
            if (value < 10.0f) return juce::String (value, 1) + " ms";
            return juce::String (juce::roundToInt (value)) + " ms";
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout EmberdriveProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using Choice    = juce::AudioParameterChoice;
    using Boolean   = juce::AudioParameterBool;
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::drive, kSchemaV1Frozen }, "Drive",
        skewedRange (0.0f, 30.0f, 12.0f), 0.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::character, kSchemaV1Frozen }, "Character",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.35f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.02f) return juce::String ("Tape");
            if (value > 0.98f) return juce::String ("Valve");
            return juce::String (juce::roundToInt (value * 100.0f)) + "% valve";
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::tone, kSchemaV1Frozen }, "Tone",
        juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (std::abs (value) < 0.02f) return juce::String ("Flat");
            return juce::String (value > 0.0f ? "Bright " : "Dark ")
                 + juce::String (juce::roundToInt (std::abs (value) * 100.0f)) + "%";
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::ceiling, kSchemaV1Frozen }, "Ceiling",
        juce::NormalisableRange<float> { -24.0f, 0.0f }, -0.3f,
        formatted ("dBFS", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::knee, kSchemaV1Frozen }, "Knee",
        juce::NormalisableRange<float> { 0.0f, 24.0f }, 6.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::speed, kSchemaV1Frozen }, "Speed",
        skewedRange (0.05f, 100.0f, 5.0f), 5.0f,
        millisecondAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::release, kSchemaV1Frozen }, "Release",
        skewedRange (20.0f, 2000.0f, 200.0f), 200.0f,
        millisecondAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::autoRelease, kSchemaV1Frozen }, "Auto Release", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::mix, kSchemaV1Frozen }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f }, 100.0f,
        formatted ("%", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1Frozen }, "Output",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::autoTrim, kSchemaV1Frozen }, "Auto Trim", true));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::oversampling, kSchemaV1Frozen }, "Oversampling",
        juce::StringArray { "Auto", "Off", "x2", "x4", "x8" }, 0));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::bypass, kSchemaV1Frozen }, "Bypass", false));

    // ---- schema version 2: mangle, multiband and expert --------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::foldAmount, kSchemaV2 }, "Fold",
        juce::NormalisableRange<float> { 0.0f, 100.0f }, 0.0f,
        formatted ("%", 0)));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::foldRange, kSchemaV2 }, "Fold Range",
        juce::StringArray { "x1", "x10", "x100" }, 0));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::multiband, kSchemaV2 }, "Multiband", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::crossoverLow, kSchemaV2 }, "Low / Mid",
        skewedRange (40.0f, 800.0f, 150.0f), 120.0f,
        formatted ("Hz", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::crossoverHigh, kSchemaV2 }, "Mid / High",
        skewedRange (800.0f, 12000.0f, 2500.0f), 2500.0f,
        formatted ("Hz", 0)));

    const auto bandDrive = [&layout] (const char* id, const juce::String& name)
    {
        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { id, kSchemaV2 }, name,
            juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
            formatted ("dB", 1)));
    };
    bandDrive (ids::bandLowDrive,  "Low Drive");
    bandDrive (ids::bandMidDrive,  "Mid Drive");
    bandDrive (ids::bandHighDrive, "High Drive");

    const auto bandState = [&layout] (const char* id, const juce::String& name)
    {
        layout.add (std::make_unique<Choice> (
            juce::ParameterID { id, kSchemaV2 }, name,
            juce::StringArray { "On", "Mute", "Solo" }, 0));
    };
    bandState (ids::bandLowState,  "Low Band");
    bandState (ids::bandMidState,  "Mid Band");
    bandState (ids::bandHighState, "High Band");

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::expertEnabled, kSchemaV2 }, "Expert", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expBias, kSchemaV2 }, "Bias",
        juce::NormalisableRange<float> { -2.0f, 2.0f }, 0.0f,
        formatted ("", 2)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expHeadBumpHz, kSchemaV2 }, "Bump Freq",
        skewedRange (40.0f, 300.0f, 90.0f), 90.0f,
        formatted ("Hz", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expHeadBumpDb, kSchemaV2 }, "Bump Gain",
        juce::NormalisableRange<float> { -6.0f, 6.0f }, 1.5f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expGapLossHz, kSchemaV2 }, "Gap Freq",
        skewedRange (2000.0f, 16000.0f, 8000.0f), 8000.0f,
        formatted ("Hz", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expGapLossDb, kSchemaV2 }, "Gap Gain",
        juce::NormalisableRange<float> { -12.0f, 6.0f }, -2.5f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expHeadroom, kSchemaV2 }, "Headroom",
        skewedRange (1.0f, 16.0f, 4.0f), 4.0f,
        formatted ("x", 2)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expDcHz, kSchemaV2 }, "DC Block",
        skewedRange (1.0f, 40.0f, 10.0f), 10.0f,
        formatted ("Hz", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expStereoLink, kSchemaV2 }, "Stereo Link",
        juce::NormalisableRange<float> { 0.0f, 100.0f }, 100.0f,
        formatted ("%", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::expDetectorRms, kSchemaV2 }, "Detector",
        juce::NormalisableRange<float> { 0.0f, 100.0f }, 0.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 1.0f)  return juce::String ("Peak");
            if (value > 99.0f) return juce::String ("RMS");
            return juce::String (juce::roundToInt (value)) + "% RMS";
        }))); 

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::expAdaa, kSchemaV2 }, "Antialiasing", true));

    // ---- schema version 3: the rest of the mangle page ---------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::rectify, kSchemaV3 }, "Rectify",
        juce::NormalisableRange<float> { 0.0f, 100.0f }, 0.0f,
        formatted ("%", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::crush, kSchemaV3 }, "Crush",
        juce::NormalisableRange<float> { 0.0f, 100.0f }, 0.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value <= 0.0f)
                return juce::String ("Off");

            // Show the depth it is actually quantising to, not the percentage.
            const double bits = 16.0 - (value / 100.0) * 15.0;
            return juce::String (bits, 1) + " bit";
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::downsample, kSchemaV3 }, "Downsample",
        skewedRange (1.0f, 64.0f, 6.0f), 1.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 1.005f)
                return juce::String ("Off");

            return juce::String (value, value < 10.0f ? 2 : 1) + " x";
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::feedback, kSchemaV3 }, "Feedback",
        juce::NormalisableRange<float> { 0.0f, 95.0f }, 0.0f,
        formatted ("%", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::feedbackTime, kSchemaV3 }, "FB Time",
        skewedRange (0.1f, 50.0f, 6.0f), 8.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            // A feedback loop repeats at its delay period, so the resonance it
            // settles on is 1/delay. Showing both saves the arithmetic -- but
            // it has to fit the readout, so no padding and no brackets.
            const juce::String time = value < 10.0f ? juce::String (value, 1) : juce::String (juce::roundToInt (value));
            return time + "ms " + juce::String (juce::roundToInt (1000.0f / value)) + "Hz";
        })));

    // ---- schema version 4: modulation ---------------------------------------
    //
    // Defined in shared/tezla-ui rather than here, because Halo declares the
    // same forty-five. Two copies of a frozen table would have to be kept in
    // step by eye, and a difference between them would read as a bug in the LFO
    // rather than as a typo in a table.
    //
    // The destination list stays here. It is the part that genuinely differs
    // between the two, and its order is this plugin's own permanent commitment.
    {
        juce::StringArray destinationNames;

        for (const auto* name : dest::displayNames)
            destinationNames.add (name);

        ui::modulation::addParameters (layout, kSchemaV4, destinationNames);
    }

    return layout;
}

EmberdriveProcessor::EmberdriveProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));

    // Resolved once, so the audio thread never looks a parameter up by string.
    // A null here would be a destination naming a parameter that does not
    // exist, which the static_assert on the table's length cannot catch.
    for (int index = 0; index < dest::count; ++index)
    {
        const auto i = static_cast<std::size_t> (index);
        destinationParameters_[i] = state_.getParameter (dest::parameterIds[i]);
        destinationRaw_[i] = state_.getRawParameterValue (dest::parameterIds[i]);

        jassert (destinationParameters_[i] != nullptr);
        jassert (destinationRaw_[i] != nullptr);
    }
}

bool EmberdriveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void EmberdriveProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    const int numChannels = juce::jlimit (1, Engine::kMaxChannels, getTotalNumOutputChannels());

    modulation_.prepare (sampleRate);

    // Base values before pullParameters(), which now reads the modulated ones.
    readBaseParameters();
    applyModulation();

    pullParameters();
    engine_.prepare (sampleRate, maximumExpectedSamplesPerBlock, numChannels);
    engine_.setParameters (parameters_);
    engine_.reset();

    scratch_.setSize (numChannels, maximumExpectedSamplesPerBlock, false, true, true);

    for (int channel = 0; channel < Engine::kMaxChannels; ++channel)
    {
        inputMeter_[channel].prepare (sampleRate);
        outputMeter_[channel].prepare (sampleRate);
    }

    dryScratch_.setSize (numChannels, maximumExpectedSamplesPerBlock, false, true, true);

    // Sized for x8 whatever factor is running, so updateLatency() -- which is
    // reached from the audio thread when the oversampling parameter moves --
    // only has to change a length.
    bypassMixer_.prepare (sampleRate, dsp::Oversampler::latencyForFactor (8),
                          juce::jmax (1, getTotalNumOutputChannels()));

    updateLatency (engine_.getLatencySamples());
}

void EmberdriveProcessor::updateLatency (int engineLatencySamples)
{
    reportedLatency_ = engineLatencySamples;
    setLatencySamples (reportedLatency_);

    // The bypass path is delayed by exactly the latency the host is told about,
    // so switching bypass does not shift the timing.
    const bool bypassed = bypassParameter_ != nullptr && bypassParameter_->get();

    // setLatency, not prepare: the latency follows the oversampling factor, and
    // the factor is a parameter -- so this runs from the audio thread. prepare()
    // allocates and is called from prepareToPlay, sized for x8.
    bypassMixer_.setLatency (reportedLatency_);
    bypassMixer_.reset (bypassed);
}

void EmberdriveProcessor::processSpan (int offset, int numSamples, int numChannels)
{
    pullParameters();

    if (engine_.setParameters (parameters_))
        updateLatency (engine_.getLatencySamples());

    double* pointers[Engine::kMaxChannels] {};
    for (int channel = 0; channel < numChannels; ++channel)
        pointers[channel] = channelPointers_[static_cast<std::size_t> (channel)] + offset;

    engine_.process (pointers, numChannels, numSamples);
}

void EmberdriveProcessor::readBaseParameters()
{
    for (int index = 0; index < dest::count; ++index)
    {
        const auto i = static_cast<std::size_t> (index);
        auto* parameter = destinationParameters_[i];

        if (parameter == nullptr || destinationRaw_[i] == nullptr)
            continue;

        // The raw value straight from where pullParameters() has always read
        // it, so an unmodulated destination hands over the identical float --
        // and the normalised one alongside, which is the only thing an offset
        // is ever added to.
        baseValues_[i]     = static_cast<double> (destinationRaw_[i]->load());
        baseNormalised_[i] = static_cast<double> (parameter->getValue());
    }
}

void EmberdriveProcessor::applyModulation()
{
    for (int index = 0; index < dest::count; ++index)
    {
        const auto i = static_cast<std::size_t> (index);
        const double offset = modulation_.offsetFor (index);

        // Exactly zero, not nearly: a destination nothing points at, or one
        // whose slot sits at the centre of its depth control, hands over the
        // base value untouched. That is what keeps every bit-exact neutral
        // setting in this plugin bit-exact once modulation exists.
        if (dsp::isExactlyZero (offset) || destinationParameters_[i] == nullptr)
        {
            destinationValues_[i] = baseValues_[i];
            continue;
        }

        const double moved = std::clamp (baseNormalised_[i] + offset, 0.0, 1.0);
        destinationValues_[i] = static_cast<double> (
            destinationParameters_[i]->convertFrom0to1 (static_cast<float> (moved)));
    }
}

void EmberdriveProcessor::updateModulationSettings()
{
    // Shared with Halo, for the same reason the parameters are: the two plugins
    // have to read forty-five identically named controls in identical units, and
    // a difference would be silent.
    ui::modulation::pushSettings (state_, modulation_, dest::count);
}

void EmberdriveProcessor::pullParameters()
{
    const auto value = [this] (const char* id)
    {
        return static_cast<double> (state_.getRawParameterValue (id)->load());
    };

    /// A modulatable control, after modulation. With nothing pointed at it this
    /// is the identical float `value()` would have returned, which is what makes
    /// the unmodulated plugin bit-identical to the one before this existed.
    const auto moved = [this] (int destination)
    {
        return destinationValues_[static_cast<std::size_t> (destination)];
    };

    parameters_.driveDb     = moved (dest::drive);
    parameters_.character   = moved (dest::character);
    parameters_.toneTilt    = moved (dest::tone);
    parameters_.ceilingDb   = moved (dest::ceiling);
    parameters_.kneeDb      = moved (dest::knee);
    parameters_.attackMs    = moved (dest::speed);
    parameters_.releaseMs   = moved (dest::release);
    parameters_.autoRelease = value (ids::autoRelease) > 0.5;
    parameters_.mix         = moved (dest::mix) / 100.0;
    parameters_.outputDb    = moved (dest::output);
    parameters_.autoTrim    = value (ids::autoTrim) > 0.5;

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (value (ids::oversampling))));

    // ---- mangle ----------------------------------------------------------
    parameters_.foldAmount = moved (dest::foldAmount) / 100.0;

    static constexpr double kFoldRanges[] = { 1.0, 10.0, 100.0 };
    parameters_.foldRange = kFoldRanges[juce::jlimit (0, 2, static_cast<int> (value (ids::foldRange)))];

    // ---- multiband -------------------------------------------------------
    parameters_.multiband       = value (ids::multiband) > 0.5;
    parameters_.crossoverLowHz  = moved (dest::crossoverLow);
    parameters_.crossoverHighHz = moved (dest::crossoverHigh);

    const int bandDriveDests[kNumBands] { dest::bandLowDrive, dest::bandMidDrive, dest::bandHighDrive };
    const char* bandStateIds[kNumBands] { ids::bandLowState, ids::bandMidState, ids::bandHighState };

    for (int band = 0; band < kNumBands; ++band)
    {
        const auto b = static_cast<std::size_t> (band);
        parameters_.bands[b].driveTrimDb = moved (bandDriveDests[band]);
        parameters_.bands[b].state = static_cast<BandState> (
            juce::jlimit (0, 2, static_cast<int> (value (bandStateIds[band]))));
    }

    // ---- expert ----------------------------------------------------------
    auto& expert = parameters_.expert;
    expert.enabled        = value (ids::expertEnabled) > 0.5;
    expert.bias           = moved (dest::expBias);
    expert.headBumpHz     = moved (dest::expHeadBumpHz);
    expert.headBumpDb     = moved (dest::expHeadBumpDb);
    expert.gapLossHz      = moved (dest::expGapLossHz);
    expert.gapLossDb      = moved (dest::expGapLossDb);
    expert.shaperHeadroom = moved (dest::expHeadroom);
    expert.dcBlockerHz    = moved (dest::expDcHz);
    expert.stereoLink     = moved (dest::expStereoLink) / 100.0;
    expert.detectorRms    = moved (dest::expDetectorRms) / 100.0;
    expert.adaaEnabled    = value (ids::expAdaa) > 0.5;

    // ---- the rest of mangle ----------------------------------------------
    parameters_.rectify    = moved (dest::rectify) / 100.0;
    parameters_.crush      = moved (dest::crush) / 100.0;
    parameters_.downsample = moved (dest::downsample);
    parameters_.feedback   = moved (dest::feedback) / 100.0;
    parameters_.feedbackMs = moved (dest::feedbackTime);
}

void EmberdriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void EmberdriveProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

template <typename FloatType>
void EmberdriveProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), Engine::kMaxChannels);

    for (int channel = numChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (scratch_.getNumSamples() < numSamples || scratch_.getNumChannels() < numChannels)
    {
        scratch_.setSize (numChannels, numSamples, false, true, true);
        dryScratch_.setSize (numChannels, numSamples, false, true, true);
    }

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* destination = scratch_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<double> (source[i]);

        inputMeter_[channel].processBlock (destination, numSamples);

        // The bypass path needs the input as doubles and untouched, and the
        // engine is about to overwrite `destination` in place.
        std::copy (destination, destination + numSamples, dryScratch_.getWritePointer (channel));

        channelPointers_[static_cast<std::size_t> (channel)] = destination;
        dryPointers_[static_cast<std::size_t> (channel)] = dryScratch_.getReadPointer (channel);
    }

    // ---- parameters, and modulation if there is any -------------------------
    //
    // Read here rather than at the top of the block, because the level follower
    // needs the dry input and the dry copy is only made above.
    readBaseParameters();
    updateModulationSettings();

    if (! modulation_.isActive())
    {
        // The path the plugin has always taken: one push, one call. Nothing
        // here rounds, converts or re-derives anything, so with nothing
        // assigned the output is what it was before modulation existed --
        // byte for byte, and there is a test that says so.
        applyModulation();
        processSpan (0, numSamples, numChannels);
    }
    else
    {
        // Short spans, so a source moving at 20 Hz arrives as a curve rather
        // than as a staircase.
        const auto position = getPlayHead() != nullptr ? getPlayHead()->getPosition()
                                                       : juce::nullopt;

        const bool hasTransport = position.hasValue() && position->getBpm().hasValue()
                               && position->getPpqPosition().hasValue()
                               && *position->getBpm() > 0.0;

        const double ppqStart = hasTransport ? *position->getPpqPosition() : 0.0;

        // How far the transport moves per sample, so a source stays locked to
        // the grid *within* a block and not only at its edges.
        const double ppqPerSample = hasTransport
            ? *position->getBpm() / (60.0 * sampleRate_) : 0.0;

        for (int offset = 0; offset < numSamples; offset += kModulationChunkSamples)
        {
            const int span = juce::jmin (kModulationChunkSamples, numSamples - offset);

            const double* inputs[Engine::kMaxChannels] {};
            for (int channel = 0; channel < numChannels; ++channel)
                inputs[channel] = dryPointers_[static_cast<std::size_t> (channel)] + offset;

            modulation_.advance (span, inputs, numChannels, hasTransport,
                                 ppqStart + ppqPerSample * offset);

            applyModulation();
            processSpan (offset, span, numChannels);
        }
    }

    // Latency-matched, crossfaded, and shared by every plugin so there is one
    // copy of this to get right. See BypassMixer.hpp for what it used to do.
    bypassMixer_.setBypassed (bypassParameter_ != nullptr && bypassParameter_->get());
    bypassMixer_.process (channelPointers_.data(), dryPointers_.data(), numChannels, numSamples);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* processed = scratch_.getReadPointer (channel);
        auto* destination = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<FloatType> (processed[i]);

        // Metered after the bypass mix, so the meters show what is being heard
        // rather than what the engine produced and the bypass then discarded.
        outputMeter_[channel].processBlock (processed, numSamples);
    }


    meters_.inputVuDb.store    (static_cast<float> (inputMeter_[0].getVuDb()),    std::memory_order_relaxed);
    meters_.inputPeakDb.store  (static_cast<float> (inputMeter_[0].getPeakDb()),  std::memory_order_relaxed);
    meters_.outputVuDb.store   (static_cast<float> (outputMeter_[0].getVuDb()),   std::memory_order_relaxed);
    meters_.outputPeakDb.store (static_cast<float> (outputMeter_[0].getPeakDb()), std::memory_order_relaxed);
    meters_.gainReductionDb.store (static_cast<float> (engine_.getGainReductionDb()), std::memory_order_relaxed);

    for (int band = 0; band < kNumBands; ++band)
        meters_.bandGainReductionDb[static_cast<std::size_t> (band)].store (
            static_cast<float> (engine_.getBandGainReductionDb (band)), std::memory_order_relaxed);
}

juce::String EmberdriveProcessor::describeOversampling() const
{
    // Derived from the mode and the host's rate rather than read back from the
    // engine. Before prepareToPlay the engine still holds its defaults, and
    // reporting those told a 44.1 kHz session that oversampling was off when
    // Auto would in fact run x4.
    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    if (hostRate <= 0.0)
        return "Waiting for the host to report its sample rate.";

    const auto mode = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (state_.getRawParameterValue (ids::oversampling)->load())));

    const int factor = dsp::oversamplingFactor (mode, hostRate);
    const double internalRate = hostRate * factor;
    const int latency = dsp::Oversampler::latencyForFactor (factor);

    const auto rateText = [] (double rate)
    {
        return juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " kHz";
    };

    juce::String description;
    description << "Your session is at " << rateText (hostRate) << ". ";

    if (factor == 1 && mode == dsp::OversamplingMode::Auto)
        description << "Auto is running x1: at " << rateText (hostRate)
                    << " there is already room above the audio band for the harmonics, "
                    << "so it spends no CPU and adds no latency.";
    else if (factor == 1)
        description << "Oversampling is off. Saturation runs at the session rate, which saves "
                    << "CPU and gives up about 25 dB of alias rejection at high drive.";
    else
        description << (mode == dsp::OversamplingMode::Auto ? "Auto is running x" : "Running x")
                    << factor << ", saturating at " << rateText (internalRate)
                    << " internally so the harmonics land on real frequencies instead of folding "
                    << "back. Costs " << juce::String (1000.0 * latency / hostRate, 2)
                    << " ms of latency, which the host compensates.";

    return description;
}

void EmberdriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    // Versioned, so a future layout change can migrate old projects rather than
    // silently resetting them.
    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    state.appendChild (abCompare_.toValueTree(), nullptr);

    // A panel preference rather than a parameter, so it rides in the state tree
    // rather than in the parameter layout -- see getTooltipsEnabled.
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EmberdriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    // Version 1 projects predate the mangle, multiband and expert parameters;
    // version 2 predates rectify, crush, downsample and feedback. Nothing to
    // migrate in either case: every added parameter defaults to neutral, so an
    // older project reopens sounding exactly as it did. A version from the
    // future is refused rather than half-loaded.
    const int version = tree.getProperty ("schemaVersion", 1);
    if (version > kStateSchemaVersion)
        return;

    state_.replaceState (tree);
    abCompare_.restoreFromValueTree (tree.getChildWithName ("abCompare"));
    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);
}

namespace
{
/// Factory presets. Small and opinionated, aimed at the work this plugin exists
/// for rather than at filling a list. Each one is a full parameter set, so
/// loading a preset never leaves a stale control behind.
struct Preset
{
    const char* name;
    Parameters  parameters;

    /// What, if anything, is moving. Default-constructed means nothing is --
    /// which is what every preset written before modulation existed carries, and
    /// what stops one of them leaving the last patch's assignments behind.
    ui::modulation::Settings modulation {};
};

const Preset& presetAt (int index)
{
    static const Preset presets[] = {
        // Proof it can get out of the way. If this does not sound like bypass,
        // something is wrong.
        { "Clean",
          [] { Parameters p; p.driveDb = 0.0;  p.character = 0.0;  p.toneTilt = 0.0;
               p.ceilingDb = 0.0;  p.kneeDb = 0.0;  p.attackMs = 10.0; p.releaseMs = 200.0;
               p.mix = 1.0; p.outputDb = 0.0; p.autoTrim = true; return p; }() },

        // Slow attack so the front of every hit survives; wide knee so it is
        // always gently working rather than grabbing.
        { "Drum bus",
          [] { Parameters p; p.driveDb = 9.0;  p.character = 0.45; p.toneTilt = 0.15;
               p.ceilingDb = -1.0; p.kneeDb = 9.0;  p.attackMs = 18.0; p.releaseMs = 140.0;
               p.autoRelease = true; p.mix = 0.75; p.outputDb = 0.0; p.autoTrim = true; return p; }() },

        // Harmonics so a 40 Hz fundamental survives a speaker that cannot
        // reproduce it. Tape end, because odd harmonics track the fundamental
        // and even ones can muddy an octave up.
        { "Sub bass",
          [] { Parameters p; p.driveDb = 14.0; p.character = 0.15; p.toneTilt = -0.2;
               p.ceilingDb = -0.5; p.kneeDb = 4.0;  p.attackMs = 3.0;  p.releaseMs = 320.0;
               p.autoRelease = true; p.mix = 1.0; p.outputDb = 0.0; p.autoTrim = true; return p; }() },

        // The reason this plugin exists.
        { "Reese",
          [] { Parameters p; p.driveDb = 22.0; p.character = 0.8;  p.toneTilt = 0.25;
               p.ceilingDb = -0.3; p.kneeDb = 12.0; p.attackMs = 1.0;  p.releaseMs = 90.0;
               p.mix = 1.0; p.outputDb = 0.0; p.autoTrim = true; return p; }() },

        // The gentlest useful setting: barely working, but the mix sits together.
        { "Mix glue",
          [] { Parameters p; p.driveDb = 4.0;  p.character = 0.3;  p.toneTilt = 0.0;
               p.ceilingDb = -0.3; p.kneeDb = 18.0; p.attackMs = 30.0; p.releaseMs = 400.0;
               p.autoRelease = true; p.mix = 1.0; p.outputDb = 0.0; p.autoTrim = true; return p; }() },

        // Multiband doing the job it exists for: the sub band left alone while
        // everything above it is driven hard.
        { "Clean sub, dirty top",
          [] { Parameters p; p.driveDb = 15.0; p.character = 0.5;  p.toneTilt = 0.1;
               p.ceilingDb = -0.5; p.kneeDb = 8.0;  p.attackMs = 6.0;  p.releaseMs = 180.0;
               p.autoRelease = true; p.mix = 1.0; p.autoTrim = true;
               p.multiband = true; p.crossoverLowHz = 130.0; p.crossoverHighHz = 2200.0;
               p.bands[0].driveTrimDb = -18.0;
               p.bands[1].driveTrimDb =  6.0;
               p.bands[2].driveTrimDb =  0.0;
               return p; }() },

        // The folder at a musical setting: metallic and hollow, still tracking
        // the note. Range x1, because on a sustained bass x10 already stops
        // sounding like the note you played.
        { "Folded reese",
          [] { Parameters p; p.driveDb = 8.0;  p.character = 0.7;  p.toneTilt = 0.2;
               p.ceilingDb = -0.3; p.kneeDb = 10.0; p.attackMs = 2.0;  p.releaseMs = 120.0;
               p.mix = 1.0; p.autoTrim = true;
               p.foldAmount = 0.45; p.foldRange = 1.0;
               return p; }() },

        // Schizo. Everything at once, on purpose.
        { "Annihilate",
          [] { Parameters p; p.driveDb = 24.0; p.character = 0.9;  p.toneTilt = 0.3;
               p.ceilingDb = -0.3; p.kneeDb = 3.0;  p.attackMs = 0.5;  p.releaseMs = 60.0;
               p.mix = 1.0; p.autoTrim = true;
               p.foldAmount = 1.0; p.foldRange = 10.0;
               p.multiband = true; p.crossoverLowHz = 110.0; p.crossoverHighHz = 1800.0;
               p.bands[0].driveTrimDb = -12.0;   // even here, the sub survives
               p.bands[1].driveTrimDb =   6.0;
               p.bands[2].driveTrimDb =   0.0;
               return p; }() },

        // The XP-era bitcrusher, near enough. Rate reduction into bit
        // reduction, no saturation to speak of, mixed in parallel so the
        // bottom end survives.
        { "Bitcrush",
          [] { Parameters p; p.driveDb = 3.0;  p.character = 0.2;  p.toneTilt = 0.1;
               p.ceilingDb = -0.3; p.kneeDb = 2.0;  p.attackMs = 1.0;  p.releaseMs = 80.0;
               p.mix = 0.8; p.autoTrim = true;
               p.crush = 0.55; p.downsample = 7.0;
               return p; }() },

        // Octave-up ghost over the original, for mid-bass and leads.
        { "Octave ghost",
          [] { Parameters p; p.driveDb = 10.0; p.character = 0.6;  p.toneTilt = 0.15;
               p.ceilingDb = -0.3; p.kneeDb = 6.0;  p.attackMs = 4.0;  p.releaseMs = 150.0;
               p.mix = 0.6; p.autoTrim = true;
               p.rectify = 0.75;
               return p; }() },

        // The loop doing what it does. Short delay, high feedback: the plugin
        // sustains and rings on after the note has gone.
        { "Screamer",
          [] { Parameters p; p.driveDb = 16.0; p.character = 0.8;  p.toneTilt = 0.2;
               p.ceilingDb = -0.5; p.kneeDb = 8.0;  p.attackMs = 2.0;  p.releaseMs = 100.0;
               p.mix = 1.0; p.autoTrim = true;
               p.feedback = 0.72; p.feedbackMs = 3.2;
               p.foldAmount = 0.3; p.foldRange = 1.0;
               return p; }() },

        // ---- modulation -----------------------------------------------------
        //
        // Appended, never inserted, for the same reason a parameter ID is: a
        // host stores which program is selected as an *index*, so putting these
        // anywhere but the end would silently repoint every saved preset choice.

        // A saturator that bites harder the harder it is hit. Static drive gives
        // you one answer for the whole performance; this gives the hits their
        // own. Fast attack so it lands on the transient, long release so it does
        // not chatter between hits in a break.
        { "Mod: drive follows",
          [] { Parameters p; p.driveDb = 6.0;  p.character = 0.5;  p.toneTilt = 0.12;
               p.ceilingDb = -1.0; p.kneeDb = 9.0;  p.attackMs = 18.0; p.releaseMs = 140.0;
               p.autoRelease = true; p.mix = 0.85; p.autoTrim = true; return p; }(),
          [] { ui::modulation::Settings m;
               m.envAttackMs = 4.0;
               m.envReleaseMs = 260.0;
               m.envSensitivityDb = -10.0;
               m.slots[0] = { 4, dest::drive, 0.30 };
               return m; }() },

        // The same follower with the sign flipped, which is the half nobody
        // expects: clean on the peaks and dirty underneath them. A compressor
        // made of harmonics rather than of gain -- the loud parts stay open and
        // the quiet parts fill in.
        { "Mod: reverse bite",
          [] { Parameters p; p.driveDb = 20.0; p.character = 0.75; p.toneTilt = 0.2;
               p.ceilingDb = -0.3; p.kneeDb = 12.0; p.attackMs = 1.5;  p.releaseMs = 110.0;
               p.mix = 1.0; p.autoTrim = true; return p; }(),
          [] { ui::modulation::Settings m;
               m.envAttackMs = 2.0;
               m.envReleaseMs = 160.0;
               m.envSensitivityDb = -8.0;
               m.slots[0] = { 4, dest::drive, -0.35 };
               return m; }() },

        // The folder on the grid. Fold is the control whose character changes
        // most across its travel, so sweeping it on the eighth turns one setting
        // into a phrase. Triangle rather than sine: the turnaround is where the
        // interesting part is, and a triangle spends less time sitting in it.
        { "Mod: fold wobble",
          [] { Parameters p; p.driveDb = 8.0;  p.character = 0.65; p.toneTilt = 0.18;
               p.ceilingDb = -0.3; p.kneeDb = 10.0; p.attackMs = 2.0;  p.releaseMs = 120.0;
               p.mix = 1.0; p.autoTrim = true;
               p.foldAmount = 0.25; p.foldRange = 1.0;
               return p; }(),
          [] { ui::modulation::Settings m;
               m.lfos[0].wave = 1;          // triangle
               m.lfos[0].sync = true;
               m.lfos[0].division = 6;      // an eighth
               m.lfos[0].smooth = 0.2;
               m.slots[0] = { 1, dest::foldAmount, 0.35 };
               return m; }() },
    };

    return presets[juce::jlimit (0, static_cast<int> (std::size (presets)) - 1, index)];
}

constexpr int kNumPresets = 14;
} // namespace

int EmberdriveProcessor::getNumPrograms() { return kNumPresets; }

const juce::String EmberdriveProcessor::getProgramName (int index)
{
    return presetAt (index).name;
}

void EmberdriveProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumPresets)
        return;

    currentProgram_ = index;
    const auto& preset = presetAt (index).parameters;

    // Written through the parameters rather than straight into the engine, so
    // the host sees the change, the editor follows, and undo works.
    const auto set = [this] (const char* id, float value)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    set (ids::drive,     static_cast<float> (preset.driveDb));
    set (ids::character, static_cast<float> (preset.character));
    set (ids::tone,      static_cast<float> (preset.toneTilt));
    set (ids::ceiling,   static_cast<float> (preset.ceilingDb));
    set (ids::knee,      static_cast<float> (preset.kneeDb));
    set (ids::speed,     static_cast<float> (preset.attackMs));
    set (ids::release,   static_cast<float> (preset.releaseMs));
    set (ids::mix,       static_cast<float> (preset.mix * 100.0));
    set (ids::output,    static_cast<float> (preset.outputDb));
    set (ids::autoRelease, preset.autoRelease ? 1.0f : 0.0f);
    set (ids::autoTrim,    preset.autoTrim    ? 1.0f : 0.0f);

    // Schema version 2 parameters. A preset is a complete parameter set, so
    // these are written even when the preset leaves them neutral -- otherwise
    // loading a preset would inherit whatever the last one left behind.
    set (ids::foldAmount, static_cast<float> (preset.foldAmount * 100.0));
    set (ids::foldRange,  preset.foldRange >= 100.0 ? 2.0f : (preset.foldRange >= 10.0 ? 1.0f : 0.0f));

    set (ids::multiband,     preset.multiband ? 1.0f : 0.0f);
    set (ids::crossoverLow,  static_cast<float> (preset.crossoverLowHz));
    set (ids::crossoverHigh, static_cast<float> (preset.crossoverHighHz));

    const char* bandDriveIds[kNumBands] { ids::bandLowDrive, ids::bandMidDrive, ids::bandHighDrive };
    const char* bandStateIds[kNumBands] { ids::bandLowState, ids::bandMidState, ids::bandHighState };

    for (int band = 0; band < kNumBands; ++band)
    {
        const auto b = static_cast<std::size_t> (band);
        set (bandDriveIds[band], static_cast<float> (preset.bands[b].driveTrimDb));
        set (bandStateIds[band], static_cast<float> (static_cast<int> (preset.bands[b].state)));
    }

    set (ids::rectify,      static_cast<float> (preset.rectify * 100.0));
    set (ids::crush,        static_cast<float> (preset.crush * 100.0));
    set (ids::downsample,   static_cast<float> (preset.downsample));
    set (ids::feedback,     static_cast<float> (preset.feedback * 100.0));
    set (ids::feedbackTime, static_cast<float> (preset.feedbackMs));

    // Presets never turn the expert panel on: it exists for deliberate hands-on
    // work, and a preset silently overriding Character would be a surprise.
    set (ids::expertEnabled, 0.0f);

    // Every preset writes all forty-five, including the ones that leave them
    // neutral. A preset is a complete parameter set or it is a trap: without
    // this, loading "Clean" after a modulated patch would leave the LFOs still
    // driving whatever they were pointed at.
    ui::modulation::applyPreset (state_, presetAt (index).modulation);
}

juce::AudioProcessorEditor* EmberdriveProcessor::createEditor()
{
    return new EmberdriveEditor (*this);
}

} // namespace tezla::emberdrive

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::emberdrive::EmberdriveProcessor();
}
