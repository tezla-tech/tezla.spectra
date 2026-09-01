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

namespace tezla::phonoss {

namespace
{
constexpr auto kStateTypeName = "PhonossState";
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

/// One control set to one value.
struct Setting
{
    const char* id;
    float value;
};

/// A preset is a name, a sentence saying what it is for, and a list of
/// departures from the defaults.
///
/// **Every preset is applied by resetting every parameter to its default
/// first**, so a preset is a complete parameter set rather than a patch over
/// whatever happened to be loaded before it. This is the house pattern since
/// Sonitus, and on a strip it matters more than anywhere else: fifty controls
/// means fifty ways for a leftover value to survive a preset change and be
/// blamed on the preset.
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
            "Rap Lead",
            "The defaults with the gate opened up: a lead vocal that sits "
            "forward and stays there.",
            {
                { ids::gateOn, 1.0f },
                { ids::gateThreshold, -42.0f },
                { ids::gateRange, 12.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Rap Ad-lib",
            "Harder and brighter, and gated tighter -- an ad-lib is a short "
            "shout with room tone either side of it, and it has to cut through "
            "the lead rather than sit with it.",
            {
                { ids::gateOn, 1.0f },
                { ids::gateThreshold, -36.0f },
                { ids::gateRange, 24.0f },
                { ids::gateHold, 25.0f },
                { ids::gateRelease, 60.0f },

                { ids::comp1Threshold, -26.0f },
                { ids::comp1Ratio, 3.5f },
                { ids::comp1Makeup, 5.0f },

                { ids::comp2Threshold, -14.0f },
                { ids::comp2Ratio, 8.0f },

                { ids::eqMidDb, 2.5f },
                { ids::eqHighDb, 3.0f },
                { ids::deEssRange, 8.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Rap Double",
            "For the layer under the lead. Levelled hard so it never peeks out, "
            "and rolled off on top so it thickens the lead instead of "
            "competing with its consonants.",
            {
                { ids::highpass, 110.0f },

                { ids::comp1Threshold, -30.0f },
                { ids::comp1Ratio, 4.0f },
                { ids::comp1Attack, 15.0f },
                { ids::comp1Makeup, 4.0f },

                { ids::comp2Threshold, -14.0f },
                { ids::comp2Ratio, 8.0f },

                { ids::eqHighDb, -3.5f },
                { ids::deEssRange, 12.0f },
                { ids::outputTrim, -3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Aggressive Forward",
            "Everything leaning in: fast, flat and loud, for a take that has to "
            "survive a busy drop. This is the one to reach for when the vocal "
            "keeps disappearing.",
            {
                { ids::gateOn, 1.0f },
                { ids::gateThreshold, -38.0f },
                { ids::gateRange, 18.0f },

                { ids::comp1Threshold, -30.0f },
                { ids::comp1Ratio, 4.0f },
                { ids::comp1Attack, 8.0f },
                { ids::comp1Release, 140.0f },
                { ids::comp1Makeup, 6.0f },

                { ids::comp2Threshold, -16.0f },
                { ids::comp2Ratio, 10.0f },
                { ids::comp2Attack, 1.0f },

                { ids::eqLowDb, -2.0f },
                { ids::eqMidDb, 3.0f },
                { ids::eqHighDb, 3.5f },
                { ids::deEssRange, 10.0f },

                // Measured: without this the preset peaks at 1.0060 on the
                // render probe -- six dB of makeup and two EQ boosts stacked
                // on top of a peak catcher that is holding, not limiting. A
                // preset that clips on its own is a bad preset, whatever is
                // downstream of it.
                { ids::outputTrim, -2.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Sung Verse",
            "Gentle and slow, with the gate out of the way. A sung verse has "
            "quiet ends of phrases that a rap-tuned gate would eat.",
            {
                { ids::highpass, 70.0f },

                { ids::comp1Threshold, -26.0f },
                { ids::comp1Ratio, 2.0f },
                { ids::comp1Attack, 40.0f },
                { ids::comp1Release, 400.0f },
                { ids::comp1Makeup, 3.0f },

                { ids::comp2Threshold, -8.0f },
                { ids::comp2Ratio, 4.0f },

                { ids::eqHighDb, 1.5f },
                { ids::deEssRange, 5.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Sung Chorus",
            "The same voice, holding a bigger part. More levelling and a touch "
            "of weight low down, since a chorus vocal is usually doubled and "
            "needs to stay one thing.",
            {
                { ids::highpass, 70.0f },

                { ids::comp1Threshold, -28.0f },
                { ids::comp1Ratio, 3.0f },
                { ids::comp1Attack, 25.0f },
                { ids::comp1Release, 300.0f },
                { ids::comp1Makeup, 4.5f },

                { ids::comp2Threshold, -10.0f },
                { ids::comp2Ratio, 6.0f },

                { ids::eqLowDb, 1.5f },
                { ids::eqMidDb, 1.0f },
                { ids::eqHighDb, 2.5f },
                { ids::deEssRange, 7.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Gentle Leveller",
            "One compressor, low ratio, nothing else. The setting for a take "
            "that is already good and only needs steadying -- and the one to "
            "start from when a preset is doing too much.",
            {
                { ids::highpass, 60.0f },

                { ids::deEssOn, 0.0f },

                { ids::comp1Threshold, -22.0f },
                { ids::comp1Ratio, 1.8f },
                { ids::comp1Attack, 50.0f },
                { ids::comp1Release, 500.0f },
                { ids::comp1Makeup, 2.0f },

                { ids::comp2On, 0.0f },
                { ids::eqOn, 0.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "De-ess Only",
            "Everything off but the de-esser, worked hard. For putting Phonoss "
            "after another chain that already did the dynamics, or for hearing "
            "what the sibilance detector is actually doing.",
            {
                { ids::comp1On, 0.0f },
                { ids::comp2On, 0.0f },
                { ids::eqOn, 0.0f },

                { ids::deEssThreshold, -9.0f },
                { ids::deEssRange, 12.0f },
                { ids::deEssRatio, 6.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Gate Only",
            "Everything off but the gate. For cleaning up a noisy take before "
            "anything else touches it -- and the fastest way to hear whether "
            "the gate is set right, with nothing else moving.",
            {
                { ids::highpass, 0.0f },

                { ids::gateOn, 1.0f },
                { ids::gateThreshold, -40.0f },
                { ids::gateRange, 20.0f },

                { ids::deEssOn, 0.0f },
                { ids::comp1On, 0.0f },
                { ids::comp2On, 0.0f },
                { ids::eqOn, 0.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            "Neutral",
            "Every stage off. Bit-exact: the samples that go in are the samples "
            "that come out, not almost. Where to start when building a setting "
            "from nothing, and the honest reference for any A/B.",
            {
                { ids::highpass, 0.0f },
                { ids::gateOn, 0.0f },
                { ids::deEssOn, 0.0f },
                { ids::comp1On, 0.0f },
                { ids::comp2On, 0.0f },
                { ids::eqOn, 0.0f },
            }
        },
    };

    return list;
}
} // namespace

PhonossProcessor::PhonossProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, juce::Identifier (kStateTypeName), createParameterLayout())
{
    bypassParameter_ = dynamic_cast<juce::AudioParameterBool*> (state_.getParameter (ids::bypass));
}

juce::AudioProcessorValueTreeState::ParameterLayout
PhonossProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using BoolParameter = juce::AudioParameterBool;
    using Range = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    // **A number with no unit is not a reading.**
    //
    // `withLabel` alone was not enough and that is easy to miss: the label is
    // what a *host* puts beside an automation lane, and JUCE's own text box
    // never sees it -- `AudioParameterFloat::getText` returns the bare float.
    // So the panel showed `-45.0`, `3.0`, `12.0`, `1.00`, `40`, `120`, `0`,
    // `6000` and `0.90` side by side, and nothing on it said which of those
    // were dB, which were milliseconds, which were hertz and which was a Q.
    // Every other plugin in the suite formats its own values; this one was the
    // odd one out because `withLabel` *looks* like it does the job.
    //
    // Formatting here rather than in the editor also fixes the host: an
    // automation lane now reads "6.00 kHz" instead of "6000".
    const auto attributes = [] (const char* label)
    {
        const juce::String unit { label };

        return juce::AudioParameterFloatAttributes{}
            .withLabel (unit)
            .withStringFromValueFunction ([unit] (float value, int)
            {
                if (unit == "Hz")
                {
                    // Zero is **off** on every frequency control here -- the
                    // input HPF and the two sidechain filters all use it to
                    // mean "no filter", which is a change of kind rather than
                    // of degree and has to read as one.
                    if (value < 0.5f)
                        return juce::String ("off");

                    return value < 1000.0f
                             ? juce::String (juce::roundToInt (value)) + " Hz"
                             : juce::String (value / 1000.0f, 2) + " kHz";
                }

                if (unit == "ms")
                {
                    // Below a tenth of a millisecond an attack is not a time
                    // the ear can hold on to; it is the absence of one.
                    if (value < 0.05f)  return juce::String ("instant");
                    if (value < 1.0f)   return juce::String (value, 2) + " ms";
                    if (value < 10.0f)  return juce::String (value, 1) + " ms";
                    if (value < 1000.0f) return juce::String (juce::roundToInt (value)) + " ms";

                    // A three-second release is a number you read as seconds.
                    return juce::String (value / 1000.0f, 2) + " s";
                }

                // A ratio of 1 is not compression at all, and saying so is
                // more useful than "1.0 : 1".
                if (unit == ": 1")
                    return value < 1.05f ? juce::String ("1 : 1 off")
                                         : juce::String (value, 1) + " : 1";

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

    // ---- INPUT -----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::inputTrim, kSchemaV1 }, "Input",
        Range (-24.0f, 24.0f, 0.1f), 0.0f, attributes ("dB")));

    // 0 is OFF, not "1 Hz". The bottom of this control changes kind rather
    // than degree, which is why it gets a tooltip of its own. 300 Hz at the
    // top because a rap vocal on a busy mix is sometimes cut that high, and a
    // control that stopped at 120 would just be moved to another plugin.
    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::highpass, kSchemaV1 }, "HPF",
        Range (0.0f, 300.0f, 1.0f), 80.0f, attributes ("Hz")));

    // ---- GATE ------------------------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::gateOn, kSchemaV1 }, "Gate", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::gateThreshold, kSchemaV1 }, "Gate threshold",
        Range (-80.0f, 0.0f, 0.1f), -45.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::gateHysteresis, kSchemaV1 }, "Gate hysteresis",
        Range (0.0f, 24.0f, 0.1f), 3.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::gateRange, kSchemaV1 }, "Gate range",
        Range (0.0f, 80.0f, 0.1f), 12.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::gateAttack, kSchemaV1 }, "Gate attack",
        Range (0.0f, 50.0f, 0.01f, 0.4f), 1.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::gateHold, kSchemaV1 }, "Gate hold",
        Range (0.0f, 500.0f, 1.0f, 0.5f), 40.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::gateRelease, kSchemaV1 }, "Gate release",
        Range (5.0f, 2000.0f, 1.0f, 0.4f), 120.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::gateSidechain, kSchemaV1 }, "Gate sidechain",
        Range (0.0f, 1000.0f, 1.0f, 0.5f), 0.0f, attributes ("Hz")));

    // ---- DE-ESS ----------------------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::deEssOn, kSchemaV1 }, "De-ess", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::deEssCorner, kSchemaV1 }, "De-ess corner",
        Range (2000.0f, 12000.0f, 10.0f, 0.5f), 6000.0f, attributes ("Hz")));

    // **This is a ratio, not a level**, and its range says so: an /s/ carries
    // high-band energy comparable to the body of the voice or above it, while
    // a vowel is 20 to 40 dB down. So the useful settings live around zero and
    // the control reaches well either side of it. See describeSibilance().
    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::deEssThreshold, kSchemaV1 }, "De-ess threshold",
        Range (-40.0f, 20.0f, 0.1f), -6.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::deEssRatio, kSchemaV1 }, "De-ess ratio",
        Range (1.0f, 20.0f, 0.1f, 0.5f), 4.0f, attributes (": 1")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::deEssKnee, kSchemaV1 }, "De-ess knee",
        Range (0.0f, 24.0f, 0.1f), 3.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::deEssRange, kSchemaV1 }, "De-ess range",
        Range (0.0f, 36.0f, 0.1f), 6.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::deEssAttack, kSchemaV1 }, "De-ess attack",
        Range (0.0f, 20.0f, 0.01f, 0.4f), 0.5f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::deEssRelease, kSchemaV1 }, "De-ess release",
        Range (5.0f, 500.0f, 1.0f, 0.5f), 40.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::deEssListen, kSchemaV1 }, "De-ess listen", false));

    // ---- COMP 1 (leveller) -----------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::comp1On, kSchemaV1 }, "Leveller", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Threshold, kSchemaV1 }, "Leveller threshold",
        Range (-60.0f, 0.0f, 0.1f), -24.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Ratio, kSchemaV1 }, "Leveller ratio",
        Range (1.0f, 20.0f, 0.1f, 0.5f), 2.5f, attributes (": 1")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Knee, kSchemaV1 }, "Leveller knee",
        Range (0.0f, 24.0f, 0.1f), 6.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Attack, kSchemaV1 }, "Leveller attack",
        Range (0.1f, 300.0f, 0.1f, 0.4f), 30.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Release, kSchemaV1 }, "Leveller release",
        Range (5.0f, 3000.0f, 1.0f, 0.4f), 250.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Makeup, kSchemaV1 }, "Leveller makeup",
        Range (0.0f, 24.0f, 0.1f), 3.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Mix, kSchemaV1 }, "Leveller mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp1Sidechain, kSchemaV1 }, "Leveller sidechain",
        Range (0.0f, 500.0f, 1.0f, 0.5f), 0.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::comp1Auto, kSchemaV1 }, "Leveller auto release", true));

    // ---- COMP 2 (peak catcher) -------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::comp2On, kSchemaV1 }, "Peak", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Threshold, kSchemaV1 }, "Peak threshold",
        Range (-60.0f, 0.0f, 0.1f), -12.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Ratio, kSchemaV1 }, "Peak ratio",
        Range (1.0f, 20.0f, 0.1f, 0.5f), 6.0f, attributes (": 1")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Knee, kSchemaV1 }, "Peak knee",
        Range (0.0f, 24.0f, 0.1f), 3.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Attack, kSchemaV1 }, "Peak attack",
        Range (0.1f, 300.0f, 0.1f, 0.4f), 2.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Release, kSchemaV1 }, "Peak release",
        Range (5.0f, 3000.0f, 1.0f, 0.4f), 80.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Makeup, kSchemaV1 }, "Peak makeup",
        Range (0.0f, 24.0f, 0.1f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Mix, kSchemaV1 }, "Peak mix",
        Range (0.0f, 1.0f, 0.001f), 1.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::comp2Sidechain, kSchemaV1 }, "Peak sidechain",
        Range (0.0f, 500.0f, 1.0f, 0.5f), 0.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::comp2Auto, kSchemaV1 }, "Peak auto release", false));

    // ---- EQ --------------------------------------------------------------

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::eqOn, kSchemaV1 }, "EQ", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::eqLowHz, kSchemaV1 }, "Low frequency",
        Range (40.0f, 500.0f, 1.0f, 0.5f), 120.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::eqLowDb, kSchemaV1 }, "Low gain",
        Range (-18.0f, 18.0f, 0.1f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::eqMidHz, kSchemaV1 }, "Mid frequency",
        Range (200.0f, 8000.0f, 1.0f, 0.35f), 2500.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::eqMidQ, kSchemaV1 }, "Mid Q",
        Range (0.2f, 6.0f, 0.01f, 0.5f), 0.9f, attributes ("")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::eqMidDb, kSchemaV1 }, "Mid gain",
        Range (-18.0f, 18.0f, 0.1f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::eqHighHz, kSchemaV1 }, "High frequency",
        Range (2000.0f, 16000.0f, 10.0f, 0.5f), 8000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::eqHighDb, kSchemaV1 }, "High gain",
        Range (-18.0f, 18.0f, 0.1f), 0.0f, attributes ("dB")));

    // ---- OUTPUT ----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::outputTrim, kSchemaV1 }, "Output",
        Range (-24.0f, 24.0f, 0.1f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<BoolParameter> (
        juce::ParameterID { ids::bypass, kSchemaV1 }, "Bypass", false));

    return { parameters.begin(), parameters.end() };
}

bool PhonossProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void PhonossProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    engine_.prepare (sampleRate);

    const int preparedBlock = juce::jmax (16, maximumExpectedSamplesPerBlock);
    scratch_.setSize (PhonossEngine::kChannels, preparedBlock, false, false, true);
    dry_.setSize (PhonossEngine::kChannels, preparedBlock, false, false, true);

    // Zero latency, and that is a design choice rather than an omission:
    // nothing here is oversampled and nothing looks ahead, so there is nothing
    // to declare. The mixer still runs, because a bypass that switches rather
    // than crossfades clicks whatever the latency is.
    bypassMixer_.prepare (sampleRate, 0, PhonossEngine::kChannels);
    bypassMixer_.reset (bypassParameter_ != nullptr && bypassParameter_->get());

    setLatencySamples (0);

    // prepare() resets, so whatever the parameters currently say has to be
    // pushed again afterwards -- what prepare built must be re-checked against
    // what it actually built rather than against whether anything has been set
    // yet (the Emberdrive lesson, CLAUDE.md section 7).
    pullParameters();
    engine_.setSettings (settings_);
}

void PhonossProcessor::pullParameters()
{
    settings_ = settingsFromParameters();
}

bool PhonossProcessor::isIdentity() const
{
    // Built from the **parameters**, not from what the engine happens to be
    // set to. Before the transport has ever run, the engine still holds its
    // default-constructed settings -- which are neutral -- so asking it would
    // have the panel claim bit-exact transparency for a strip whose controls
    // say otherwise. Crossbar's chain readout had exactly this bug.
    return PhonossEngine::isIdentity (settingsFromParameters());
}

PhonossEngine::Settings PhonossProcessor::settingsFromParameters() const
{
    PhonossEngine::Settings settings;

    settings.inputTrimDb = valueOf (state_, ids::inputTrim);
    settings.highpassHz = valueOf (state_, ids::highpass);
    settings.outputTrimDb = valueOf (state_, ids::outputTrim);

    settings.gate.thresholdDb = valueOf (state_, ids::gateThreshold);
    settings.gate.hysteresisDb = valueOf (state_, ids::gateHysteresis);
    settings.gate.attackMs = valueOf (state_, ids::gateAttack);
    settings.gate.holdMs = valueOf (state_, ids::gateHold);
    settings.gate.releaseMs = valueOf (state_, ids::gateRelease);
    settings.gate.sidechainHz = valueOf (state_, ids::gateSidechain);

    settings.deEss.cornerHz = valueOf (state_, ids::deEssCorner);
    settings.deEss.thresholdDb = valueOf (state_, ids::deEssThreshold);
    settings.deEss.ratio = valueOf (state_, ids::deEssRatio);
    settings.deEss.kneeDb = valueOf (state_, ids::deEssKnee);
    settings.deEss.attackMs = valueOf (state_, ids::deEssAttack);
    settings.deEss.releaseMs = valueOf (state_, ids::deEssRelease);
    settings.deEss.listen = flagOf (state_, ids::deEssListen);

    settings.leveller.thresholdDb = valueOf (state_, ids::comp1Threshold);
    settings.leveller.kneeDb = valueOf (state_, ids::comp1Knee);
    settings.leveller.attackMs = valueOf (state_, ids::comp1Attack);
    settings.leveller.releaseMs = valueOf (state_, ids::comp1Release);
    settings.leveller.sidechainHz = valueOf (state_, ids::comp1Sidechain);
    settings.leveller.programDependent = flagOf (state_, ids::comp1Auto);

    settings.peak.thresholdDb = valueOf (state_, ids::comp2Threshold);
    settings.peak.kneeDb = valueOf (state_, ids::comp2Knee);
    settings.peak.attackMs = valueOf (state_, ids::comp2Attack);
    settings.peak.releaseMs = valueOf (state_, ids::comp2Release);
    settings.peak.sidechainHz = valueOf (state_, ids::comp2Sidechain);
    settings.peak.programDependent = flagOf (state_, ids::comp2Auto);

    settings.eq.lowShelfHz = valueOf (state_, ids::eqLowHz);
    settings.eq.midHz = valueOf (state_, ids::eqMidHz);
    settings.eq.midQ = valueOf (state_, ids::eqMidQ);
    settings.eq.highShelfHz = valueOf (state_, ids::eqHighHz);

    // The per-stage switches. Each forces the stage to the neutral value the
    // engine already proves is a bit-exact identity, rather than branching
    // around the stage: one definition of "off", and a switched-off stage
    // costs its arithmetic and changes nothing. A branch would be marginally
    // cheaper and would be a second, untested claim about what neutral means.
    const bool gateOn = flagOf (state_, ids::gateOn);
    const bool deEssOn = flagOf (state_, ids::deEssOn);
    const bool comp1On = flagOf (state_, ids::comp1On);
    const bool comp2On = flagOf (state_, ids::comp2On);
    const bool eqOn = flagOf (state_, ids::eqOn);

    settings.gate.rangeDb = gateOn ? valueOf (state_, ids::gateRange) : 0.0;
    settings.deEss.rangeDb = deEssOn ? valueOf (state_, ids::deEssRange) : 0.0;

    settings.leveller.ratio = comp1On ? valueOf (state_, ids::comp1Ratio) : 1.0;
    settings.leveller.makeupDb = comp1On ? valueOf (state_, ids::comp1Makeup) : 0.0;
    settings.leveller.mix = comp1On ? valueOf (state_, ids::comp1Mix) : 1.0;

    settings.peak.ratio = comp2On ? valueOf (state_, ids::comp2Ratio) : 1.0;
    settings.peak.makeupDb = comp2On ? valueOf (state_, ids::comp2Makeup) : 0.0;
    settings.peak.mix = comp2On ? valueOf (state_, ids::comp2Mix) : 1.0;

    settings.eq.lowShelfDb = eqOn ? valueOf (state_, ids::eqLowDb) : 0.0;
    settings.eq.midDb = eqOn ? valueOf (state_, ids::eqMidDb) : 0.0;
    settings.eq.highShelfDb = eqOn ? valueOf (state_, ids::eqHighDb) : 0.0;

    return settings;
}

template <typename FloatType>
void PhonossProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();

    for (int channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0)
        return;

    const int channels = juce::jlimit (1, PhonossEngine::kChannels,
                                       std::min (inputChannels, outputChannels));

    if (numSamples > scratch_.getNumSamples())
    {
        scratch_.setSize (PhonossEngine::kChannels, numSamples, false, true, true);
        dry_.setSize (PhonossEngine::kChannels, numSamples, false, true, true);
    }

    pullParameters();
    engine_.setSettings (settings_);

    // Into double, which is what the whole DSP path runs in, keeping an
    // untouched copy for the bypass crossfade.
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

    // A mono bus still runs the stereo engine, with the second channel fed
    // from the first: the detectors are linked, so one channel in means the
    // linked signal is that channel, and the answer is the same either way.
    if (channels == 1)
    {
        scratch_.copyFrom (1, 0, scratch_, 0, 0, numSamples);
        dry_.copyFrom (1, 0, dry_, 0, 0, numSamples);
    }

    engine_.process (scratch_.getWritePointer (0), scratch_.getWritePointer (1), numSamples);

    bypassMixer_.setBypassed (bypassParameter_ != nullptr && bypassParameter_->get());
    bypassMixer_.process (scratch_.getArrayOfWritePointers(),
                          dry_.getArrayOfReadPointers(),
                          PhonossEngine::kChannels, numSamples);

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

    meters_.gateDb.store (static_cast<float> (stage.gateDb), std::memory_order_relaxed);
    meters_.deEssDb.store (static_cast<float> (stage.deEssDb), std::memory_order_relaxed);
    meters_.comp1Db.store (static_cast<float> (stage.levellerDb), std::memory_order_relaxed);
    meters_.comp2Db.store (static_cast<float> (stage.peakDb), std::memory_order_relaxed);
    meters_.sibilanceDb.store (static_cast<float> (stage.sibilanceDb), std::memory_order_relaxed);
    meters_.inputDb.store (peakDb (inputPeak), std::memory_order_relaxed);
    meters_.outputDb.store (peakDb (outputPeak), std::memory_order_relaxed);
}

void PhonossProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

void PhonossProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer);
}

juce::String PhonossProcessor::describeHighpass() const
{
    const auto hz = valueOf (state_, ids::highpass);

    if (hz <= 0.0f)
        return "OFF -- no filter at all, not a very low one. The stage is "
               "removed from the path rather than set flat.";

    juce::String description;
    description << juce::String (juce::roundToInt (hz)) << " Hz, first thing in the chain. "
                << "Rumble and proximity never reach a detector, so a plosive cannot hold "
                << "the gate open or duck a whole word.";

    if (hz >= 140.0f)
        description << " This high it is also a tone control: it is taking weight out of "
                       "the voice, which is what you want under a busy mix and not what "
                       "you want on a solo take.";

    return description;
}

juce::String PhonossProcessor::describeSibilance() const
{
    const auto threshold = valueOf (state_, ids::deEssThreshold);
    const auto reading = meters_.sibilanceDb.load (std::memory_order_relaxed);

    juce::String description;
    description << "Sibilance is measured as a RATIO -- high-band energy against the body "
                << "of the voice -- not as a level. So it does not re-trigger when the "
                << "singer pushes, and a bright vowel does not read as an /s/.\n\n"
                << "Threshold " << juce::String (threshold, 1) << " dB";

    if (reading > -119.0f)
        description << ", reading " << juce::String (reading, 1) << " dB right now";

    description << ". A vowel typically sits 20 to 40 dB below its own body; an /s/ comes "
                << "up to around zero or above.";

    return description;
}

void PhonossProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    state.appendChild (abCompare_.toValueTree(), nullptr);
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhonossProcessor::setStateInformation (const void* data, int sizeInBytes)
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

int PhonossProcessor::getNumPrograms()
{
    return static_cast<int> (presets().size());
}

const juce::String PhonossProcessor::getProgramName (int index)
{
    const auto& list = presets();
    const auto count = static_cast<int> (list.size());

    if (index < 0 || index >= count)
        return {};

    return list[static_cast<std::size_t> (index)].name;
}

void PhonossProcessor::setCurrentProgram (int index)
{
    const auto& list = presets();
    const auto count = static_cast<int> (list.size());

    if (count == 0)
        return;

    currentProgram_ = juce::jlimit (0, count - 1, index);

    // Every parameter back to its default first, so a preset is a complete
    // parameter set rather than a patch over whatever was loaded before it.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (ranged->paramID != ids::bypass)
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& setting : list[static_cast<std::size_t> (currentProgram_)].settings)
        if (auto* parameter = state_.getParameter (setting.id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (setting.value));
}

juce::AudioProcessorEditor* PhonossProcessor::createEditor()
{
    return new PhonossEditor (*this);
}

} // namespace tezla::phonoss

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::phonoss::PhonossProcessor();
}
