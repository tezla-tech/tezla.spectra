#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <tezla/dsp/Decibels.hpp>

namespace tezla::emberdrive
{

namespace
{
constexpr int kStateSchemaVersion = 1;
constexpr auto kStateTypeName = "EmberdriveState";
constexpr double kBypassFadeSeconds = 0.010;

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
        juce::ParameterID { ids::drive, kStateSchemaVersion }, "Drive",
        skewedRange (0.0f, 30.0f, 12.0f), 0.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::character, kStateSchemaVersion }, "Character",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.35f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.02f) return juce::String ("Tape");
            if (value > 0.98f) return juce::String ("Valve");
            return juce::String (juce::roundToInt (value * 100.0f)) + "% valve";
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::tone, kStateSchemaVersion }, "Tone",
        juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (std::abs (value) < 0.02f) return juce::String ("Flat");
            return juce::String (value > 0.0f ? "Bright " : "Dark ")
                 + juce::String (juce::roundToInt (std::abs (value) * 100.0f)) + "%";
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::ceiling, kStateSchemaVersion }, "Ceiling",
        juce::NormalisableRange<float> { -24.0f, 0.0f }, -0.3f,
        formatted ("dBFS", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::knee, kStateSchemaVersion }, "Knee",
        juce::NormalisableRange<float> { 0.0f, 24.0f }, 6.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::speed, kStateSchemaVersion }, "Speed",
        skewedRange (0.05f, 100.0f, 5.0f), 5.0f,
        millisecondAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::release, kStateSchemaVersion }, "Release",
        skewedRange (20.0f, 2000.0f, 200.0f), 200.0f,
        millisecondAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::autoRelease, kStateSchemaVersion }, "Auto Release", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::mix, kStateSchemaVersion }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f }, 100.0f,
        formatted ("%", 0)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kStateSchemaVersion }, "Output",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        formatted ("dB", 1)));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::autoTrim, kStateSchemaVersion }, "Auto Trim", true));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::oversampling, kStateSchemaVersion }, "Oversampling",
        juce::StringArray { "Auto", "Off", "x2", "x4", "x8" }, 0));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::bypass, kStateSchemaVersion }, "Bypass", false));

    return layout;
}

EmberdriveProcessor::EmberdriveProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));
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

    bypassMix_ = bypassParameter_ != nullptr && bypassParameter_->get() ? 1.0 : 0.0;

    updateLatency (engine_.getLatencySamples());
}

void EmberdriveProcessor::updateLatency (int engineLatencySamples)
{
    reportedLatency_ = engineLatencySamples;
    setLatencySamples (reportedLatency_);

    // The bypass path is delayed by exactly the latency the host is told about,
    // so switching bypass does not shift the timing. +1 keeps the buffer valid
    // when the latency is zero.
    bypassDelay_.setSize (juce::jmax (1, getTotalNumOutputChannels()),
                          juce::jmax (1, reportedLatency_ + 1), false, true, true);
    bypassDelay_.clear();
    bypassDelayWrite_ = 0;
}

void EmberdriveProcessor::pullParameters()
{
    const auto value = [this] (const char* id)
    {
        return static_cast<double> (state_.getRawParameterValue (id)->load());
    };

    parameters_.driveDb     = value (ids::drive);
    parameters_.character   = value (ids::character);
    parameters_.toneTilt    = value (ids::tone);
    parameters_.ceilingDb   = value (ids::ceiling);
    parameters_.kneeDb      = value (ids::knee);
    parameters_.attackMs    = value (ids::speed);
    parameters_.releaseMs   = value (ids::release);
    parameters_.autoRelease = value (ids::autoRelease) > 0.5;
    parameters_.mix         = value (ids::mix) / 100.0;
    parameters_.outputDb    = value (ids::output);
    parameters_.autoTrim    = value (ids::autoTrim) > 0.5;

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (value (ids::oversampling))));
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

    pullParameters();
    if (engine_.setParameters (parameters_))
        updateLatency (engine_.getLatencySamples());

    if (scratch_.getNumSamples() < numSamples || scratch_.getNumChannels() < numChannels)
        scratch_.setSize (numChannels, numSamples, false, true, true);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* destination = scratch_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<double> (source[i]);

        inputMeter_[channel].processBlock (destination, numSamples);
        channelPointers_[static_cast<std::size_t> (channel)] = destination;
    }

    // Keep the delayed dry copy running whether or not bypass is engaged, so
    // engaging it mid-note does not read stale samples.
    const int delayLength = juce::jmax (1, reportedLatency_ + 1);
    for (int channel = 0; channel < numChannels && channel < bypassDelay_.getNumChannels(); ++channel)
    {
        auto* delayLine = bypassDelay_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);
        int write = bypassDelayWrite_;

        for (int i = 0; i < numSamples; ++i)
        {
            delayLine[write] = static_cast<double> (source[i]);
            write = (write + 1) % delayLength;
        }
    }

    engine_.process (channelPointers_.data(), numChannels, numSamples);

    const bool bypassRequested = bypassParameter_ != nullptr && bypassParameter_->get();
    const double bypassTarget = bypassRequested ? 1.0 : 0.0;
    const double fadeStep = 1.0 / juce::jmax (1.0, kBypassFadeSeconds * sampleRate_);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* processed = scratch_.getReadPointer (channel);
        auto* destination = buffer.getWritePointer (channel);

        const double* delayLine = channel < bypassDelay_.getNumChannels()
                                ? bypassDelay_.getReadPointer (channel) : nullptr;

        int read = bypassDelayWrite_;
        double mix = bypassMix_;

        for (int i = 0; i < numSamples; ++i)
        {
            mix = bypassTarget > mix ? juce::jmin (bypassTarget, mix + fadeStep)
                                     : juce::jmax (bypassTarget, mix - fadeStep);

            const double dry = delayLine != nullptr ? delayLine[read] : 0.0;
            destination[i] = static_cast<FloatType> (processed[i] * (1.0 - mix) + dry * mix);

            read = (read + 1) % delayLength;
        }

        if (channel == numChannels - 1)
            bypassMix_ = mix;

        outputMeter_[channel].processBlock (scratch_.getReadPointer (channel), numSamples);
    }

    bypassDelayWrite_ = (bypassDelayWrite_ + numSamples) % delayLength;

    meters_.inputVuDb.store    (static_cast<float> (inputMeter_[0].getVuDb()),    std::memory_order_relaxed);
    meters_.inputPeakDb.store  (static_cast<float> (inputMeter_[0].getPeakDb()),  std::memory_order_relaxed);
    meters_.outputVuDb.store   (static_cast<float> (outputMeter_[0].getVuDb()),   std::memory_order_relaxed);
    meters_.outputPeakDb.store (static_cast<float> (outputMeter_[0].getPeakDb()), std::memory_order_relaxed);
    meters_.gainReductionDb.store (static_cast<float> (engine_.getGainReductionDb()), std::memory_order_relaxed);
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

    // Only version 1 exists so far. When version 2 arrives, migrate here rather
    // than dropping the state on the floor.
    const int version = tree.getProperty ("schemaVersion", 1);
    if (version > kStateSchemaVersion)
        return;

    state_.replaceState (tree);
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
    };

    return presets[juce::jlimit (0, static_cast<int> (std::size (presets)) - 1, index)];
}

constexpr int kNumPresets = 5;
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
