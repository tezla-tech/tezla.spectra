// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include <tezla/dsp/ScalaFile.hpp>
#include <tezla/ui/StateIds.hpp>

#include "PluginEditor.h"

namespace tezla::ictus {

namespace
{
constexpr auto kStateTypeName = "IctusState";

/// Every parameter carries the schema version it was born at, forever: the
/// hint feeds the VST3 parameter ID (CLAUDE.md section 8).
constexpr int kSchemaV1 = 1;
/// Schema 2: Gate, Release and Bass mode, from the rig's first ear round.
constexpr int kSchemaV2 = 2;
/// Schema 3: Snare 1 (I3).
constexpr int kSchemaV3 = 3;
constexpr int kStateSchemaVersion = kSchemaV3;

/// The tuning travels with the project as text (the Malleus property names,
/// so the shared panel's state reads the same in every instrument).
constexpr auto kScaleNameProperty = "scaleName";
constexpr auto kScaleTextProperty = "scalaText";
constexpr auto kKeyboardMapProperty = "keyboardMapText";
constexpr auto kConcertPitchProperty = "concertPitch";

/// The pads' notes travel as state-tree properties, one per pad.
const char* padNoteProperty (PadIndex pad)
{
    static const char* const names[kPadCount] {
        "kick1Note", "snare1Note", "hatClosedNote", "hatOpenNote",
        "clapNote", "percNote", "kick2Note", "snare2Note"
    };

    return names[static_cast<int> (pad)];
}

[[nodiscard]] float valueOf (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    if (auto* raw = state.getRawParameterValue (id))
        return raw->load();

    return 0.0f;
}

juce::String rateText (double rate)
{
    return juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " kHz";
}

/// One control set to one value.
struct Setting
{
    juce::String id;
    float value;
};

/// A preset is a name and a list of departures from the defaults, applied
/// after every parameter has been reset to its default -- a complete
/// parameter set, never a patch over the previous one (the Sonitus pattern,
/// docs/PLUGIN-CONVENTIONS.md).
struct Preset
{
    const char* name;
    std::vector<Setting> settings;
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list
    {
        // -------------------------------------------------------------------
        {
            // The defaults: a plain kick with a drop and a sigh, no
            // harmonics, no click, no tone -- the body the others are built
            // from, and the one that proves the engine adds nothing it was
            // not asked for.
            "Init Kit -- the plain body",
            {}
        },
        // -------------------------------------------------------------------
        {
            // Short, high, bright: a fast drop that lands quickly, a
            // filtered attack that opens for the first cycles, and a click
            // that carries the velocity. The one that sits on a busy break.
            "DnB Tight",
            {
                { ids::k1Tune, 55.0f },
                { ids::k1Start, 36.0f },
                { ids::k1Drop, 18.0f },
                { ids::k1Sigh, 1.0f },
                { ids::k1SighTime, 300.0f },
                { ids::k1Harmonics, 35.0f },
                { ids::k1Even, 60.0f },
                { ids::k1ToneOn, 1.0f },
                { ids::k1Tone, 6.0f },
                { ids::k1Click, 45.0f },
                { ids::k1ClickTone, 3500.0f },
                { ids::k1Noise, 25.0f },
                { ids::k1Decay, 220.0f },
                { ids::k1Shape, 20.0f },
                { ids::k1Level, 85.0f },
                // The snare: a short, cracking shell with bright wires that
                // follow it a little.
                { ids::s1Tune, 205.0f },
                { ids::s1Tone, 70.0f },
                { ids::s1Decay, 180.0f },
                { ids::s1Start, 6.0f },
                { ids::s1Drop, 15.0f },
                { ids::s1Wires, 70.0f },
                { ids::s1Snappy, 4000.0f },
                { ids::s1Snap, 20.0f },
                { ids::s1WiresDecay, 140.0f },
                { ids::s1Rattle, 30.0f },
                { ids::s1Crack, 50.0f },
                { ids::s1CrackTone, 4500.0f },
                { ids::s1Noise, 30.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Low and long: a slower drop onto 45 Hz, a deeper sigh, and the
            // tail carrying the landed pitch on after the body has gone.
            "Sub Long",
            {
                { ids::k1Tune, 45.0f },
                { ids::k1Start, 24.0f },
                { ids::k1Drop, 40.0f },
                { ids::k1Sigh, 2.0f },
                { ids::k1SighTime, 800.0f },
                { ids::k1Harmonics, 15.0f },
                { ids::k1Even, 30.0f },
                { ids::k1Decay, 600.0f },
                { ids::k1Tail, 60.0f },
                { ids::k1TailTime, 1500.0f },
                { ids::k1Click, 15.0f },
                { ids::k1Level, 90.0f },
                // The snare: deeper and longer, band-passed wires with a
                // real rattle so they ring with the shell.
                { ids::s1Tune, 170.0f },
                { ids::s1Tone, 50.0f },
                { ids::s1Decay, 350.0f },
                { ids::s1Wires, 55.0f },
                { ids::s1Snappy, 2500.0f },
                { ids::s1Snap, 50.0f },
                { ids::s1WiresDecay, 220.0f },
                { ids::s1Rattle, 50.0f },
                { ids::s1Crack, 30.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Snappy and mid-forward: a very fast drop from high up, the body
            // started part way into its cycle so the first sample already
            // moves, hard click and noise, harmonics up, short.
            "Jungle Snap",
            {
                { ids::k1Tune, 62.0f },
                { ids::k1Start, 48.0f },
                { ids::k1Drop, 12.0f },
                { ids::k1Sigh, 0.5f },
                { ids::k1Phase, 30.0f },
                { ids::k1Harmonics, 55.0f },
                { ids::k1Even, 40.0f },
                { ids::k1ToneOn, 1.0f },
                { ids::k1Tone, 10.0f },
                { ids::k1Click, 70.0f },
                { ids::k1ClickTone, 4500.0f },
                { ids::k1Noise, 45.0f },
                { ids::k1NoiseTime, 1.5f },
                { ids::k1Decay, 160.0f },
                { ids::k1Shape, 35.0f },
                { ids::k1Level, 85.0f },
                { ids::k1VelClick, 80.0f },
                // The snare: high, tight and all crack, the wires short and
                // hissing, velocity into the stick.
                { ids::s1Tune, 230.0f },
                { ids::s1Spread, 90.0f },
                { ids::s1Tone, 80.0f },
                { ids::s1Decay, 120.0f },
                { ids::s1Start, 10.0f },
                { ids::s1Drop, 10.0f },
                { ids::s1Wires, 80.0f },
                { ids::s1Snappy, 5500.0f },
                { ids::s1WiresDecay, 90.0f },
                { ids::s1Rattle, 20.0f },
                { ids::s1Crack, 70.0f },
                { ids::s1CrackTone, 5000.0f },
                { ids::s1Noise, 50.0f },
                { ids::s1NoiseTime, 1.0f },
                { ids::s1VelCrack, 80.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Bass mode: the kick as a tuned sub instrument. Every key plays
            // it at the key's pitch; the gate ends the note when the key
            // lifts, over a short release; a small, fast drop for the pluck;
            // no sigh, so the pitch holds; a little even warmth so it reads
            // on a small speaker. Appended -- presets are recalled by index.
            "Bass Keys",
            {
                { ids::bassMode, 1.0f },
                { ids::k1Gate, 1.0f },
                { ids::k1Release, 40.0f },
                { ids::k1Start, 12.0f },
                { ids::k1Drop, 15.0f },
                { ids::k1Sigh, 0.0f },
                { ids::k1Harmonics, 25.0f },
                { ids::k1Even, 70.0f },
                { ids::k1ToneOn, 1.0f },
                { ids::k1Tone, 6.0f },
                { ids::k1Click, 10.0f },
                { ids::k1Decay, 1200.0f },
                { ids::k1Shape, 15.0f },
                { ids::k1Level, 85.0f },
                { ids::k1VelLevel, 60.0f },
            }
        },
    };

    return list;
}
} // namespace

// ---------------------------------------------------------------------------
// Construction and parameters
// ---------------------------------------------------------------------------

IctusProcessor::IctusProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    for (int pad = 0; pad < kPadCount; ++pad)
        padNotes_[pad].store (kDefaultPadNotes[pad]);

    // scales:: rather than the Tuning class's bare builder, so the default
    // scale arrives with the construction and story the tuning panel shows.
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
    previewTuning_.setScale (scale_);
}

juce::AudioProcessorValueTreeState::ParameterLayout
IctusProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using Switch = juce::AudioParameterBool;
    using ChoiceParameter = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    const auto attributes = [] (const char* label)
    {
        return juce::AudioParameterFloatAttributes{}.withLabel (label);
    };

    // A range whose useful part sits mid-travel: the skew that puts `centre`
    // at the knob's half-way point (docs/PLUGIN-CONVENTIONS.md, Parameters).
    const auto skewed = [] (float low, float high, float step, float centre)
    {
        Range range (low, high, step);
        range.setSkewForCentre (centre);
        return range;
    };

    // ---- KICK 1: pitch ----------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Tune, kSchemaV1 }, "Kick 1 Tune",
        skewed (20.0f, 400.0f, 0.1f, 60.0f), 50.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::k1FollowKey, kSchemaV1 }, "Kick 1 Follow key", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Start, kSchemaV1 }, "Kick 1 Start",
        Range (0.0f, 60.0f, 0.1f), 30.0f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Drop, kSchemaV1 }, "Kick 1 Drop",
        skewed (2.0f, 200.0f, 0.1f, 30.0f), 30.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Sigh, kSchemaV1 }, "Kick 1 Sigh",
        Range (-12.0f, 12.0f, 0.01f), 1.5f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1SighTime, kSchemaV1 }, "Kick 1 Sigh time",
        skewed (100.0f, 2000.0f, 1.0f, 500.0f), 500.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Phase, kSchemaV1 }, "Kick 1 Phase",
        Range (0.0f, 90.0f, 0.1f), 0.0f, attributes ("deg")));

    // ---- KICK 1: colour ---------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Harmonics, kSchemaV1 }, "Kick 1 Harmonics",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Even, kSchemaV1 }, "Kick 1 Even",
        Range (0.0f, 100.0f, 0.1f), 50.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::k1ToneOn, kSchemaV1 }, "Kick 1 Tone on", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Tone, kSchemaV1 }, "Kick 1 Tone",
        skewed (1.0f, 64.0f, 0.01f, 8.0f), 8.0f, attributes ("x")));

    // ---- KICK 1: click ----------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Click, kSchemaV1 }, "Kick 1 Click",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1ClickTone, kSchemaV1 }, "Kick 1 Click tone",
        skewed (200.0f, 8000.0f, 1.0f, 2000.0f), 3000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Noise, kSchemaV1 }, "Kick 1 Noise",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1NoiseTime, kSchemaV1 }, "Kick 1 Noise time",
        skewed (0.5f, 8.0f, 0.01f, 2.0f), 2.0f, attributes ("ms")));

    // ---- KICK 1: amplitude ------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Attack, kSchemaV1 }, "Kick 1 Attack",
        Range (0.0f, 20.0f, 0.01f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Hold, kSchemaV1 }, "Kick 1 Hold",
        Range (0.0f, 50.0f, 0.1f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Decay, kSchemaV1 }, "Kick 1 Decay",
        skewed (20.0f, 2000.0f, 1.0f, 300.0f), 350.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Shape, kSchemaV1 }, "Kick 1 Shape",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Tail, kSchemaV1 }, "Kick 1 Tail",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1TailTime, kSchemaV1 }, "Kick 1 Tail time",
        skewed (100.0f, 4000.0f, 1.0f, 1000.0f), 1000.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Level, kSchemaV1 }, "Kick 1 Level",
        Range (0.0f, 100.0f, 0.1f), 80.0f, attributes ("%")));

    // ---- KICK 1: velocity -------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelLevel, kSchemaV1 }, "Kick 1 Velocity to level",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelClick, kSchemaV1 }, "Kick 1 Velocity to click",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelDrop, kSchemaV1 }, "Kick 1 Velocity to drop",
        Range (0.0f, 100.0f, 0.1f), 30.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1VelDecay, kSchemaV1 }, "Kick 1 Velocity to decay",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    // ---- GLOBAL ----------------------------------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        Range (-24.0f, 12.0f, 0.1f), 0.0f, attributes ("dB")));

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::oversampling, kSchemaV1 }, "Oversampling",
        choices::oversampling, 0));

    parameters.push_back (std::make_unique<ChoiceParameter> (
        juce::ParameterID { ids::renderOversampling, kSchemaV1 }, "Render quality",
        choices::renderOversampling, 0));

    // ---- schema 2 -- APPENDED, never reordered (CLAUDE.md section 8) --------
    // Every one defaults to neutral, so a project saved at schema 1 reopens
    // sounding the same: no gate, no release, no bass mode.

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::k1Gate, kSchemaV2 }, "Kick 1 Gate", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Release, kSchemaV2 }, "Kick 1 Release",
        skewed (0.0f, 2000.0f, 1.0f, 100.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::bassMode, kSchemaV2 }, "Bass mode", false));

    // ---- schema 3: SNARE 1 -- APPENDED (I3) --------------------------------

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Tune, kSchemaV3 }, "Snare 1 Tune",
        skewed (60.0f, 800.0f, 0.1f, 200.0f), 180.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::s1FollowKey, kSchemaV3 }, "Snare 1 Follow key", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Spread, kSchemaV3 }, "Snare 1 Spread",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Tone, kSchemaV3 }, "Snare 1 Tone",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Decay, kSchemaV3 }, "Snare 1 Decay",
        skewed (50.0f, 2000.0f, 1.0f, 300.0f), 250.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Start, kSchemaV3 }, "Snare 1 Start",
        Range (0.0f, 24.0f, 0.1f), 4.0f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Drop, kSchemaV3 }, "Snare 1 Drop",
        skewed (2.0f, 200.0f, 0.1f, 30.0f), 20.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Body, kSchemaV3 }, "Snare 1 Body",
        Range (0.0f, 100.0f, 0.1f), 80.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Wires, kSchemaV3 }, "Snare 1 Wires",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Snappy, kSchemaV3 }, "Snare 1 Snappy",
        skewed (1000.0f, 8000.0f, 1.0f, 3000.0f), 3000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Snap, kSchemaV3 }, "Snare 1 Snap",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1WiresDecay, kSchemaV3 }, "Snare 1 Wires decay",
        skewed (50.0f, 400.0f, 1.0f, 150.0f), 150.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Rattle, kSchemaV3 }, "Snare 1 Rattle",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Crack, kSchemaV3 }, "Snare 1 Crack",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1CrackTone, kSchemaV3 }, "Snare 1 Crack tone",
        skewed (200.0f, 8000.0f, 1.0f, 2000.0f), 4000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Noise, kSchemaV3 }, "Snare 1 Noise",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1NoiseTime, kSchemaV3 }, "Snare 1 Noise time",
        skewed (0.5f, 8.0f, 0.01f, 2.0f), 1.5f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Level, kSchemaV3 }, "Snare 1 Level",
        Range (0.0f, 100.0f, 0.1f), 80.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::s1Gate, kSchemaV3 }, "Snare 1 Gate", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Release, kSchemaV3 }, "Snare 1 Release",
        skewed (0.0f, 2000.0f, 1.0f, 100.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1VelLevel, kSchemaV3 }, "Snare 1 Velocity to level",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1VelWires, kSchemaV3 }, "Snare 1 Velocity to wires",
        Range (0.0f, 100.0f, 0.1f), 40.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1VelCrack, kSchemaV3 }, "Snare 1 Velocity to crack",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1VelDrop, kSchemaV3 }, "Snare 1 Velocity to drop",
        Range (0.0f, 100.0f, 0.1f), 30.0f, attributes ("%")));

    return { parameters.begin(), parameters.end() };
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

bool IctusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void IctusProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    // The parameters go in BEFORE prepare, so the graph is built at the
    // factor the saved state asks for rather than rebuilt on the first
    // block. The offline flag likewise: the VST3 wrapper sets it in
    // setupProcessing, before activation, which is exactly this order.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.setOffline (isNonRealtime());

    engine_.prepare (sampleRate_, std::max (maximumExpectedSamplesPerBlock, 1));

    // A scale loaded with the state before the host prepared us is pending
    // until the audio thread collects it; nothing is running yet, so collect
    // it here and the first block already plays through it.
    collectTuning();

    scratch_.setSize (2, std::max (maximumExpectedSamplesPerBlock, 1), false, false, true);

    reportedLatency_ = engine_.getLatencySamples();
    setLatencySamples (reportedLatency_);

    prepared_ = true;
}

void IctusProcessor::pullParameters()
{
    auto& k = parameters_.kick1;

    k.tuneHz = valueOf (state_, ids::k1Tune);
    k.followKey = valueOf (state_, ids::k1FollowKey) > 0.5f;
    k.startSemitones = valueOf (state_, ids::k1Start);
    k.dropSeconds = valueOf (state_, ids::k1Drop) * 0.001;
    k.sighSemitones = valueOf (state_, ids::k1Sigh);
    k.sighSeconds = valueOf (state_, ids::k1SighTime) * 0.001;
    k.phaseDegrees = valueOf (state_, ids::k1Phase);

    k.harmonics = valueOf (state_, ids::k1Harmonics) * 0.01;
    k.even = valueOf (state_, ids::k1Even) * 0.01;
    k.toneEnabled = valueOf (state_, ids::k1ToneOn) > 0.5f;
    k.toneRatio = valueOf (state_, ids::k1Tone);

    k.click = valueOf (state_, ids::k1Click) * 0.01;
    k.clickToneHz = valueOf (state_, ids::k1ClickTone);
    k.clickNoise = valueOf (state_, ids::k1Noise) * 0.01;
    k.clickNoiseSeconds = valueOf (state_, ids::k1NoiseTime) * 0.001;

    k.attackSeconds = valueOf (state_, ids::k1Attack) * 0.001;
    k.holdSeconds = valueOf (state_, ids::k1Hold) * 0.001;
    k.decaySeconds = valueOf (state_, ids::k1Decay) * 0.001;
    k.shape = valueOf (state_, ids::k1Shape) * 0.01;
    k.tailMix = valueOf (state_, ids::k1Tail) * 0.01;
    k.tailSeconds = valueOf (state_, ids::k1TailTime) * 0.001;
    k.level = valueOf (state_, ids::k1Level) * 0.01;

    k.velocityLevel = valueOf (state_, ids::k1VelLevel) * 0.01;
    k.velocityClick = valueOf (state_, ids::k1VelClick) * 0.01;
    k.velocityDrop = valueOf (state_, ids::k1VelDrop) * 0.01;
    k.velocityDecay = valueOf (state_, ids::k1VelDecay) * 0.01;

    k.gate = valueOf (state_, ids::k1Gate) > 0.5f;
    k.releaseSeconds = valueOf (state_, ids::k1Release) * 0.001;

    auto& n = parameters_.snare1;

    n.tuneHz = valueOf (state_, ids::s1Tune);
    n.followKey = valueOf (state_, ids::s1FollowKey) > 0.5f;
    n.spread = valueOf (state_, ids::s1Spread) * 0.01;
    n.tone = valueOf (state_, ids::s1Tone) * 0.01;
    n.decaySeconds = valueOf (state_, ids::s1Decay) * 0.001;
    n.startSemitones = valueOf (state_, ids::s1Start);
    n.dropSeconds = valueOf (state_, ids::s1Drop) * 0.001;
    n.body = valueOf (state_, ids::s1Body) * 0.01;

    n.wires = valueOf (state_, ids::s1Wires) * 0.01;
    n.snappyHz = valueOf (state_, ids::s1Snappy);
    n.snap = valueOf (state_, ids::s1Snap) * 0.01;
    n.wiresDecaySeconds = valueOf (state_, ids::s1WiresDecay) * 0.001;
    n.rattle = valueOf (state_, ids::s1Rattle) * 0.01;

    n.crack = valueOf (state_, ids::s1Crack) * 0.01;
    n.crackToneHz = valueOf (state_, ids::s1CrackTone);
    n.crackNoise = valueOf (state_, ids::s1Noise) * 0.01;
    n.crackNoiseSeconds = valueOf (state_, ids::s1NoiseTime) * 0.001;

    n.level = valueOf (state_, ids::s1Level) * 0.01;
    n.gate = valueOf (state_, ids::s1Gate) > 0.5f;
    n.releaseSeconds = valueOf (state_, ids::s1Release) * 0.001;

    n.velocityLevel = valueOf (state_, ids::s1VelLevel) * 0.01;
    n.velocityWires = valueOf (state_, ids::s1VelWires) * 0.01;
    n.velocityCrack = valueOf (state_, ids::s1VelCrack) * 0.01;
    n.velocityDrop = valueOf (state_, ids::s1VelDrop) * 0.01;

    for (int pad = 0; pad < kPadCount; ++pad)
        parameters_.padNotes[pad] = padNotes_[pad].load();

    parameters_.masterDb = valueOf (state_, ids::output);
    parameters_.bassMode = valueOf (state_, ids::bassMode) > 0.5f;

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, static_cast<int> (dsp::OversamplingMode::X8),
                      static_cast<int> (std::lround (valueOf (state_, ids::oversampling)))));

    parameters_.renderOversampling = static_cast<dsp::RenderOversampling> (
        juce::jlimit (0, static_cast<int> (dsp::RenderOversampling::X8),
                      static_cast<int> (std::lround (valueOf (state_, ids::renderOversampling)))));
}

void IctusProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
        engine_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        engine_.noteOff (message.getNoteNumber());
    else if (message.isAllNotesOff() || message.isAllSoundOff())
        engine_.allNotesOff();
}

void IctusProcessor::triggerHit (PadIndex pad) noexcept
{
    pendingHits_.fetch_or (1u << static_cast<unsigned> (pad));
}

void IctusProcessor::setPadNote (PadIndex pad, int note) noexcept
{
    padNotes_[static_cast<int> (pad)].store (juce::jlimit (0, 127, note));
}

int IctusProcessor::getPadNote (PadIndex pad) const noexcept
{
    return padNotes_[static_cast<int> (pad)].load();
}

template <typename FloatType>
void IctusProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer,
                                      juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    if (numSamples <= 0 || ! prepared_)
        return;

    // A new scale, if the message thread published one: a swap, no
    // allocation, and every hit struck from here on lands on it.
    collectTuning();

    // Every knob is snapshotted into a hit at note-on, so pushing the whole
    // set once per block is all the engine needs; only the output level is
    // continuous, and the engine smooths it.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.setOffline (isNonRealtime());

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    double* const engineLeft = scratch_.getWritePointer (0);
    double* const engineRight = scratch_.getWritePointer (1);
    double* const engineOut[2] { engineLeft, engineRight };

    // The HIT button lands at the top of the block, at full velocity.
    if (const unsigned hits = pendingHits_.exchange (0); hits != 0)
        for (int pad = 0; pad < kPadCount; ++pad)
            if (hits & (1u << static_cast<unsigned> (pad)))
                engine_.noteOn (padNotes_[pad].load(), 1.0);

    // Sample-accurate MIDI: the render is cut at every event, so a hit lands
    // where it was played. The engine cuts again at its own control grid,
    // which is why 64- and 512-sample blocks are bit-identical.
    int rendered = 0;

    auto renderSpan = [&] (int from, int count)
    {
        engine_.process (engineOut, count);

        for (int i = 0; i < count; ++i)
        {
            buffer.setSample (0, from + i, static_cast<FloatType> (engineLeft[i]));
            buffer.setSample (1, from + i, static_cast<FloatType> (engineRight[i]));
        }
    };

    for (const auto metadata : midi)
    {
        const int position = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (position > rendered)
        {
            renderSpan (rendered, position - rendered);
            rendered = position;
        }

        handleMidi (metadata.getMessage());
    }

    if (rendered < numSamples)
        renderSpan (rendered, numSamples - rendered);

    activeHits_.store (engine_.activeHitCount());

    // Latency changes when the oversampling factor does, and a host that is
    // not told simply plays the drum late. CLAUDE.md section 2.2.
    const int latency = engine_.getLatencySamples();

    if (latency != reportedLatency_)
    {
        reportedLatency_ = latency;
        setLatencySamples (latency);
    }
}

void IctusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void IctusProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

// ---------------------------------------------------------------------------
// Tooltips that read the live state
// ---------------------------------------------------------------------------

juce::String IctusProcessor::describeOversampling() const
{
    const int mode = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::oversampling)->load()));

    const int factor = engine_.getOversamplingFactor();
    const double internal = sampleRate_ * factor;

    const int render = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::renderOversampling)->load()));

    if (isNonRealtime() && render != 0)
        return "Rendering offline, so Render quality is in force instead: x" + juce::String (factor)
                 + " (" + rateText (internal) + " internally). This setting comes back when "
                   "playback does.";

    juce::String latency = factor > 1
        ? " Latency " + juce::String (engine_.getLatencySamples()) + " samples, compensated."
        : juce::String (" No latency.");

    if (mode == 0)
    {
        juce::String text = "Auto -- your session is at " + rateText (sampleRate_) + ", so this is ";

        text += factor == 1 ? juce::String ("off: the headroom is already there.")
                            : "running x" + juce::String (factor) + ", giving "
                                + rateText (internal) + " internally.";

        text += " Every drum, its harmonics and its click are made at that rate, so a "
                "3 kHz click never aliases and the Harmonics curves fold nothing back.";

        return text + latency;
    }

    return "x" + juce::String (factor) + " -- " + rateText (internal) + " internally, "
             + juce::String (factor) + " times the CPU of Off. Auto would pick x"
             + juce::String (dsp::autoOversamplingFactor (sampleRate_)) + " here." + latency;
}

juce::String IctusProcessor::describeRenderQuality() const
{
    const auto indexOfParameter = [this] (const char* id)
    {
        return static_cast<int> (std::lround (state_.getRawParameterValue (id)->load()));
    };

    const int render = indexOfParameter (ids::renderOversampling);
    const bool offline = isNonRealtime();

    if (render == 0)
    {
        juce::String text = "Same as live -- an offline bounce runs at whatever Oversampling is set "
                            "to, so the render is exactly what you heard. Pick x8 here to bounce at "
                            "the highest quality without paying for it while you play: it applies "
                            "only while the host renders offline, and costs render time, not CPU.";

        if (offline)
            text += " The host is rendering offline right now, at the live setting.";

        return text;
    }

    const auto live = static_cast<dsp::OversamplingMode> (indexOfParameter (ids::oversampling));
    const auto mode = dsp::renderOversamplingMode (static_cast<dsp::RenderOversampling> (render), live);
    const int factor = dsp::oversamplingFactor (mode, sampleRate_);
    const int liveFactor = dsp::oversamplingFactor (live, sampleRate_);

    juce::String text = (render == 1 ? juce::String ("Auto") : "x" + juce::String (factor))
                      + " while rendering -- an offline bounce runs x" + juce::String (factor)
                      + " (" + rateText (sampleRate_ * factor) + " internally)";

    text += factor == liveFactor ? juce::String (", the same as live, so it changes nothing.")
                                 : " against x" + juce::String (liveFactor) + " live. Costs render "
                                   "time and no CPU while playing.";

    if (offline)
        text += " The host is rendering offline right now, so this is in force.";

    return text;
}

// ---------------------------------------------------------------------------
// The tuning (mirrors Malleus, Sonitus and Svarayantra)
// ---------------------------------------------------------------------------

void IctusProcessor::publishTuning()
{
    const juce::SpinLock::ScopedLockType lock (tuningLock_);

    pendingScale_ = scale_;
    pendingMap_ = hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {};
    pendingConcertHz_ = concertPitchHz_;

    tuningPending_.store (true, std::memory_order_release);

    previewTuning_.setScale (scale_);
    previewTuning_.setKeyboardMap (hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {});
    previewTuning_.setConcertPitch (concertPitchHz_);
}

void IctusProcessor::collectTuning() noexcept
{
    if (! tuningPending_.load (std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock (tuningLock_);

    if (! lock.isLocked())
        return;

    // swapScale rather than setScale: a swap allocates nothing and hands
    // the old scale back for the message thread to destroy.
    engine_.swapScale (pendingScale_);
    engine_.tuning().swapKeyboardMap (pendingMap_);
    engine_.tuning().setConcertPitch (pendingConcertHz_);

    tuningPending_.store (false, std::memory_order_release);
}

juce::String IctusProcessor::loadScalaText (const juce::String& text,
                                            const juce::String& name)
{
    dsp::Scale parsed;

    const auto result = dsp::parseScl (text.toStdString(), parsed);

    if (! result.ok)
        return "Line " + juce::String (result.line) + ": "
                 + juce::String (result.message);

    scale_ = parsed;
    scalaText_ = text;
    scaleName_ = name.isNotEmpty() ? name : juce::String (parsed.name);

    publishTuning();
    return {};
}

juce::String IctusProcessor::loadKeyboardMapText (const juce::String& text)
{
    dsp::KeyboardMap parsed;

    const auto result = dsp::parseKbm (text.toStdString(), parsed);

    if (! result.ok)
        return "Line " + juce::String (result.line) + ": "
                 + juce::String (result.message);

    keyboardMap_ = parsed;
    keyboardMapText_ = text;
    hasKeyboardMap_ = true;

    publishTuning();
    return {};
}

juce::String IctusProcessor::selectBuiltInScale (const juce::String& name)
{
    for (const auto& scale : dsp::scales::all())
    {
        if (name == juce::String (scale.name))
        {
            scale_ = scale;
            scaleName_ = name;
            scalaText_.clear();

            publishTuning();
            return {};
        }
    }

    return "No built-in scale is named \"" + name + "\".";
}

void IctusProcessor::resetTuning()
{
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
    scalaText_.clear();
    keyboardMap_ = {};
    keyboardMapText_.clear();
    hasKeyboardMap_ = false;

    publishTuning();
}

void IctusProcessor::setConcertPitch (double hz)
{
    concertPitchHz_ = std::clamp (hz, dsp::Tuning::kMinimumConcertHz,
                                  dsp::Tuning::kMaximumConcertHz);
    publishTuning();
}

double IctusProcessor::previewFrequencyFor (int midiNote) const
{
    return previewTuning_.frequencyFor (midiNote);
}

double IctusProcessor::getRootHz() const noexcept
{
    return previewTuning_.frequencyFor (previewTuning_.getRootNote());
}

juce::String IctusProcessor::describeTuning() const
{
    const int root = previewTuning_.getRootNote();
    const double hz = previewTuning_.frequencyFor (root);

    return scaleName_ + "  --  " + juce::String (scale_.ratios.size())
             + " degrees, root " + juce::MidiMessage::getMidiNoteName (root, true, true, 4)
             + " at " + juce::String (hz, 2) + " Hz";
}

juce::String IctusProcessor::describeKeying() const
{
    const bool bass = state_.getRawParameterValue (ids::bassMode)->load() > 0.5f;

    const auto plays = [this] (int note)
    {
        return juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " = "
                 + juce::String (previewTuning_.frequencyFor (note), 2) + " Hz";
    };

    const juce::String scale = scaleName_ + " at A4 = " + juce::String (concertPitchHz_, 1) + " Hz";

    if (bass)
        return "Lit -- every key plays Kick 1 at the key's pitch through the TUNING page's "
               "scale, " + scale + ": " + plays (36) + ", " + plays (48) + ", " + plays (55)
               + ". The other pads are silent and Kick 1's own note is no longer special. "
                 "Costs nothing extra: one hit per key held. Light Gate on the KICK page and "
                 "set Release so a note ends when the key lifts.";

    return "Dark -- the pads play on their own notes (Kick 1 on "
             + juce::MidiMessage::getMidiNoteName (getPadNote (PadIndex::kick1), true, true, 3)
             + "). Lit: every key plays Kick 1 tuned to the key -- a sub-bass instrument made "
               "of the kick -- through the TUNING page's scale, " + scale + ", so "
             + plays (36) + " and " + plays (48) + ". The other pads fall silent while it is lit.";
}

juce::String IctusProcessor::describeFollowKey (PadIndex pad) const
{
    const int note = getPadNote (pad);
    const juce::String scale = scaleName_ + " at A4 = " + juce::String (concertPitchHz_, 1) + " Hz";
    const bool kick = pad == PadIndex::kick1 || pad == PadIndex::kick2;

    juce::String text = "Lit: the " + juce::String (kick ? "landed pitch" : "shell's fundamental")
                      + " comes from the MIDI note through the TUNING page's scale (" + scale
                      + "). This pad only sounds on its own note, "
                      + juce::MidiMessage::getMidiNoteName (note, true, true, 3) + ", which plays "
                      + juce::String (previewTuning_.frequencyFor (note), 2)
                      + " Hz -- a fixed transposition, not a keyboard.";

    if (kick)
        text += " To play the kick across the keys, light BASS in the strip.";

    return text + " Dark: Tune sets the pitch.";
}

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

int IctusProcessor::getNumPrograms()
{
    return static_cast<int> (presets().size());
}

const juce::String IctusProcessor::getProgramName (int index)
{
    const auto& list = presets();

    return list[static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (list.size()) - 1, index))].name;
}

void IctusProcessor::setCurrentProgram (int index)
{
    const auto& list = presets();

    currentProgram_ = juce::jlimit (0, static_cast<int> (list.size()) - 1, index);

    // Everything to its default first, so a preset is a complete parameter
    // set rather than a patch over whatever was loaded before it.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& setting : list[static_cast<std::size_t> (currentProgram_)].settings)
        if (auto* parameter = state_.getParameter (setting.id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (setting.value));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void IctusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);

    // Both A/B slots, or the button is a session-only convenience.
    state.appendChild (abCompare_.toValueTree(), nullptr);

    for (int pad = 0; pad < kPadCount; ++pad)
        state.setProperty (padNoteProperty (static_cast<PadIndex> (pad)),
                           padNotes_[pad].load(), nullptr);

    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    // The tuning travels with the project, as text: a .scl file lives on one
    // machine and a project does not, so a path alone would open silently
    // detuned somewhere else -- and here it would retune every bass note.
    state.setProperty (kScaleNameProperty, scaleName_, nullptr);
    state.setProperty (kScaleTextProperty, scalaText_, nullptr);
    state.setProperty (kKeyboardMapProperty, keyboardMapText_, nullptr);
    state.setProperty (kConcertPitchProperty, concertPitchHz_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void IctusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    state_.replaceState (tree);

    abCompare_.restoreFromValueTree (tree.getChildWithName ("abCompare"));

    for (int pad = 0; pad < kPadCount; ++pad)
        padNotes_[pad].store (juce::jlimit (0, 127,
            static_cast<int> (tree.getProperty (padNoteProperty (static_cast<PadIndex> (pad)),
                                                kDefaultPadNotes[pad]))));

    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);

    // Concert pitch before the scale, so every publish carries it. A schema 1
    // state has none of these and lands on 12-TET at 440, which is what it
    // was playing.
    concertPitchHz_ = std::clamp (
        double (tree.getProperty (kConcertPitchProperty, 440.0)),
        dsp::Tuning::kMinimumConcertHz, dsp::Tuning::kMaximumConcertHz);

    const juce::String name = tree.getProperty (kScaleNameProperty, "").toString();
    const juce::String text = tree.getProperty (kScaleTextProperty, "").toString();
    const juce::String map = tree.getProperty (kKeyboardMapProperty, "").toString();

    resetTuning();

    if (text.isNotEmpty())
    {
        if (loadScalaText (text, name).isNotEmpty())
            resetTuning();
    }
    else if (name.isNotEmpty() && name != juce::String (scale_.name))
    {
        selectBuiltInScale (name);
    }

    if (map.isNotEmpty())
        loadKeyboardMapText (map);
}

juce::AudioProcessorEditor* IctusProcessor::createEditor()
{
    return new IctusEditor (*this);
}

} // namespace tezla::ictus

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::ictus::IctusProcessor();
}
