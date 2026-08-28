// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include <tezla/ui/StateIds.hpp>
#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Exact.hpp>

namespace tezla::halo
{

namespace
{
// Every parameter carries the schema version it was introduced at, and keeps it
// forever: the version hint feeds the VST3 parameter ID, so moving it on a live
// parameter is indistinguishable from renaming it. That is why these are
// separate constants and why nothing below reaches for kStateSchemaVersion --
// bumping the state version must never move an existing parameter's ID.
constexpr int kSchemaV1 = 1;
constexpr int kSchemaV2 = 2;
constexpr int kSchemaV3 = 3;
constexpr int kSchemaV4 = 4;
constexpr int kStateSchemaVersion = kSchemaV4;
constexpr auto kStateTypeName = "HaloState";

/// A skew that puts the useful part of a range in the middle of the travel. A
/// Focus control that spends nine tenths of its travel above 8 kHz is a broken
/// control, however correct its maths.
juce::NormalisableRange<float> skewedRange (float minimum, float maximum, float centre)
{
    juce::NormalisableRange<float> range { minimum, maximum };
    range.setSkewForCentre (centre);
    return range;
}

/// Formats a value the way the user thinks about it. Without this JUCE prints
/// the raw float and a frequency reads "2999.99878", which looks broken
/// because it is.
juce::AudioParameterFloatAttributes formatted (const juce::String& unit, int decimals)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel (unit)
        .withStringFromValueFunction ([unit, decimals] (float value, int)
        {
            return juce::String (value, decimals) + " " + unit;
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

juce::AudioParameterFloatAttributes hertzAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("Hz")
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value >= 1000.0f)
                return juce::String (value / 1000.0f, value < 10000.0f ? 2 : 1) + " kHz";
            return juce::String (juce::roundToInt (value)) + " Hz";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const float value = text.getFloatValue();
            return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
        });
}

/// A harmonic level. Shown in decibels because that is how a recipe is read,
/// with a real Off at the bottom rather than a very small number -- the
/// generator is exactly the zero function when every level is zero, and the
/// display should not imply otherwise.
juce::AudioParameterFloatAttributes harmonicAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("dB")
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value <= 0.0f)
                return juce::String ("Off");

            // Snapped, or unity comes back from a skewed range as 0.99999 and
            // prints "-0.0 dB" -- which reads as a bug in the plugin rather than
            // as the last digit of a float.
            const auto db = juce::Decibels::gainToDecibels (value, -100.0f);
            return juce::String (std::abs (db) < 0.05f ? 0.0f : db, 1) + " dB";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            if (trimmed.startsWithIgnoreCase ("off"))
                return 0.0f;

            return juce::jlimit (0.0f, 10.0f,
                juce::Decibels::decibelsToGain (trimmed.getFloatValue(), -100.0f));
        });
}

juce::AudioParameterFloatAttributes percentAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("%")
        .withStringFromValueFunction ([] (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            return text.getFloatValue() * 0.01f;
        });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout HaloProcessor::createParameterLayout()
{
    using Parameter  = juce::AudioParameterFloat;
    using Choice     = juce::AudioParameterChoice;
    using Boolean    = juce::AudioParameterBool;
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::bandMode, kSchemaV1 }, "Mode",
        juce::StringArray { "Above (exciter)", "Below (bass)" }, 0));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::focus, kSchemaV1 }, "Focus",
        skewedRange (40.0f, 12000.0f, 1500.0f), 3000.0f,
        hertzAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::drive, kSchemaV1 }, "Drive",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.4f,
        percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::colour, kSchemaV1 }, "Colour",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.02f) return juce::String ("Odd");
            if (value > 0.98f) return juce::String ("Even");
            return juce::String (juce::roundToInt (value * 100.0f)) + "% even";
        })
        // Without this, JUCE parses typed text with getFloatValue(), which reads
        // "Odd" as zero and "50% even" as fifty -- so a host that offers a text
        // field, or that round-trips the displayed string, sets the wrong value.
        // Steinberg's validator reports it; it is not a warning to leave alone.
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();

            if (trimmed.startsWithIgnoreCase ("odd"))  return 0.0f;
            if (trimmed.startsWithIgnoreCase ("even")) return 1.0f;

            return juce::jlimit (0.0f, 1.0f, trimmed.getFloatValue() * 0.01f);
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::track, kSchemaV1 }, "Track",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.35f,
        percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::punch, kSchemaV1 }, "Punch",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
        percentAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::floorOn, kSchemaV1 }, "Floor On", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::floorHz, kSchemaV1 }, "Floor",
        skewedRange (20.0f, 2000.0f, 200.0f), 200.0f,
        hertzAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::ceilingOn, kSchemaV1 }, "Ceiling On", true));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::ceilingHz, kSchemaV1 }, "Ceiling",
        skewedRange (2000.0f, 20000.0f, 10000.0f), 16000.0f,
        hertzAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::amount, kSchemaV1 }, "Amount",
        juce::NormalisableRange<float> { static_cast<float> (Engine::kAmountSilenceDb), 12.0f }, 0.0f,
        Attributes().withLabel ("dB").withStringFromValueFunction ([] (float value, int)
        {
            if (value <= static_cast<float> (Engine::kAmountSilenceDb))
                return juce::String ("Off");
            return juce::String (value, 1) + " dB";
        })));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::listen, kSchemaV1 }, "Listen", false));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::autoTrim, kSchemaV1 }, "Auto Trim", true));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::input, kSchemaV1 }, "Input",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::oversampling, kSchemaV1 }, "Oversampling",
        juce::StringArray { "Auto", "Off", "x2", "x4", "x8" }, 0));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::bypass, kSchemaV1 }, "Bypass", false));

    // ---- schema version 2 ---------------------------------------------------
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::width, kSchemaV2 }, "Width",
        juce::NormalisableRange<float> { 0.0f, 2.0f }, 1.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.005f) return juce::String ("Mono");
            if (std::abs (value - 1.0f) < 0.005f) return juce::String ("Normal");
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();

            if (trimmed.startsWithIgnoreCase ("mono"))   return 0.0f;
            if (trimmed.startsWithIgnoreCase ("normal")) return 1.0f;

            return juce::jlimit (0.0f, 2.0f, trimmed.getFloatValue() * 0.01f);
        })));

    // ---- schema version 3: Chebyshev precision mode -------------------------
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::generator, kSchemaV3 }, "Generator",
        // Short, because the box is one of five on a row and a truncated
        // "Chebyshev (preci..." tells the user less than the bare name does.
        // The tooltip and the page note carry the citation in full.
        juce::StringArray { "Curve", "Chebyshev" }, 0));

    for (int n = dsp::ChebyshevGenerator::kFirstHarmonic;
         n <= dsp::ChebyshevGenerator::kLastHarmonic; ++n)
    {
        const auto index = static_cast<std::size_t> (n - dsp::ChebyshevGenerator::kFirstHarmonic);

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { ids::harmonics[index], kSchemaV3 },
            "H" + juce::String (n),
            // Skewed so unity sits at a third of the travel: the useful range is
            // 0 to about 2, and the ceiling of 10 is there for the deliberately
            // silly settings rather than for the ones anyone dials by accident.
            skewedRange (0.0f, 10.0f, 1.0f), n == 2 ? 1.0f : 0.0f,
            harmonicAttributes()));
    }

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::chebIndex, kSchemaV3 }, "Index",
        juce::NormalisableRange<float> { 0.0f, 2.0f }, 1.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.0005f) return juce::String ("Off");
            if (std::abs (value - 1.0f) < 0.005f) return juce::String ("Exact");
            return juce::String (value, 2);
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            if (trimmed.startsWithIgnoreCase ("off"))   return 0.0f;
            if (trimmed.startsWithIgnoreCase ("exact")) return 1.0f;
            return juce::jlimit (0.0f, 2.0f, trimmed.getFloatValue());
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::chebTilt, kSchemaV3 }, "Tilt",
        juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (std::abs (value) < 0.005f) return juce::String ("Flat");
            const auto db = value * static_cast<float> (dsp::ChebyshevGenerator::kTiltDbPerStep);
            return juce::String (db, 1) + " dB/step";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            if (trimmed.startsWithIgnoreCase ("flat")) return 0.0f;
            return juce::jlimit (-1.0f, 1.0f,
                trimmed.getFloatValue() / static_cast<float> (dsp::ChebyshevGenerator::kTiltDbPerStep));
        })));

    // ---- schema version 4: modulation ---------------------------------------
    //
    // Defined in shared/tezla-ui rather than here, because Emberdrive declares
    // the same forty-five. Two copies of a frozen table would have to be kept in
    // step by eye, and a difference between them -- a rate range that stops at
    // 16 Hz in one plugin and 20 in the other -- would read as a bug in the LFO
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

HaloProcessor::HaloProcessor()
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

bool HaloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void HaloProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    const int numChannels = juce::jlimit (1, Engine::kMaxChannels, getTotalNumOutputChannels());

    modulation_.prepare (sampleRate);

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

    // Two windows of the largest transform the display uses, so a frame is
    // always available even if the editor is a little late asking for one.
    inputCapture_.prepare (1 << 14);
    outputCapture_.prepare (1 << 14);
    captureScratch_.assign (static_cast<std::size_t> (maximumExpectedSamplesPerBlock), 0.0);

    // Sized for x8 whatever factor is running, so updateLatency() -- which is
    // reached from the audio thread when the oversampling parameter moves --
    // only has to change a length.
    bypassMixer_.prepare (sampleRate, dsp::Oversampler::latencyForFactor (8),
                          juce::jmax (1, getTotalNumOutputChannels()));

    updateLatency (engine_.getLatencySamples());
}

void HaloProcessor::updateLatency (int engineLatencySamples)
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

void HaloProcessor::processSpan (int offset, int numSamples, int numChannels)
{
    pullParameters();

    if (engine_.setParameters (parameters_))
        updateLatency (engine_.getLatencySamples());

    double* pointers[Engine::kMaxChannels] {};
    for (int channel = 0; channel < numChannels; ++channel)
        pointers[channel] = channelPointers_[static_cast<std::size_t> (channel)] + offset;

    engine_.process (pointers, numChannels, numSamples);
}

void HaloProcessor::readBaseParameters()
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

void HaloProcessor::applyModulation()
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

void HaloProcessor::updateModulationSettings()
{
    // Shared with Emberdrive, for the same reason the parameters are: the two
    // plugins have to read forty-five identically named controls in identical
    // units, and a difference would be silent.
    ui::modulation::pushSettings (state_, modulation_, dest::count);
}
void HaloProcessor::pullParameters()
{
    const auto value = [this] (const char* id)
    {
        return static_cast<double> (state_.getRawParameterValue (id)->load());
    };

    const auto flag = [this] (const char* id)
    {
        return state_.getRawParameterValue (id)->load() > 0.5f;
    };

    /// A modulatable control, after modulation.
    const auto moved = [this] (int destination)
    {
        return destinationValues_[static_cast<std::size_t> (destination)];
    };

    parameters_.bandMode  = value (ids::bandMode) < 0.5 ? BandMode::Above : BandMode::Below;
    parameters_.focusHz   = moved (dest::focus);
    parameters_.drive     = moved (dest::drive);
    parameters_.colour    = moved (dest::colour);
    parameters_.track     = moved (dest::track);
    parameters_.punch     = moved (dest::punch);
    parameters_.floorOn   = flag (ids::floorOn);
    parameters_.floorHz   = moved (dest::floorHz);
    parameters_.ceilingOn = flag (ids::ceilingOn);
    parameters_.ceilingHz = moved (dest::ceilingHz);
    parameters_.width     = moved (dest::width);
    parameters_.amountDb  = moved (dest::amount);
    parameters_.listen    = flag (ids::listen);
    parameters_.autoTrim  = flag (ids::autoTrim);
    parameters_.inputDb   = moved (dest::input);
    parameters_.outputDb  = moved (dest::output);

    parameters_.generator = value (ids::generator) < 0.5 ? Generator::Curve
                                                        : Generator::Chebyshev;
    parameters_.chebIndex = moved (dest::chebIndex);
    parameters_.chebTilt  = moved (dest::chebTilt);

    for (std::size_t i = 0; i < parameters_.harmonics.size(); ++i)
        parameters_.harmonics[i] = moved (dest::harm2 + static_cast<int> (i));

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (value (ids::oversampling))));
}

void HaloProcessor::captureMonoSum (const double* const* channels, int numChannels,
                                    int numSamples, dsp::SpectrumCapture& capture) noexcept
{
    if (numChannels <= 0 || numSamples <= 0
        || captureScratch_.size() < static_cast<std::size_t> (numSamples))
        return;

    const double scale = 1.0 / static_cast<double> (numChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int channel = 0; channel < numChannels; ++channel)
            sum += channels[channel][i];

        captureScratch_[static_cast<std::size_t> (i)] = sum * scale;
    }

    capture.push (captureScratch_.data(), numSamples);
}

template <typename FloatType>
void HaloProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
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

    // Captured before the engine runs, from the dry copy, so the input curve is
    // genuinely the input and not something that has already been through the
    // input trim's smoother mid-ramp.
    captureMonoSum (dryPointers_.data(), numChannels, numSamples, inputCapture_);

    // ---- parameters, and modulation if there is any -------------------------
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
        // than as a staircase. The engine is bit-identical across span sizes,
        // which is what makes this safe to do at all -- see
        // halo_output_does_not_depend_on_the_host_block_size.
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

    captureMonoSum (const_cast<const double* const*> (channelPointers_.data()),
                    numChannels, numSamples, outputCapture_);

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
    meters_.harmonicsDb.store  (static_cast<float> (engine_.getHarmonicsDb()),    std::memory_order_relaxed);
}

void HaloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void HaloProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

juce::String HaloProcessor::describeOversampling() const
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
        description << "Oversampling is off. The generator runs at the session rate, which saves "
                    << "CPU and gives up about 25 dB of alias rejection at high drive.";
    else
        description << (mode == dsp::OversamplingMode::Auto ? "Auto is running x" : "Running x")
                    << factor << ", generating harmonics at " << rateText (internalRate)
                    << " internally so they land on real frequencies instead of folding back. "
                    << "Costs " << juce::String (1000.0 * latency / hostRate, 2)
                    << " ms of latency, which the host compensates.";

    return description;
}

void HaloProcessor::getStateInformation (juce::MemoryBlock& destData)
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

void HaloProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    // Version 1 projects predate Width. Nothing to migrate: it defaults to
    // Normal, which is a bit-exact identity, so an older project reopens
    // sounding exactly as it did.
    //
    // A version from the future is refused rather than half-loaded: a partial
    // parameter set is worse than the plugin's defaults, because it looks like
    // it worked.
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
          [] { Parameters p; p.amountDb = Engine::kAmountSilenceDb; p.drive = 0.0; return p; }() },

        // Vocals and the top of a mix: second-harmonic sheen well above the
        // presence region, with the very top kept out of it.
        { "Air",
          [] { Parameters p; p.focusHz = 5000.0; p.drive = 0.35; p.colour = 0.8;
               p.track = 0.5; p.ceilingHz = 17000.0; p.width = 1.35;
               p.amountDb = -3.0; return p; }() },

        // Drum bus. Punch high, so the harmonics arrive on the hits and leave
        // the sustain alone -- which is what stops an exciter turning a break
        // into a wash of cymbals.
        { "Drum bus",
          [] { Parameters p; p.focusHz = 2200.0; p.drive = 0.55; p.colour = 0.35;
               p.track = 0.25; p.punch = 0.7; p.ceilingHz = 15000.0; p.amountDb = -1.0; return p; }() },

        // The reason Below mode exists. A 40 Hz sub is inaudible on a phone or
        // a laptop; its harmonics at 80 and 120 Hz are not, and the ear puts the
        // fundamental back. Floor keeps the generated content out of the sub
        // itself so the low end does not get muddier.
        { "Sub translate",
          [] { Parameters p; p.bandMode = BandMode::Below; p.focusHz = 110.0;
               p.drive = 0.5; p.colour = 0.45; p.track = 0.8;
               p.floorOn = true; p.floorHz = 90.0; p.ceilingHz = 1200.0;
               p.amountDb = -4.0; return p; }() },

        // A reese needs edge in the mids without the sub moving at all.
        { "Reese edge",
          [] { Parameters p; p.focusHz = 900.0; p.drive = 0.6; p.colour = 0.15;
               p.track = 0.6; p.floorOn = true; p.floorHz = 300.0;
               p.ceilingHz = 9000.0; p.amountDb = -2.0; return p; }() },

        // Mastering: barely there, level-independent, and honest about it.
        { "Mix sheen",
          [] { Parameters p; p.focusHz = 6500.0; p.drive = 0.22; p.colour = 0.65;
               p.track = 1.0; p.ceilingHz = 18000.0; p.width = 1.2;
               p.amountDb = -8.0; return p; }() },

        // ---- Chebyshev precision mode ---------------------------------------
        //
        // The three below are a different instrument behind the same panel, and
        // they are named so that is obvious from the list.

        // One octave up and nothing else -- the thing no curve-based exciter can
        // be asked for. On a sub this is the missing-fundamental trick with the
        // muddying harmonics simply absent rather than filtered away afterwards.
        { "Cheb: octave lock",
          [] { Parameters p; p.generator = Generator::Chebyshev;
               p.bandMode = BandMode::Below; p.focusHz = 130.0;
               p.harmonics = { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
               p.floorOn = true; p.floorHz = 70.0; p.ceilingHz = 1500.0;
               p.amountDb = -3.0; return p; }() },

        // Where the high orders are free. A 40 Hz sub's 8th harmonic is 320 Hz,
        // nowhere near the internal Nyquist, so the whole series is exact -- and
        // the result is a bass that reads on a phone without touching the sub.
        { "Cheb: sub bloom",
          [] { Parameters p; p.generator = Generator::Chebyshev;
               p.bandMode = BandMode::Below; p.focusHz = 150.0;
               p.harmonics = { 1.0, 0.7, 0.5, 0.35, 0.25, 0.18, 0.12 };
               p.chebTilt = -0.25;
               p.floorOn = true; p.floorHz = 80.0; p.ceilingHz = 4000.0;
               p.amountDb = -5.0; return p; }() },

        // The wreckage end. Index past 1 clamps the input, so this stops being a
        // chosen series and becomes a distortion -- which is the point, and why
        // it aliases like one. Odd harmonics only, for the metallic reading.
        { "Cheb: reese teeth",
          [] { Parameters p; p.generator = Generator::Chebyshev;
               p.focusHz = 700.0; p.chebIndex = 1.45; p.chebTilt = 0.3;
               p.harmonics = { 0.0, 1.0, 0.0, 0.8, 0.0, 0.6, 0.0 };
               p.floorOn = true; p.floorHz = 250.0; p.ceilingHz = 11000.0;
               p.amountDb = -6.0; return p; }() },

        // ---- modulation -----------------------------------------------------
        //
        // Appended, never inserted, for the same reason a parameter ID is: a
        // host stores which program is selected as an *index*, so putting these
        // anywhere but the end would silently repoint every saved preset choice.
        //
        // Each one exists to show a different thing modulation is for, rather
        // than to fill the list.

        // The level follower doing what it is actually good at: grit that
        // arrives with the note and leaves with it. On a held reese the third
        // and fifth bloom on the attack and settle back, which no static setting
        // can do -- turn the harmonics up enough for the attack and the sustain
        // is a wall.
        { "Mod: reese bloom",
          [] { Parameters p; p.generator = Generator::Chebyshev;
               p.focusHz = 800.0; p.chebIndex = 0.85;
               p.harmonics = { 0.25, 0.8, 0.0, 0.55, 0.0, 0.3, 0.0 };
               p.floorOn = true; p.floorHz = 280.0; p.ceilingHz = 10000.0;
               p.amountDb = -10.0; return p; }(),
          [] { ui::modulation::Settings m;
               m.envAttackMs = 6.0;
               m.envReleaseMs = 220.0;
               m.envSensitivityDb = -14.0;
               m.slots[0] = { 4, dest::harm3, 0.40 };
               m.slots[1] = { 4, dest::harm5, 0.32 };
               return m; }() },

        // A sweep that lands on the grid. Saw up rather than a sine, because the
        // reset is the musical event -- it arrives on the downbeat and climbs
        // away from it. Synced, so a loop repeats identically and a bounce
        // matches what you heard.
        { "Mod: bar sweep",
          [] { Parameters p; p.focusHz = 700.0; p.drive = 0.55; p.colour = 0.3;
               p.track = 0.7; p.punch = 0.2;
               p.floorOn = true; p.floorHz = 200.0; p.ceilingHz = 12000.0;
               p.width = 1.25; p.amountDb = -3.0; return p; }(),
          [] { ui::modulation::Settings m;
               m.lfos[0].wave = 2;          // saw up
               m.lfos[0].sync = true;
               m.lfos[0].division = 3;      // one cycle a bar
               m.lfos[0].smooth = 0.12;     // takes the corner off the reset
               m.slots[0] = { 1, dest::focus, 0.45 };
               return m; }() },

        // The thing only the Chebyshev generator can be asked for: one octave,
        // and only when you want it. The sub underneath is untouched at every
        // point in the cycle, because the octave is a chosen harmonic rather
        // than the top of a series.
        { "Mod: octave pulse",
          [] { Parameters p; p.generator = Generator::Chebyshev;
               p.bandMode = BandMode::Below; p.focusHz = 140.0;
               p.harmonics = { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
               p.floorOn = true; p.floorHz = 75.0; p.ceilingHz = 1600.0;
               p.amountDb = -8.0; return p; }(),
          [] { ui::modulation::Settings m;
               m.lfos[1].wave = 0;          // sine
               m.lfos[1].sync = true;
               m.lfos[1].division = 4;      // a half bar
               m.lfos[1].phase = 0.75;      // at the bottom on the downbeat
               m.slots[0] = { 2, dest::harm2, 0.28 };
               return m; }() },
    };

    static constexpr int count = static_cast<int> (std::size (presets));
    return presets[static_cast<std::size_t> (juce::jlimit (0, count - 1, index))];
}

constexpr int kNumPresets = 12;
} // namespace

int HaloProcessor::getNumPrograms() { return kNumPresets; }

const juce::String HaloProcessor::getProgramName (int index)
{
    return presetAt (index).name;
}

void HaloProcessor::setCurrentProgram (int index)
{
    currentProgram_ = juce::jlimit (0, kNumPresets - 1, index);
    const auto& preset = presetAt (currentProgram_).parameters;

    const auto set = [this] (const char* id, float value)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    set (ids::bandMode,  preset.bandMode == BandMode::Above ? 0.0f : 1.0f);
    set (ids::focus,     static_cast<float> (preset.focusHz));
    set (ids::drive,     static_cast<float> (preset.drive));
    set (ids::colour,    static_cast<float> (preset.colour));
    set (ids::track,     static_cast<float> (preset.track));
    set (ids::punch,     static_cast<float> (preset.punch));
    set (ids::floorOn,   preset.floorOn ? 1.0f : 0.0f);
    set (ids::floorHz,   static_cast<float> (preset.floorHz));
    set (ids::ceilingOn, preset.ceilingOn ? 1.0f : 0.0f);
    set (ids::ceilingHz, static_cast<float> (preset.ceilingHz));
    set (ids::width,     static_cast<float> (preset.width));
    set (ids::amount,    static_cast<float> (preset.amountDb));
    set (ids::listen,    preset.listen ? 1.0f : 0.0f);
    set (ids::autoTrim,  preset.autoTrim ? 1.0f : 0.0f);
    set (ids::input,     static_cast<float> (preset.inputDb));
    set (ids::output,    static_cast<float> (preset.outputDb));

    set (ids::generator, preset.generator == Generator::Curve ? 0.0f : 1.0f);
    set (ids::chebIndex, static_cast<float> (preset.chebIndex));
    set (ids::chebTilt,  static_cast<float> (preset.chebTilt));

    for (std::size_t i = 0; i < preset.harmonics.size(); ++i)
        set (ids::harmonics[i], static_cast<float> (preset.harmonics[i]));

    // Every preset writes all forty-five, including the ones that leave them
    // neutral. A preset is a complete parameter set or it is a trap: without
    // this, loading "Clean" after a modulated patch would leave the LFOs still
    // driving whatever they were pointed at.
    ui::modulation::applyPreset (state_, presetAt (currentProgram_).modulation);
}

juce::AudioProcessorEditor* HaloProcessor::createEditor()
{
    return new HaloEditor (*this);
}

} // namespace tezla::halo

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::halo::HaloProcessor();
}
