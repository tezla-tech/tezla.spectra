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

namespace tezla::anvil
{

namespace
{
// Every parameter carries the schema version it was introduced at, and keeps it
// forever: the version hint feeds the VST3 parameter ID, so moving it on a live
// parameter is indistinguishable from renaming it. CLAUDE.md §8.
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;
constexpr auto kStateTypeName = "AnvilState";

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
            // Snapped, or a skewed range hands back -0.0001 and prints
            // "-0.0 dB", which reads as a bug rather than as a float's last digit.
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

/// The microphone's position, named at the ends rather than numbered.
juce::AudioParameterFloatAttributes positionAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value <= 0.02f) return juce::String ("Cap");
            if (value >= 0.98f) return juce::String ("Edge");
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            if (trimmed.startsWithIgnoreCase ("cap"))  return 0.0f;
            if (trimmed.startsWithIgnoreCase ("edge")) return 1.0f;
            return trimmed.getFloatValue() * 0.01f;
        });
}

juce::String rateText (double rate)
{
    return juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " kHz";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AnvilProcessor::createParameterLayout()
{
    using Parameter  = juce::AudioParameterFloat;
    using Choice     = juce::AudioParameterChoice;
    using Boolean    = juce::AudioParameterBool;
    using Integer    = juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- the amplifier -------------------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::voicing, kSchemaV1 }, "Voicing",
        choices::voicing, static_cast<int> (Voicing::vintage)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::gain, kSchemaV1 }, "Gain",
        skewedRange (-6.0f, 48.0f, 18.0f), 18.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Integer> (
        juce::ParameterID { ids::stages, kSchemaV1 }, "Stages", 0, 2, 0,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int value, int)
            {
                return value == 0 ? juce::String ("Stock")
                                  : juce::String ("+") + juce::String (value);
            })));

    // ---- the tone stack ------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::bass, kSchemaV1 }, "Bass",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::middle, kSchemaV1 }, "Middle",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::treble, kSchemaV1 }, "Treble",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    // ---- the power amplifier -------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::master, kSchemaV1 }, "Master",
        skewedRange (-40.0f, 0.0f, -12.0f), -6.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::presence, kSchemaV1 }, "Presence",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.3f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::resonance, kSchemaV1 }, "Resonance",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.2f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::sag, kSchemaV1 }, "Sag",
        juce::NormalisableRange<float> { 0.0f, 2.0f }, 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                if (value <= 0.005f) return juce::String ("Off");
                return juce::String (juce::roundToInt (value * 100.0f)) + " %";
            })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::core, kSchemaV1 }, "Core",
        skewedRange (static_cast<float> (Engine::kMinimumCoreHz),
                     static_cast<float> (Engine::kMaximumCoreHz), 60.0f), 45.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value)) + " Hz";
            })));

    // ---- the cabinet ---------------------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::cabinet, kSchemaV1 }, "Cabinet",
        choices::cabinet, static_cast<int> (CabinetChoice::british)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::micPosition, kSchemaV1 }, "Mic Position",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.25f, positionAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::micDistance, kSchemaV1 }, "Mic Distance",
        skewedRange (static_cast<float> (Engine::kMinimumMicCm),
                     static_cast<float> (Engine::kMaximumMicCm), 8.0f), 5.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("cm")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, value < 10.0f ? 1 : 0) + " cm";
            })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::damping, kSchemaV1 }, "Damping",
        skewedRange (static_cast<float> (Engine::kMinimumDamping),
                     static_cast<float> (Engine::kMaximumDamping), 1.5f), 1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, value < 10.0f ? 2 : 1);
            })));

    // ---- global --------------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::mix, kSchemaV1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f, percentAttributes()));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::oversampling, kSchemaV1 }, "Oversampling",
        choices::oversampling, static_cast<int> (dsp::OversamplingMode::Auto)));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::bypass, kSchemaV1 }, "Bypass", false));

    return layout;
}

AnvilProcessor::AnvilProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));
}

bool AnvilProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& main = layouts.getMainOutputChannelSet();

    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == main;
}

void AnvilProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    const int maximumBlock = std::max (maximumExpectedSamplesPerBlock, 1);
    const int channels = std::max (getTotalNumInputChannels(), getTotalNumOutputChannels());
    const int engineChannels = juce::jlimit (1, Engine::kMaxChannels, channels);

    engine_.prepare (sampleRate_, maximumBlock, engineChannels);

    scratch_.setSize (Engine::kMaxChannels, maximumBlock, false, true, true);

    for (int channel = 0; channel < Engine::kMaxChannels; ++channel)
    {
        inputMeter_[channel].prepare (sampleRate_);
        outputMeter_[channel].prepare (sampleRate_);
    }

    // prepare() runs before any parameter is known, so this is what makes the
    // very first push take effect rather than being swallowed. CLAUDE.md §7.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.reset();

    reportedLatency_ = engine_.getLatencySamples();
    setLatencySamples (reportedLatency_);

    prepared_ = true;
}

void AnvilProcessor::pullParameters()
{
    const auto value = [this] (const char* id)
    {
        return static_cast<double> (state_.getRawParameterValue (id)->load());
    };

    parameters_.voicing = static_cast<Voicing> (
        juce::jlimit (0, static_cast<int> (Voicing::count) - 1,
                      static_cast<int> (value (ids::voicing))));

    parameters_.gainDb = value (ids::gain);
    parameters_.extraStages = juce::jlimit (0, 2, static_cast<int> (value (ids::stages)));

    parameters_.bass   = value (ids::bass);
    parameters_.middle = value (ids::middle);
    parameters_.treble = value (ids::treble);

    parameters_.masterDb  = value (ids::master);
    parameters_.presence  = value (ids::presence);
    parameters_.resonance = value (ids::resonance);
    parameters_.sag       = value (ids::sag);
    parameters_.coreHz    = value (ids::core);

    parameters_.cabinet = static_cast<CabinetChoice> (
        juce::jlimit (0, static_cast<int> (CabinetChoice::count) - 1,
                      static_cast<int> (value (ids::cabinet))));

    parameters_.micPosition   = value (ids::micPosition);
    parameters_.micDistanceCm = value (ids::micDistance);
    parameters_.damping       = value (ids::damping);

    parameters_.outputDb = value (ids::output);
    parameters_.mix      = value (ids::mix);

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (value (ids::oversampling))));

    parameters_.bypass = bypassParameter_ != nullptr && bypassParameter_->get();
}

template <typename FloatType>
void AnvilProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
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

    if (engine_.setParameters (parameters_))
    {
        reportedLatency_ = engine_.getLatencySamples();
        setLatencySamples (reportedLatency_);
    }

    // Into double, which is what the whole DSP path runs in.
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = scratch_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<double> (source[i]);

        channelPointers_[static_cast<std::size_t> (channel)] = destination;
        inputMeter_[channel].processBlock (destination, numSamples);
    }

    engine_.process (channelPointers_.data(), channels, numSamples);

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* processed = scratch_.getReadPointer (channel);
        auto* destination = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<FloatType> (processed[i]);

        outputMeter_[channel].processBlock (processed, numSamples);
    }

    // A mono input into a stereo bus: copy rather than leave silence, so the
    // plugin does not appear to work only on the left.
    for (int channel = channels; channel < outputChannels; ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples);

    float inputVu = -100.0f, inputPeak = -100.0f;
    float outputVu = -100.0f, outputPeak = -100.0f;

    for (int channel = 0; channel < channels; ++channel)
    {
        inputVu    = std::max (inputVu,    static_cast<float> (inputMeter_[channel].getVuDb()));
        inputPeak  = std::max (inputPeak,  static_cast<float> (inputMeter_[channel].getPeakDb()));
        outputVu   = std::max (outputVu,   static_cast<float> (outputMeter_[channel].getVuDb()));
        outputPeak = std::max (outputPeak, static_cast<float> (outputMeter_[channel].getPeakDb()));
    }

    meters_.inputVuDb.store (inputVu, std::memory_order_relaxed);
    meters_.inputPeakDb.store (inputPeak, std::memory_order_relaxed);
    meters_.outputVuDb.store (outputVu, std::memory_order_relaxed);
    meters_.outputPeakDb.store (outputPeak, std::memory_order_relaxed);

    meters_.sag.store (static_cast<float> (engine_.getSag()), std::memory_order_relaxed);
    meters_.flux.store (static_cast<float> (engine_.getFlux()), std::memory_order_relaxed);
    meters_.bias.store (static_cast<float> (engine_.getBiasShift()), std::memory_order_relaxed);
}

void AnvilProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void AnvilProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

juce::String AnvilProcessor::describeOversampling() const
{
    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    const auto mode = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (state_.getRawParameterValue (ids::oversampling)->load())));

    const int autoFactor = Engine::autoFactorFor (hostRate);
    const int factor = mode == dsp::OversamplingMode::Auto
                         ? autoFactor
                         : dsp::oversamplingFactor (mode, hostRate);

    juce::String description;

    if (mode == dsp::OversamplingMode::Auto)
    {
        description
            << "Auto -- runs the amplifier at about 384 kHz internally. Your session is at "
            << rateText (hostRate) << ", so this is x" << autoFactor << ".";
    }
    else
    {
        description << "Forced x" << factor << " at " << rateText (hostRate)
                    << " -- " << rateText (hostRate * factor) << " internally. Auto would pick x"
                    << autoFactor << ".";
    }

    description
        << " Cascaded valves make far more out-of-band energy than a single saturator does, "
        << "because each one distorts the harmonics the last one made. Measured at full gain "
        << "with five valves, worst of a sweep from 82 Hz to 4.4 kHz: x4 aliases at -46 dBFS "
        << "and x8 at -65. A single 1 kHz probe reads x4 at -69 and flatters it by twenty "
        << "decibels, which is why the figure is swept. x8 costs about 37% of one core for a "
        << "stereo instance here against 17% at x4.";

    description << " Latency " << dsp::Oversampler::latencyForFactor (factor) << " samples.";

    return description;
}

juce::String AnvilProcessor::describeVoicing() const
{
    const auto voicing = static_cast<Voicing> (
        juce::jlimit (0, static_cast<int> (Voicing::count) - 1,
                      static_cast<int> (state_.getRawParameterValue (ids::voicing)->load())));

    switch (voicing)
    {
        case Voicing::clean:
            return "One valve, then the tone stack -- so the controls shape what you hear rather "
                   "than what gets distorted. A lot of global feedback, a stiff supply and a "
                   "large transformer: at low Gain this lane is 56 dB down and genuinely out of "
                   "the way, not a quieter version of the dirty one.";

        case Voicing::vintage:
            return "Two valves with the stack between them, almost no feedback loop, and a valve "
                   "rectifier that sags 32% under load. The transformer is small enough to feel "
                   "-- its core fills at 58 Hz, so a low note goes woolly while a lead line does "
                   "not. The lane that moves under you.";

        case Voicing::modern:
        case Voicing::count:
        default:
            return "Three valves with the stack after the second, scooped hard before the last "
                   "one -- which is what a modern high-gain amplifier actually is. A tight supply "
                   "and a stiff transformer, so it stays articulate where the other two fall "
                   "apart. Add valves with Stages if that is not enough.";
    }
}

juce::String AnvilProcessor::describeCore() const
{
    const double core = state_.getRawParameterValue (ids::core)->load();

    juce::String description;

    description
        << "The frequency at which a full swing just fills the output transformer's core. "
        << "Flux is the integral of voltage, so the same voltage puts twice as much of it in an "
        << "octave down -- at " << juce::roundToInt (core) << " Hz, a note an octave below that "
        << "saturates the core while one an octave above does not touch it. Measured: 6 dB per "
        << "octave of flux, about 18 dB per octave of the distortion it makes.";

    if (core < 30.0)
        description << " Down here the transformer is enormous and the low end stays clean.";
    else if (core > 150.0)
        description << " Up here it is smaller than any transformer ever wound, which is the "
                       "point: everything under a couple of hundred hertz goes thick and "
                       "harmonically rich while the midrange stays exactly where it was.";

    return description;
}

juce::String AnvilProcessor::describeLatency() const
{
    if (! prepared_)
        return "Latency is reported once the host has started audio.";

    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    if (reportedLatency_ == 0)
        return "No latency. Oversampling is off, so there is no resampling filter to delay by.";

    juce::String description;
    description << reportedLatency_ << " samples, "
                << juce::String (1000.0 * reportedLatency_ / hostRate, 2)
                << " ms at " << rateText (hostRate)
                << ". Declared to the host, so delay compensation lines it up -- and the bypass "
                   "path is delayed to match, so A/B is honest.";

    return description;
}

void AnvilProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    state.appendChild (abCompare_.toValueTree(), nullptr);

    // A panel preference rather than a parameter, so it rides in the state tree
    // rather than in the parameter layout -- see getTooltipsEnabled.
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AnvilProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    // A version from the future is refused rather than half-loaded: a partial
    // parameter set is worse than the defaults, because it looks like it worked.
    const int version = tree.getProperty ("schemaVersion", 1);
    if (version > kStateSchemaVersion)
        return;

    state_.replaceState (tree);
    abCompare_.restoreFromValueTree (tree.getChildWithName ("abCompare"));
    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);
}

namespace
{
/// The presets, aimed at the cases this rig actually has.
struct Preset
{
    const char* name;
    Parameters  parameters;
};

const Preset& presetAt (int index)
{
    static const Preset presets[]
    {
        // First, and deliberately so: the amplifier out of the way. Mix at 0 is
        // bit-exact, which makes this the thing to A/B everything else against.
        { "Bypass reference (bit-exact)",
          [] { Parameters p; p.mix = 0.0; return p; }() },

        // A clean amplifier that is actually clean. 56 dB down at this Gain.
        { "Clean: pedal platform",
          [] { Parameters p; p.voicing = Voicing::clean; p.gainDb = 0.0;
               p.masterDb = -6.0; p.bass = 0.5; p.middle = 0.6; p.treble = 0.55;
               p.presence = 0.2; p.resonance = 0.15; p.sag = 0.5;
               p.cabinet = CabinetChoice::combo; p.micPosition = 0.35;
               p.micDistanceCm = 8.0; p.damping = 6.0; p.coreHz = 32.0; return p; }() },

        // The classic: everything up, no master, and let the output stage do it.
        { "Vintage: cranked",
          [] { Parameters p; p.voicing = Voicing::vintage; p.gainDb = 30.0;
               p.masterDb = 0.0; p.bass = 0.6; p.middle = 0.5; p.treble = 0.7;
               p.presence = 0.5; p.resonance = 0.35; p.sag = 1.4;
               p.cabinet = CabinetChoice::vintage; p.micPosition = 0.3;
               p.micDistanceCm = 4.0; p.damping = 0.6; p.coreHz = 58.0; return p; }() },

        // Modern rhythm: scooped, tight, articulate at high gain.
        { "Modern: rhythm",
          [] { Parameters p; p.voicing = Voicing::modern; p.gainDb = 34.0;
               p.masterDb = -8.0; p.bass = 0.7; p.middle = 0.25; p.treble = 0.65;
               p.presence = 0.4; p.resonance = 0.3; p.sag = 0.6;
               p.cabinet = CabinetChoice::british; p.micPosition = 0.2;
               p.micDistanceCm = 3.0; p.damping = 2.5; p.coreHz = 40.0; return p; }() },

        // What this plugin is for on this rig. The core pushed right up so
        // everything under 200 Hz saturates the transformer and blooms, with
        // the cabinet off so nothing removes the top. Mix low, because on a sub
        // it is the harmonics you want and not the amplifier's low end.
        { "Sub bass: transformer bloom",
          [] { Parameters p; p.voicing = Voicing::vintage; p.gainDb = 6.0;
               p.masterDb = -2.0; p.bass = 0.7; p.middle = 0.5; p.treble = 0.3;
               p.presence = 0.0; p.resonance = 0.8; p.sag = 1.6; p.coreHz = 220.0;
               p.cabinet = CabinetChoice::none; p.damping = 0.4;
               p.outputDb = -3.0; p.mix = 0.45; return p; }() },

        // A reese wants grind that does not smear the note. Modern lane for the
        // articulation, cabinet on to keep the top under control, mix at 70%
        // so the original's weight survives.
        { "Reese: grind",
          [] { Parameters p; p.voicing = Voicing::modern; p.gainDb = 26.0;
               p.masterDb = -10.0; p.bass = 0.55; p.middle = 0.45; p.treble = 0.6;
               p.presence = 0.35; p.resonance = 0.5; p.sag = 0.8; p.coreHz = 90.0;
               p.cabinet = CabinetChoice::british; p.micPosition = 0.45;
               p.micDistanceCm = 10.0; p.damping = 1.5; p.mix = 0.7; return p; }() },

        // Drum bus. Very little gain, master near the top, so what you get is
        // the output stage's sag and the transformer -- weight and glue rather
        // than distortion. Cabinet off, because a 4x12 on a drum bus removes
        // the cymbals.
        { "Drum bus: glue and weight",
          [] { Parameters p; p.voicing = Voicing::vintage; p.gainDb = -2.0;
               p.masterDb = -3.0; p.bass = 0.5; p.middle = 0.5; p.treble = 0.5;
               p.presence = 0.25; p.resonance = 0.4; p.sag = 1.8; p.coreHz = 50.0;
               p.cabinet = CabinetChoice::none; p.damping = 0.8;
               p.outputDb = -2.0; p.mix = 0.55; return p; }() },

        // Five valves, everything up, cabinet close on the cap. The loudest,
        // dirtiest thing the plugin does, and a legitimate starting point.
        { "Modern: five valves",
          [] { Parameters p; p.voicing = Voicing::modern; p.extraStages = 2;
               p.gainDb = 42.0; p.masterDb = -4.0; p.bass = 0.75; p.middle = 0.2;
               p.treble = 0.7; p.presence = 0.55; p.resonance = 0.45; p.sag = 0.5;
               p.coreHz = 45.0; p.cabinet = CabinetChoice::british;
               p.micPosition = 0.15; p.micDistanceCm = 2.5; p.damping = 3.0;
               p.outputDb = -3.0; return p; }() },

        // Room, not grille: the microphone backed off and moved to the cone
        // edge, which is dark and open where the cap is bright and direct.
        { "Vintage: edge, backed off",
          [] { Parameters p; p.voicing = Voicing::vintage; p.gainDb = 20.0;
               p.masterDb = -8.0; p.bass = 0.55; p.middle = 0.6; p.treble = 0.6;
               p.presence = 0.4; p.resonance = 0.25; p.sag = 1.0; p.coreHz = 55.0;
               p.cabinet = CabinetChoice::vintage; p.micPosition = 0.8;
               p.micDistanceCm = 25.0; p.damping = 0.9; return p; }() },
    };

    static constexpr int count = static_cast<int> (std::size (presets));
    return presets[static_cast<std::size_t> (juce::jlimit (0, count - 1, index))];
}

constexpr int kNumPresets = 9;
} // namespace

int AnvilProcessor::getNumPrograms() { return kNumPresets; }

const juce::String AnvilProcessor::getProgramName (int index)
{
    return presetAt (index).name;
}

void AnvilProcessor::setCurrentProgram (int index)
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
    set (ids::voicing,      static_cast<float> (static_cast<int> (preset.voicing)));
    set (ids::gain,         static_cast<float> (preset.gainDb));
    set (ids::stages,       static_cast<float> (preset.extraStages));

    set (ids::bass,         static_cast<float> (preset.bass));
    set (ids::middle,       static_cast<float> (preset.middle));
    set (ids::treble,       static_cast<float> (preset.treble));

    set (ids::master,       static_cast<float> (preset.masterDb));
    set (ids::presence,     static_cast<float> (preset.presence));
    set (ids::resonance,    static_cast<float> (preset.resonance));
    set (ids::sag,          static_cast<float> (preset.sag));
    set (ids::core,         static_cast<float> (preset.coreHz));

    set (ids::cabinet,      static_cast<float> (static_cast<int> (preset.cabinet)));
    set (ids::micPosition,  static_cast<float> (preset.micPosition));
    set (ids::micDistance,  static_cast<float> (preset.micDistanceCm));
    set (ids::damping,      static_cast<float> (preset.damping));

    set (ids::output,       static_cast<float> (preset.outputDb));
    set (ids::mix,          static_cast<float> (preset.mix));
    set (ids::oversampling, static_cast<float> (static_cast<int> (preset.oversampling)));
}

juce::AudioProcessorEditor* AnvilProcessor::createEditor()
{
    return new AnvilEditor (*this);
}

} // namespace tezla::anvil

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::anvil::AnvilProcessor();
}
