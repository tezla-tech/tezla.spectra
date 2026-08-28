#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include <tezla/ui/StateIds.hpp>
#include <tezla/dsp/Decibels.hpp>

namespace tezla::capstone
{

namespace
{
// Every parameter carries the schema version it was introduced at, and keeps it
// forever: the version hint feeds the VST3 parameter ID, so moving it on a live
// parameter is indistinguishable from renaming it.
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;
constexpr auto kStateTypeName = "CapstoneState";

/// A skew that puts the useful part of a range in the middle of the travel.
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
            // "-0.0 dB", which reads as a bug rather than as the last digit of
            // a float.
            const float shown = std::abs (value) < 0.05f ? 0.0f : value;
            return juce::String (shown, decimals) + " dB";
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

/// Milliseconds, with a real zero at the bottom rather than "0.0 ms" meaning
/// something very small. Attack at zero is a different mode, not a fast one.
juce::AudioParameterFloatAttributes millisecondAttributes (bool zeroIsOff)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("ms")
        .withStringFromValueFunction ([zeroIsOff] (float value, int)
        {
            if (zeroIsOff && value <= 0.0f)
                return juce::String ("Off");

            if (value >= 100.0f)
                return juce::String (juce::roundToInt (value)) + " ms";

            return juce::String (value, value < 1.0f ? 2 : 1) + " ms";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            return trimmed.startsWithIgnoreCase ("off") ? 0.0f : trimmed.getFloatValue();
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

juce::String rateText (double rate)
{
    return juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " kHz";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout CapstoneProcessor::createParameterLayout()
{
    using Parameter  = juce::AudioParameterFloat;
    using Choice     = juce::AudioParameterChoice;
    using Boolean    = juce::AudioParameterBool;
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- drive ---------------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::threshold, kSchemaV1 }, "Threshold",
        juce::NormalisableRange<float> { -30.0f, 0.0f }, 0.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::ceiling, kSchemaV1 }, "Ceiling",
        skewedRange (-24.0f, 6.0f, -3.0f), -0.3f,
        decibelAttributes()));

    // ---- clip ----------------------------------------------------------------

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::clipOn, kSchemaV1 }, "Clip", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::clipThreshold, kSchemaV1 }, "Clip Threshold",
        juce::NormalisableRange<float> { -12.0f, 6.0f }, 0.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::clipShape, kSchemaV1 }, "Clip Shape",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
        Attributes().withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.02f) return juce::String ("Hard");
            if (value > 0.98f) return juce::String ("Soft");
            return juce::String (juce::roundToInt (value * 100.0f)) + "% soft";
        })
        // Without this JUCE parses typed text with getFloatValue(), which reads
        // "Hard" as zero and "50% soft" as fifty. Steinberg's validator reports
        // it and it is not a warning to leave alone.
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();

            if (trimmed.startsWithIgnoreCase ("hard")) return 0.0f;
            if (trimmed.startsWithIgnoreCase ("soft")) return 1.0f;

            return juce::jlimit (0.0f, 1.0f, trimmed.getFloatValue() * 0.01f);
        })));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::clipOversampling, kSchemaV1 }, "Clip Oversampling",
        choices::clipOversampling, 0));

    // ---- limit ---------------------------------------------------------------

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::limitOn, kSchemaV1 }, "Limit", true));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::lookaheadOn, kSchemaV1 }, "Lookahead", true));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::attack, kSchemaV1 }, "Attack",
        skewedRange (0.0f, static_cast<float> (dsp::LimiterCore::kMaxAttackMs), 2.0f), 1.0f,
        millisecondAttributes (true)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::hold, kSchemaV1 }, "Hold",
        skewedRange (0.0f, static_cast<float> (dsp::LimiterCore::kMaxHoldMs), 10.0f), 5.0f,
        millisecondAttributes (true)));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::release, kSchemaV1 }, "Release",
        skewedRange (1.0f, 2000.0f, 150.0f), 100.0f,
        millisecondAttributes (false)));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::autoRelease, kSchemaV1 }, "Auto Release", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::knee, kSchemaV1 }, "Knee",
        juce::NormalisableRange<float> { 0.0f, 24.0f }, 0.0f,
        Attributes().withLabel ("dB").withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.05f)
                return juce::String ("Hard");
            return juce::String (value, 1) + " dB";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            return trimmed.startsWithIgnoreCase ("hard") ? 0.0f : trimmed.getFloatValue();
        })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::stereoLink, kSchemaV1 }, "Stereo Link",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f,
        percentAttributes()));

    // ---- metering ------------------------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::truePeak, kSchemaV1 }, "True Peak",
        choices::truePeak, static_cast<int> (dsp::TruePeakMode::Standard)));

    // ---- global --------------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        juce::NormalisableRange<float> { -12.0f, 12.0f }, 0.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::listen, kSchemaV1 }, "Listen", false));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::bypass, kSchemaV1 }, "Bypass", false));

    return layout;
}

CapstoneProcessor::CapstoneProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));
}

bool CapstoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& main = layouts.getMainOutputChannelSet();

    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == main;
}

void CapstoneProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
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
    // very first push take effect rather than being swallowed.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.reset();

    reportedLatency_ = engine_.getLatencySamples();
    setLatencySamples (reportedLatency_);

    prepared_ = true;
}

void CapstoneProcessor::pullParameters()
{
    const auto value = [this] (const char* id)
    {
        return static_cast<double> (state_.getRawParameterValue (id)->load());
    };

    const auto flag = [this] (const char* id)
    {
        return state_.getRawParameterValue (id)->load() > 0.5f;
    };

    parameters_.thresholdDb = value (ids::threshold);
    parameters_.ceilingDb   = value (ids::ceiling);

    parameters_.clipOn          = flag (ids::clipOn);
    parameters_.clipThresholdDb = value (ids::clipThreshold);
    parameters_.clipShape       = value (ids::clipShape);
    parameters_.clipOversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (value (ids::clipOversampling))));

    parameters_.limitOn     = flag (ids::limitOn);
    parameters_.lookaheadOn = flag (ids::lookaheadOn);
    parameters_.attackMs    = value (ids::attack);
    parameters_.holdMs      = value (ids::hold);
    parameters_.releaseMs   = value (ids::release);
    parameters_.autoRelease = flag (ids::autoRelease);
    parameters_.kneeDb      = value (ids::knee);
    parameters_.stereoLink  = value (ids::stereoLink);

    parameters_.truePeak = static_cast<dsp::TruePeakMode> (
        juce::jlimit (0, 2, static_cast<int> (value (ids::truePeak))));

    parameters_.outputDb = value (ids::output);
    parameters_.listen   = flag (ids::listen);
    parameters_.bypass   = bypassParameter_ != nullptr && bypassParameter_->get();
}

template <typename FloatType>
void CapstoneProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
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

    meters_.limiterReductionDb.store (static_cast<float> (engine_.getLimiterReductionDb()),
                                      std::memory_order_relaxed);
    meters_.clipReductionDb.store (static_cast<float> (engine_.getClipReductionDb()),
                                   std::memory_order_relaxed);
}

void CapstoneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void CapstoneProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

juce::String CapstoneProcessor::describeTruePeak() const
{
    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    const auto mode = static_cast<dsp::TruePeakMode> (
        juce::jlimit (0, 2, static_cast<int> (state_.getRawParameterValue (ids::truePeak)->load())));

    const int factor = dsp::truePeakFactorFor (mode, hostRate);

    juce::String description;

    switch (mode)
    {
        case dsp::TruePeakMode::Off:
            description
                << "Sample peak. Cheapest, and wrong about what your converter will produce: "
                << "measured 1.5 dB over the ceiling on dense high content, and 3.01 dB over "
                << "on a tone at a quarter of the sample rate. Use it when something later "
                << "in the chain is doing the true-peak work.";
            break;

        case dsp::TruePeakMode::Standard:
            description
                << "The ITU's own filter, interpolating x" << factor << ". Agrees with every "
                << "other dBTP meter, which is what you want for a delivery spec -- but it is "
                << "not a guarantee: measured 0.26 dB over the ceiling on dense high content. "
                << "About 1.4x the CPU of Off.";
            break;

        case dsp::TruePeakMode::Strict:
            description
                << "Interpolating x" << factor << ", which holds the ceiling on everything "
                << "measured here -- 0.00 dB over. About 4x the CPU of Standard, and at "
                << rateText (hostRate) << " that is the most expensive setting in the plugin. "
                << "Worth it on a master, wasted on a drum bus that something else will limit.";
            break;
    }

    description << " The ratio is the same at every session rate: for content near Nyquist it "
                << "is the ratio, not the rate, that bounds the error.";

    return description;
}

juce::String CapstoneProcessor::describeClipOversampling() const
{
    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    const auto mode = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, 4, static_cast<int> (
            state_.getRawParameterValue (ids::clipOversampling)->load())));

    const int factor = dsp::oversamplingFactor (mode, hostRate);
    const int latency = dsp::Oversampler::latencyForFactor (factor);

    juce::String description;
    description << "Your session is at " << rateText (hostRate) << ". ";

    if (factor == 1 && mode == dsp::OversamplingMode::Auto)
        description
            << "Auto is running x1: at " << rateText (hostRate) << " there is already room "
            << "above the audio band for the clipper's harmonics, so it spends no CPU and "
            << "adds no latency. Measured -96 dB of audible-band aliasing there.";
    else if (factor == 1)
        description
            << "Oversampling is off. The clipper still band-limits its own curve, which gets "
            << "it to -57 dB of audible-band aliasing at 48 kHz -- audible on a hard corner, "
            << "and exactly the raw sound some material wants.";
    else
        description
            << (mode == dsp::OversamplingMode::Auto ? "Auto is running x" : "Running x")
            << factor << ", clipping at " << rateText (hostRate * factor) << " internally. "
            << "Measured " << (factor >= 8 ? "-115" : factor >= 4 ? "-96" : "-77")
            << " dB of audible-band aliasing at 48 kHz, against a house target of -60. "
            << "Costs " << juce::String (1000.0 * latency / hostRate, 2)
            << " ms of latency, which the host compensates.";

    return description;
}

juce::String CapstoneProcessor::describeLatency() const
{
    const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_;

    if (reportedLatency_ > 0)
    {
        juce::String description;
        description
            << "Latency " << reportedLatency_ << " samples, "
            << juce::String (1000.0 * reportedLatency_ / hostRate, 2)
            << " ms. The host compensates for it, so the plugin stays in time -- but live "
            << "monitoring through it will not. Look-ahead is most of it; Hold adds none.";

        return description;
    }

    // Zero, and the reason has to be read off the controls rather than assumed.
    // Saying "look-ahead and the true-peak filter are both off" when they are
    // not is worse than saying nothing: it is the plugin reporting on itself,
    // and a user has no way to tell it is wrong.
    const auto flag = [this] (const char* id)
    {
        return state_.getRawParameterValue (id)->load() > 0.5f;
    };

    if (! prepared_)
        return "No latency reported yet -- the host has not started audio, so nothing has "
               "been prepared. The number appears as soon as it does.";

    juce::StringArray reasons;

    if (! flag (ids::limitOn))
        reasons.add ("the limiter is off");
    else if (! flag (ids::lookaheadOn))
        reasons.add ("look-ahead is off");

    if (! flag (ids::clipOn))
        reasons.add ("the clipper is off");
    else if (dsp::oversamplingFactor (static_cast<dsp::OversamplingMode> (
                 juce::jlimit (0, 4, static_cast<int> (
                     state_.getRawParameterValue (ids::clipOversampling)->load()))),
                 hostRate) == 1)
        reasons.add ("the clipper is running at the session rate");

    juce::String description { "No latency at all" };

    if (! reasons.isEmpty())
        description << ": " << reasons.joinIntoString (", and ");

    description << ". Nothing here needs future samples, so the plugin is safe to monitor "
                << "through live.";

    return description;
}

void CapstoneProcessor::getStateInformation (juce::MemoryBlock& destData)
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

void CapstoneProcessor::setStateInformation (const void* data, int sizeInBytes)
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
/// Factory presets. Small and opinionated, aimed at the work this plugin exists
/// for. Each one is a full parameter set, so loading a preset never leaves a
/// stale control behind.
struct Preset
{
    const char* name;
    Parameters  parameters;
};

const Preset& presetAt (int index)
{
    static const Preset presets[]
    {
        // The reference point, and deliberately first: a limiter doing nothing.
        // Both stages off, no drive, no trim -- bit-exact, and the thing to A/B
        // everything else against.
        { "Clean (bit-exact)",
          [] { Parameters p; p.clipOn = false; p.limitOn = false;
               p.thresholdDb = 0.0; p.outputDb = 0.0; return p; }() },

        // A ceiling and nothing else. Long look-ahead, slow release, wide knee:
        // this is the setting that should be inaudible until it is needed.
        { "Master: transparent",
          [] { Parameters p; p.ceilingDb = -1.0; p.thresholdDb = -1.0;
               p.limitOn = true; p.attackMs = 10.0; p.holdMs = 20.0;
               p.releaseMs = 250.0; p.autoRelease = true; p.kneeDb = 6.0;
               p.truePeak = dsp::TruePeakMode::Strict; return p; }() },

        // For delivery. -1.0 dBTP is the usual streaming ceiling and Strict is
        // what makes the number true rather than approximately true.
        { "Master: -1 dBTP delivery",
          [] { Parameters p; p.ceilingDb = -1.0; p.thresholdDb = -4.0;
               p.limitOn = true; p.attackMs = 5.0; p.holdMs = 30.0;
               p.releaseMs = 180.0; p.autoRelease = true; p.kneeDb = 3.0;
               p.truePeak = dsp::TruePeakMode::Strict; return p; }() },

        // The drum bus, and the reason the clipper exists. The clipper shaves
        // the transient tips 2 dB above the ceiling so the limiter never has to
        // duck the whole kit; the limiter then only has 2 dB to find.
        { "Drum bus: clip and catch",
          [] { Parameters p; p.ceilingDb = -0.3; p.thresholdDb = -6.0;
               p.clipOn = true; p.clipThresholdDb = 1.7; p.clipShape = 0.15;
               p.limitOn = true; p.attackMs = 0.5; p.holdMs = 5.0;
               p.releaseMs = 60.0; p.kneeDb = 0.0;
               p.truePeak = dsp::TruePeakMode::Standard; return p; }() },

        // No look-ahead at all, so the clipper is the whole limiter. This is
        // what 0 ms means and it is a sound rather than a compromise: 31 dB of
        // THD, no pumping, and zero latency.
        { "Drum bus: 0 ms hard",
          [] { Parameters p; p.ceilingDb = -0.3; p.thresholdDb = -8.0;
               p.clipOn = true; p.clipThresholdDb = -0.3; p.clipShape = 0.0;
               p.clipOversampling = dsp::OversamplingMode::Auto;
               p.limitOn = false; p.lookaheadOn = false;
               p.truePeak = dsp::TruePeakMode::Off; return p; }() },

        // Sub bass. Long release so the gain does not move within a cycle --
        // a 40 Hz cycle lasts 25 ms, and anything faster than that is
        // distortion rather than limiting. Link at 100% so the sub stays
        // centred, which is the whole point of a sub.
        { "Sub bass: hold it down",
          [] { Parameters p; p.ceilingDb = -0.3; p.thresholdDb = -3.0;
               p.limitOn = true; p.attackMs = 15.0; p.holdMs = 50.0;
               p.releaseMs = 400.0; p.kneeDb = 9.0; p.stereoLink = 1.0;
               p.truePeak = dsp::TruePeakMode::Standard; return p; }() },

        // A reese wants weight without the gain audibly breathing under it, so
        // the knee is wide and the release is slow. Soft clip above the ceiling
        // adds the grit the limiter alone will not.
        { "Reese: weight",
          [] { Parameters p; p.ceilingDb = -0.3; p.thresholdDb = -9.0;
               p.clipOn = true; p.clipThresholdDb = 2.0; p.clipShape = 0.7;
               p.limitOn = true; p.attackMs = 8.0; p.holdMs = 40.0;
               p.releaseMs = 300.0; p.kneeDb = 12.0; p.stereoLink = 0.7;
               p.truePeak = dsp::TruePeakMode::Standard; return p; }() },

        // Everything above the ceiling and nothing else. Ceiling at +3 dB with
        // a hard knee: on a floating-point bus this passes the mix untouched
        // and only ever catches the accident.
        { "Safety: catch the extremes",
          [] { Parameters p; p.ceilingDb = 3.0; p.thresholdDb = 0.0;
               p.limitOn = true; p.attackMs = 2.0; p.holdMs = 10.0;
               p.releaseMs = 120.0; p.kneeDb = 0.0;
               p.truePeak = dsp::TruePeakMode::Standard; return p; }() },

        // Loud, on purpose. 12 dB of drive into a clipper and then a fast
        // limiter -- what a dubstep master actually sounds like, and the
        // preset most likely to be the starting point for one.
        { "Loud: drum and bass master",
          [] { Parameters p; p.ceilingDb = -0.5; p.thresholdDb = -12.0;
               p.clipOn = true; p.clipThresholdDb = 1.0; p.clipShape = 0.25;
               p.limitOn = true; p.attackMs = 1.0; p.holdMs = 15.0;
               p.releaseMs = 90.0; p.autoRelease = true; p.kneeDb = 2.0;
               p.truePeak = dsp::TruePeakMode::Standard; return p; }() },
    };

    static constexpr int count = static_cast<int> (std::size (presets));
    return presets[static_cast<std::size_t> (juce::jlimit (0, count - 1, index))];
}

constexpr int kNumPresets = 9;
} // namespace

int CapstoneProcessor::getNumPrograms() { return kNumPresets; }

const juce::String CapstoneProcessor::getProgramName (int index)
{
    return presetAt (index).name;
}

void CapstoneProcessor::setCurrentProgram (int index)
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
    set (ids::threshold,     static_cast<float> (preset.thresholdDb));
    set (ids::ceiling,       static_cast<float> (preset.ceilingDb));

    set (ids::clipOn,        preset.clipOn ? 1.0f : 0.0f);
    set (ids::clipThreshold, static_cast<float> (preset.clipThresholdDb));
    set (ids::clipShape,     static_cast<float> (preset.clipShape));
    set (ids::clipOversampling, static_cast<float> (static_cast<int> (preset.clipOversampling)));

    set (ids::limitOn,       preset.limitOn ? 1.0f : 0.0f);
    set (ids::lookaheadOn,   preset.lookaheadOn ? 1.0f : 0.0f);
    set (ids::attack,        static_cast<float> (preset.attackMs));
    set (ids::hold,          static_cast<float> (preset.holdMs));
    set (ids::release,       static_cast<float> (preset.releaseMs));
    set (ids::autoRelease,   preset.autoRelease ? 1.0f : 0.0f);
    set (ids::knee,          static_cast<float> (preset.kneeDb));
    set (ids::stereoLink,    static_cast<float> (preset.stereoLink));

    set (ids::truePeak,      static_cast<float> (static_cast<int> (preset.truePeak)));

    set (ids::output,        static_cast<float> (preset.outputDb));
    set (ids::listen,        preset.listen ? 1.0f : 0.0f);
}

juce::AudioProcessorEditor* CapstoneProcessor::createEditor()
{
    return new CapstoneEditor (*this);
}

} // namespace tezla::capstone

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::capstone::CapstoneProcessor();
}
