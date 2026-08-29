// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"

#include <tezla/ui/StateIds.hpp>

#include <algorithm>
#include <cmath>

namespace tezla::ferrite
{

namespace
{
// Every parameter carries the schema version it was introduced at, and keeps
// it forever: the version hint feeds the VST3 parameter ID, so moving it on a
// live parameter is indistinguishable from renaming it. CLAUDE.md §8.
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;
constexpr auto kStateTypeName = "FerriteState";

juce::NormalisableRange<float> skewedRange (float minimum, float maximum, float centre)
{
    juce::NormalisableRange<float> range { minimum, maximum };
    range.setSkewForCentre (centre);
    return range;
}

juce::AudioParameterFloatAttributes decibelAttributes (int decimals = 1)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("dB")
        .withStringFromValueFunction ([decimals] (float value, int)
        {
            const float shown = std::abs (value) < 0.05f ? 0.0f : value;
            return juce::String (shown, decimals) + " dB";
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
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

/// Hiss reads as its true output floor in dBFS -- the engine is calibrated
/// for exactly that -- and the bottom of the travel is OFF, which the
/// engine honours as bit-exact absence.
juce::AudioParameterFloatAttributes hissAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("dBFS")
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value <= -119.5f)
                return juce::String ("Off");
            return juce::String (value, 0) + " dBFS";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            if (text.trim().startsWithIgnoreCase ("off"))
                return -120.0f;
            return text.getFloatValue();
        });
}

juce::AudioParameterFloatAttributes unitAttributes (const char* unit, int decimals)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel (unit)
        .withStringFromValueFunction ([unit, decimals] (float value, int)
        {
            return juce::String (value, decimals) + " " + unit;
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

juce::String rateText (double rate)
{
    return juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " kHz";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout FerriteProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using Choice    = juce::AudioParameterChoice;
    using Boolean   = juce::AudioParameterBool;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- onto the tape -------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::input, kSchemaV1 }, "Input",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::drive, kSchemaV1 }, "Drive",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::saturation, kSchemaV1 }, "Saturation",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::bias, kSchemaV1 }, "Bias",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    // ---- the transport -------------------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::speed, kSchemaV1 }, "Speed",
        choices::speed, 2));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::bump, kSchemaV1 }, "Head Bump",
        juce::NormalisableRange<float> { 0.0f, 2.0f }, 1.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::wow, kSchemaV1 }, "Wow",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.15f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::flutter, kSchemaV1 }, "Flutter",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.15f, percentAttributes()));

    // ---- expert: the machine's physical numbers ------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::wowRate, kSchemaV1 }, "Wow Rate",
        skewedRange (0.2f, 3.0f, 0.9f), 0.9f, unitAttributes ("Hz", 2)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::flutterRate, kSchemaV1 }, "Flutter Rate",
        skewedRange (4.0f, 30.0f, 12.0f), 12.0f, unitAttributes ("Hz", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::spacing, kSchemaV1 }, "Spacing",
        skewedRange (0.5f, 20.0f, 5.0f), 5.0f, unitAttributes ("um", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::thickness, kSchemaV1 }, "Thickness",
        skewedRange (5.0f, 50.0f, 35.0f), 35.0f, unitAttributes ("um", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::gap, kSchemaV1 }, "Head Gap",
        skewedRange (0.5f, 10.0f, 2.5f), 2.5f, unitAttributes ("um", 1)));

    // ---- off the tape --------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::hiss, kSchemaV1 }, "Hiss",
        juce::NormalisableRange<float> { -120.0f, -40.0f }, -120.0f,
        hissAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::autoTrim, kSchemaV1 }, "Auto Trim", true));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::mix, kSchemaV1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::oversampling, kSchemaV1 }, "Oversampling",
        choices::oversampling, static_cast<int> (dsp::OversamplingMode::Auto)));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::bypass, kSchemaV1 }, "Bypass", false));

    return layout;
}

FerriteProcessor::FerriteProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));
}

bool FerriteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& main = layouts.getMainOutputChannelSet();

    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == main;
}

void FerriteProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    const int maximumBlock = std::max (maximumExpectedSamplesPerBlock, 1);
    const int channels = std::max (getTotalNumInputChannels(), getTotalNumOutputChannels());
    const int engineChannels = juce::jlimit (1, Engine::kMaxChannels, channels);

    engine_.prepare (sampleRate_, maximumBlock, engineChannels);

    scratch_.setSize (Engine::kMaxChannels, maximumBlock, false, true, true);

    // prepare() runs before any parameter is known, so this is what makes the
    // very first push take effect rather than being swallowed. CLAUDE.md §7.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.reset();

    reportedLatency_ = engine_.getLatencySamples();
    setLatencySamples (reportedLatency_);

    prepared_ = true;
}

void FerriteProcessor::pullParameters()
{
    const auto value = [this] (const char* id)
    {
        return static_cast<double> (state_.getRawParameterValue (id)->load());
    };

    parameters_.inputDb    = value (ids::input);
    parameters_.drive      = value (ids::drive);
    parameters_.saturation = value (ids::saturation);
    parameters_.bias       = value (ids::bias);

    parameters_.speedChoice = juce::jlimit (0, choices::speed.size() - 1,
                                            static_cast<int> (value (ids::speed)));
    parameters_.bumpAmount  = value (ids::bump);

    parameters_.wowDepth      = value (ids::wow);
    parameters_.flutterDepth  = value (ids::flutter);
    parameters_.wowRateHz     = value (ids::wowRate);
    parameters_.flutterRateHz = value (ids::flutterRate);

    parameters_.spacingUm   = value (ids::spacing);
    parameters_.thicknessUm = value (ids::thickness);
    parameters_.gapUm       = value (ids::gap);

    parameters_.hissDb   = value (ids::hiss);
    parameters_.autoTrim = value (ids::autoTrim) > 0.5;
    parameters_.mix      = value (ids::mix);
    parameters_.outputDb = value (ids::output);

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (value (ids::oversampling))));

    parameters_.bypassed = bypassParameter_ != nullptr && bypassParameter_->get();
}

template <typename FloatType>
void FerriteProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();

    for (int channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0)
        return;

    const int channels = juce::jlimit (1, Engine::kMaxChannels,
                                       std::min (inputChannels, outputChannels));

    if (numSamples > scratch_.getNumSamples())
        scratch_.setSize (Engine::kMaxChannels, numSamples, false, true, true);

    pullParameters();
    engine_.setParameters (parameters_);

    // Into double, which is what the whole DSP path runs in.
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = scratch_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<double> (source[i]);

        channelPointers_[static_cast<std::size_t> (channel)] = destination;
    }

    engine_.process (channelPointers_.data(), channels, numSamples);

    // The engine applies parameters on its control grid, so a factor change
    // lands during process(); the host hears about the new latency here.
    if (engine_.getLatencySamples() != reportedLatency_)
    {
        reportedLatency_ = engine_.getLatencySamples();
        setLatencySamples (reportedLatency_);
    }

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* processed = scratch_.getReadPointer (channel);
        auto* destination = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<FloatType> (processed[i]);
    }

    // A mono input into a stereo bus: copy rather than leave silence.
    for (int channel = channels; channel < outputChannels; ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples);

    // The engine meters the level going ONTO the tape (after input gain) and
    // the level leaving the plugin; the editor's needles read these.
    float inputVu = -100.0f, inputPeak = -100.0f;
    float outputVu = -100.0f, outputPeak = -100.0f;

    for (int channel = 0; channel < channels; ++channel)
    {
        inputVu    = std::max (inputVu,    static_cast<float> (engine_.inputVu (channel).getVuDb()));
        inputPeak  = std::max (inputPeak,  static_cast<float> (engine_.inputVu (channel).getPeakDb()));
        outputVu   = std::max (outputVu,   static_cast<float> (engine_.outputVu (channel).getVuDb()));
        outputPeak = std::max (outputPeak, static_cast<float> (engine_.outputVu (channel).getPeakDb()));
    }

    meters_.inputVuDb.store (inputVu, std::memory_order_relaxed);
    meters_.inputPeakDb.store (inputPeak, std::memory_order_relaxed);
    meters_.outputVuDb.store (outputVu, std::memory_order_relaxed);
    meters_.outputPeakDb.store (outputPeak, std::memory_order_relaxed);
}

void FerriteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void FerriteProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

juce::String FerriteProcessor::describeOversampling() const
{
    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    const auto mode = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (state_.getRawParameterValue (ids::oversampling)->load())));

    const int autoFactor = dsp::autoOversamplingFactor (hostRate);
    const int factor = dsp::oversamplingFactor (mode, hostRate);

    juce::String description;

    if (mode == dsp::OversamplingMode::Auto)
    {
        description
            << "Auto -- runs the tape stage at about 192 kHz internally. Your session is at "
            << rateText (hostRate) << ", so this is x" << autoFactor
            << (autoFactor == 1 ? " (off -- the headroom is already there)." : ".");
    }
    else
    {
        description << "Forced x" << factor << " at " << rateText (hostRate)
                    << " -- " << rateText (hostRate * factor) << " internally. Auto would pick x"
                    << autoFactor << ".";
    }

    description
        << " Measured at maximum drive, worst of a sweep to 4.4 kHz at 48 kHz: unoversampled "
           "the hysteresis aliases at -44 dBFS, x2 at -61, x4 at -74. The gate is -60, so x2 "
           "scrapes it and x4 clears it with margin -- which is why Auto picks x4. A stereo "
           "instance at x4 costs about 13% of one core.";

    return description;
}

juce::String FerriteProcessor::describeLatency() const
{
    if (! prepared_)
        return "Latency is reported once the host has started audio.";

    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    juce::String description;
    description << reportedLatency_ << " samples, "
                << juce::String (1000.0 * reportedLatency_ / hostRate, 2)
                << " ms at " << rateText (hostRate)
                << ". The oversampling filters plus the wow/flutter head's one-millisecond "
                   "centre offset. Declared to the host, so delay compensation lines it up -- "
                   "and both the mix's dry path and the bypass are delayed to match, so A/B "
                   "is honest.";

    return description;
}

void FerriteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    state.appendChild (abCompare_.toValueTree(), nullptr);
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FerriteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    // A version from the future is refused rather than half-loaded.
    const int version = tree.getProperty ("schemaVersion", 1);
    if (version > kStateSchemaVersion)
        return;

    state_.replaceState (tree);
    abCompare_.restoreFromValueTree (tree.getChildWithName ("abCompare"));
    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);
}

namespace
{
/// The presets, aimed at what this rig does with a tape machine.
struct Preset
{
    const char* name;
    Parameters  parameters;
};

const Preset& presetAt (int index)
{
    static const Preset presets[]
    {
        // First, and deliberately so: mix 0 is bit-exact (latency-matched),
        // which makes this the thing to A/B everything else against.
        { "Flat reference (mix 0, bit-exact)",
          [] { Parameters p; p.mix = 0.0; p.wowDepth = 0.0; p.flutterDepth = 0.0;
               p.hissDb = -120.0; return p; }() },

        // Weight and glue on a drum bus, not distortion: gentle drive, the
        // wobble barely on, half the signal dry so transients keep their edge.
        { "Drum bus: glue",
          [] { Parameters p; p.inputDb = 2.0; p.drive = 0.35; p.saturation = 0.4;
               p.bias = 0.6; p.speedChoice = 2; p.bumpAmount = 1.0;
               p.wowDepth = 0.05; p.flutterDepth = 0.08; p.hissDb = -120.0;
               p.mix = 0.6; return p; }() },

        // Sub bass wants the head bump and pitch stability: 7.5 ips puts the
        // bump at 21 Hz, the wobble stays nearly off so the sub holds its note.
        { "Sub weight: 7.5 ips",
          [] { Parameters p; p.drive = 0.55; p.saturation = 0.6; p.bias = 0.35;
               p.speedChoice = 1; p.bumpAmount = 1.6; p.wowDepth = 0.04;
               p.flutterDepth = 0.03; p.hissDb = -120.0; p.mix = 0.5; return p; }() },

        // A reese thickens by motion: real wobble on top of hot, wide-loop
        // drive, blended so the original's weight survives underneath.
        { "Reese: thicken",
          [] { Parameters p; p.inputDb = 4.0; p.drive = 0.7; p.saturation = 0.7;
               p.bias = 0.3; p.speedChoice = 2; p.bumpAmount = 0.8;
               p.wowDepth = 0.25; p.flutterDepth = 0.3; p.hissDb = -120.0;
               p.mix = 0.7; return p; }() },

        // Mix-bus glue at the professional speed: barely driven, high bias,
        // the bump turned down so the low end stays honest.
        { "Master glue: 30 ips",
          [] { Parameters p; p.drive = 0.25; p.saturation = 0.3; p.bias = 0.75;
               p.speedChoice = 3; p.bumpAmount = 0.6; p.wowDepth = 0.02;
               p.flutterDepth = 0.04; p.hissDb = -120.0; return p; }() },

        // Priority two in CLAUDE.md: a clean setting that is genuinely clean.
        // High bias narrows the loop toward the anhysteretic curve; the
        // wobble and hiss are off entirely.
        { "Clean: 30 ips, high bias",
          [] { Parameters p; p.drive = 0.12; p.saturation = 0.15; p.bias = 0.95;
               p.speedChoice = 3; p.bumpAmount = 0.3; p.wowDepth = 0.0;
               p.flutterDepth = 0.0; p.hissDb = -120.0; return p; }() },

        // The cassette in the glovebox: slowest speed, widest loop, the
        // transport audibly failing, the noise floor part of the sound.
        { "Trashed: 3.75 ips",
          [] { Parameters p; p.inputDb = 6.0; p.drive = 0.95; p.saturation = 0.9;
               p.bias = 0.05; p.speedChoice = 0; p.bumpAmount = 2.0;
               p.wowDepth = 0.8; p.flutterDepth = 0.7; p.hissDb = -55.0;
               return p; }() },

        // Full wow at 7.5 ips: the pitch visibly seasick, moderate drive, the
        // hiss up where a worn machine keeps it.
        { "Warble: 7.5 ips",
          [] { Parameters p; p.drive = 0.45; p.saturation = 0.5; p.bias = 0.5;
               p.speedChoice = 1; p.wowDepth = 1.0; p.flutterDepth = 0.35;
               p.hissDb = -65.0; return p; }() },
    };

    static constexpr int count = static_cast<int> (std::size (presets));
    return presets[static_cast<std::size_t> (juce::jlimit (0, count - 1, index))];
}

constexpr int kNumPresets = 8;
} // namespace

int FerriteProcessor::getNumPrograms() { return kNumPresets; }

const juce::String FerriteProcessor::getProgramName (int index)
{
    return presetAt (index).name;
}

void FerriteProcessor::setCurrentProgram (int index)
{
    currentProgram_ = juce::jlimit (0, kNumPresets - 1, index);
    const auto& preset = presetAt (currentProgram_).parameters;

    const auto set = [this] (const char* id, float value)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    // Every preset writes every parameter, including the ones it leaves
    // neutral. A preset is a complete parameter set or it is a trap.
    set (ids::input,        static_cast<float> (preset.inputDb));
    set (ids::drive,        static_cast<float> (preset.drive));
    set (ids::saturation,   static_cast<float> (preset.saturation));
    set (ids::bias,         static_cast<float> (preset.bias));

    set (ids::speed,        static_cast<float> (preset.speedChoice));
    set (ids::bump,         static_cast<float> (preset.bumpAmount));

    set (ids::wow,          static_cast<float> (preset.wowDepth));
    set (ids::flutter,      static_cast<float> (preset.flutterDepth));
    set (ids::wowRate,      static_cast<float> (preset.wowRateHz));
    set (ids::flutterRate,  static_cast<float> (preset.flutterRateHz));

    set (ids::spacing,      static_cast<float> (preset.spacingUm));
    set (ids::thickness,    static_cast<float> (preset.thicknessUm));
    set (ids::gap,          static_cast<float> (preset.gapUm));

    set (ids::hiss,         static_cast<float> (preset.hissDb));
    set (ids::autoTrim,     preset.autoTrim ? 1.0f : 0.0f);
    set (ids::mix,          static_cast<float> (preset.mix));
    set (ids::output,       static_cast<float> (preset.outputDb));
    set (ids::oversampling, static_cast<float> (static_cast<int> (preset.oversampling)));
}

juce::AudioProcessorEditor* FerriteProcessor::createEditor()
{
    // The real panel lands in the next phase; the generic editor keeps every
    // control reachable until then.
    return new juce::GenericAudioProcessorEditor (*this);
}

} // namespace tezla::ferrite

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::ferrite::FerriteProcessor();
}
