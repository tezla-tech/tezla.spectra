#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <tezla/dsp/Decibels.hpp>

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
constexpr int kStateSchemaVersion = kSchemaV1;
constexpr auto kStateTypeName = "HaloState";
constexpr double kBypassFadeSeconds = 0.010;

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

    return layout;
}

HaloProcessor::HaloProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));
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

void HaloProcessor::updateLatency (int engineLatencySamples)
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

    parameters_.bandMode  = value (ids::bandMode) < 0.5 ? BandMode::Above : BandMode::Below;
    parameters_.focusHz   = value (ids::focus);
    parameters_.drive     = value (ids::drive);
    parameters_.colour    = value (ids::colour);
    parameters_.track     = value (ids::track);
    parameters_.punch     = value (ids::punch);
    parameters_.floorOn   = flag (ids::floorOn);
    parameters_.floorHz   = value (ids::floorHz);
    parameters_.ceilingOn = flag (ids::ceilingOn);
    parameters_.ceilingHz = value (ids::ceilingHz);
    parameters_.amountDb  = value (ids::amount);
    parameters_.listen    = flag (ids::listen);
    parameters_.autoTrim  = flag (ids::autoTrim);
    parameters_.inputDb   = value (ids::input);
    parameters_.outputDb  = value (ids::output);

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (value (ids::oversampling))));
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

    // A version from the future is refused rather than half-loaded: a partial
    // parameter set is worse than the plugin's defaults, because it looks like
    // it worked.
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
          [] { Parameters p; p.amountDb = Engine::kAmountSilenceDb; p.drive = 0.0; return p; }() },

        // Vocals and the top of a mix: second-harmonic sheen well above the
        // presence region, with the very top kept out of it.
        { "Air",
          [] { Parameters p; p.focusHz = 5000.0; p.drive = 0.35; p.colour = 0.8;
               p.track = 0.5; p.ceilingHz = 17000.0; p.amountDb = -3.0; return p; }() },

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
               p.track = 1.0; p.ceilingHz = 18000.0; p.amountDb = -8.0; return p; }() },
    };

    static constexpr int count = static_cast<int> (std::size (presets));
    return presets[static_cast<std::size_t> (juce::jlimit (0, count - 1, index))];
}

constexpr int kNumPresets = 6;
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
    set (ids::amount,    static_cast<float> (preset.amountDb));
    set (ids::listen,    preset.listen ? 1.0f : 0.0f);
    set (ids::autoTrim,  preset.autoTrim ? 1.0f : 0.0f);
    set (ids::input,     static_cast<float> (preset.inputDb));
    set (ids::output,    static_cast<float> (preset.outputDb));
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
