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
#include <vector>

#include <tezla/ui/StateIds.hpp>

namespace tezla::membrana {

namespace
{
constexpr auto kStateTypeName = "MembranaState";
constexpr int kSchemaV1 = 1;
constexpr int kStateSchemaVersion = kSchemaV1;

[[nodiscard]] float valueOf (const juce::AudioProcessorValueTreeState& state,
                             const juce::String& id)
{
    if (auto* raw = state.getRawParameterValue (id))
        return raw->load();

    return 0.0f;
}

[[nodiscard]] bool flagOf (const juce::AudioProcessorValueTreeState& state,
                           const juce::String& id)
{
    return valueOf (state, id) > 0.5f;
}

struct Setting
{
    const char* id;
    float value;
};

/// A preset is a complete parameter set: every parameter returns to its
/// default first, then the departures apply (the house pattern since
/// Sonitus). No preset names a commercial microphone -- CLAUDE.md
/// section 2.1: the physics continuum is the product, not an imitation.
struct Preset
{
    const char* name;
    const char* description;
    std::vector<Setting> settings;
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list
    {
        // -------------------------------------------------------------------
        {
            "Neutral",
            "The reference condition: 1 m, on axis, nothing riding. Bit-exact "
            "identity, not merely transparent -- the A/B that can be trusted.",
            {}
        },
        // -------------------------------------------------------------------
        {
            "Close & Warm",
            "A cardioid moved in to 8 cm: proximity weight under the voice, "
            "the top settling the way close range really does.",
            {
                { ids::distanceCm, 8.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Radio Chest",
            "The late-night voice: very close, the low limit raised so the "
            "boom stays musical, more of the body's shadow in the top.",
            {
                { ids::distanceCm, 7.0f },
                { ids::lowLimitHz, 60.0f },
                { ids::character, 0.5f },
            }
        },
        // -------------------------------------------------------------------
        {
            "De-Boom",
            "The opposite direction: pattern opened toward omni and the mic "
            "backed to 20 cm, taking proximity OUT of a take recorded too "
            "close.",
            {
                { ids::pattern, 0.25f },
                { ids::distanceCm, 20.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Backed-Off Detail",
            "Room distance with the consonants carried back in: the mic at "
            "45 cm, detail lifting what the distance loses.",
            {
                { ids::distanceCm, 45.0f },
                { ids::detail, 6.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Quiet-Verse Lift",
            "Fully tracked presence: the shelf leans in when the singer backs "
            "off and gets out of the way when they push.",
            {
                { ids::presence, 6.0f },
                { ids::track, 1.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Crisp Small Capsule",
            "A 25 mm body: the presence rise moved up an octave, most of the "
            "raw diffraction kept, a bright grille ring.",
            {
                { ids::capsuleMm, 25.0f },
                { ids::character, 0.7f },
                { ids::grille, 0.35f },
                { ids::grilleHz, 9000.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Podcast Presence",
            "Spoken word: a modest standing shelf at 5 kHz with a little "
            "detail, intelligibility without brightness.",
            {
                { ids::presence, 4.0f },
                { ids::presHz, 5000.0f },
                { ids::track, 0.5f },
                { ids::detail, 3.0f },
            }
        },
    };

    return list;
}
} // namespace

MembranaProcessor::MembranaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, juce::Identifier (kStateTypeName), createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));
}

juce::AudioProcessorValueTreeState::ParameterLayout
MembranaProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using BoolParameter = juce::AudioParameterBool;
    using Range = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    // Units through stringFromValue, never withLabel alone -- the Phonoss
    // lesson: JUCE's own text box never reads the label, and a host's
    // automation lane deserves "4.50 kHz", not "4500".
    const auto attributes = [] (const char* label)
    {
        const juce::String unit { label };

        return juce::AudioParameterFloatAttributes{}
            .withLabel (unit)
            .withStringFromValueFunction ([unit] (float value, int)
            {
                if (unit == "Hz")
                {
                    return value < 1000.0f
                             ? juce::String (juce::roundToInt (value)) + " Hz"
                             : juce::String (value / 1000.0f, 2) + " kHz";
                }

                if (unit == "cm")
                {
                    // 100 cm IS the reference condition the whole stage is
                    // computed against; say so where the number is.
                    if (value > 99.5f)
                        return juce::String ("1 m (ref)");

                    return value < 10.0f
                             ? juce::String (value, 1) + " cm"
                             : juce::String (juce::roundToInt (value)) + " cm";
                }

                if (unit == "deg")
                {
                    if (value < 0.5f)
                        return juce::String ("on axis");

                    return juce::String (juce::roundToInt (value))
                           + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
                }

                if (unit == "pattern")
                {
                    // The landmarks are the names the control is thought in;
                    // between them, the truth is a percentage.
                    if (value < 0.035f)  return juce::String ("Omni");
                    if (std::abs (value - 0.25f) < 0.035f) return juce::String ("Subcardioid");
                    if (std::abs (value - 0.5f) < 0.035f)  return juce::String ("Cardioid");
                    if (std::abs (value - 0.75f) < 0.035f) return juce::String ("Hypercardioid");
                    if (value > 0.965f)  return juce::String ("Figure-8");

                    return juce::String (juce::roundToInt (value * 100.0f)) + " %";
                }

                if (unit == "mm")
                    return juce::String (juce::roundToInt (value)) + " mm";

                if (unit == "%")
                    return juce::String (juce::roundToInt (value * 100.0f)) + " %";

                if (unit.isEmpty())
                    return juce::String (value, 2);

                return juce::String (value, 1) + " " + unit;
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return text.getFloatValue();
            });
    };

    // ---- MIC -------------------------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::micOn, kSchemaV1 }, "Mic model", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::pattern, kSchemaV1 }, "Pattern",
        Range (0.0f, 1.0f, 0.001f), 0.5f, attributes ("pattern")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::capsuleMm, kSchemaV1 }, "Body",
        Range (20.0f, 60.0f, 0.5f), 50.0f, attributes ("mm")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::character, kSchemaV1 }, "Character",
        Range (0.0f, 1.0f, 0.001f), 0.35f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::grille, kSchemaV1 }, "Grille",
        Range (0.0f, 1.0f, 0.001f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::grilleHz, kSchemaV1 }, "Grille freq",
        Range (3000.0f, 12000.0f, 10.0f, 0.5f), 7000.0f, attributes ("Hz")));

    // ---- POSITION --------------------------------------------------------

    // Log-skewed so the working range -- a hand-span from the grille --
    // sits mid-travel: skew 0.343 puts 15 cm at the centre.
    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::distanceCm, kSchemaV1 }, "Distance",
        Range (2.0f, 100.0f, 0.1f, 0.343f), 100.0f, attributes ("cm")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::axisDeg, kSchemaV1 }, "Off-axis",
        Range (0.0f, 90.0f, 0.5f), 0.0f, attributes ("deg")));

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::autoLevel, kSchemaV1 }, "Auto level", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::lowLimitHz, kSchemaV1 }, "LF limit",
        Range (20.0f, 120.0f, 1.0f, 0.6f), 40.0f, attributes ("Hz")));

    // ---- PRESENCE --------------------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::presenceOn, kSchemaV1 }, "Presence", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::presence, kSchemaV1 }, "Presence",
        Range (0.0f, 9.0f, 0.05f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::presHz, kSchemaV1 }, "Pres freq",
        Range (2000.0f, 8000.0f, 10.0f, 0.5f), 4500.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::presThresh, kSchemaV1 }, "Pres thresh",
        Range (-60.0f, 0.0f, 0.1f), -28.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::track, kSchemaV1 }, "Track",
        Range (0.0f, 1.0f, 0.001f), 0.65f, attributes ("%")));

    // ---- DETAIL ----------------------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::detailOn, kSchemaV1 }, "Detail", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::detail, kSchemaV1 }, "Detail",
        Range (0.0f, 12.0f, 0.05f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::detHz, kSchemaV1 }, "Detail split",
        Range (1500.0f, 8000.0f, 10.0f, 0.5f), 3000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::detFloor, kSchemaV1 }, "Floor",
        Range (-90.0f, -30.0f, 0.5f), -55.0f, attributes ("dB")));

    // ---- OUTPUT ----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        Range (-24.0f, 24.0f, 0.1f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::bypass, kSchemaV1 }, "Bypass", false));

    return { parameters.begin(), parameters.end() };
}

bool MembranaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void MembranaProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    engine_.prepare (sampleRate);

    const int preparedBlock = juce::jmax (16, maximumExpectedSamplesPerBlock);
    scratch_.setSize (MembranaEngine::kChannels, preparedBlock, false, false, true);
    dry_.setSize (MembranaEngine::kChannels, preparedBlock, false, false, true);

    // Latency 0 by construction: the capsule FIR is minimum-phase and
    // nothing looks ahead. The mixer still runs -- a bypass that switches
    // rather than crossfades clicks whatever the latency is.
    bypassMixer_.prepare (sampleRate, 0, MembranaEngine::kChannels);
    bypassMixer_.reset (bypassParameter_ != nullptr && bypassParameter_->get());

    setLatencySamples (0);

    // prepare() resets, so the current parameters are pushed again -- what
    // prepare built is re-checked against what it actually built
    // (CLAUDE.md section 7, the Emberdrive lesson).
    pullParameters();
    engine_.setSettings (settings_);
}

void MembranaProcessor::pullParameters()
{
    settings_ = settingsFromParameters();
}

bool MembranaProcessor::isIdentity() const
{
    return MembranaEngine::isIdentity (settingsFromParameters());
}

MembranaEngine::Settings MembranaProcessor::settingsFromParameters() const
{
    MembranaEngine::Settings settings;

    settings.mic.on = flagOf (state_, ids::micOn);
    settings.mic.pattern01 = valueOf (state_, ids::pattern);
    settings.mic.capsuleMm = valueOf (state_, ids::capsuleMm);
    settings.mic.character01 = valueOf (state_, ids::character);
    settings.mic.grille01 = valueOf (state_, ids::grille);
    settings.mic.grilleHz = valueOf (state_, ids::grilleHz);
    settings.mic.distanceCm = valueOf (state_, ids::distanceCm);
    settings.mic.axisDeg = valueOf (state_, ids::axisDeg);
    settings.mic.autoLevel = flagOf (state_, ids::autoLevel);
    settings.mic.lowLimitHz = valueOf (state_, ids::lowLimitHz);

    settings.presence.on = flagOf (state_, ids::presenceOn);
    settings.presence.amountDb = valueOf (state_, ids::presence);
    settings.presence.frequencyHz = valueOf (state_, ids::presHz);
    settings.presence.thresholdDb = valueOf (state_, ids::presThresh);
    settings.presence.track01 = valueOf (state_, ids::track);

    settings.detail.on = flagOf (state_, ids::detailOn);
    settings.detail.amountDb = valueOf (state_, ids::detail);
    settings.detail.splitHz = valueOf (state_, ids::detHz);
    settings.detail.floorDb = valueOf (state_, ids::detFloor);

    settings.outputDb = valueOf (state_, ids::output);

    return settings;
}

template <typename FloatType>
void MembranaProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();

    for (int channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0)
        return;

    const int channels = juce::jlimit (1, MembranaEngine::kChannels,
                                       std::min (inputChannels, outputChannels));

    if (numSamples > scratch_.getNumSamples())
    {
        scratch_.setSize (MembranaEngine::kChannels, numSamples, false, true, true);
        dry_.setSize (MembranaEngine::kChannels, numSamples, false, true, true);
    }

    pullParameters();
    engine_.setSettings (settings_);

    float inputPeak = 0.0f;

    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = scratch_.getWritePointer (channel);
        auto* dry = dry_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto value = static_cast<double> (source[i]);

            destination[i] = value;
            dry[i] = value;
            inputPeak = std::max (inputPeak, std::abs (static_cast<float> (value)));
        }
    }

    // A mono bus runs the stereo engine with the second channel fed from
    // the first: the decisions are linked, so the answer is the same.
    if (channels == 1)
    {
        scratch_.copyFrom (1, 0, scratch_, 0, 0, numSamples);
        dry_.copyFrom (1, 0, dry_, 0, 0, numSamples);
    }

    engine_.process (scratch_.getWritePointer (0), scratch_.getWritePointer (1), numSamples);

    bypassMixer_.setBypassed (bypassParameter_ != nullptr && bypassParameter_->get());
    bypassMixer_.process (scratch_.getArrayOfWritePointers(),
                          dry_.getArrayOfReadPointers(),
                          MembranaEngine::kChannels, numSamples);

    float outputPeak = 0.0f;

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* processed = scratch_.getReadPointer (channel);
        auto* destination = buffer.getWritePointer (channel);

        for (int i = 0; i < numSamples; ++i)
        {
            destination[i] = static_cast<FloatType> (processed[i]);
            outputPeak = std::max (outputPeak, std::abs (static_cast<float> (processed[i])));
        }
    }

    const auto peakDb = [] (float linear)
    {
        return linear > 0.0f ? 20.0f * std::log10 (linear) : -100.0f;
    };

    const auto stage = engine_.getMeters();

    meters_.presenceLiftDb.store (static_cast<float> (stage.presenceLiftDb), std::memory_order_relaxed);
    meters_.detailLiftDb.store (static_cast<float> (stage.detailLiftDb), std::memory_order_relaxed);
    meters_.capsuleTrimDb.store (static_cast<float> (stage.capsuleTrimDb), std::memory_order_relaxed);
    meters_.inputDb.store (peakDb (inputPeak), std::memory_order_relaxed);
    meters_.outputDb.store (peakDb (outputPeak), std::memory_order_relaxed);
}

void MembranaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void MembranaProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void MembranaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    state.appendChild (abCompare_.toValueTree(), nullptr);
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MembranaProcessor::setStateInformation (const void* data, int sizeInBytes)
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

int MembranaProcessor::getNumPrograms()
{
    return static_cast<int> (presets().size());
}

const juce::String MembranaProcessor::getProgramName (int index)
{
    const auto& list = presets();
    const auto count = static_cast<int> (list.size());

    if (index < 0 || index >= count)
        return {};

    return list[static_cast<std::size_t> (index)].name;
}

void MembranaProcessor::setCurrentProgram (int index)
{
    const auto& list = presets();
    const auto count = static_cast<int> (list.size());

    if (count == 0)
        return;

    currentProgram_ = juce::jlimit (0, count - 1, index);

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (ranged->paramID != ids::bypass)
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& setting : list[static_cast<std::size_t> (currentProgram_)].settings)
        if (auto* parameter = state_.getParameter (setting.id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (setting.value));
}

juce::AudioProcessorEditor* MembranaProcessor::createEditor()
{
    return new MembranaEditor (*this);
}

} // namespace tezla::membrana

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::membrana::MembranaProcessor();
}
