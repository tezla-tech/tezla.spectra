// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <iterator>
#include <random>
#include <cmath>
#include <vector>

#include <tezla/ui/StateIds.hpp>
#include <tezla/dsp/ScalaFile.hpp>
#include <tezla/dsp/Scales.hpp>

namespace tezla::sonitus
{

namespace
{
// Every parameter carries the schema version it was introduced at, and keeps it
// forever: the version hint feeds the VST3 parameter ID, so moving it on a live
// parameter is indistinguishable from renaming it. CLAUDE.md section 8.
constexpr int kSchemaV1 = 1;

/// Phase 3. Every parameter added by the phase-3 work carries this, and
/// existing parameters keep V1 forever -- the version hint feeds the VST3
/// parameter ID, so bumping a live one is indistinguishable from renaming it.
constexpr int kSchemaV2 = 2;

/// Phase 4, and the same rule again: new parameters carry V3, every existing
/// one keeps the version it was born with.
constexpr int kSchemaV3 = 3;

/// The ADV envelopes' points 9..16, added when the ceiling went from eight
/// points to sixteen. Points 1..8 keep V2 -- they are the same parameters they
/// always were and their hints must not move.
constexpr int kSchemaV4 = 4;

/// The rest of phase 4: the filter morph, and whatever P4-4 and P4-5 append.
/// A version of its own rather than reusing V4, so each hint keeps meaning one
/// piece of work.
constexpr int kSchemaV5 = 5;

/// Render quality -- the one parameter this version appends. A version of its
/// own, so the hint keeps meaning one piece of work.
constexpr int kSchemaV6 = 6;

/// Voice drift -- the voice card's temperature. Its own version, as ever.
constexpr int kSchemaV7 = 7;

/// **Phase 5 -- the horror phase.** Stack, Tract and Sag, all appended here and
/// all neutral at their defaults. Its own version, as ever: the hint feeds the
/// VST3 parameter ID, so one version means one piece of work forever.
constexpr int kSchemaV8 = 8;

/// **Phase 6.** The three deferred Stack items -- origin, shear and phase
/// panning -- and the filter's own limit cycle. All appended here, all neutral
/// at their defaults, and its own version for the same reason as every version
/// above it.
constexpr int kSchemaV9 = 9;

/// Shepard *Retrigger*, asked for on 2026-09-03 after phase 6 was played on
/// the rig. Its own version, as every parameter here has its own: the hint
/// feeds the VST3 parameter ID, so one version means one piece of work forever.
constexpr int kSchemaV10 = 10;

constexpr int kStateSchemaVersion = kSchemaV10;
constexpr auto kStateTypeName = "SonitusState";

/// DICEROLL's locks and strengths, stored beside the tuning rather than as
/// parameters. See `randomizeAllParameters`.
constexpr auto kDiceLocksProperty = "diceLocks";
constexpr auto kDiceAmountProperty = "diceAmount";
constexpr auto kDiceSpreadProperty = "diceSpread";

/// Where the tuning is stashed inside the state tree.
///
/// A property rather than a parameter, because a scale is text and a parameter
/// is a number. It has to be *in* the state, though: a project that reopens on
/// another machine has to sound the same without needing the `.scl` file to be
/// there, which means the file travels with the project.
constexpr auto kScaleTextProperty = "scalaText";
constexpr auto kScaleNameProperty = "scaleName";
constexpr auto kKeyboardMapProperty = "keyboardMapText";
constexpr auto kConcertPitchProperty = "concertPitch";

/// How fast an LFO can be set to run.
///
/// **160 Hz, which is well past a wobble and into audio rate.** Above roughly
/// 20 Hz an LFO on the cutoff stops being movement and starts being
/// modulation: it makes sidebands of its own, and the filter becomes part of
/// the oscillator. That is the same mechanism as the FILTER page's own FM
/// control, reached from the modulation matrix instead, and it is exactly the
/// kind of extremity this instrument is for.
///
/// The centre of the travel stays at 2 Hz, so everything below it feels exactly
/// as it did and only the top is new: the old 40 Hz maximum now sits at 80% of
/// the knob, and the last fifth is the audio-rate region.
///
/// The engine clamps the *effective* rate to half its control rate on top of
/// this, because key tracking and the sequencer both multiply it -- see
/// `Engine::advanceGlobalSources`.
constexpr float kMaximumLfoRateHz = 160.0f;

/// How long an envelope segment can be.
///
/// **Twenty seconds on all three**, which is a pad rather than a bass: a slow
/// swell that takes a whole phrase to arrive, and a release that is still
/// sounding four bars after the key came up. The attack was five seconds and
/// the other two were ten, which is fine for anything percussive and nowhere
/// near enough for the long warm end of the instrument.
///
/// Well inside `dsp::Adsr::kMaximumSeconds`, which is thirty -- the generator
/// clamps there, so a range above it would produce a control whose top could
/// not be reached.
///
/// The centres are unchanged (0.12 s for the attack, 0.35 s for decay and
/// release), so the short end feels exactly as it did and the added seconds are
/// all at the top of the travel. The envelope graphs on the ENV page follow the
/// parameter's own normalised position, so they re-scale with no change.
constexpr float kMaximumEnvelopeSeconds = 20.0f;

/// How many voices a fresh instance uses, against a ceiling of 32.
///
/// Not the ceiling itself, which would hand every new project a worst case
/// nobody asked for. Sixteen is roughly 79% of one core with all sixteen
/// sounding at once, and it is enough for overlapping pads with long releases,
/// which is the case that runs out first: a four-note chord under a six-second
/// release re-triggered every bar is twelve to sixteen voices in flight.
constexpr int kDefaultPolyphony = 16;

juce::NormalisableRange<float> skewedRange (float minimum, float maximum, float centre)
{
    juce::NormalisableRange<float> range { minimum, maximum };
    range.setSkewForCentre (centre);
    return range;
}

/// A bipolar range whose law is `sign(u) * u^2` about the centre.
///
/// The same shape the modulation matrix's depth uses, and for the same reason:
/// a tenth of the travel from the middle is a hundredth of the range, so fine
/// control survives at the bottom while the ends stay enormous. **Exactly zero
/// is reachable**, at exactly half travel, which matters when zero is a
/// setting in its own right rather than "very slow".
juce::NormalisableRange<float> bipolarSquaredRange (float maximum)
{
    return { -maximum, maximum,
             [maximum] (float, float, float t)
             {
                 const float u = 2.0f * t - 1.0f;
                 return maximum * std::copysign (u * u, u);
             },
             [maximum] (float, float, float value)
             {
                 const float u = std::copysign (std::sqrt (std::abs (value) / maximum), value);
                 return 0.5f * (u + 1.0f);
             },
             [] (float low, float high, float value)
             {
                 return juce::jlimit (low, high, value);
             } };
}

/// A range that is symmetric in *ratio* about 1.0 rather than in value.
///
/// Half travel is exactly 1.0, a quarter is the geometric mean of 1.0 and the
/// minimum, and so on: the knob's two halves are mirror images to the ear,
/// which linear travel between 0.5 and 2.0 is not (it would put 1.25 at the
/// middle).
juce::NormalisableRange<float> geometricRange (float minimum, float maximum)
{
    return { minimum, maximum,
             [minimum, maximum] (float, float, float t)
             {
                 return minimum * std::pow (maximum / minimum, t);
             },
             [minimum, maximum] (float, float, float value)
             {
                 return std::log (value / minimum) / std::log (maximum / minimum);
             },
             [] (float low, float high, float value)
             {
                 return juce::jlimit (low, high, value);
             } };
}

/// The size of the throat, shown as the ratio *and* the length it means -- the
/// number is only legible once it is centimetres.
juce::AudioParameterFloatAttributes tractAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int)
        {
            const double cm = dsp::Formant::tractLengthCm (static_cast<double> (value));

            return juce::String (value, 2) + "x  (" + juce::String (cm, 1) + " cm)";
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

/// Whole seconds, for a control whose useful range is tens of them.
juce::AudioParameterFloatAttributes secondsAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("s")
        .withStringFromValueFunction ([] (float value, int)
        {
            return juce::String (value, value < 10.0f ? 1 : 0) + " s";
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

/// Octaves per second, signed, with zero shown as "held" rather than 0.00 --
/// the same reading LFO rate 0 already gets, and for the same reason: it is a
/// deliberate setting, not the bottom of a range.
juce::AudioParameterFloatAttributes octavesPerSecondAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int)
        {
            if (std::abs (value) < 0.0005f)
                return juce::String ("held");

            return (value > 0.0f ? juce::String ("+") : juce::String ("-"))
                     + juce::String (std::abs (value), 2) + " oct/s";
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
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

juce::AudioParameterFloatAttributes bipolarPercentAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int)
        {
            const int shown = juce::roundToInt (value * 100.0f);
            return (shown > 0 ? juce::String ("+") : juce::String()) + juce::String (shown) + " %";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            return text.getFloatValue() * 0.01f;
        });
}

juce::AudioParameterFloatAttributes centsAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("ct")
        .withStringFromValueFunction ([] (float value, int)
        {
            const int shown = juce::roundToInt (value);
            return (shown > 0 ? juce::String ("+") : juce::String()) + juce::String (shown) + " ct";
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
                return juce::String (value / 1000.0f, 2) + " kHz";

            return juce::String (juce::roundToInt (value)) + " Hz";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            const float value = trimmed.getFloatValue();

            return trimmed.endsWithIgnoreCase ("k") || trimmed.containsIgnoreCase ("khz")
                     ? value * 1000.0f
                     : value;
        });
}

/// A time in seconds, shown in the unit the number is comfortable in.
juce::AudioParameterFloatAttributes timeAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int)
        {
            if (value < 0.0005f)
                return juce::String ("instant");

            if (value < 1.0f)
                return juce::String (value * 1000.0f, value < 0.1f ? 1 : 0) + " ms";

            return juce::String (value, 2) + " s";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            const auto trimmed = text.trim();
            const float value = trimmed.getFloatValue();

            return trimmed.containsIgnoreCase ("ms") ? value * 0.001f : value;
        });
}

/// A number of semitones, which reads better with its sign than without.
juce::AudioParameterFloatAttributes semitoneAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int)
        {
            const int shown = juce::roundToInt (value);
            return (shown > 0 ? juce::String ("+") : juce::String()) + juce::String (shown) + " st";
        })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

juce::String rateText (double rate)
{
    return juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " kHz";
}

[[nodiscard]] float valueOf (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    if (auto* parameter = state.getRawParameterValue (id))
        return parameter->load();

    return 0.0f;
}

[[nodiscard]] int indexOf (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    return static_cast<int> (std::lround (valueOf (state, id)));
}
} // namespace

// ---------------------------------------------------------------------------
// The parameter layout
// ---------------------------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout SonitusProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using Choice    = juce::AudioParameterChoice;
    using Boolean   = juce::AudioParameterBool;
    using Integer   = juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto addOscillator = [&layout] (const char* shapeId, const char* octaveId,
                                          const char* semitoneId, const char* centsId,
                                          const char* widthId, const char* morphId,
                                          const char* unisonId,
                                          const char* detuneId, const char* spreadId,
                                          const char* driftId, const char* levelId,
                                          const char* stackId, const char* stackStepId,
                                          const char* stackOriginId, const char* shepardPanId,
                                          const juce::String& prefix, float defaultLevel)
    {
        layout.add (std::make_unique<Choice> (
            juce::ParameterID { shapeId, kSchemaV1 }, prefix + " shape",
            choices::shape, 0));

        layout.add (std::make_unique<Integer> (
            juce::ParameterID { octaveId, kSchemaV1 }, prefix + " octave", -3, 3, 0));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { semitoneId, kSchemaV1 }, prefix + " semitones",
            juce::NormalisableRange<float> { -24.0f, 24.0f, 1.0f }, 0.0f, semitoneAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { centsId, kSchemaV1 }, prefix + " fine",
            juce::NormalisableRange<float> { -100.0f, 100.0f }, 0.0f, centsAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { widthId, kSchemaV1 }, prefix + " width",
            juce::NormalisableRange<float> { 0.02f, 0.98f }, 0.5f, percentAttributes()));

        // The shape's own tweak, in the Surge sense: its meaning depends on
        // the shape and 0 is always that shape's canonical self. The original
        // four shapes ignore it entirely, which is what keeps old projects
        // bit-exact -- the morphable relatives of saw, sine and triangle are
        // the new shapes.
        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { morphId, kSchemaV2 }, prefix + " morph",
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

        layout.add (std::make_unique<Integer> (
            juce::ParameterID { unisonId, kSchemaV1 }, prefix + " unison", 1, 7, 1));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { detuneId, kSchemaV1 }, prefix + " detune",
            skewedRange (0.0f, 60.0f, 15.0f), 0.0f, centsAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { spreadId, kSchemaV1 }, prefix + " spread",
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { driftId, kSchemaV1 }, prefix + " drift",
            skewedRange (0.0f, 30.0f, 6.0f), 0.0f, centsAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { levelId, kSchemaV1 }, prefix + " level",
            juce::NormalisableRange<float> { 0.0f, 1.0f }, defaultLevel, percentAttributes()));

        // **Stack** -- where the unison copies go. Index 0 is Detune, which is
        // what shipped and is bit-exact, so a project saved before phase 5
        // reopens with the stack it always had.
        layout.add (std::make_unique<Choice> (
            juce::ParameterID { stackId, kSchemaV8 }, prefix + " stack",
            choices::stack, 0));

        // Keys per copy, and it means something only in Scale mode -- which is
        // why the page greys it everywhere else rather than leaving a control
        // that silently does nothing.
        layout.add (std::make_unique<Integer> (
            juce::ParameterID { stackStepId, kSchemaV8 }, prefix + " stack step",
            kMinimumStackStep, kMaximumStackStep, 1));

        // **Origin** -- which side of the played note the stack builds on.
        // Centre is what shipped and is index 0. Exactly one copy sits at rank
        // 0 at every count and every origin, so this moves the stack without
        // ever moving the note.
        layout.add (std::make_unique<Choice> (
            juce::ParameterID { stackOriginId, kSchemaV9 }, prefix + " stack origin",
            choices::stackOrigin, 0));

        // **Phase pan** -- a Shepard stack sweeping the image as it climbs.
        // Off is what shipped: the copies then pan by their rank, which is
        // fixed, so the stereo picture stands still while the pitch moves.
        layout.add (std::make_unique<Boolean> (
            juce::ParameterID { shepardPanId, kSchemaV9 }, prefix + " shepard pan", false));
    };

    addOscillator (ids::shapeA, ids::octaveA, ids::semitonesA, ids::centsA, ids::widthA,
                   ids::morphA, ids::unisonA, ids::detuneA, ids::spreadA, ids::driftA, ids::levelA,
                   ids::stackA, ids::stackStepA, ids::stackOriginA, ids::shepardPanA,
                   "Osc A", 1.0f);

    addOscillator (ids::shapeB, ids::octaveB, ids::semitonesB, ids::centsB, ids::widthB,
                   ids::morphB, ids::unisonB, ids::detuneB, ids::spreadB, ids::driftB, ids::levelB,
                   ids::stackB, ids::stackStepB, ids::stackOriginB, ids::shepardPanB,
                   "Osc B", 0.0f);

    // **The Shepard glissando's speed**, in octaves per second, signed --
    // negative falls. One control for the instrument, because the phase is one
    // accumulator for the instrument: a held chord has to glide as one thing.
    // Zero is a legitimate setting and is a *held* windowed octave stack.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::shepardRate, kSchemaV8 }, "Shepard rate",
        bipolarSquaredRange (4.0f), 0.0f, octavesPerSecondAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::shepardSync, kSchemaV8 }, "Shepard sync", false));

    // **Shear** -- how far oscillator B's Shepard phase runs against A's. At 0
    // the two share one accumulator exactly, which is what shipped; at 1 B
    // falls at the same rate A rises, so the two stacks pass through each
    // other and the beating between them is the sound. Linear, because the
    // interesting settings are spread across the whole travel rather than
    // bunched at one end: the midpoint is B held still while A climbs.
    // **Retrigger** -- whether a note starts its own climb.
    //
    // Off is the shared accumulator that shipped: one clock, so a chord climbs
    // as a single gesture. On, each note captures the clock at its own note-on
    // and subtracts it, so it starts at the bottom while everything already
    // sounding carries on untouched -- an offset per voice rather than a reset
    // of the clock, which would drag every voice already sounding back with it.
    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::shepardRetrig, kSchemaV10 }, "Shepard retrigger", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::shepardShear, kSchemaV9 }, "Shepard shear",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    // **Sag** -- one slow instability shared by every voice. 0 is bit-exactly
    // out of the path, and the walk keeps walking regardless: it is still a
    // modulation source there, because the depth is how much reaches the voice
    // directly rather than whether the machine has a temperature.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::sag, kSchemaV8 }, "Sag",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::sagRate, kSchemaV8 }, "Sag rate",
        skewedRange (2.0f, 120.0f, 20.0f), 20.0f, secondsAttributes()));

    // **Tract** -- the size of the throat the vowel filter is modelling, as a
    // ratio on all three formants. Geometric about 1.0, because it is a
    // frequency ratio and the ear hears ratios; exactly 1.0 is neutral and is
    // bit-exact, so a project saved before phase 5 sounds identical.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::tract, kSchemaV8 }, "Tract",
        geometricRange (static_cast<float> (dsp::Formant::kNarrowestTract),
                        static_cast<float> (dsp::Formant::kWidestTract)),
        1.0f, tractAttributes()));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::shepardDiv, kSchemaV8 }, "Shepard division",
        choices::lfoDivision, dsp::defaultDivision));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::syncB, kSchemaV1 }, "Sync B to A", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::pmIndex, kSchemaV1 }, "PM index",
        skewedRange (0.0f, 8.0f, 1.5f), 0.0f, percentAttributes()));

    // **B modulating A** -- the other half of the FM pair. Same range and
    // skew as the forward path, because they are the same quantity in the
    // other direction; what differs is that this one is a sample late, which
    // is what makes the loop computable rather than algebraic.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::pmReverse, kSchemaV3 }, "PM reverse",
        skewedRange (0.0f, 8.0f, 1.5f), 0.0f, percentAttributes()));

    // **Operator feedback**, in cycles of self-deviation. One cycle is the
    // ceiling, which is about where the DX-series tops out and about where the
    // sound stops being a bright waveform and starts being noise -- which is
    // a legitimate destination, and the tooltip says so.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::feedbackA, kSchemaV3 }, "Feedback A",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::feedbackB, kSchemaV3 }, "Feedback B",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes()));

    // ---- sub and destruction ------------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::subShape, kSchemaV1 }, "Sub shape", choices::subShape, 0));

    // **Not only downward.** It is called a sub because that is what it is
    // usually for, but there is no reason the third oscillator should be barred
    // from the octave the note is in or the one above it -- at 0 it doubles the
    // note with a clean sine or square, which is a different and useful thing
    // from detuning oscillator B.
    layout.add (std::make_unique<Integer> (
        juce::ParameterID { ids::subOctave, kSchemaV1 }, "Sub octave", -2, 2, -1,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int value, int)
            {
                if (value == 0)
                    return juce::String ("unison");

                return (value > 0 ? juce::String ("+") : juce::String())
                         + juce::String (value) + " oct";
            })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::subLevel, kSchemaV1 }, "Sub level",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::ringAmount, kSchemaV1 }, "Ring",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::foldAmount, kSchemaV1 }, "Fold",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    // ---- kargyraa ------------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::kargyraa, kSchemaV1 }, "Kargyraa",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::kargyraaRasp, kSchemaV1 }, "Kargyraa rasp",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::kargyraaDivisor, kSchemaV1 }, "Kargyraa divisor",
        choices::kargyraaDivisor, 0));

    // ---- filter -------------------------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::filterMode, kSchemaV1 }, "Filter", choices::filterMode, 0));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::cutoff, kSchemaV1 }, "Cutoff",
        skewedRange (20.0f, 18000.0f, 800.0f), 12000.0f, hertzAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::resonance, kSchemaV1 }, "Resonance",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::filterDrive, kSchemaV1 }, "Filter drive",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::filterTrack, kSchemaV1 }, "Key track",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::filterFm, kSchemaV1 }, "Filter FM",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::filterVel, kSchemaV1 }, "Velocity to cutoff",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    // Bipolar and centred on the mode switch, so its default of 0 is the mode
    // the project saved -- an absolute 0..1 "position" control would default
    // to lowpass and silently convert every bandpass patch that exists.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::filterMorph, kSchemaV5 }, "Filter morph",
        juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f, percentAttributes()));

    // **Sing** -- the filter driven past its own damping and into a limit
    // cycle. 0 is bit-exactly the filter that shipped: the damping is 1/Q and
    // so is positive until this asks otherwise, and the sample loop tests that
    // sign rather than a flag.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::filterSing, kSchemaV9 }, "Filter sing",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    // ---- envelopes ----------------------------------------------------------

    /// One envelope's eight parameters. A struct rather than eight positional
    /// arguments, because eight `const char*` in a row is a wiring error
    /// waiting to happen and the compiler cannot tell decay from release.
    struct EnvelopeIds
    {
        const char* attack;
        const char* hold;
        const char* decay;
        const char* sustain;
        const char* release;
        const char* attackTension;
        const char* decayTension;
        const char* releaseTension;
    };

    const auto addEnvelope = [&layout] (const EnvelopeIds& ids_, const juce::String& prefix,
                                        float defaultSustain)
    {
        const char* attackId = ids_.attack;
        const char* decayId = ids_.decay;
        const char* sustainId = ids_.sustain;
        const char* releaseId = ids_.release;

        // **The skew is the control**, and the first version of it made the
        // attack knob unusable. A range of 0 to 10 seconds centred at 50 ms
        // puts the *bottom forty percent of the travel under ten milliseconds*
        // and the first quarter under a quarter of one -- so a knob that looks
        // like it has an attack on it has none, which reads as the envelope
        // jumping straight to its sustain.
        //
        // Centred at 120 ms over five seconds instead: 2.9 ms at a quarter
        // turn, 36 at 40%, 120 at half, 1.06 s at three quarters. Every part of
        // the travel does something.
        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { attackId, kSchemaV1 }, prefix + " attack",
            skewedRange (0.0f, kMaximumEnvelopeSeconds, 0.12f), 0.005f, timeAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { decayId, kSchemaV1 }, prefix + " decay",
            skewedRange (0.0f, kMaximumEnvelopeSeconds, 0.35f), 0.25f, timeAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { sustainId, kSchemaV1 }, prefix + " sustain",
            juce::NormalisableRange<float> { 0.0f, 1.0f }, defaultSustain, percentAttributes()));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { releaseId, kSchemaV1 }, prefix + " release",
            skewedRange (0.0f, kMaximumEnvelopeSeconds, 0.35f), 0.15f, timeAttributes()));

        // **Hold**, between the attack and the decay. Zero by default, so an
        // envelope that has never been told about it behaves exactly as an
        // ADSR does. Skewed like the other times, centred lower because a hold
        // is usually a moment rather than a passage.
        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { ids_.hold, kSchemaV1 }, prefix + " hold",
            skewedRange (0.0f, kMaximumEnvelopeSeconds, 0.20f), 0.0f, timeAttributes()));

        // **Tension, per segment and bipolar.** The old single Shape control
        // ran from "sharp analogue" to "nearly straight" and no further,
        // because aiming past a destination only bends a curve one way. These
        // go both ways from a straight middle, and the three segments get their
        // own, because a percussive envelope is usually a sharp decay under a
        // straight attack and a swell is the reverse.
        const auto addTension = [&layout, &prefix] (const char* id, const juce::String& name)
        {
            layout.add (std::make_unique<Parameter> (
                juce::ParameterID { id, kSchemaV1 }, prefix + " " + name + " tension",
                juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.35f,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float value, int)
                    {
                        if (std::abs (value) < 0.005f)
                            return juce::String ("linear");

                        return juce::String (value > 0.0f ? "+" : "")
                                 + juce::String (value * 100.0f, 0) + " %";
                    })));
        };

        addTension (ids_.attackTension, "attack");
        addTension (ids_.decayTension, "decay");
        addTension (ids_.releaseTension, "release");
    };

    addEnvelope ({ ids::ampAttack, ids::ampHold, ids::ampDecay, ids::ampSustain,
                   ids::ampRelease, ids::ampAttackT, ids::ampDecayT, ids::ampReleaseT },
                 "Amp", 0.8f);

    // Snap-to-tempo, one per envelope. The quantising happens in the engine,
    // which knows the live tempo -- Engine::snappedVoice.
    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::ampSnap, kSchemaV2 }, "Amp snap", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::ampVelocity, kSchemaV1 }, "Velocity to level",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    addEnvelope ({ ids::env1Attack, ids::env1Hold, ids::env1Decay, ids::env1Sustain,
                   ids::env1Release, ids::env1AttackT, ids::env1DecayT, ids::env1ReleaseT },
                 "Env 1", 0.0f);

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::env1Snap, kSchemaV2 }, "Env 1 snap", false));

    addEnvelope ({ ids::env2Attack, ids::env2Hold, ids::env2Decay, ids::env2Sustain,
                   ids::env2Release, ids::env2AttackT, ids::env2DecayT, ids::env2ReleaseT },
                 "Env 2", 0.0f);

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::env2Snap, kSchemaV2 }, "Env 2 snap", false));

    // ---- ADV envelopes ------------------------------------------------------
    //
    // Three multi-stage breakpoint envelopes, 162 parameters built by ids::adv
    // rather than typed. Everything defaults to a disabled, sensible four-point
    // ADSR-ish curve, so switching one on does something audible before any
    // editing and a project that never heard of them is untouched.
    //
    // **Points 1..8 carry V2 and points 9..16 carry V4**, which is the whole
    // of the append-only discipline here: the first eight are the parameters
    // that shipped and their ids and version hints are frozen; the eight that
    // raised the ceiling to sixteen are new names at a new hint, so a project
    // saved against the eight-point build finds every id it stored exactly
    // where it left it.
    //
    // The one thing that genuinely changed under an existing id is the *range*
    // of Points, Sustain and LoopStart, 8 -> 16. The stored value is the plain
    // integer, not a normalised fraction (that is what APVTS keeps in its
    // tree), so a saved project reopens on the same numbers -- verified by
    // rendering an eight-point-era patch before and after and comparing bit
    // for bit. A *recorded automation curve* on one of those three is the
    // exception, because automation is normalised: a lane drawn to 4 points on
    // the old range reads 6 or 7 on the new one. They are structural controls
    // that nobody automates, and saying so is cheaper than a second parameter.
    for (int envelope = 0; envelope < 3; ++envelope)
    {
        const auto prefix = "ADV " + juce::String (envelope + 1);

        layout.add (std::make_unique<Boolean> (
            juce::ParameterID { ids::adv (envelope, "Enable"), kSchemaV2 },
            prefix + " enable", false));

        layout.add (std::make_unique<Boolean> (
            juce::ParameterID { ids::adv (envelope, "Loop"), kSchemaV2 },
            prefix + " loop", false));

        layout.add (std::make_unique<Boolean> (
            juce::ParameterID { ids::adv (envelope, "Snap"), kSchemaV2 },
            prefix + " snap", false));

        layout.add (std::make_unique<Integer> (
            juce::ParameterID { ids::adv (envelope, "Points"), kSchemaV2 },
            prefix + " points", 2, dsp::MultiEnvelope::kMaxPoints, 4));

        // Displayed 1-based; the engine subtracts one.
        layout.add (std::make_unique<Integer> (
            juce::ParameterID { ids::adv (envelope, "Sustain"), kSchemaV2 },
            prefix + " sustain point", 1, dsp::MultiEnvelope::kMaxPoints, 3));

        layout.add (std::make_unique<Integer> (
            juce::ParameterID { ids::adv (envelope, "LoopStart"), kSchemaV2 },
            prefix + " loop start", 1, dsp::MultiEnvelope::kMaxPoints, 1));

        // Sixteen of each; points past the fourth are flat, level-0 legs, so
        // lengthening an envelope adds time rather than a shape.
        constexpr float defaultSeconds[] { 0.01f, 0.25f, 0.05f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f,
                                           0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
        constexpr float defaultLevel[]   { 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        constexpr float defaultTension[] { 0.35f, 0.35f, 0.0f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        static_assert (static_cast<int> (std::size (defaultSeconds)) == dsp::MultiEnvelope::kMaxPoints);
        static_assert (static_cast<int> (std::size (defaultLevel)) == dsp::MultiEnvelope::kMaxPoints);
        static_assert (static_cast<int> (std::size (defaultTension)) == dsp::MultiEnvelope::kMaxPoints);

        for (int point = 0; point < dsp::MultiEnvelope::kMaxPoints; ++point)
        {
            const auto n = juce::String (point + 1);

            // The first eight are frozen at V2; everything the sixteen-point
            // ceiling added is V4. Never the other way round.
            const int hint = point < 8 ? kSchemaV2 : kSchemaV4;

            layout.add (std::make_unique<Parameter> (
                juce::ParameterID { ids::adv (envelope, "T" + n), hint },
                prefix + " time " + n,
                skewedRange (0.0f, 20.0f, 0.12f),
                defaultSeconds[point], timeAttributes()));

            layout.add (std::make_unique<Parameter> (
                juce::ParameterID { ids::adv (envelope, "L" + n), hint },
                prefix + " level " + n,
                juce::NormalisableRange<float> { 0.0f, 1.0f },
                defaultLevel[point], percentAttributes()));

            layout.add (std::make_unique<Parameter> (
                juce::ParameterID { ids::adv (envelope, "C" + n), hint },
                prefix + " tension " + n,
                juce::NormalisableRange<float> { -1.0f, 1.0f },
                defaultTension[point], percentAttributes()));
        }
    }

    // ---- keyboard -----------------------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::keyMode, kSchemaV1 }, "Keyboard", choices::keyMode, 0));

    layout.add (std::make_unique<Integer> (
        juce::ParameterID { ids::polyphony, kSchemaV1 }, "Voices", 1,
        VoiceManager::kMaxVoices, kDefaultPolyphony));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::glide, kSchemaV1 }, "Glide",
        skewedRange (0.0f, static_cast<float> (VoiceManager::kMaximumGlideSeconds), 0.15f),
        0.0f, timeAttributes()));

    layout.add (std::make_unique<Integer> (
        juce::ParameterID { ids::bendRange, kSchemaV1 }, "Bend range", 0, 24, 2,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int value, int)
            {
                return juce::String (value) + " st";
            })));

    // ---- global modulation sources ------------------------------------------

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::lfo1Wave, kSchemaV1 }, "LFO 1 wave", choices::lfoWave, 0));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo1Rate, kSchemaV1 }, "LFO 1 rate",
        skewedRange (0.0f, kMaximumLfoRateHz, 2.0f), 2.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                // Zero is a legitimate setting and the brief's whole trick --
                // the rate pinned at nothing, with the depth driven from
                // somewhere else. Saying so is better than showing "0.00 Hz".
                if (value < 0.005f)
                    return juce::String ("held");

                return juce::String (value, value < 10.0f ? 2 : 1) + " Hz";
            })));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::lfo1Sync, kSchemaV2 }, "LFO 1 sync", false));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::lfo1Div, kSchemaV2 }, "LFO 1 division",
        choices::lfoDivision, dsp::defaultDivision));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo1Smooth, kSchemaV1 }, "LFO 1 smooth",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::lfo1Retrig, kSchemaV1 }, "LFO 1 retrigger", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo1Key, kSchemaV1 }, "LFO 1 key track",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo1Att, kSchemaV1 }, "LFO 1 attack",
        skewedRange (0.0f, 10.0f, 0.5f), 0.0f, timeAttributes()));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::lfo2Wave, kSchemaV1 }, "LFO 2 wave", choices::lfoWave, 1));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo2Rate, kSchemaV1 }, "LFO 2 rate",
        skewedRange (0.0f, kMaximumLfoRateHz, 2.0f), 0.25f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                if (value < 0.005f)
                    return juce::String ("held");

                return juce::String (value, value < 10.0f ? 2 : 1) + " Hz";
            })));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::lfo2Sync, kSchemaV2 }, "LFO 2 sync", false));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::lfo2Div, kSchemaV2 }, "LFO 2 division",
        choices::lfoDivision, dsp::defaultDivision));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo2Smooth, kSchemaV1 }, "LFO 2 smooth",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::lfo2Retrig, kSchemaV1 }, "LFO 2 retrigger", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo2Key, kSchemaV1 }, "LFO 2 key track",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::lfo2Att, kSchemaV1 }, "LFO 2 attack",
        skewedRange (0.0f, 10.0f, 0.5f), 0.0f, timeAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::seqRate, kSchemaV1 }, "Seq rate",
        skewedRange (0.25f, 64.0f, 8.0f), 8.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, value < 10.0f ? 2 : 1) + " /beat";
            })));

    layout.add (std::make_unique<Integer> (
        juce::ParameterID { ids::seqLength, kSchemaV1 }, "Seq length", 1,
        dsp::StepSequencer::kMaxSteps, 16));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::seqGlide, kSchemaV1 }, "Seq glide",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::seqToLfoRate, kSchemaV1 }, "Seq to LFO 1 rate",
        juce::NormalisableRange<float> { 0.0f, 3.0f }, 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, 2) + " oct";
            })));

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { ids::step (step), kSchemaV1 },
            "Step " + juce::String (step + 1),
            juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f, bipolarPercentAttributes()));

    // ---- the matrix ---------------------------------------------------------

    for (int slot = 0; slot < VoiceParameters::kSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::modSource (slot), kSchemaV1 },
            "Mod " + number + " source", choices::modSource, 0));

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::modDest (slot), kSchemaV1 },
            "Mod " + number + " target", choices::modDest, 0));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { ids::modDepth (slot), kSchemaV1 },
            "Mod " + number + " depth",
            juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f, bipolarPercentAttributes()));
    }

    for (int slot = 0; slot < EngineParameters::kGlobalSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::globalSource (slot), kSchemaV1 },
            "Global " + number + " source", choices::globalSource, 0));

        layout.add (std::make_unique<Choice> (
            juce::ParameterID { ids::globalDest (slot), kSchemaV1 },
            "Global " + number + " target", choices::globalDest, 0));

        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { ids::globalDepth (slot), kSchemaV1 },
            "Global " + number + " depth",
            juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f, bipolarPercentAttributes()));
    }

    // ---- the split and the mangle -------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::splitHz, kSchemaV1 }, "Split",
        skewedRange (40.0f, 400.0f, 120.0f), 120.0f, hertzAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::subMono, kSchemaV1 }, "Sub to mono", true));

    // Default ON, which is the engine that shipped -- a project saved before
    // this parameter existed reopens sounding the same (CLAUDE.md section 8).
    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::subSplit, kSchemaV2 }, "Sub split", true));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::order, kSchemaV1 }, "Order", choices::order, 0));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::tubeDrive, kSchemaV1 }, "Tube",
        skewedRange (0.0f, static_cast<float> (Engine::kMaximumTubeDriveDb), 9.0f), 0.0f,
        decibelAttributes()));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::combMode, kSchemaV1 }, "Comb", choices::combMode, 0));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::combTime, kSchemaV1 }, "Comb time",
        skewedRange (0.02f, 20.0f, 3.0f), 3.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float value, int)
            {
                // The notch is what the control is *for*, so both are shown.
                const int notch = juce::roundToInt (500.0f / value);

                return juce::String (value, value < 1.0f ? 3 : 2) + " ms  ("
                         + juce::String (notch) + " Hz)";
            })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::combTrack, kSchemaV1 }, "Comb key track",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::combFeed, kSchemaV1 }, "Feedback",
        juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f, bipolarPercentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::combDamp, kSchemaV1 }, "Damp",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::combSpread, kSchemaV1 }, "Comb spread",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::combMix, kSchemaV1 }, "Comb mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::combInvert, kSchemaV1 }, "Invert wet", false));

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::combScale, kSchemaV5 }, "Comb scale lock", false));

    // ---- macros -------------------------------------------------------------
    //
    // Four knobs that are sources in **both** matrices. Default 0, which
    // contributes exactly nothing wherever it is pointed -- so a project that
    // never heard of them is untouched, and an unassigned macro costs one
    // array copy per control chunk.
    for (int macro = 0; macro < 4; ++macro)
        layout.add (std::make_unique<Parameter> (
            juce::ParameterID { ids::macro (macro), kSchemaV5 },
            "Macro " + juce::String (macro + 1),
            juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::phaseFreq, kSchemaV1 }, "Phase centre",
        skewedRange (20.0f, 18000.0f, 800.0f), 800.0f, hertzAttributes()));

    layout.add (std::make_unique<Integer> (
        juce::ParameterID { ids::phaseStages, kSchemaV1 }, "Phase stages",
        dsp::Phaser::kMinimumStages, dsp::Phaser::kMaximumStages, 4));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::formantMorph, kSchemaV1 }, "Vowel",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                static const char* const names[] = { "ee", "eh", "ah", "oh", "oo" };

                const float position = value * (dsp::Formant::kVowels - 1);
                const int lower = juce::jlimit (0, dsp::Formant::kVowels - 1,
                                                static_cast<int> (position));
                const float blend = position - static_cast<float> (lower);

                if (blend < 0.05f)
                    return juce::String (names[lower]);

                const int upper = juce::jmin (lower + 1, dsp::Formant::kVowels - 1);

                return juce::String (names[lower]) + juce::String ("-") + juce::String (names[upper]);
            })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::formantSharp, kSchemaV1 }, "Vowel sharpness",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::formantMix, kSchemaV1 }, "Vowel mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    // The overtone-singing controls. Appended and neutral by default, so a
    // project saved before they existed reopens sounding the same.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::formantLock, kSchemaV1 }, "Harmonic lock",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::formantHarmonic, kSchemaV1 }, "Harmonic",
        juce::NormalisableRange<float> { 1.0f,
                                         static_cast<float> (dsp::Formant::kMaximumHarmonic) },
        1.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                // The interval above the fundamental, because that is what a
                // player hears: partial 3 is a twelfth, partial 5 a major third
                // two octaves up. Shown alongside the number, since the number
                // is what the sequencer is stepping.
                static const char* const names[] = {
                    "unison", "octave", "12th", "2 oct", "+M3", "+5th", "+m7", "3 oct",
                    "+M2", "+M3", "+4th", "+5th", "+m6", "+m7", "+M7", "4 oct" };

                const int partial = juce::roundToInt (value);
                const int index = juce::jlimit (1, 16, partial) - 1;

                return juce::String (value, 2) + "  (" + names[index] + ")";
            })));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::formantNotch, kSchemaV1 }, "Notch",
        skewedRange (100.0f, 8000.0f, 1000.0f), 1000.0f, hertzAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::formantNotchDepth, kSchemaV1 }, "Notch depth",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentAttributes()));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::tilt, kSchemaV1 }, "Tilt",
        juce::NormalisableRange<float> { -1.0f, 1.0f }, 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                const float decibels = value * static_cast<float> (Engine::kTiltRangeDb);

                if (std::abs (decibels) < 0.05f)
                    return juce::String ("flat");

                return juce::String (decibels > 0.0f ? "+" : "") + juce::String (decibels, 1)
                         + " dB top";
            })));

    // ---- global -------------------------------------------------------------

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::output, kSchemaV1 }, "Output",
        juce::NormalisableRange<float> { -36.0f, 12.0f }, 0.0f, decibelAttributes()));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::oversampling, kSchemaV1 }, "Oversampling",
        choices::oversampling, 0));

    // What an offline bounce runs at. Default 0, "Same as live": neutral, so a
    // project saved before this existed renders exactly what it played.
    layout.add (std::make_unique<Choice> (
        juce::ParameterID { ids::renderOversampling, kSchemaV6 }, "Render quality",
        choices::renderOversampling, 0));

    // ---- the voice card -----------------------------------------------------
    // Appended at V7, at the end, because a host's automation list is order
    // sensitive. Cents of cutoff, skewed so the 10..60 cents a warm polysynth
    // actually does sits in the first half of the travel and the rest is the
    // creative range. 0 is bit-exactly off.
    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::voiceDrift, kSchemaV7 }, "Voice drift",
        skewedRange (0.0f, 600.0f, 40.0f), 0.0f, centsAttributes()));

    return layout;
}

// ---------------------------------------------------------------------------
// Construction and preparation
// ---------------------------------------------------------------------------

SonitusProcessor::SonitusProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state_ (*this, nullptr, kStateTypeName, createParameterLayout())
{
    // scales:: rather than the Tuning class's bare builder, so the default
    // scale arrives with the construction and story the tuning panel shows.
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
}

bool SonitusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument, so there is no input bus to match. Stereo out only: every
    // stage after the split is genuinely stereo, and folding to mono at the end
    // would throw away the spread the comb exists to create.
    return layouts.getMainInputChannelSet().isDisabled()
             && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SonitusProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    // The parameters before prepare, so the graph is built at the factor the
    // saved state asks for rather than rebuilt on the first block -- which
    // would cut any note already sounding.
    pullParameters();
    engine_.setParameters (parameters_);

    // Whether the host is rendering offline, taken before `prepare` so the
    // graph is built at the render factor from the first bounced sample. The
    // VST3 wrapper sets the flag in setupProcessing, before activation, which
    // is exactly this order; a host that only flips it per block gets the
    // rebuild -- and its clean stop -- at the first offline block instead.
    engine_.setOffline (isNonRealtime());

    engine_.prepare (sampleRate_, std::max (maximumExpectedSamplesPerBlock, 1));

    // `prepare` builds a fresh graph with a fresh tuning, and it is not
    // concurrent with `processBlock`, so the hand-off is taken here and now
    // rather than a block later -- otherwise the first note of a session could
    // be played in the wrong scale.
    publishTuning();
    collectTuning();

    scratch_.setSize (2, std::max (maximumExpectedSamplesPerBlock, 1), false, false, true);

    for (auto& meter : outputMeter_)
    {
        meter.prepare (sampleRate_);
        meter.reset();
    }

    reportedLatency_ = engine_.getLatencySamples();
    setLatencySamples (reportedLatency_);

    prepared_ = true;
}

double SonitusProcessor::getTailLengthSeconds() const
{
    // The amp release plus the filter's own ring-down at full resonance, which
    // is 6.9 * Q / (pi * f0) and reaches 1.1 s at 1 kHz. Reporting zero from an
    // instrument truncates every tail the host renders offline.
    auto* release = state_.getRawParameterValue (ids::ampRelease);

    const double envelope = release != nullptr ? static_cast<double> (release->load()) : 0.2;

    return envelope + 1.5;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

void SonitusProcessor::pullParameters()
{
    auto& p = parameters_;
    auto& v = p.voice;

    v.shapeA = static_cast<dsp::OscShape> (indexOf (state_, ids::shapeA));
    v.octaveA = valueOf (state_, ids::octaveA);
    v.semitonesA = valueOf (state_, ids::semitonesA);
    v.centsA = valueOf (state_, ids::centsA);
    v.widthA = valueOf (state_, ids::widthA);
    v.morphA = valueOf (state_, ids::morphA);
    v.unisonA = indexOf (state_, ids::unisonA);
    v.detuneA = valueOf (state_, ids::detuneA);
    v.spreadA = valueOf (state_, ids::spreadA);
    v.driftA = valueOf (state_, ids::driftA);
    v.levelA = valueOf (state_, ids::levelA);

    v.shapeB = static_cast<dsp::OscShape> (indexOf (state_, ids::shapeB));
    v.octaveB = valueOf (state_, ids::octaveB);
    v.semitonesB = valueOf (state_, ids::semitonesB);
    v.centsB = valueOf (state_, ids::centsB);
    v.widthB = valueOf (state_, ids::widthB);
    v.morphB = valueOf (state_, ids::morphB);

    v.feedbackA = valueOf (state_, ids::feedbackA);
    v.feedbackB = valueOf (state_, ids::feedbackB);
    v.pmReverse = valueOf (state_, ids::pmReverse);
    v.unisonB = indexOf (state_, ids::unisonB);
    v.detuneB = valueOf (state_, ids::detuneB);
    v.spreadB = valueOf (state_, ids::spreadB);
    v.driftB = valueOf (state_, ids::driftB);
    v.levelB = valueOf (state_, ids::levelB);

    // **Stack.** Where each bank's copies go, and how many keys apart Scale
    // mode puts them. Index 0 is Detune, which is what shipped.
    v.stackA = static_cast<StackMode> (juce::jlimit (0, static_cast<int> (StackMode::count) - 1,
                                                     indexOf (state_, ids::stackA)));
    v.stackB = static_cast<StackMode> (juce::jlimit (0, static_cast<int> (StackMode::count) - 1,
                                                     indexOf (state_, ids::stackB)));

    v.stackStepA = juce::jlimit (kMinimumStackStep, kMaximumStackStep,
                                 static_cast<int> (valueOf (state_, ids::stackStepA)));
    v.stackStepB = juce::jlimit (kMinimumStackStep, kMaximumStackStep,
                                 static_cast<int> (valueOf (state_, ids::stackStepB)));

    // Which side of the played note the stack builds on, and whether a Shepard
    // stack sweeps the image while it climbs. Both index 0 / false are what
    // shipped.
    v.stackOriginA = static_cast<StackOrigin> (juce::jlimit (0, static_cast<int> (StackOrigin::count) - 1,
                                                             indexOf (state_, ids::stackOriginA)));
    v.stackOriginB = static_cast<StackOrigin> (juce::jlimit (0, static_cast<int> (StackOrigin::count) - 1,
                                                             indexOf (state_, ids::stackOriginB)));

    v.shepardPanA = valueOf (state_, ids::shepardPanA) > 0.5f;
    v.shepardPanB = valueOf (state_, ids::shepardPanB) > 0.5f;
    v.syncB = valueOf (state_, ids::syncB) > 0.5f;
    v.pmIndex = valueOf (state_, ids::pmIndex);

    v.subShape = static_cast<SubShape> (indexOf (state_, ids::subShape));
    v.subOctave = indexOf (state_, ids::subOctave);
    v.subLevel = valueOf (state_, ids::subLevel);
    v.ringAmount = valueOf (state_, ids::ringAmount);
    v.foldAmount = valueOf (state_, ids::foldAmount);

    v.kargyraaDepth = valueOf (state_, ids::kargyraa);
    v.kargyraaRasp = valueOf (state_, ids::kargyraaRasp);

    // The choice stores an index; the divisor it names starts at two.
    v.kargyraaDivisor = 2 + static_cast<int> (std::lround (valueOf (state_, ids::kargyraaDivisor)));

    v.filterMode = static_cast<dsp::SvfMode> (indexOf (state_, ids::filterMode));
    v.cutoffHz = valueOf (state_, ids::cutoff);
    v.resonance = valueOf (state_, ids::resonance);
    v.filterDrive = valueOf (state_, ids::filterDrive);
    v.filterMorph = valueOf (state_, ids::filterMorph);
    v.filterSing = valueOf (state_, ids::filterSing);
    v.voiceDrift = valueOf (state_, ids::voiceDrift);
    v.filterKeyTrack = valueOf (state_, ids::filterTrack);
    v.filterFm = valueOf (state_, ids::filterFm);
    v.filterVelocity = valueOf (state_, ids::filterVel);

    // One envelope's eight, read the same way three times over.
    const auto pullEnvelope = [this] (VoiceParameters::Envelope& envelope,
                                      const char* attack, const char* hold, const char* decay,
                                      const char* sustain, const char* release,
                                      const char* attackT, const char* decayT,
                                      const char* releaseT)
    {
        envelope.attack = valueOf (state_, attack);
        envelope.hold = valueOf (state_, hold);
        envelope.decay = valueOf (state_, decay);
        envelope.sustain = valueOf (state_, sustain);
        envelope.release = valueOf (state_, release);
        envelope.attackTension = valueOf (state_, attackT);
        envelope.decayTension = valueOf (state_, decayT);
        envelope.releaseTension = valueOf (state_, releaseT);
    };

    pullEnvelope (v.amp, ids::ampAttack, ids::ampHold, ids::ampDecay, ids::ampSustain,
                  ids::ampRelease, ids::ampAttackT, ids::ampDecayT, ids::ampReleaseT);
    v.amp.snap = valueOf (state_, ids::ampSnap) > 0.5f;

    v.ampVelocity = valueOf (state_, ids::ampVelocity);

    pullEnvelope (v.mod1, ids::env1Attack, ids::env1Hold, ids::env1Decay, ids::env1Sustain,
                  ids::env1Release, ids::env1AttackT, ids::env1DecayT, ids::env1ReleaseT);
    v.mod1.snap = valueOf (state_, ids::env1Snap) > 0.5f;

    pullEnvelope (v.mod2, ids::env2Attack, ids::env2Hold, ids::env2Decay, ids::env2Sustain,
                  ids::env2Release, ids::env2AttackT, ids::env2DecayT, ids::env2ReleaseT);
    v.mod2.snap = valueOf (state_, ids::env2Snap) > 0.5f;

    for (int envelope = 0; envelope < 3; ++envelope)
    {
        auto& adv = v.adv[static_cast<std::size_t> (envelope)];

        adv.enable = valueOf (state_, ids::adv (envelope, "Enable")) > 0.5f;
        adv.loop = valueOf (state_, ids::adv (envelope, "Loop")) > 0.5f;
        adv.snap = valueOf (state_, ids::adv (envelope, "Snap")) > 0.5f;
        adv.points = static_cast<int> (std::lround (valueOf (state_, ids::adv (envelope, "Points"))));
        adv.sustain = static_cast<int> (std::lround (valueOf (state_, ids::adv (envelope, "Sustain")))) - 1;
        adv.loopStart = static_cast<int> (std::lround (valueOf (state_, ids::adv (envelope, "LoopStart")))) - 1;

        for (int point = 0; point < dsp::MultiEnvelope::kMaxPoints; ++point)
        {
            const auto n = juce::String (point + 1);
            const auto i = static_cast<std::size_t> (point);

            adv.seconds[i] = valueOf (state_, ids::adv (envelope, "T" + n));
            adv.level[i] = valueOf (state_, ids::adv (envelope, "L" + n));
            adv.tension[i] = valueOf (state_, ids::adv (envelope, "C" + n));
        }
    }

    v.level = 1.0;

    for (int slot = 0; slot < VoiceParameters::kSlots; ++slot)
    {
        v.slots[slot].source = static_cast<ModSource> (indexOf (state_, ids::modSource (slot)));
        v.slots[slot].destination = static_cast<ModDestination> (indexOf (state_, ids::modDest (slot)));

        // The depth is stored as -1..+1 and scaled here into each
        // destination's own units, so the control reads the same wherever it
        // is pointed and the DSP never has to know what a percentage means.
        //
        // **Squared on the way through** -- see `shapedDepth`. The ranges below
        // are deliberately extreme, and a linear knob at these ranges would
        // have no usable middle: five octaves of pitch means a tenth of the
        // travel is already six semitones.
        const double depth = shapedDepth (valueOf (state_, ids::modDepth (slot)));

        switch (v.slots[slot].destination)
        {
            // Ten octaves is the whole audible band, so one control move can
            // cross it and come back.
            case ModDestination::cutoff:      v.slots[slot].depth = depth * 10.0; break;   // octaves

            // Six octaves -- seventy-two semitones. This is the sync scream:
            // from a bass C the slave reaches 8.4 kHz, which is the slave
            // leaving the note behind entirely and its own pitch ceasing to be
            // a pitch at all.
            case ModDestination::pitch:
            case ModDestination::pitchB:      v.slots[slot].depth = depth * 7200.0; break; // cents

            // An octave of detune is not a unison any more, it is a cluster --
            // which is a sound rather than a mistake.
            case ModDestination::detuneA:
            case ModDestination::detuneB:     v.slots[slot].depth = depth * 1200.0; break; // cents

            // The voice clamps phase modulation at 16 and the depth reached 8,
            // so half the range was unreachable from the matrix. The reverse
            // path shares the ceiling and therefore the scale.
            case ModDestination::pmIndex:
            case ModDestination::pmReverse:   v.slots[slot].depth = depth * 16.0; break;

            // The rest are already normalised, so the -1..+1 depth is the
            // depth. Listed rather than defaulted: a destination added to the
            // enum and forgotten here would silently get a scale of 1, and
            // that is a bug the compiler can find instead.
            case ModDestination::none:
            case ModDestination::resonance:
            case ModDestination::filterDrive:
            case ModDestination::pulseWidthA:
            case ModDestination::pulseWidthB:
            case ModDestination::oscMix:
            case ModDestination::subLevel:
            case ModDestination::ringAmount:
            case ModDestination::foldAmount:
            case ModDestination::level:

            // Already a fraction, like the three above it: the voice clamps the
            // sum to 0..1, so full depth reaches either end of the control from
            // wherever the knob is set.
            case ModDestination::kargyraa:
            case ModDestination::morphA:
            case ModDestination::morphB:

            // Feedback is in cycles and tops out at one, so a normalised depth
            // already spans the whole control.
            case ModDestination::feedbackA:
            case ModDestination::feedbackB:

            // Bipolar already, and the voice clamps the sum to -1..+1, so full
            // depth reaches either end of the axis from wherever the knob sits.
            case ModDestination::filterMorph:

            // A fraction the voice clamps to 0..1, like the group above: full
            // depth reaches either end of the control from wherever it is set.
            case ModDestination::filterSing:
            case ModDestination::count:
            default:                          v.slots[slot].depth = depth; break;
        }
    }

    p.keyboard = static_cast<KeyboardMode> (indexOf (state_, ids::keyMode));
    p.polyphony = indexOf (state_, ids::polyphony);
    p.glideSeconds = valueOf (state_, ids::glide);

    p.lfo1Wave = static_cast<dsp::Lfo::Wave> (indexOf (state_, ids::lfo1Wave));
    p.lfo1RateHz = valueOf (state_, ids::lfo1Rate);
    p.lfo1Sync = valueOf (state_, ids::lfo1Sync) > 0.5f;
    p.lfo1Division = indexOf (state_, ids::lfo1Div);
    p.lfo1Smooth = valueOf (state_, ids::lfo1Smooth);
    p.lfo1Retrigger = valueOf (state_, ids::lfo1Retrig) > 0.5f;
    p.lfo1KeyTrack = valueOf (state_, ids::lfo1Key);
    p.lfo1AttackSeconds = valueOf (state_, ids::lfo1Att);
    p.lfo2Wave = static_cast<dsp::Lfo::Wave> (indexOf (state_, ids::lfo2Wave));
    p.lfo2RateHz = valueOf (state_, ids::lfo2Rate);
    p.lfo2Sync = valueOf (state_, ids::lfo2Sync) > 0.5f;
    p.lfo2Division = indexOf (state_, ids::lfo2Div);
    p.lfo2Smooth = valueOf (state_, ids::lfo2Smooth);
    p.lfo2Retrigger = valueOf (state_, ids::lfo2Retrig) > 0.5f;
    p.lfo2KeyTrack = valueOf (state_, ids::lfo2Key);
    p.lfo2AttackSeconds = valueOf (state_, ids::lfo2Att);

    p.sequencerRateHz = valueOf (state_, ids::seqRate);
    p.sequencerLength = indexOf (state_, ids::seqLength);
    p.sequencerGlide = valueOf (state_, ids::seqGlide);
    p.sequencerToLfo1Rate = valueOf (state_, ids::seqToLfoRate);

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
        p.sequencerSteps[static_cast<std::size_t> (step)] = valueOf (state_, ids::step (step));

    for (int slot = 0; slot < EngineParameters::kGlobalSlots; ++slot)
    {
        auto& target = p.globalSlots[static_cast<std::size_t> (slot)];

        target.source = static_cast<GlobalSource> (indexOf (state_, ids::globalSource (slot)));
        target.destination = static_cast<GlobalDestination> (indexOf (state_, ids::globalDest (slot)));

        // Left in -1..+1 here, unlike the voice matrix: the engine knows what
        // each of its own eight destinations is measured in, and three of them
        // are already normalised.
        target.depth = valueOf (state_, ids::globalDepth (slot));
    }

    p.splitHz = valueOf (state_, ids::splitHz);
    p.subMono = valueOf (state_, ids::subMono) > 0.5f;
    p.subSplit = valueOf (state_, ids::subSplit) > 0.5f;

    p.order = static_cast<MangleOrder> (indexOf (state_, ids::order));
    p.tubeDriveDb = valueOf (state_, ids::tubeDrive);
    p.combMode = static_cast<CombMode> (indexOf (state_, ids::combMode));
    p.combTimeMs = valueOf (state_, ids::combTime);
    p.combKeyTrack = valueOf (state_, ids::combTrack);
    p.combFeedback = valueOf (state_, ids::combFeed);
    p.combDamping = valueOf (state_, ids::combDamp);
    p.combSpread = valueOf (state_, ids::combSpread);
    p.combMix = valueOf (state_, ids::combMix);
    p.combInverted = valueOf (state_, ids::combInvert) > 0.5f;
    p.combScaleLock = valueOf (state_, ids::combScale) > 0.5f;

    // The Shepard glissando, which is one control for the instrument because
    // its phase is one accumulator for the instrument.
    p.shepardRate = valueOf (state_, ids::shepardRate);
    p.shepardShear = valueOf (state_, ids::shepardShear);
    p.voice.shepardRetrigger = valueOf (state_, ids::shepardRetrig) > 0.5f;
    p.shepardSync = valueOf (state_, ids::shepardSync) > 0.5f;
    p.shepardDivision = indexOf (state_, ids::shepardDiv);

    p.formantTract = valueOf (state_, ids::tract);

    // Sag: the depth reaches the voices through `VoiceParameters`, the period
    // stays with the engine that owns the walk.
    p.voice.sagDepth = valueOf (state_, ids::sag);
    p.sagDepth = p.voice.sagDepth;
    p.sagPeriodSeconds = valueOf (state_, ids::sagRate);

    for (int macro = 0; macro < 4; ++macro)
        p.macros[static_cast<std::size_t> (macro)] = valueOf (state_, ids::macro (macro));
    p.phaseFrequencyHz = valueOf (state_, ids::phaseFreq);
    p.phaseStages = indexOf (state_, ids::phaseStages);

    p.formantMorph = valueOf (state_, ids::formantMorph);
    p.formantSharpness = valueOf (state_, ids::formantSharp);
    p.formantMix = valueOf (state_, ids::formantMix);
    p.formantHarmonic = valueOf (state_, ids::formantHarmonic);
    p.formantLock = valueOf (state_, ids::formantLock);
    p.formantNotchHz = valueOf (state_, ids::formantNotch);
    p.formantNotchDepth = valueOf (state_, ids::formantNotchDepth);
    p.tilt = valueOf (state_, ids::tilt);

    p.outputDb = valueOf (state_, ids::output);
    p.oversampling = static_cast<dsp::OversamplingMode> (indexOf (state_, ids::oversampling));
    p.renderOversampling
        = static_cast<dsp::RenderOversampling> (indexOf (state_, ids::renderOversampling));
}

// ---------------------------------------------------------------------------
// MIDI and rendering
// ---------------------------------------------------------------------------

void SonitusProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        engine_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
        return;
    }

    if (message.isNoteOff())
    {
        engine_.noteOff (message.getNoteNumber());
        return;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        engine_.allNotesOff();
        return;
    }

    if (message.isSustainPedalOn())  { engine_.setSustain (true);  return; }
    if (message.isSustainPedalOff()) { engine_.setSustain (false); return; }

    if (message.isPitchWheel())
    {
        const double range = valueOf (state_, ids::bendRange);
        const double normalised = (message.getPitchWheelValue() - 8192) / 8192.0;

        engine_.setBendSemitones (normalised * range);
    }
}

template <typename FloatType>
void SonitusProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer,
                                        juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    if (numSamples <= 0)
        return;

    // The offline flag first: the VST3 wrapper refreshes it per block, and the
    // engine reads it when it decides the factor at the top of `process`.
    engine_.setOffline (isNonRealtime());

    pullParameters();
    engine_.setParameters (parameters_);

    // Before any MIDI is handled, so a note-on in this block is played in the
    // scale the player just loaded rather than the one before it.
    collectTuning();

    if (auto* transport = getPlayHead())
    {
        if (const auto position = transport->getPosition())
        {
            const auto ppq = position->getPpqPosition();
            const auto bpm = position->getBpm();

            // The bar length as well as the tempo, so the envelope rulers draw
            // the player's bar rather than assuming four. A 7/8 session gets a
            // 7/8 ruler; the beat is still the denominator's note, which is
            // what "beats per bar" has to mean for a grid.
            int beatsPerBar = 4;

            if (const auto signature = position->getTimeSignature())
                if (signature->numerator > 0)
                    beatsPerBar = signature->numerator;

            engine_.setTransport (ppq.orFallback (-1.0), bpm.orFallback (120.0),
                                  position->getIsPlaying(), beatsPerBar);
        }
    }

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    channelPointers_[0] = scratch_.getWritePointer (0);
    channelPointers_[1] = scratch_.getWritePointer (1);

    // **Sample-accurate MIDI.** The render is cut at every event, so a note lands
    // where it was played rather than at the start of the next block. At 512
    // samples that is up to 11 ms of slop, which on a sixteenth-note bass line
    // at 174 bpm is a fifth of a step.
    int rendered = 0;

    for (const auto metadata : midi)
    {
        const int position = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (position > rendered)
        {
            engine_.process (channelPointers_.data(), position - rendered);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < position - rendered; ++i)
                    buffer.setSample (channel, rendered + i,
                                      static_cast<FloatType> (channelPointers_[static_cast<std::size_t> (channel)][i]));

            rendered = position;
        }

        handleMidi (metadata.getMessage());
    }

    if (rendered < numSamples)
    {
        const int remaining = numSamples - rendered;

        engine_.process (channelPointers_.data(), remaining);

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < remaining; ++i)
                buffer.setSample (channel, rendered + i,
                                  static_cast<FloatType> (channelPointers_[static_cast<std::size_t> (channel)][i]));
    }

    activeVoices_.store (engine_.activeVoiceCount());

    // Metered off the double scratch rather than the host buffer, so a float
    // host does not quietly change what the meter reads. Only the last chunk
    // of the block is in the scratch after a split render, so the meter is fed
    // from the buffer that actually holds the whole block -- which is the one
    // that just got written.
    float outputVu = -100.0f;
    float outputPeak = -100.0f;

    for (int channel = 0; channel < 2; ++channel)
    {
        auto* destination = scratch_.getWritePointer (channel);
        const auto* source = buffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = static_cast<double> (source[i]);

        outputMeter_[channel].processBlock (destination, numSamples);

        outputVu = std::max (outputVu, static_cast<float> (outputMeter_[channel].getVuDb()));
        outputPeak = std::max (outputPeak, static_cast<float> (outputMeter_[channel].getPeakDb()));
    }

    meters_.outputVuDb.store (outputVu, std::memory_order_relaxed);
    meters_.outputPeakDb.store (outputPeak, std::memory_order_relaxed);

    // Latency changes when the oversampling factor does, and a host that is not
    // told simply plays the instrument late. CLAUDE.md section 2.2.
    const int latency = engine_.getLatencySamples();

    if (latency != reportedLatency_)
    {
        reportedLatency_ = latency;
        setLatencySamples (latency);
    }
}

void SonitusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void SonitusProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

// ---------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------

void SonitusProcessor::publishTuning()
{
    const juce::SpinLock::ScopedLockType lock (tuningLock_);

    // Copied here, on the message thread, where allocating is allowed. The
    // audio thread only ever swaps.
    pendingScale_ = scale_;
    pendingMap_ = hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {};
    pendingConcertHz_ = concertPitchHz_;

    tuningPending_.store (true, std::memory_order_release);
}

void SonitusProcessor::collectTuning() noexcept
{
    if (! tuningPending_.load (std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock (tuningLock_);

    // Not taken: the message thread is mid-write. Leave the flag up and try
    // again next block rather than waiting for it.
    if (! lock.isLocked())
        return;

    engine_.tuning().swapScale (pendingScale_);
    engine_.tuning().swapKeyboardMap (pendingMap_);
    engine_.tuning().setConcertPitch (pendingConcertHz_);

    tuningPending_.store (false, std::memory_order_release);
}

juce::String SonitusProcessor::loadScalaText (const juce::String& text, const juce::String& name)
{
    dsp::Scale parsed;

    const auto result = dsp::parseScl (text.toStdString(), parsed);

    if (! result.ok)
        return "Line " + juce::String (result.line) + ": " + juce::String (result.message);

    scale_ = parsed;
    scalaText_ = text;
    scaleName_ = name.isNotEmpty() ? name : juce::String (parsed.name);

    publishTuning();

    return {};
}

juce::String SonitusProcessor::loadKeyboardMapText (const juce::String& text)
{
    dsp::KeyboardMap parsed;

    const auto result = dsp::parseKbm (text.toStdString(), parsed);

    if (! result.ok)
        return "Line " + juce::String (result.line) + ": " + juce::String (result.message);

    keyboardMap_ = parsed;
    keyboardMapText_ = text;
    hasKeyboardMap_ = true;

    publishTuning();

    return {};
}

juce::String SonitusProcessor::selectBuiltInScale (const juce::String& name)
{
    for (const auto& scale : dsp::scales::all())
        if (name == juce::String (scale.name))
        {
            scale_ = scale;
            scaleName_ = name;
            scalaText_.clear();

            publishTuning();

            return {};
        }

    return "No built-in scale called \"" + name + "\"";
}

void SonitusProcessor::resetTuning()
{
    // The scales:: builder rather than the Tuning class's bare default, so
    // even the reset scale arrives with its construction and story -- the
    // panel shows them, and a blank panel on the default was the bug.
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
    scalaText_.clear();
    keyboardMapText_.clear();
    hasKeyboardMap_ = false;

    publishTuning();
}

void SonitusProcessor::setConcertPitch (double hz)
{
    concertPitchHz_ = std::clamp (hz, dsp::Tuning::kMinimumConcertHz,
                                  dsp::Tuning::kMaximumConcertHz);
    publishTuning();
}

int SonitusProcessor::getRootNote() const
{
    // Without a map the tuning's degree 0 sits on middle C; a map moves it to
    // its own middle note.
    return hasKeyboardMap_ ? keyboardMap_.middleNote : 60;
}

double SonitusProcessor::getRootHz() const
{
    // A throwaway preview built the way collectTuning builds the live one,
    // asked one question on the message thread. The panel multiplies the
    // scale's ratios by this to fill its Hz column.
    dsp::Tuning preview;

    preview.setScale (scale_);

    if (hasKeyboardMap_)
        preview.setKeyboardMap (keyboardMap_);

    preview.setConcertPitch (concertPitchHz_);

    return preview.frequencyFor (getRootNote());
}

juce::String SonitusProcessor::getScaleName() const
{
    return scaleName_;
}

juce::String SonitusProcessor::describeTuning() const
{
    const double repeat = scale_.repeatCents();

    juce::String text = scaleName_ + " -- " + juce::String (scale_.size()) + " notes, repeating at ";

    if (std::abs (repeat - 1200.0) < 0.5)
        text += "the octave";
    else if (std::abs (repeat - 1901.955) < 0.5)
        text += "3/1, the tritave";
    else if (std::abs (repeat - 701.955) < 0.5)
        text += "3/2, a fifth -- this scale has no octave at all";
    else
        text += juce::String (repeat, 1) + " cents";

    if (hasKeyboardMap_)
        text += ", with a keyboard map";

    // The concrete anchor: where degree 0 actually sounds, at the current
    // pitch standard. This is the line that moves when the A4 control does.
    const double rootHz = getRootHz();

    if (rootHz > 0.0)
        text += " -- root "
                  + juce::MidiMessage::getMidiNoteName (getRootNote(), true, true, 4)
                  + " = " + juce::String (rootHz, 2) + " Hz at A4 = "
                  + juce::String (concertPitchHz_, 1) + " Hz";

    return text;
}

// ---------------------------------------------------------------------------
// What the panel reads
// ---------------------------------------------------------------------------

juce::String SonitusProcessor::describeOversampling() const
{
    const int mode = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::oversampling)->load()));

    const int factor = engine_.getOversamplingFactor();
    const double internal = sampleRate_ * factor;

    // While a bounce runs at a render setting, the factor the engine reports
    // is the render one, and attributing it to Auto would be a lie.
    const int render = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::renderOversampling)->load()));

    if (isNonRealtime() && render != 0)
        return "Rendering offline, so Render quality is in force instead: x" + juce::String (factor)
                 + " (" + rateText (internal) + " internally). This setting comes back when "
                   "playback does.";

    if (mode == 0)
    {
        juce::String text = "Auto -- your session is at " + rateText (sampleRate_) + ", so this is ";

        text += factor == 1 ? juce::String ("off: the headroom is already there.")
                            : "running x" + juce::String (factor) + ", giving "
                                + rateText (internal) + " internally.";

        if (factor > 1)
            text += " x4 clears -60 dB of aliasing from E1 to A3; above that, or with the "
                    "folder right up, x8 buys another 12 dB for twice the CPU.";

        return text;
    }

    return "x" + juce::String (factor) + " -- " + rateText (internal) + " internally, "
             + juce::String (factor) + " times the CPU of Off. Auto would pick x"
             + juce::String (dsp::autoOversamplingFactor (sampleRate_)) + " here.";
}

juce::String SonitusProcessor::describeRenderQuality() const
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

    if (liveFactor == factor)
        text += ", which is what Oversampling gives you live anyway, so this changes nothing.";
    else
        text += ", against x" + juce::String (liveFactor) + " live. Nothing while you play; the "
                "bounce costs about " + juce::String (static_cast<double> (factor) / liveFactor, 1)
              + " times the live CPU, paid in render time. A bounce at a higher factor than you "
                "auditioned is the point, and also why the default is Same as live.";

    if (offline)
        text += " Rendering offline right now, so this is what is running.";

    return text;
}

juce::String SonitusProcessor::describeLatency() const
{
    if (! prepared_)
        return "Latency: not measured yet.";

    if (reportedLatency_ == 0)
        return "Latency: none. Nothing here looks ahead.";

    const double milliseconds = 1000.0 * reportedLatency_ / sampleRate_;

    return "Latency: " + juce::String (reportedLatency_) + " samples ("
             + juce::String (milliseconds, 2) + " ms), all of it the oversampling filters. "
               "Declared to the host, so its delay compensation lines the track up.";
}

juce::String SonitusProcessor::describeComb() const
{
    const int mode = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::combMode)->load()));

    if (mode == 0)
        return "The comb is off. It is the section this instrument is built around -- "
               "pin the LFO rate at zero and drive the time from an envelope or the sequencer.";

    if (mode == 1)
    {
        const double track = state_.getRawParameterValue (ids::combTrack)->load();
        const bool inverted = state_.getRawParameterValue (ids::combInvert)->load() > 0.5f;

        // Read off the comb rather than off the knob. Key tracking and the
        // global matrix both move the delay, so a figure worked out from the
        // control would be wrong exactly when the control is interesting.
        const double first = engine_.readouts().combNotchHz.load (std::memory_order_relaxed);
        const double spacing = inverted ? first : 2.0 * first;

        juce::String text = "Flange -- notches every " + juce::String (spacing, 0)
                              + " Hz, the first at " + juce::String (first, 0) + " Hz.";

        if (track > 0.01)
            text += " Key tracking at " + juce::String (juce::roundToInt (track * 100.0))
                      + " %, so they are moving onto the note's own harmonics.";

        return text;
    }

    const int stages = static_cast<int> (std::lround (
        state_.getRawParameterValue (ids::phaseStages)->load()));

    return "Phase -- " + juce::String (stages / 2) + " notches, unevenly spaced and bunched "
             "around the centre. Smoother than the flange and it does not ring.";
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

namespace
{
/// One control set to one value.
struct Setting
{
    juce::String id;
    float value;
};

/// A preset is a **name and a list of departures from the defaults**, and every
/// preset is applied by resetting every parameter to its default first.
///
/// Anvil's presets each write all twenty of their parameters out longhand, and
/// with twenty that is the clearer thing to do. Here there are a hundred and
/// forty, and a longhand table would be a hundred and forty lines per preset in
/// which the four that matter are invisible. What must not change is the
/// *guarantee*: a preset is a complete parameter set, or switching from one to
/// another leaves the first one's settings behind on every control the second
/// does not mention. Resetting to the defaults first keeps that guarantee and
/// keeps the table readable -- the defaults are the Init preset, and they are
/// neutral by construction because CLAUDE.md section 8 requires every new
/// parameter to default to neutral.
struct Preset
{
    const char* name;
    std::vector<Setting> settings;
};

/// **Every preset's Output is a measured number, not a taste one.** A synth
/// preset that reaches full scale on *one* note has nothing left for a chord,
/// and this is a sixteen-voice instrument.
///
/// The rule arrived late and found four presets breaking it, which is why it is
/// written down here rather than assumed. Rendered through the JUCE layer at
/// note 45, five seconds, peak taken per channel:
///
/// | preset | was | peak | now | peak |
/// |---|---|---|---|---|
/// | Reese -- the classic  | -9 dB | 1.132 | -12 dB | 0.801 |
/// | Clockwork wobble      | -4 dB | 1.089 |  -7 dB | 0.771 |
/// | Morphing pluck        | -4 dB | 1.153 |  -7 dB | 0.817 |
/// | Scale drone           | -6 dB | 1.949 | -14 dB | 0.776 |
///
/// Scale drone is the one that matters: a comb at 88% feedback locked to the
/// tuning is a resonator being fed its own notes, so it *builds*, and 1.949 is
/// nearly +6 dB over. It shipped that way an hour earlier and no test could
/// have caught it, because a preset's level is not a claim any test makes.
///
/// **Init is deliberately not in the table** even though it reads 1.065. It is
/// the parameter defaults by definition, so trimming it would be changing the
/// plugin's neutral output gain for every new instance rather than editing a
/// preset -- a different decision, and one for the user rather than for a
/// preset pass. One saw at unity really is that loud.
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list
    {
        // -------------------------------------------------------------------
        {
            // Deliberately first, and deliberately empty: this *is* the
            // defaults, and it is the genuinely clean setting CLAUDE.md
            // section 7 asks every plugin for. One saw, the filter open, no
            // mangle, nothing in the path that is not a straight line.
            "Init -- one clean saw",
            {}
        },
        // -------------------------------------------------------------------
        {
            // The classic. Two saws a few cents apart is a comb whose notches
            // sweep at the beat frequency; the flanger is the same comb with a
            // handle on it, and the two together are what a reese is.
            "Reese -- the classic",
            {
                { ids::levelB, 1.0f },
                { ids::centsB, 9.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 14.0f }, { ids::spreadA, 0.55f },
                { ids::unisonB, 3.0f }, { ids::detuneB, 18.0f }, { ids::spreadB, 0.75f },
                { ids::driftA, 3.0f },  { ids::driftB, 3.0f },

                { ids::subLevel, 0.45f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 900.0f }, { ids::resonance, 0.28f }, { ids::filterTrack, 0.35f },

                { ids::ampAttack, 0.004f }, { ids::ampDecay, 0.35f },
                { ids::ampSustain, 0.9f }, { ids::ampRelease, 0.12f },

                { ids::keyMode, 1.0f },     // mono: a reese is one voice
                { ids::glide, 0.03f },

                { ids::combMode, 1.0f },    // flange
                { ids::combTime, 4.2f }, { ids::combFeed, -0.72f }, { ids::combMix, 1.0f },
                { ids::combInvert, 1.0f }, { ids::combSpread, 0.35f },

                // The brief's trick, wired up: LFO 2 at a quarter of a hertz
                // drawing the comb rather than a hand on an automation lane.
                { ids::lfo2Rate, 0.22f },
                { ids::globalSource (0), 2.0f },   // LFO 2
                { ids::globalDest (0), 1.0f },     // comb time
                { ids::globalDepth (0), 0.42f },

                { ids::tubeDrive, 7.0f },
                { ids::splitHz, 130.0f },
                { ids::output, -12.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Where the comb is *tuned*: key tracking locks the notches onto
            // harmonics of the played note, so the growl comes out in key
            // instead of churning against itself.
            "Growl -- tuned comb",
            {
                { ids::levelB, 0.85f }, { ids::semitonesB, -12.0f }, { ids::centsB, 6.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 11.0f }, { ids::spreadA, 0.4f },

                { ids::cutoff, 1400.0f }, { ids::resonance, 0.42f },
                { ids::filterDrive, 0.35f }, { ids::filterTrack, 0.6f },

                { ids::ampAttack, 0.003f }, { ids::ampSustain, 0.95f }, { ids::ampRelease, 0.1f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.02f },

                { ids::combMode, 1.0f },
                { ids::combTime, 2.6f }, { ids::combTrack, 1.0f },
                { ids::combFeed, 0.68f }, { ids::combMix, 1.0f }, { ids::combSpread, 0.2f },

                // The sequencer stepping the comb: sixteen steps of notch
                // position, locked to the host's tempo. This is the pattern the
                // brief used to draw by hand.
                { ids::seqRate, 8.0f }, { ids::seqGlide, 0.15f },
                { ids::step (0), 0.0f },   { ids::step (1), 0.55f },
                { ids::step (2), -0.3f },  { ids::step (3), 0.8f },
                { ids::step (4), 0.15f },  { ids::step (5), -0.6f },
                { ids::step (6), 0.4f },   { ids::step (7), 1.0f },
                { ids::step (8), -0.2f },  { ids::step (9), 0.35f },
                { ids::step (10), 0.9f },  { ids::step (11), -0.45f },
                { ids::step (12), 0.25f }, { ids::step (13), 0.7f },
                { ids::step (14), -0.15f },{ ids::step (15), 0.5f },

                { ids::globalSource (0), 3.0f },   // sequencer
                { ids::globalDest (0), 1.0f },     // comb time
                { ids::globalDepth (0), 0.55f },

                { ids::tubeDrive, 13.0f },
                { ids::formantMix, 0.3f }, { ids::formantMorph, 0.45f },
                { ids::splitHz, 140.0f },
                { ids::output, -10.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Hard sync, which is the Pro-53 trick: the slave restarts on the
            // master's period, so its own pitch becomes a formant rather than a
            // note. Sweeping it is the sound.
            "Sync scream",
            {
                { ids::levelA, 0.25f },
                { ids::levelB, 1.0f }, { ids::syncB, 1.0f }, { ids::semitonesB, 7.0f },
                { ids::unisonB, 2.0f }, { ids::detuneB, 7.0f }, { ids::spreadB, 0.5f },

                { ids::cutoff, 6000.0f }, { ids::resonance, 0.2f },

                { ids::ampAttack, 0.002f }, { ids::ampDecay, 0.6f },
                { ids::ampSustain, 0.7f }, { ids::ampRelease, 0.18f },

                { ids::env1Attack, 0.001f }, { ids::env1Decay, 0.55f },
                { ids::env1Sustain, 0.0f }, { ids::env1Release, 0.2f },

                // The envelope on the slave's pitch: the sweep that makes sync
                // a sound rather than a setting.
                { ids::modSource (0), 2.0f },   // mod env 1
                { ids::modDest (0), 14.0f },    // pitch B
                { ids::modDepth (0), 0.5f },

                { ids::keyMode, 1.0f },
                { ids::tubeDrive, 6.0f },
                { ids::output, -10.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Nothing above the split, everything below it. A sub that is one
            // sine and a little weight, mono by construction.
            "Sub -- clean weight",
            {
                { ids::shapeA, 3.0f },          // sine
                { ids::octaveA, -1.0f },
                { ids::subLevel, 0.5f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 400.0f }, { ids::filterTrack, 1.0f },

                { ids::ampAttack, 0.006f }, { ids::ampSustain, 1.0f }, { ids::ampRelease, 0.08f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.04f },

                { ids::splitHz, 200.0f },
                { ids::tubeDrive, 4.0f },
                { ids::output, -7.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The vowel filter is the same comb shaped like a mouth. Morphing
            // it under the sequencer is the talking bass.
            "Talkbox",
            {
                { ids::levelB, 0.7f }, { ids::centsB, -7.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 12.0f }, { ids::spreadA, 0.5f },

                { ids::cutoff, 3000.0f }, { ids::resonance, 0.15f },

                { ids::ampAttack, 0.005f }, { ids::ampSustain, 0.95f }, { ids::ampRelease, 0.12f },

                { ids::keyMode, 1.0f },

                { ids::formantMix, 1.0f }, { ids::formantSharp, 0.7f },

                { ids::seqRate, 4.0f }, { ids::seqGlide, 0.4f }, { ids::seqLength, 8.0f },
                { ids::step (0), -1.0f }, { ids::step (1), -0.2f },
                { ids::step (2), 0.5f },  { ids::step (3), 1.0f },
                { ids::step (4), 0.3f },  { ids::step (5), -0.6f },
                { ids::step (6), 0.8f },  { ids::step (7), 0.0f },

                { ids::globalSource (0), 3.0f },   // sequencer
                { ids::globalDest (0), 5.0f },     // vowel
                { ids::globalDepth (0), 0.5f },

                { ids::tubeDrive, 9.0f },
                { ids::output, -10.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The other comb. An allpass cascade's notches are not harmonically
            // spaced, so it smears rather than rings -- which is what you want
            // under a pad and never under a reese.
            "Phase wash",
            {
                { ids::levelB, 0.9f }, { ids::centsB, 11.0f }, { ids::octaveB, -1.0f },
                { ids::unisonA, 5.0f }, { ids::detuneA, 22.0f }, { ids::spreadA, 0.9f },
                { ids::driftA, 8.0f },

                { ids::cutoff, 2600.0f }, { ids::resonance, 0.12f },

                { ids::ampAttack, 0.35f }, { ids::ampSustain, 1.0f }, { ids::ampRelease, 0.9f },

                { ids::polyphony, 6.0f },

                { ids::combMode, 2.0f },    // phase
                { ids::phaseFreq, 700.0f }, { ids::phaseStages, 8.0f },
                { ids::combFeed, 0.55f }, { ids::combMix, 0.85f }, { ids::combSpread, 0.6f },

                { ids::lfo1Rate, 0.15f },
                { ids::globalSource (0), 1.0f },   // LFO 1
                { ids::globalDest (0), 4.0f },     // phase centre
                { ids::globalDepth (0), 0.5f },

                { ids::tilt, 0.25f },
                { ids::output, -12.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The destruction section on its own: ring modulation and the
            // folder, with the tube behind them. Inharmonic on purpose.
            "Metal fold",
            {
                { ids::levelB, 1.0f }, { ids::semitonesB, 7.0f }, { ids::centsB, 3.0f },
                { ids::ringAmount, 0.7f }, { ids::foldAmount, 0.55f },

                { ids::cutoff, 3500.0f }, { ids::resonance, 0.3f }, { ids::filterDrive, 0.4f },

                { ids::ampAttack, 0.001f }, { ids::ampDecay, 0.25f },
                { ids::ampSustain, 0.5f }, { ids::ampRelease, 0.15f },

                { ids::env1Attack, 0.001f }, { ids::env1Decay, 0.18f }, { ids::env1Sustain, 0.0f },
                { ids::modSource (0), 2.0f },   // mod env 1
                { ids::modDest (0), 12.0f },    // fold
                { ids::modDepth (0), 0.45f },

                { ids::keyMode, 1.0f },

                // The folder is the widest-band thing in here, so it is the one
                // setting that genuinely wants x8.
                { ids::oversampling, 4.0f },

                { ids::tubeDrive, 11.0f },
                { ids::splitHz, 110.0f },
                { ids::output, -12.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // What the tuning page is for, as a preset: the comb key-tracked
            // onto a note whose scale is harmonic, so the intervals beat at the
            // rate the comb is combing at. Load a scale on the TUNING page --
            // the preset cannot, because a scale is not a parameter.
            "Just growl -- for a pure scale",
            {
                { ids::levelB, 0.9f }, { ids::centsB, 4.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 6.0f }, { ids::spreadA, 0.45f },

                { ids::cutoff, 1100.0f }, { ids::resonance, 0.35f }, { ids::filterTrack, 0.8f },

                { ids::ampAttack, 0.004f }, { ids::ampSustain, 0.95f }, { ids::ampRelease, 0.14f },

                { ids::polyphony, 4.0f },

                { ids::combMode, 1.0f },
                { ids::combTime, 3.0f }, { ids::combTrack, 1.0f },
                { ids::combFeed, 0.6f }, { ids::combMix, 0.9f },

                { ids::tubeDrive, 8.0f },
                { ids::output, -12.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The hold stage doing what it is for: full level for a moment,
            // then gone. A gated stab rather than a plucked one -- the release
            // starts from the top every time, which is what setting the sustain
            // to 1 and playing short notes cannot give you.
            "Gate stab -- the hold, doing its job",
            {
                { ids::levelA, 1.0f }, { ids::levelB, 0.8f }, { ids::semitonesB, -12.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 12.0f }, { ids::spreadA, 0.5f },

                { ids::cutoff, 3200.0f }, { ids::resonance, 0.4f },
                { ids::filterDrive, 0.35f },

                { ids::ampAttack, 0.001f }, { ids::ampHold, 0.09f },
                { ids::ampDecay, 0.012f }, { ids::ampSustain, 0.0f },
                { ids::ampRelease, 0.03f },
                { ids::ampAttackT, 0.0f }, { ids::ampDecayT, 0.9f },

                { ids::keyMode, 1.0f },
                { ids::tubeDrive, 10.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A pad, which this instrument was not built for and turns out to
            // be good at: twenty-second attack and release, negative tension on
            // the attack so it starts slowly and arrives rather than jumping,
            // and an LFO fading in behind it.
            "Slow bloom -- a pad, the long way up",
            {
                { ids::levelA, 0.9f }, { ids::levelB, 0.9f }, { ids::centsB, 7.0f },
                { ids::unisonA, 4.0f }, { ids::detuneA, 14.0f }, { ids::spreadA, 0.7f },
                { ids::unisonB, 4.0f }, { ids::detuneB, 11.0f }, { ids::spreadB, 0.7f },

                { ids::cutoff, 1400.0f }, { ids::resonance, 0.15f },
                { ids::filterTrack, 0.6f },

                // Negative tension: slow at first, accelerating. The half of
                // the control the old Shape knob could not reach.
                { ids::ampAttack, 4.0f }, { ids::ampAttackT, -0.7f },
                { ids::ampDecay, 3.0f }, { ids::ampSustain, 0.85f },
                { ids::ampRelease, 6.0f }, { ids::ampReleaseT, 0.5f },

                { ids::polyphony, 6.0f },

                // The vibrato creeping in after the note has settled.
                { ids::lfo2Rate, 4.2f }, { ids::lfo2Att, 2.5f }, { ids::lfo2Retrig, 1.0f },
                { ids::modSource (0), 8.0f },   // LFO 2
                { ids::modDest (0), 13.0f },    // pitch
                { ids::modDepth (0), 0.06f },

                { ids::formantMix, 0.35f }, { ids::formantMorph, 0.6f },
                { ids::splitHz, 60.0f },
                { ids::output, -3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The LFO at the top of its new range, where it stops being a
            // wobble and becomes modulation: 140 Hz on the cutoff makes
            // sidebands rather than movement, and key tracking keeps the
            // relationship constant up the keyboard.
            "Sideband growl -- the LFO as an oscillator",
            {
                { ids::levelA, 1.0f }, { ids::levelB, 0.6f }, { ids::semitonesB, 7.0f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 9.0f },

                { ids::cutoff, 700.0f }, { ids::resonance, 0.55f },
                { ids::filterDrive, 0.5f }, { ids::filterTrack, 0.4f },

                { ids::lfo1Rate, 140.0f }, { ids::lfo1Key, 1.0f },
                { ids::lfo1Retrig, 1.0f }, { ids::lfo1Att, 0.15f },

                { ids::modSource (0), 7.0f },   // LFO 1
                { ids::modDest (0), 1.0f },     // cutoff
                { ids::modDepth (0), 0.35f },

                { ids::ampAttack, 0.004f }, { ids::ampSustain, 0.9f },
                { ids::ampRelease, 0.12f },

                { ids::keyMode, 1.0f },
                { ids::tubeDrive, 12.0f },
                { ids::oversampling, 3.0f },    // x4
                { ids::output, -4.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Kargyraa, close to what a singer does with it: a drone in the
            // bass, the period doubled, and the vowel filter over the top --
            // which is the whole trick, because the throat's subharmonic and
            // its formants are two independent things happening at once.
            //
            // Mono and legato, because it is one voice: a phrase played without
            // gaps runs through a single envelope and the growl carries across
            // the notes rather than restarting on each.
            "Kargyraa -- the doubled voice",
            {
                { ids::levelA, 1.0f }, { ids::levelB, 0.35f }, { ids::centsB, 6.0f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 5.0f }, { ids::spreadA, 0.3f },

                { ids::kargyraa, 0.85f }, { ids::kargyraaRasp, 0.55f },

                { ids::cutoff, 2400.0f }, { ids::resonance, 0.25f },
                { ids::filterTrack, 0.5f },

                { ids::ampAttack, 0.02f }, { ids::ampDecay, 0.4f },
                { ids::ampSustain, 0.9f }, { ids::ampRelease, 0.3f },

                { ids::keyMode, 2.0f },   // legato
                { ids::glide, 0.06f },

                // The vowel over the top, and an envelope walking it: what a
                // singer's tongue does while the folds hold the drone.
                { ids::formantMix, 0.75f }, { ids::formantMorph, 0.35f },
                { ids::formantSharp, 0.6f },

                { ids::env1Attack, 0.4f }, { ids::env1Decay, 1.2f },
                { ids::env1Sustain, 0.6f }, { ids::env1Release, 0.5f },

                { ids::globalSource (0), 5.0f },   // mod env 1
                { ids::globalDest (0), 5.0f },     // vowel
                { ids::globalDepth (0), 0.45f },

                { ids::splitHz, 90.0f },
                { ids::tubeDrive, 6.0f },
                { ids::output, -9.0f },
            }
        },
        // -------------------------------------------------------------------
        // The phase-3 set: each one exists to show one new mechanism doing the
        // thing it was built for. Same rule as the rest -- a preset is a
        // complete parameter set over the defaults, and every one is a sound,
        // not a demo.
        // -------------------------------------------------------------------
        {
            // The ADV envelope as a wobble engine: a looping two-leg envelope,
            // snapped to the grid, closing and opening the filter in exact
            // 1/8s. This is the dubstep wobble with no LFO anywhere -- and
            // because the legs have independent times and tensions, the down
            // is a snap and the up is a swell, which no symmetric LFO does.
            "Clockwork wobble -- ADV loop, snapped",
            {
                { ids::levelB, 1.0f }, { ids::centsB, 8.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 12.0f }, { ids::spreadA, 0.5f },
                { ids::unisonB, 3.0f }, { ids::detuneB, 16.0f }, { ids::spreadB, 0.6f },

                { ids::subLevel, 0.5f }, { ids::subOctave, -1.0f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.02f },

                { ids::cutoff, 2600.0f }, { ids::resonance, 0.42f }, { ids::filterTrack, 0.3f },

                { ids::ampSustain, 1.0f }, { ids::ampRelease, 0.1f },

                // ADV 1: down fast-with-a-snap, up slow-with-a-swell, looping
                // between point 1 and the sustain at point 2, every time on
                // the grid. Tempo changes retune the wobble live.
                { "adv1Enable", 1.0f }, { "adv1Loop", 1.0f }, { "adv1Snap", 1.0f },
                { "adv1Points", 2.0f }, { "adv1Sustain", 2.0f }, { "adv1LoopStart", 1.0f },
                { "adv1T1", 0.11f }, { "adv1L1", 1.0f }, { "adv1C1", 0.6f },
                { "adv1T2", 0.24f }, { "adv1L2", 0.0f }, { "adv1C2", -0.5f },

                // Slot 1: ADV 1 closes the filter by four octaves.
                { ids::modSource (0), 10.0f },   // ADV 1
                { ids::modDest (0), 1.0f },      // cutoff
                { ids::modDepth (0), -0.63f },   // ~4 octaves on the square law

                { ids::tubeDrive, 7.0f },
                { ids::output, -7.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The one-oscillator flanger: a double saw whose second ramp is
            // swept by a bar-locked LFO on Morph. No comb, no delay line --
            // the cancellation is inside the waveform, and because the LFO is
            // synced with retrigger off, the same bar of the song gets the
            // same sweep on every pass. Print it and it stays printed.
            "Twin ramp -- the flanger inside the wave",
            {
                { ids::shapeA, 6.0f },           // double saw
                { ids::unisonA, 3.0f }, { ids::detuneA, 10.0f }, { ids::spreadA, 0.6f },

                { ids::subLevel, 0.45f }, { ids::subOctave, -1.0f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.025f },

                { ids::cutoff, 3200.0f }, { ids::resonance, 0.2f }, { ids::filterTrack, 0.4f },

                { ids::ampSustain, 1.0f },

                { ids::lfo1Sync, 1.0f }, { ids::lfo1Div, 3.0f },   // one bar
                { ids::lfo1Wave, 1.0f },                            // triangle

                { ids::modSource (0), 7.0f },    // LFO 1
                { ids::modDest (0), 17.0f },     // Morph A
                { ids::modDepth (0), 0.9f },

                { ids::tubeDrive, 6.0f },
                { ids::output, -3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The Dome doing the thing only it can do: a bass that grows
            // harmonics with **zero aliasing at any drive**, because the shape
            // never has a partial above k times the note. ADV 1 breathes the
            // pressing slowly; velocity adds bite per note.
            "Dome bloom -- harmonics from a pure tone",
            {
                { ids::shapeA, 5.0f },           // dome
                { ids::unisonA, 2.0f }, { ids::detuneA, 6.0f }, { ids::spreadA, 0.35f },

                { ids::subLevel, 0.35f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 9000.0f },        // the shape is the filter here

                { ids::ampAttack, 0.01f }, { ids::ampSustain, 0.9f }, { ids::ampRelease, 1.0f },

                // A slow four-point breathing loop on the pressing.
                { "adv1Enable", 1.0f }, { "adv1Loop", 1.0f },
                { "adv1Points", 3.0f }, { "adv1Sustain", 3.0f }, { "adv1LoopStart", 1.0f },
                { "adv1T1", 0.8f },  { "adv1L1", 0.85f }, { "adv1C1", 0.3f },
                { "adv1T2", 1.4f },  { "adv1L2", 0.15f }, { "adv1C2", -0.3f },
                { "adv1T3", 1.2f },  { "adv1L3", 0.7f },  { "adv1C3", 0.0f },

                { ids::modSource (0), 10.0f },   // ADV 1
                { ids::modDest (0), 17.0f },     // Morph A
                { ids::modDepth (0), 0.85f },

                { ids::modSource (1), 4.0f },    // velocity
                { ids::modDest (1), 17.0f },     // Morph A
                { ids::modDepth (1), 0.4f },

                { ids::tubeDrive, 9.0f },        // drive it: still nothing folds
                { ids::output, -3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Noise into a key-tracked comb: broadband in, *pitch* out. The
            // comb's feedback rings the noise at harmonics of the played note,
            // which is how a filter-swept hiss becomes a playable metallic
            // growl. The classic jungle riser lives at the top of this patch's
            // mod wheel... or its LFO, here, fading in per note.
            "Steam pipe -- noise made pitched",
            {
                { ids::shapeA, 8.0f },           // noise
                { ids::morphA, 0.25f },          // just off white
                { ids::unisonA, 5.0f }, { ids::spreadA, 1.0f },   // five streams, wide

                { ids::subLevel, 0.55f }, { ids::subOctave, 0.0f },  // sine at pitch: the tone

                { ids::cutoff, 7000.0f }, { ids::resonance, 0.15f },

                { ids::ampAttack, 0.02f }, { ids::ampSustain, 1.0f }, { ids::ampRelease, 0.3f },

                { ids::combMode, 1.0f },         // flange
                { ids::combTrack, 1.0f },        // locked to the note
                { ids::combFeed, 0.88f },        // ring it hard
                { ids::combMix, 1.0f }, { ids::combDamp, 0.35f }, { ids::combSpread, 0.3f },

                { ids::lfo1Att, 0.6f }, { ids::lfo1Retrig, 1.0f },
                { ids::lfo1Rate, 0.9f },

                { ids::modSource (0), 7.0f },    // LFO 1, fading in
                { ids::modDest (0), 17.0f },     // Morph A: the colour breathes
                { ids::modDepth (0), 0.5f },

                { ids::output, -4.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Two-operator FM, the DX way: B is a silent modulator a twelfth
            // up (a 3:1 ratio -- FM lives on integer ratios, and Semis is
            // snapped so the ratio holds), A is a sine carrier, and the PM
            // index rides a fast-decay envelope. The brightness *is* the
            // envelope, which is the whole FM bass trick.
            "FM punch -- the index is the envelope",
            {
                { ids::shapeA, 3.0f },           // sine carrier... but see below:
                // PM in this instrument runs A -> B, so the *carrier* is B and
                // the modulator is A. A is set silent and a twelfth up; B
                // carries at the note.
                { ids::levelA, 0.0f },
                { ids::semitonesA, 19.0f },      // 3:1 against B
                { ids::shapeB, 3.0f },           // sine
                { ids::levelB, 1.0f },
                { ids::pmIndex, 0.0f },          // the envelope brings it

                { ids::subLevel, 0.4f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 12000.0f },       // FM makes its own brightness

                { ids::ampAttack, 0.002f }, { ids::ampDecay, 0.4f },
                { ids::ampSustain, 0.55f }, { ids::ampRelease, 0.12f },

                { ids::env1Attack, 0.001f }, { ids::env1Decay, 0.16f },
                { ids::env1Sustain, 0.12f }, { ids::env1Release, 0.1f },
                { ids::env1DecayT, 0.7f },       // the DX snap

                { ids::modSource (0), 2.0f },    // mod env 1
                { ids::modDest (0), 4.0f },      // PM index
                { ids::modDepth (0), 0.55f },

                { ids::modSource (1), 4.0f },    // velocity opens it further
                { ids::modDest (1), 4.0f },
                { ids::modDepth (1), 0.25f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.015f },
                { ids::output, -3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The same two operators pushed inharmonic: the modulator a major
            // seventh plus two octaves up, which lands the sidebands between
            // the harmonics -- bell territory -- with an ADV envelope giving
            // the index a strike, a shimmer-back, and a slow fade that no
            // ADSR draws. Poly, long release: chords ring like a gamelan.
            "Bell foundry -- inharmonic FM, ADV-shaped",
            {
                { ids::levelA, 0.0f },
                { ids::octaveA, 2.0f }, { ids::semitonesA, 11.0f },  // 4.756:1 -- inharmonic
                { ids::shapeB, 3.0f }, { ids::levelB, 1.0f },

                { ids::cutoff, 14000.0f },

                // The ring must outlast the ADV's 3.5 s index fade, or the amp
                // curtails the shimmer the envelope was drawn for.
                { ids::ampAttack, 0.002f }, { ids::ampDecay, 8.0f },
                { ids::ampSustain, 0.0f }, { ids::ampRelease, 6.0f },
                { ids::ampDecayT, 0.5f },

                // The index's life: strike hard, duck, shimmer back up a
                // little, then fade over seconds -- four points, no loop.
                { "adv1Enable", 1.0f },
                { "adv1Points", 4.0f }, { "adv1Sustain", 4.0f },
                { "adv1T1", 0.004f }, { "adv1L1", 1.0f },  { "adv1C1", 0.0f },
                { "adv1T2", 0.35f },  { "adv1L2", 0.25f }, { "adv1C2", 0.6f },
                { "adv1T3", 0.8f },   { "adv1L3", 0.45f }, { "adv1C3", -0.4f },
                { "adv1T4", 3.5f },   { "adv1L4", 0.0f },  { "adv1C4", 0.4f },

                { ids::modSource (0), 10.0f },   // ADV 1
                { ids::modDest (0), 4.0f },      // PM index
                { ids::modDepth (0), 0.5f },

                { ids::polyphony, 12.0f },
                { ids::output, -6.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The Vintage saw doing pad work: the RC curve softens the top the
            // way the old polys did, drift keeps the stack breathing, and both
            // envelopes are snapped so the swell always lands on the grid.
            // Slow bar-locked movement on the vowel from a synced LFO 2.
            "Vintage swell -- the analogue curve, on the grid",
            {
                { ids::shapeA, 4.0f },           // vintage
                { ids::morphA, 0.45f },
                { ids::shapeB, 4.0f }, { ids::morphB, 0.45f },
                { ids::levelB, 0.8f }, { ids::centsB, 7.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 9.0f }, { ids::spreadA, 0.7f },
                { ids::unisonB, 3.0f }, { ids::detuneB, 11.0f }, { ids::spreadB, 0.8f },
                { ids::driftA, 6.0f }, { ids::driftB, 6.0f },

                { ids::cutoff, 4200.0f }, { ids::filterTrack, 0.3f },

                { ids::ampAttack, 0.5f }, { ids::ampSustain, 0.85f }, { ids::ampRelease, 2.0f },
                { ids::ampSnap, 1.0f },
                { ids::env1Snap, 1.0f },

                { ids::formantMix, 0.35f }, { ids::formantSharp, 0.3f },

                { ids::lfo2Sync, 1.0f }, { ids::lfo2Div, 2.0f },   // two bars
                { ids::globalSource (0), 2.0f },   // LFO 2, bar-locked
                { ids::globalDest (0), 5.0f },     // Vowel
                { ids::globalDepth (0), 0.4f },

                { ids::output, -6.0f },

                { ids::polyphony, 12.0f },
            }
        },

        // ===================================================================
        // Phase 4: the feedback presets
        // ===================================================================

        // -------------------------------------------------------------------
        {
            // The reese, made with teeth instead of with more oscillators.
            //
            // The classic reese gets its bite from unison detune -- a comb
            // whose notches beat. This one keeps a modest stack and gets the
            // bite from **operator feedback** instead: each saw modulates its
            // own phase, which sharpens it without adding voices. The result
            // is the same aggression at a third of the CPU, and it stays
            // mono-compatible because the extra harmonics are generated per
            // voice rather than by cancellation between voices.
            //
            // Env 1 opens the feedback as the note sustains, so the bite
            // arrives after the transient rather than fighting it.
            "FM reese -- teeth without unison",
            {
                { ids::levelB, 1.0f }, { ids::centsB, 7.0f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 11.0f }, { ids::spreadA, 0.5f },
                { ids::unisonB, 2.0f }, { ids::detuneB, 13.0f }, { ids::spreadB, 0.7f },
                { ids::driftA, 3.0f }, { ids::driftB, 3.0f },

                { ids::feedbackA, 0.30f }, { ids::feedbackB, 0.22f },

                { ids::subLevel, 0.5f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 1400.0f }, { ids::resonance, 0.25f },
                { ids::filterTrack, 0.35f },

                { ids::ampAttack, 0.004f }, { ids::ampSustain, 0.95f },
                { ids::ampRelease, 0.11f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.025f },

                // Env 1 brings the feedback in over the first half second.
                { ids::env1Attack, 0.45f }, { ids::env1Sustain, 1.0f },
                { ids::env1Release, 0.3f },
                { ids::modSource (0), 2.0f },     // mod envelope 1
                { ids::modDest (0), 19.0f },      // feedback A
                { ids::modDepth (0), 0.55f },

                { ids::tubeDrive, 4.0f },
                { ids::output, -5.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Two sines and nothing else -- the whole sound is the modulation.
            //
            // This is the DX-style operator bass: a sine carrier, a sine
            // modulator an octave up, and the index under the envelope so the
            // attack is bright and the sustain is nearly pure. Feedback on the
            // modulator adds the extra series that a two-operator pair cannot
            // reach on its own.
            //
            // No filter movement, no unison, no comb. It is here as the
            // counter-argument to every other preset in the list: the cleanest
            // way to get a hard bass is often not to mangle a saw.
            "Operator bass -- two sines, all index",
            {
                { ids::shapeA, 3.0f },            // sine
                { ids::shapeB, 3.0f },            // sine
                { ids::octaveB, 1.0f },           // the modulator, an octave up
                { ids::levelB, 0.0f },            // heard only through the PM

                { ids::pmIndex, 3.2f },
                { ids::feedbackB, 0.42f },

                { ids::subLevel, 0.4f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 8000.0f }, { ids::filterTrack, 0.2f },

                { ids::ampAttack, 0.002f }, { ids::ampDecay, 0.18f },
                { ids::ampSustain, 0.75f }, { ids::ampRelease, 0.09f },

                // The index is the envelope: bright on the hit, pure after.
                { ids::env1Attack, 0.001f }, { ids::env1Decay, 0.22f },
                { ids::env1Sustain, 0.0f },
                { ids::modSource (0), 2.0f },
                { ids::modDest (0), 4.0f },       // PM index
                { ids::modDepth (0), 0.62f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.01f },
                { ids::output, -3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **The loop, held on a leash.** A modulates B and B modulates A,
            // which is the pair the forward path only half-made. Both depths
            // are moderate on purpose: this is the setting where the loop is
            // a growl rather than a texture, and turning either one up is how
            // you find the edge.
            //
            // The reverse path is a sample late, so the two directions do not
            // sound alike at the same depth -- worth hearing by soloing each.
            "Cross-bite -- the loop, on a leash",
            {
                { ids::shapeA, 0.0f },            // saw
                { ids::shapeB, 3.0f },            // sine
                { ids::levelB, 0.55f },
                { ids::semitonesB, 7.0f },        // a fifth: harmonic, not beating

                { ids::pmIndex, 2.4f },
                { ids::pmReverse, 1.8f },
                { ids::feedbackA, 0.18f },

                { ids::subLevel, 0.55f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 1900.0f }, { ids::resonance, 0.38f },
                { ids::filterDrive, 3.0f }, { ids::filterTrack, 0.3f },

                { ids::ampAttack, 0.003f }, { ids::ampSustain, 0.9f },
                { ids::ampRelease, 0.13f },

                // LFO 1 walks the reverse depth, so the growl moves.
                { ids::lfo1Rate, 5.5f }, { ids::lfo1Wave, 0.0f },
                { ids::modSource (0), 7.0f },     // LFO 1
                { ids::modDest (0), 21.0f },      // PM reverse
                { ids::modDepth (0), 0.35f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.02f },
                { ids::tubeDrive, 6.0f },
                { ids::output, -2.0f },
            }
        },

        // ===================================================================
        // The ones with no restraint. Every safety in the instrument is doing
        // real work in these -- the feedback bound, the fold's ADAA, the
        // comb's cap, the safety limiter -- and that is the point of shipping
        // them: a preset that leans on every guard at once is the honest
        // stress test, and it is where this instrument is supposed to live.
        // ===================================================================

        // -------------------------------------------------------------------
        {
            // Everything at once, and it holds together because each stage is
            // bounded on its own: both operators at full feedback, the loop
            // closed hard in both directions, the folder wide, ring
            // modulation, kargyraa period-doubling and a resonant comb after
            // it. There is no clean signal anywhere in this patch.
            //
            // Mono and glided, because a chord of this would be a wall.
            "SCREAMFACE -- every guard at once",
            {
                { ids::shapeA, 0.0f }, { ids::shapeB, 6.0f },   // saw + double saw
                { ids::morphB, 0.4f },
                { ids::levelB, 0.9f }, { ids::centsB, 13.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 19.0f }, { ids::spreadA, 0.8f },
                { ids::unisonB, 3.0f }, { ids::detuneB, 23.0f }, { ids::spreadB, 0.9f },

                { ids::feedbackA, 0.85f }, { ids::feedbackB, 0.72f },
                { ids::pmIndex, 5.5f }, { ids::pmReverse, 4.0f },

                { ids::ringAmount, 0.35f },
                { ids::foldAmount, 0.55f },
                { ids::kargyraa, 0.4f }, { ids::kargyraaRasp, 0.5f },

                { ids::subLevel, 0.6f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 2800.0f }, { ids::resonance, 0.55f },
                { ids::filterDrive, 6.0f }, { ids::filterTrack, 0.45f },

                { ids::ampAttack, 0.002f }, { ids::ampSustain, 0.92f },
                { ids::ampRelease, 0.14f },

                { ids::combMode, 1.0f }, { ids::combTime, 3.1f },
                { ids::combFeed, -0.68f }, { ids::combMix, 0.7f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.03f },
                { ids::tubeDrive, 9.0f },
                { ids::output, -3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A sound that comes apart while you hold the note.
            //
            // The trick is that the *sequencer* drives the reverse PM depth,
            // so the loop's character is re-decided sixteen times a bar --
            // and a loop through two nonlinearities does not respond linearly
            // to its own depth, so each step lands somewhere unrelated to the
            // one before it. ADV 1, looping and snapped to the grid, opens
            // the filter underneath.
            //
            // Deterministic, not random: the same bar gives the same collapse
            // every pass, so it prints.
            "Neural collapse -- the loop, re-decided per step",
            {
                { ids::shapeA, 7.0f }, { ids::morphA, 0.3f },   // harmonic
                { ids::shapeB, 3.0f },                          // sine
                { ids::levelB, 0.4f }, { ids::octaveB, 1.0f },

                { ids::pmIndex, 4.0f },
                { ids::feedbackA, 0.5f },

                { ids::seqRate, 8.0f }, { ids::seqLength, 16.0f },
                { ids::modSource (0), 9.0f },     // sequencer
                { ids::modDest (0), 21.0f },      // PM reverse
                { ids::modDepth (0), 0.8f },

                { ids::modSource (1), 9.0f },
                { ids::modDest (1), 20.0f },      // feedback B
                { ids::modDepth (1), 0.5f },

                { "adv1Enable", 1.0f }, { "adv1Loop", 1.0f }, { "adv1Snap", 1.0f },
                { "adv1Points", 3.0f }, { "adv1Sustain", 3.0f }, { "adv1LoopStart", 1.0f },
                { "adv1T1", 0.08f }, { "adv1L1", 1.0f }, { "adv1C1", 0.7f },
                { "adv1T2", 0.19f }, { "adv1L2", 0.15f }, { "adv1C2", -0.6f },
                { "adv1T3", 0.13f }, { "adv1L3", 0.6f }, { "adv1C3", 0.3f },

                { ids::modSource (2), 10.0f },    // ADV 1
                { ids::modDest (2), 1.0f },       // cutoff
                { ids::modDepth (2), -0.5f },

                { ids::subLevel, 0.45f }, { ids::subOctave, -1.0f },
                { ids::cutoff, 3200.0f }, { ids::resonance, 0.48f },

                { ids::ampAttack, 0.003f }, { ids::ampSustain, 0.95f },
                { ids::ampRelease, 0.12f },

                { ids::keyMode, 1.0f },
                { ids::foldAmount, 0.3f },
                { ids::tubeDrive, 7.0f },
                { ids::output, -9.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A throat rather than an instrument: the formant filter locked to
            // the note's own harmonics, kargyraa doubling the period the way
            // an actual kargyraa singer's false folds do, and both operators
            // feeding back hard enough to give the vowel something to filter.
            //
            // The vowel walks on a synced LFO, so it says something different
            // every bar. Hold a low note and it screams; hold a high one and
            // it whistles.
            "Tearout larynx -- a throat that screams",
            {
                { ids::shapeA, 0.0f }, { ids::shapeB, 0.0f },
                { ids::levelB, 0.75f }, { ids::centsB, 5.0f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 8.0f },

                { ids::feedbackA, 0.62f }, { ids::feedbackB, 0.44f },
                { ids::pmReverse, 2.2f },

                { ids::kargyraa, 0.7f }, { ids::kargyraaRasp, 0.65f },
                { ids::kargyraaDivisor, 0.0f },   // /2, the true kargyraa

                { ids::formantMix, 0.7f }, { ids::formantSharp, 0.6f },
                { ids::formantLock, 1.0f }, { ids::formantHarmonic, 4.0f },
                { ids::formantNotch, 1.0f }, { ids::formantNotchDepth, 0.4f },

                { ids::subLevel, 0.25f }, { ids::subOctave, -1.0f },
                { ids::cutoff, 7500.0f }, { ids::filterTrack, 0.5f },

                { ids::ampAttack, 0.01f }, { ids::ampSustain, 0.95f },
                { ids::ampRelease, 0.2f },

                { ids::lfo2Sync, 1.0f }, { ids::lfo2Div, 3.0f },
                { ids::globalSource (0), 2.0f },  // LFO 2
                { ids::globalDest (0), 5.0f },    // vowel
                { ids::globalDepth (0), 0.7f },

                { ids::keyMode, 1.0f }, { ids::glide, 0.04f },
                { ids::tubeDrive, 8.0f },
                { ids::output, 3.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Noise, given a pitch by a comb and then given teeth by a
            // feedback operator ring-modulating it. There is no oscillator
            // playing the note at all -- the pitch you hear is the comb's
            // resonance tracking the key, and the note number is choosing a
            // delay length rather than a frequency.
            //
            // The nastiest texture the instrument makes, and the one most
            // likely to need Capstone after it.
            "Gravel storm -- noise with a pitch bolted on",
            {
                { ids::shapeA, 8.0f }, { ids::morphA, 0.25f },   // noise
                { ids::shapeB, 3.0f },                            // sine
                { ids::levelB, 0.0f },

                { ids::feedbackB, 0.9f },
                { ids::ringAmount, 0.8f },

                { ids::combMode, 1.0f },
                { ids::combTrack, 1.0f },          // the comb *is* the pitch
                { ids::combFeed, 0.88f }, { ids::combDamp, 0.25f },
                { ids::combMix, 1.0f }, { ids::combSpread, 0.6f },

                { ids::subLevel, 0.35f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 6000.0f }, { ids::resonance, 0.2f },

                { ids::ampAttack, 0.006f }, { ids::ampSustain, 0.9f },
                { ids::ampRelease, 0.25f },

                { ids::env1Attack, 0.002f }, { ids::env1Decay, 0.4f },
                { ids::env1Sustain, 0.2f },
                { ids::modSource (0), 2.0f },
                { ids::modDest (0), 1.0f },        // cutoff
                { ids::modDepth (0), 0.45f },

                { ids::polyphony, 4.0f },
                { ids::tubeDrive, 5.0f },
                { ids::output, -4.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **The macro patch.** One knob, MACRO 1, wired to five things at
            // once: it opens the filter, slides the filter's *type* from
            // lowpass towards bandpass, adds operator feedback, widens the
            // detune, and lengthens the comb. That is the shape a matrix has no
            // row for -- five rows here, one control on the panel.
            //
            // Start it at zero and it is a dull, close reese. Take it to full
            // and it is a screaming one. Nothing else needs to move.
            "One knob reese -- MACRO 1 does all of it",
            {
                { ids::shapeA, 1.0f }, { ids::shapeB, 1.0f },
                { ids::levelB, 1.0f }, { ids::centsB, -9.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 6.0f }, { ids::spreadA, 0.6f },
                { ids::unisonB, 3.0f }, { ids::detuneB, 6.0f }, { ids::spreadB, 0.6f },

                { ids::cutoff, 320.0f }, { ids::resonance, 0.35f },
                { ids::filterDrive, 0.3f }, { ids::filterTrack, 0.4f },

                { ids::combMode, 1.0f }, { ids::combTime, 9.0f },
                { ids::combFeed, 0.78f }, { ids::combMix, 0.85f },
                { ids::combDamp, 0.25f }, { ids::combSpread, 0.5f },

                { ids::ampAttack, 0.004f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.35f },

                // Five destinations, one source. The signs matter: the comb
                // gets a negative depth because a longer delay is a lower
                // resonance, which is the direction that reads as "bigger".
                { ids::modSource (0), 13.0f },   // Macro 1
                { ids::modDest (0), 1.0f },      // cutoff
                { ids::modDepth (0), 0.85f },

                { ids::modSource (1), 13.0f },
                { ids::modDest (1), 22.0f },     // filter morph
                { ids::modDepth (1), 0.4f },

                { ids::modSource (2), 13.0f },
                { ids::modDest (2), 19.0f },     // feedback A
                { ids::modDepth (2), 0.35f },

                { ids::modSource (3), 13.0f },
                { ids::modDest (3), 7.0f },      // detune A
                { ids::modDepth (3), 0.5f },

                { ids::globalSource (0), 11.0f },   // Macro 1, global matrix
                { ids::globalDest (0), 1.0f },      // comb time
                { ids::globalDepth (0), -0.55f },

                { ids::macro (0), 0.25f },

                { ids::polyphony, 6.0f },
                { ids::tubeDrive, 4.0f },

                // -9 rather than -5, and measured rather than guessed: at -5
                // the macro's own midpoint peaked at 1.341 of full scale, and
                // an instrument has no limiter after it. A preset that clips
                // at one setting of its own headline control is not finished.
                { ids::output, -9.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **The filter morph as the whole gesture.** One oscillator, one
            // envelope, and the filter changing *type* under the note rather
            // than merely opening: it starts as a lowpass thud and arrives as a
            // bandpass whistle, which no cutoff sweep does.
            //
            // ADV 2 drives it, so the shape of the change is drawn rather than
            // dialled -- fast up, hold, slow back down.
            "Morphing pluck -- the filter changes type, not just cutoff",
            {
                { ids::shapeA, 1.0f }, { ids::morphA, 0.2f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 5.0f },
                { ids::subLevel, 0.35f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 700.0f }, { ids::resonance, 0.5f },
                { ids::filterTrack, 0.6f },

                { ids::ampAttack, 0.002f }, { ids::ampDecay, 0.5f },
                { ids::ampSustain, 0.25f }, { ids::ampRelease, 0.4f },

                { "adv2Enable", 1.0f },
                { "adv2Points", 4.0f }, { "adv2Sustain", 3.0f },
                { "adv2T1", 0.02f }, { "adv2L1", 1.0f },  { "adv2C1", 0.3f },
                { "adv2T2", 0.18f }, { "adv2L2", 0.75f }, { "adv2C2", 0.4f },
                { "adv2T3", 0.9f },  { "adv2L3", 0.2f },  { "adv2C3", -0.5f },
                { "adv2T4", 1.2f },  { "adv2L4", 0.0f },  { "adv2C4", 0.3f },

                { ids::modSource (0), 11.0f },   // ADV 2
                { ids::modDest (0), 22.0f },     // filter morph
                { ids::modDepth (0), 0.7f },

                { ids::modSource (1), 11.0f },
                { ids::modDest (1), 1.0f },      // cutoff, a little as well
                { ids::modDepth (1), 0.4f },

                { ids::polyphony, 8.0f },
                { ids::output, -7.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **The scale-locked comb**, which is the one preset that needs a
            // microtuning loaded to show what it does. On 12-TET it is a
            // pleasant resonant drone; load Partch 43 or a Persian scale on the
            // TUNING page and the comb stops sitting between the notes.
            //
            // Key tracking is high so the comb follows the played pitch, and
            // the lock catches whatever the tracking and the Time knob leave
            // between degrees.
            "Scale drone -- the comb belongs to the tuning",
            {
                { ids::shapeA, 5.0f },           // dome
                { ids::morphA, 0.4f },
                { ids::levelB, 0.55f }, { ids::shapeB, 0.0f }, { ids::octaveB, 1.0f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 4.0f },

                { ids::cutoff, 5200.0f }, { ids::resonance, 0.2f },

                { ids::combMode, 1.0f },
                { ids::combTime, 4.0f }, { ids::combTrack, 0.85f },
                { ids::combFeed, 0.88f }, { ids::combMix, 0.75f },
                { ids::combDamp, 0.15f }, { ids::combSpread, 0.35f },
                { ids::combScale, 1.0f },

                { ids::ampAttack, 0.25f }, { ids::ampSustain, 0.95f },
                { ids::ampRelease, 1.6f },

                { ids::polyphony, 8.0f },
                { ids::output, -14.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **Sixteen points, one bar.** The whole reason the ADV ceiling
            // went from eight to sixteen: with Loop and Snap on, sixteen legs
            // of a 1/16 each is a bar of sixteenths, so one envelope is a step
            // sequencer with *curves* between the steps rather than gates.
            //
            // Watch the ENV page while it plays -- the ruler draws the bar and
            // every leg reads "1/16", which is how you know it is locked and
            // not merely fast.
            "Sixteen-step gate -- one bar, drawn",
            {
                { ids::shapeA, 1.0f }, { ids::shapeB, 1.0f },
                { ids::levelB, 0.85f }, { ids::centsB, -7.0f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 8.0f }, { ids::spreadA, 0.7f },
                { ids::subLevel, 0.4f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 2600.0f }, { ids::resonance, 0.35f },
                { ids::filterDrive, 0.25f },

                { ids::ampAttack, 0.002f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.12f },

                { "adv1Enable", 1.0f }, { "adv1Loop", 1.0f }, { "adv1Snap", 1.0f },
                { "adv1Points", 16.0f }, { "adv1Sustain", 16.0f }, { "adv1LoopStart", 1.0f },
                { "adv1T1", 0.125f }, { "adv1L1", 0.00f }, { "adv1C1", 0.9f },
                { "adv1T2", 0.125f }, { "adv1L2", 1.00f }, { "adv1C2", -0.9f },
                { "adv1T3", 0.125f }, { "adv1L3", 0.15f }, { "adv1C3", 0.9f },
                { "adv1T4", 0.125f }, { "adv1L4", 1.00f }, { "adv1C4", -0.9f },
                { "adv1T5", 0.125f }, { "adv1L5", 0.00f }, { "adv1C5", 0.9f },
                { "adv1T6", 0.125f }, { "adv1L6", 0.85f }, { "adv1C6", -0.9f },
                { "adv1T7", 0.125f }, { "adv1L7", 1.00f }, { "adv1C7", 0.9f },
                { "adv1T8", 0.125f }, { "adv1L8", 0.30f }, { "adv1C8", -0.9f },
                { "adv1T9", 0.125f }, { "adv1L9", 0.00f }, { "adv1C9", 0.9f },
                { "adv1T10", 0.125f }, { "adv1L10", 1.00f }, { "adv1C10", -0.9f },
                { "adv1T11", 0.125f }, { "adv1L11", 0.40f }, { "adv1C11", 0.9f },
                { "adv1T12", 0.125f }, { "adv1L12", 1.00f }, { "adv1C12", -0.9f },
                { "adv1T13", 0.125f }, { "adv1L13", 0.10f }, { "adv1C13", 0.9f },
                { "adv1T14", 0.125f }, { "adv1L14", 0.65f }, { "adv1C14", -0.9f },
                { "adv1T15", 0.125f }, { "adv1L15", 1.00f }, { "adv1C15", 0.9f },
                { "adv1T16", 0.125f }, { "adv1L16", 0.50f }, { "adv1C16", -0.9f },

                // Straight onto Level, so the pattern is the note's rhythm.
                //
                // **A negative depth**, and that is not a taste decision: Level
                // is a multiplier, `level * (1 + modulation)`, so a positive
                // depth only ever makes a voice *louder* and a gate drawn that
                // way never closes -- it peaked at 1.859 of full scale and read
                // as a flat pattern. Negative, the envelope's own levels read
                // as "how far this step ducks", which is why the point list
                // above has 0 where the loud steps are.
                { ids::modSource (0), 10.0f },   // ADV 1
                { ids::modDest (0), 15.0f },     // level
                { ids::modDepth (0), -0.92f },

                // ...and a touch onto the cutoff so the loud steps are also
                // the bright ones, which is what a gate on a real rig does.
                { ids::modSource (1), 10.0f },
                { ids::modDest (1), 1.0f },
                { ids::modDepth (1), -0.5f },

                { ids::polyphony, 6.0f },
                { ids::output, -8.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **Sixteen points as a shape rather than a gate.** The same
            // ceiling used the other way: a bar-long riser whose acceleration
            // is drawn leg by leg, going up in a stepped climb no ADSR and no
            // LFO makes. It ends where it started because the loop returns.
            //
            // Two destinations from one envelope: the cutoff and the filter's
            // *type*, so the riser gets thinner as well as brighter.
            "Bar riser -- sixteen legs of climb",
            {
                { ids::shapeA, 0.0f }, { ids::morphA, 0.3f },
                { ids::shapeB, 6.0f },           // shark
                { ids::levelB, 0.6f }, { ids::octaveB, 1.0f },
                { ids::unisonA, 4.0f }, { ids::detuneA, 14.0f }, { ids::spreadA, 0.9f },

                { ids::cutoff, 380.0f }, { ids::resonance, 0.45f },
                { ids::filterDrive, 0.3f },

                { ids::ampAttack, 0.01f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.3f },

                { "adv2Enable", 1.0f }, { "adv2Loop", 1.0f }, { "adv2Snap", 1.0f },
                { "adv2Points", 16.0f }, { "adv2Sustain", 16.0f }, { "adv2LoopStart", 1.0f },
                { "adv2T1", 0.125f }, { "adv2L1", 0.050f }, { "adv2C1", 0.4f },
                { "adv2T2", 0.125f }, { "adv2L2", 0.113f }, { "adv2C2", 0.4f },
                { "adv2T3", 0.125f }, { "adv2L3", 0.177f }, { "adv2C3", 0.4f },
                { "adv2T4", 0.125f }, { "adv2L4", 0.240f }, { "adv2C4", 0.4f },
                { "adv2T5", 0.125f }, { "adv2L5", 0.303f }, { "adv2C5", 0.4f },
                { "adv2T6", 0.125f }, { "adv2L6", 0.367f }, { "adv2C6", 0.4f },
                { "adv2T7", 0.125f }, { "adv2L7", 0.430f }, { "adv2C7", 0.4f },
                { "adv2T8", 0.125f }, { "adv2L8", 0.493f }, { "adv2C8", 0.4f },
                { "adv2T9", 0.125f }, { "adv2L9", 0.557f }, { "adv2C9", 0.4f },
                { "adv2T10", 0.125f }, { "adv2L10", 0.620f }, { "adv2C10", 0.4f },
                { "adv2T11", 0.125f }, { "adv2L11", 0.683f }, { "adv2C11", 0.4f },
                { "adv2T12", 0.125f }, { "adv2L12", 0.747f }, { "adv2C12", 0.4f },
                { "adv2T13", 0.125f }, { "adv2L13", 0.810f }, { "adv2C13", 0.4f },
                { "adv2T14", 0.125f }, { "adv2L14", 0.873f }, { "adv2C14", 0.4f },
                { "adv2T15", 0.125f }, { "adv2L15", 0.937f }, { "adv2C15", 0.4f },
                { "adv2T16", 0.125f }, { "adv2L16", 1.000f }, { "adv2C16", 0.4f },

                // 0.55 rather than 0.95. At full depth the sweep took the
                // resonant peak clean out of the audible band by the eighth
                // leg and the riser got *darker* for its second half --
                // measured, brightness climbing 0.048 to 0.357 and then falling
                // back to 0.150. A riser has to keep rising, so the sweep ends
                // where the ear is still listening.
                { ids::modSource (0), 11.0f },   // ADV 2
                { ids::modDest (0), 1.0f },      // cutoff
                { ids::modDepth (0), 0.55f },

                // Morph only a little. At 0.45 the riser reached the bandpass
                // halfway up and started *losing* content as the cutoff rose:
                // measured, the zero-crossing count climbed for eight legs and
                // then fell for the rest. A bandpass thins a sound whichever
                // way its corner is going, and a riser has to get brighter.
                { ids::modSource (1), 11.0f },
                { ids::modDest (1), 22.0f },     // filter morph
                { ids::modDepth (1), 0.18f },

                // Drive climbing with it, because a cutoff sweep alone runs
                // out of things to reveal: once the filter is past the saw's
                // content the brightness plateaus, and the drive is what keeps
                // making new harmonics for the second half of the bar.
                { ids::modSource (2), 11.0f },
                { ids::modDest (2), 3.0f },      // filter drive
                { ids::modDepth (2), 0.55f },

                // No detune row. It was there in the first draft and it is
                // the reason this preset took three attempts to measure: a
                // widening unison stack changes the beat pattern, which moves
                // a zero-crossing count around by more than the cutoff does.
                // The riser is a filter gesture; the stack should hold still.
                { ids::polyphony, 6.0f },
                { ids::output, -7.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **Two knobs, eight destinations.** MACRO 1 is aggression --
            // brightness, feedback, drive, filter type. MACRO 2 is size --
            // detune, spread, comb time and comb mix. Between them they are the
            // whole patch, and neither is a control the matrix could give you
            // in one row.
            //
            // Both start low. Play with one hand on each.
            "Two-hand macro -- aggression and size",
            {
                { ids::shapeA, 1.0f }, { ids::shapeB, 0.0f },
                { ids::levelB, 0.9f }, { ids::centsB, 11.0f },
                { ids::unisonA, 3.0f }, { ids::detuneA, 4.0f },
                { ids::unisonB, 3.0f }, { ids::detuneB, 4.0f },

                { ids::cutoff, 420.0f }, { ids::resonance, 0.3f },
                { ids::filterTrack, 0.45f },

                { ids::combMode, 1.0f }, { ids::combTime, 5.0f },
                { ids::combFeed, 0.7f }, { ids::combDamp, 0.3f },

                { ids::ampAttack, 0.006f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.4f },

                // Macro 1 -- aggression.
                { ids::modSource (0), 13.0f }, { ids::modDest (0), 1.0f },
                { ids::modDepth (0), 0.9f },                       // cutoff
                { ids::modSource (1), 13.0f }, { ids::modDest (1), 19.0f },
                { ids::modDepth (1), 0.45f },                      // feedback A
                { ids::modSource (2), 13.0f }, { ids::modDest (2), 3.0f },
                { ids::modDepth (2), 0.6f },                       // filter drive
                { ids::modSource (3), 13.0f }, { ids::modDest (3), 22.0f },
                { ids::modDepth (3), 0.3f },                       // filter morph

                // Macro 2 -- size.
                { ids::modSource (4), 14.0f }, { ids::modDest (4), 7.0f },
                { ids::modDepth (4), 0.8f },                       // detune A
                { ids::modSource (5), 14.0f }, { ids::modDest (5), 8.0f },
                { ids::modDepth (5), 0.8f },                       // detune B

                { ids::globalSource (0), 12.0f },   // Macro 2, global
                { ids::globalDest (0), 1.0f },      // comb time
                { ids::globalDepth (0), -0.5f },

                { ids::globalSource (1), 12.0f },
                { ids::globalDest (1), 3.0f },      // comb mix
                { ids::globalDepth (1), 0.7f },

                { ids::macro (0), 0.2f }, { ids::macro (1), 0.2f },

                { ids::polyphony, 8.0f },
                { ids::output, -8.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **A wah that changes type rather than cutoff.** A synced LFO on
            // the filter *morph*, bipolar and centred on the mode, so the
            // filter swings lowpass to bandpass and back once a bar. The
            // cutoff barely moves; what moves is what kind of filter it is,
            // which is a sound no cutoff sweep makes.
            "Morph wah -- the filter type is the LFO",
            {
                { ids::shapeA, 1.0f }, { ids::morphA, 0.25f },
                { ids::levelB, 0.7f }, { ids::shapeB, 1.0f }, { ids::centsB, -6.0f },
                { ids::unisonA, 2.0f }, { ids::detuneA, 7.0f },
                { ids::subLevel, 0.45f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 900.0f }, { ids::resonance, 0.55f },
                { ids::filterDrive, 0.35f }, { ids::filterTrack, 0.35f },

                { ids::lfo1Sync, 1.0f }, { ids::lfo1Div, 3.0f },   // 1 bar
                { ids::lfo1Wave, 1.0f },                           // triangle
                { ids::lfo1Smooth, 0.2f },

                { ids::modSource (0), 7.0f },    // LFO 1
                { ids::modDest (0), 22.0f },     // filter morph
                { ids::modDepth (0), 0.85f },

                { ids::ampAttack, 0.004f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.3f },

                { ids::polyphony, 8.0f },
                { ids::output, -6.0f },
            }
        },
        // -------------------------------------------------------------------
        // **Phase 5 -- the horror set.** Stack, Tract and Sag, one apiece and
        // then together. Appended, like every preset here: a program is
        // recalled by index, so inserting one would repoint every saved choice.
        // -------------------------------------------------------------------
        {
            // **Shepard falling.** Seven copies an octave apart under the
            // window, sliding down one octave every four seconds, into a comb
            // that key-tracks. Nothing arrives and nothing resolves.
            //
            // Hold one note. The illusion needs no playing at all -- and it
            // needs the sustain up, because a decay would end the descent that
            // is the entire point.
            "Descent -- the fall that never lands",
            {
                { ids::shapeA, 0.0f },                            // saw
                { ids::stackA, 7.0f },                            // Shepard
                { ids::unisonA, 7.0f }, { ids::spreadA, 0.7f },
                { ids::shepardRate, -0.25f },
                { ids::levelB, 0.0f },

                { ids::subLevel, 0.3f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 2600.0f }, { ids::resonance, 0.25f },
                { ids::filterTrack, 0.0f },

                { ids::combMode, 1.0f }, { ids::combTrack, 0.8f },
                { ids::combFeed, 0.62f }, { ids::combMix, 0.4f },
                { ids::combSpread, 0.6f }, { ids::combDamp, 0.35f },

                { ids::ampAttack, 0.9f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 1.6f },

                { ids::sag, 0.25f }, { ids::sagRate, 40.0f },

                { ids::polyphony, 4.0f },
                { ids::output, -15.0f },   // measured: -7 peaked at +3.53 dBFS
            }
        },
        // -------------------------------------------------------------------
        {
            // **The same trick upward, and faster.** A rising Shepard into a
            // resonant filter that an envelope opens -- the stinger, rather
            // than the drone. Short release, so it can be played as stabs.
            "Ascent -- the riser that never arrives",
            {
                { ids::shapeA, 1.0f }, { ids::widthA, 0.35f },     // pulse
                { ids::stackA, 7.0f },
                { ids::unisonA, 7.0f }, { ids::spreadA, 0.55f },
                { ids::shepardRate, 0.9f },
                { ids::levelB, 0.0f },

                { ids::cutoff, 700.0f }, { ids::resonance, 0.62f },
                { ids::filterDrive, 0.35f }, { ids::filterTrack, 0.3f },

                { ids::ampAttack, 0.02f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.5f },

                { ids::env1Attack, 1.4f }, { ids::env1Decay, 1.0f },
                { ids::env1Sustain, 1.0f },
                { ids::modSource (0), 2.0f }, { ids::modDest (0), 1.0f },
                { ids::modDepth (0), 0.55f },                      // cutoff

                { ids::tubeDrive, 6.0f },
                { ids::polyphony, 6.0f },
                { ids::output, -8.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **A cluster in a tuning that cannot resolve it.** Stack at Scale,
            // one key per copy, five copies -- so what you get depends entirely
            // on the TUNING page. In twelve-tone equal temperament it is a
            // chromatic cluster; load Werckmeister III or a Persian dastgah and
            // it becomes a cluster with a history.
            //
            // Slow attack, long release, and the drift up, so the cluster
            // breathes rather than sitting still.
            "Cloister -- a cluster in whatever tuning is loaded",
            {
                { ids::shapeA, 4.0f }, { ids::morphA, 0.45f },     // vintage saw
                { ids::stackA, 6.0f },                            // Scale
                { ids::stackStepA, 1.0f },
                { ids::unisonA, 5.0f }, { ids::spreadA, 0.85f },
                { ids::detuneA, 4.0f }, { ids::driftA, 7.0f },

                { ids::shapeB, 3.0f }, { ids::levelB, 0.35f },     // sine
                { ids::octaveB, -1.0f },

                { ids::cutoff, 1500.0f }, { ids::resonance, 0.2f },
                { ids::filterTrack, 0.5f },

                { ids::ampAttack, 1.2f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 2.2f },

                { ids::voiceDrift, 8.0f },
                { ids::sag, 0.18f }, { ids::sagRate, 55.0f },

                { ids::polyphony, 8.0f },
                { ids::output, -9.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **Something much too big is talking.** Tract at 0.55 -- a 32 cm
            // throat -- with the vowel morph crawling under a slow LFO, over
            // noise rather than an oscillator. The anti-formant is in, because
            // a nasal is what makes it read as a mouth rather than as a filter.
            "Long Room -- a throat thirty centimetres too big",
            {
                { ids::shapeA, 8.0f }, { ids::morphA, 0.35f },     // noise
                { ids::unisonA, 5.0f }, { ids::spreadA, 0.9f },
                { ids::levelB, 0.0f },

                { ids::cutoff, 3200.0f }, { ids::resonance, 0.15f },

                { ids::formantMix, 0.8f }, { ids::formantMorph, 0.3f },
                { ids::tubeDrive, 12.0f },
                { ids::formantSharp, 0.7f },
                { ids::tract, 0.55f },
                { ids::formantNotch, 1100.0f }, { ids::formantNotchDepth, 0.5f },

                { ids::lfo1Rate, 0.09f }, { ids::lfo1Wave, 6.0f },  // smooth random
                { ids::globalSource (0), 1.0f },                    // LFO 1
                { ids::globalDest (0), 5.0f },                      // vowel
                { ids::globalDepth (0), 0.6f },

                { ids::globalSource (1), 1.0f },
                { ids::globalDest (1), 10.0f },                     // tract
                { ids::globalDepth (1), 0.25f },

                { ids::ampAttack, 0.6f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 1.4f },

                { ids::sag, 0.3f }, { ids::sagRate, 25.0f },

                { ids::polyphony, 4.0f },
                { ids::output, 10.0f },   // measured: -5 peaked at -29.3 dBFS
            }
        },
        // -------------------------------------------------------------------
        {
            // **A drone on a machine that is giving up.** Stacked fifths --
            // hollow, wide, no third to say major or minor -- under deep slow
            // sag, so the whole thing goes flat and dull together and then
            // comes back. Sixty-second period: it lurches about twice a
            // chorus.
            //
            // The one to leave running while something else happens over it.
            "Cellar -- fifths on a failing machine",
            {
                { ids::shapeA, 0.0f },
                { ids::stackA, 2.0f },                            // Fifths
                { ids::unisonA, 5.0f }, { ids::spreadA, 0.75f },
                { ids::detuneA, 9.0f }, { ids::driftA, 6.0f },

                { ids::shapeB, 0.0f }, { ids::levelB, 0.5f },
                { ids::stackB, 1.0f },                            // Octaves
                { ids::unisonB, 3.0f }, { ids::octaveB, -1.0f },

                { ids::subLevel, 0.5f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 900.0f }, { ids::resonance, 0.3f },
                { ids::filterDrive, 0.25f }, { ids::filterTrack, 0.35f },

                { ids::ampAttack, 0.8f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 2.5f },

                { ids::sag, 0.85f }, { ids::sagRate, 60.0f },
                { ids::voiceDrift, 10.0f },

                { ids::tubeDrive, 5.0f },
                { ids::polyphony, 6.0f },
                { ids::output, -9.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **The action one.** Stacked tritones -- symmetric, rootless, and
            // the interval that will not sit still -- with PM for teeth and the
            // sequencer chopping the level into sixteenths. Short and hard.
            //
            // Play it as one held note and let the sequencer do the rhythm.
            "Tritone Engine -- the pulse that will not resolve",
            {
                { ids::shapeA, 1.0f }, { ids::widthA, 0.3f },
                { ids::stackA, 3.0f },                            // Tritones
                { ids::unisonA, 3.0f }, { ids::spreadA, 0.5f },

                { ids::shapeB, 3.0f }, { ids::levelB, 0.0f },
                { ids::pmIndex, 2.2f }, { ids::semitonesB, 7.0f },
                { ids::feedbackA, 0.35f },

                { ids::subLevel, 0.55f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 1800.0f }, { ids::resonance, 0.45f },
                { ids::filterDrive, 0.5f },

                { ids::ampAttack, 0.002f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.12f },

                { ids::seqRate, 8.0f }, { ids::seqLength, 16.0f },
                { ids::modSource (0), 9.0f },                      // sequencer
                { ids::modDest (0), 15.0f },                       // level
                { ids::modDepth (0), 0.85f },

                { ids::tubeDrive, 9.0f },
                { ids::polyphony, 6.0f },
                { ids::output, -8.0f },
            }
        },
        // -------------------------------------------------------------------
        // **Phase 6.** Origin, shear, phase panning and Sing -- and, first, the
        // patch the player built on the rig out of phase 5 and sent back.
        // Appended, like every preset here.
        // -------------------------------------------------------------------
        {
            // **The player's own patch, written down.** Reported from the rig
            // on 2026-09-03: a Shepard stack with a slow envelope pulling the
            // pitch down underneath it, a long release, and an amplitude
            // envelope slow at both ends. Their words for it were "real horror
            // 80s", which is exactly right and worth understanding: the two
            // motions disagree. Shepard climbs and never arrives; the envelope
            // falls and does arrive. So the ear hears a rise that is losing,
            // which is a different feeling from either one alone.
            //
            // A slow amplitude attack is the other half. Nothing has an onset,
            // so there is no moment to locate the sound at -- it is simply
            // already happening by the time you notice it.
            //
            // Hold a low chord. It wants three or four seconds before it means
            // anything.
            "Slow Descent -- a rise that is losing",
            {
                { ids::shapeA, 0.0f },                             // saw
                { ids::stackA, 7.0f },                             // Shepard
                { ids::unisonA, 5.0f }, { ids::spreadA, 0.7f },
                { ids::shepardPanA, 1.0f },                        // the fixed fan
                { ids::shepardRate, 0.18f },                       // slow rise

                { ids::levelB, 0.35f }, { ids::shapeB, 0.0f },
                { ids::octaveB, -1.0f }, { ids::centsB, -7.0f },

                { ids::subLevel, 0.4f }, { ids::subOctave, -1.0f },

                // The slow fall. Mod env 1 into pitch, negative, so the whole
                // voice sinks a fifth over about four seconds and stays down.
                { ids::env1Attack, 4.0f }, { ids::env1Sustain, 1.0f },
                { ids::env1Release, 6.0f }, { ids::env1AttackT, -0.4f },
                { ids::modSource (0), 2.0f },                      // Mod env 1
                { ids::modDest (0), 13.0f },                       // pitch
                { ids::modDepth (0), -0.35f },

                { ids::cutoff, 1400.0f }, { ids::resonance, 0.4f },
                { ids::filterDrive, 0.3f }, { ids::filterTrack, 0.3f },
                { ids::voiceDrift, 14.0f },

                // Slow at both ends, so it neither starts nor stops.
                { ids::ampAttack, 1.6f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 5.0f },
                { ids::ampAttackT, -0.3f },

                { ids::sag, 0.3f }, { ids::sagRate, 30.0f },

                { ids::combMode, 1.0f }, { ids::combTime, 26.0f },
                { ids::combFeed, 0.4f }, { ids::combMix, 0.3f },

                { ids::polyphony, 8.0f },
                { ids::output, -10.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **Origin Up.** Five copies at fifths, all of them *above* the
            // played note, so the low end stays exactly one note wide and
            // everything the stack adds happens over the top of it. The sub
            // then has somewhere to sit that nothing is competing for, which
            // is the whole reason the control exists.
            //
            // Play it low. Centre origin at this width would put two copies
            // under the note and the bass would be a chord.
            "Overhead -- the stack that opens above the note",
            {
                { ids::shapeA, 0.0f },
                { ids::stackA, 2.0f },                             // Fifths
                { ids::stackOriginA, 1.0f },                       // Up
                { ids::unisonA, 5.0f }, { ids::spreadA, 0.6f },
                { ids::detuneA, 6.0f },

                { ids::levelB, 0.0f },
                { ids::subLevel, 0.75f }, { ids::subOctave, -1.0f },
                { ids::subShape, 1.0f },                           // square

                { ids::cutoff, 2600.0f }, { ids::resonance, 0.3f },
                { ids::filterDrive, 0.4f }, { ids::filterTrack, 0.5f },

                { ids::ampAttack, 0.01f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.6f },

                { ids::tubeDrive, 6.0f },
                { ids::polyphony, 8.0f },
                { ids::output, -14.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **Shear at full.** Both oscillators on Shepard, B falling exactly
            // as fast as A rises, so the two stacks pass through each other
            // forever. Neither one is a chord and together they are not either;
            // what you hear is the beating where partials cross, at a rate set
            // by the distance between the two speeds rather than by any tuning.
            //
            // Origin Down on B puts its weight underneath, so the falling half
            // is the one you feel and the rising half is the one you hear.
            "Two Ways at Once -- one stack rising through another falling",
            {
                { ids::shapeA, 3.0f },                             // sine
                { ids::stackA, 7.0f }, { ids::unisonA, 5.0f },
                { ids::spreadA, 0.8f }, { ids::shepardPanA, 1.0f },

                { ids::shapeB, 3.0f }, { ids::levelB, 0.9f },
                { ids::stackB, 7.0f }, { ids::unisonB, 5.0f },
                { ids::stackOriginB, 2.0f },                       // Down
                { ids::spreadB, 0.8f }, { ids::shepardPanB, 1.0f },

                { ids::shepardRate, 0.3f },
                { ids::shepardShear, 1.0f },                       // B falls as A rises

                { ids::subLevel, 0.3f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 5000.0f }, { ids::resonance, 0.25f },
                { ids::filterTrack, 0.4f },

                { ids::ampAttack, 0.9f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 3.5f },

                { ids::sag, 0.2f }, { ids::sagRate, 45.0f },

                { ids::polyphony, 6.0f },
                { ids::output, -13.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **The filter as the instrument.** Both oscillators silent; the
            // only sound in the patch is the filter singing at its own cutoff.
            // Key track at full makes it playable, the envelope on Sing makes
            // it arrive rather than simply be there, and FM gives it an edge
            // that no oscillator in this instrument makes.
            //
            // Resonance is high on purpose: Sing has to cancel the damping
            // before it can go past it, so a high resonance is what puts the
            // interesting part of Sing's travel where a knob can find it.
            "The Filter Sings -- no oscillators at all",
            {
                { ids::levelA, 0.0f }, { ids::levelB, 0.0f },
                { ids::subLevel, 0.0f },

                { ids::cutoff, 220.0f }, { ids::resonance, 0.95f },
                { ids::filterSing, 0.55f },
                { ids::filterTrack, 1.0f },
                { ids::filterFm, 0.0f },
                { ids::filterMode, 1.0f },                         // bandpass
                { ids::voiceDrift, 20.0f },

                // Sing under an envelope: the tone builds rather than being
                // switched on, and the release lets it decay out on its own.
                { ids::env1Attack, 0.8f }, { ids::env1Sustain, 1.0f },
                { ids::env1Release, 2.0f },
                { ids::modSource (0), 2.0f },                      // Mod env 1
                { ids::modDest (0), 23.0f },                       // filter sing
                { ids::modDepth (0), 0.4f },

                // And a slow sweep of where it sings.
                { ids::lfo1Rate, 0.11f }, { ids::lfo1Wave, 1.0f },
                { ids::modSource (1), 7.0f },                      // LFO 1
                { ids::modDest (1), 1.0f },                        // cutoff
                { ids::modDepth (1), 0.25f },

                { ids::ampAttack, 0.05f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 2.5f },

                { ids::sag, 0.35f }, { ids::sagRate, 25.0f },

                { ids::combMode, 1.0f }, { ids::combTime, 18.0f },
                { ids::combFeed, 0.45f }, { ids::combMix, 0.35f },

                { ids::polyphony, 6.0f },
                { ids::output, -4.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **Sing under the oscillators rather than instead of them.** A
            // held saw through a filter that is just over the edge, so the
            // resonant peak is a note of its own sitting inside the chord --
            // the sound a real filter makes when it is turned up too far and
            // the reason people turn it up too far.
            //
            // Key track is deliberately part-way: the singing peak then drifts
            // against the played note as you move up the keyboard, so the
            // interval between them is different in every register.
            "Overtone -- a filter just over the edge",
            {
                { ids::shapeA, 0.0f }, { ids::unisonA, 3.0f },
                { ids::detuneA, 12.0f }, { ids::spreadA, 0.5f },
                { ids::stackA, 1.0f },                             // Octaves
                { ids::stackOriginA, 2.0f },                       // Down

                { ids::levelB, 0.0f },
                { ids::subLevel, 0.5f }, { ids::subOctave, -1.0f },

                { ids::cutoff, 1600.0f }, { ids::resonance, 0.88f },
                { ids::filterSing, 0.22f },
                { ids::filterDrive, 0.3f },
                { ids::filterTrack, 0.45f },

                { ids::ampAttack, 0.02f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 0.9f },

                { ids::tubeDrive, 7.0f },
                { ids::tilt, -1.5f },

                { ids::polyphony, 8.0f },
                { ids::output, -11.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // **The converging riser.** Asked for on 2026-09-03: the sound
            // that opened films in the eighties -- a mass of voices that starts
            // as a tight cluster, wanders, and then opens out and arrives all
            // at once on an enormous chord that simply sits there.
            //
            // **Built from the published sketch of the technique**, which the
            // player supplied: thirty voices at random pitches in a narrow band
            // around 200-400 Hz, each moving slowly and randomly, then all
            // proceeding direct to their target note -- three slightly detuned
            // voices per note, two per note in the bass. Nothing is sampled and
            // nothing is reverse engineered; the method is the published part.
            //
            // **The first version of this preset had it backwards**, and the
            // correction is the interesting bit: it started the voices spread
            // over three octaves and converged them inward onto the played
            // note. That is not the gesture. The gesture is narrow-to-wide, and
            // what reads as "converging" is that every voice stops moving at
            // the same instant.
            //
            // So the width is **modulated detune**, not a fixed stack:
            //
            //   * Stack stays at *Detune* and both banks start at 0 cents, so
            //     the note begins as a genuine unison -- one pitch, fourteen
            //     copies of it.
            //   * Both mod envelopes run a **long attack to a held sustain**,
            //     which is the ordinary direction and here means the
            //     displacement grows to its maximum and then holds.
            //   * Mod env 1 opens `detuneA` and mod env 2 opens `detuneB`, to
            //     660 and 840 cents. Two different attack times, so the banks
            //     do not arrive in lockstep.
            //   * Drift is high on both banks: that is the "moves slowly and
            //     randomly", and it carries on after the arrival, which is what
            //     stops the held chord being static.
            //
            // **What one instance cannot do**, and it is worth knowing before
            // reaching for the knobs: in the original, a voice's *starting*
            // pitch is unrelated to its target -- everything begins near 300 Hz
            // whether it ends up in the bass or three octaves up. Doing that
            // needs displacement proportional to the note, faded out over time,
            // which is a product of key tracking and an envelope. This matrix
            // multiplies neither pair, so it cannot be built here. Two or three
            // instances each holding one register, each with its own envelope
            // depth on pitch, is the faithful route -- `docs/WHATS-NEW.md` has
            // that recipe.
            //
            // Play a wide chord and **hold it**. It takes about twelve seconds.
            "Conflux -- many pitches arriving as one",
            {
                { ids::shapeA, 0.0f },                             // saw
                { ids::unisonA, 7.0f }, { ids::detuneA, 0.0f },
                { ids::spreadA, 1.0f }, { ids::driftA, 18.0f },
                { ids::levelA, 1.0f },

                { ids::shapeB, 0.0f }, { ids::levelB, 1.0f },
                { ids::unisonB, 7.0f }, { ids::detuneB, 0.0f },
                { ids::spreadB, 1.0f }, { ids::driftB, 22.0f },
                { ids::centsB, 4.0f },

                { ids::subLevel, 0.55f }, { ids::subOctave, -1.0f },
                { ids::subShape, 0.0f },                           // sine, pure weight

                // The opening, bank A. Attack to a held sustain: the spread
                // grows and then stays.
                { ids::env1Attack, 11.0f }, { ids::env1Sustain, 1.0f },
                { ids::env1Release, 4.0f }, { ids::env1AttackT, -0.25f },
                { ids::modSource (0), 2.0f },                      // Mod env 1
                { ids::modDest (0), 7.0f },                        // detune A
                { ids::modDepth (0), 0.55f },

                // Bank B, a little slower, so the two do not land together.
                { ids::env2Attack, 13.0f }, { ids::env2Sustain, 1.0f },
                { ids::env2Release, 4.0f }, { ids::env2AttackT, -0.25f },
                { ids::modSource (1), 3.0f },                      // Mod env 2
                { ids::modDest (1), 8.0f },                        // detune B
                { ids::modDepth (1), 0.7f },

                // Each voice its own few cents that never resolve -- the
                // sketch's "random pitches", at the scale one instance can
                // manage.
                { ids::modSource (2), 6.0f },                      // Note random
                { ids::modDest (2), 13.0f },                       // pitch
                { ids::modDepth (2), 0.0025f },

                // It brightens into the arrival, which is most of why the
                // arrival reads as one.
                { ids::cutoff, 700.0f }, { ids::resonance, 0.12f },
                { ids::filterTrack, 0.35f },
                { ids::modSource (3), 1.0f },                      // Amp env
                { ids::modDest (3), 1.0f },                        // cutoff
                { ids::modDepth (3), 0.38f },

                { ids::voiceDrift, 14.0f },

                // Slow in, and it holds for as long as the key is down. The
                // sketch marks the dynamics mf rising to fff, which is this
                // attack plus the filter opening under it.
                { ids::ampAttack, 3.0f }, { ids::ampSustain, 1.0f },
                { ids::ampRelease, 5.0f }, { ids::ampAttackT, -0.3f },

                { ids::tubeDrive, 5.0f },

                { ids::polyphony, 12.0f },
                { ids::output, -17.0f },
            }
        },

    };

    return list;
}
} // namespace

int SonitusProcessor::getNumPrograms()
{
    return static_cast<int> (presets().size());
}

const juce::String SonitusProcessor::getProgramName (int index)
{
    const auto& list = presets();

    return list[static_cast<std::size_t> (
        juce::jlimit (0, static_cast<int> (list.size()) - 1, index))].name;
}

void SonitusProcessor::setCurrentProgram (int index)
{
    const auto& list = presets();

    currentProgram_ = juce::jlimit (0, static_cast<int> (list.size()) - 1, index);

    // Everything to its default first, so a preset is a complete parameter set
    // rather than a patch over whatever was loaded before it. Without this,
    // moving from a preset that sets the folder to one that does not would
    // leave the folder where it was and the second preset would be wrong in a
    // way that depends on the first.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& setting : list[static_cast<std::size_t> (currentProgram_)].settings)
        if (auto* parameter = state_.getParameter (setting.id))
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (setting.value));

    // A preset changes the sound, not the tuning: a scale is loaded by the
    // player and outlives the patch they are auditioning.
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void SonitusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = state_.copyState();

    state.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);

    // Both A/B slots, or the button is a session-only convenience: a player who
    // built two versions of a sound and saved the project would reopen it with
    // one of them gone.
    state.appendChild (abCompare_.toValueTree(), nullptr);

    // The tuning travels with the project. A `.scl` file lives on one machine
    // and a project does not, so storing only its path would open silently
    // detuned somewhere else.
    state.setProperty (kScaleNameProperty, scaleName_, nullptr);
    state.setProperty (kScaleTextProperty, scalaText_, nullptr);
    state.setProperty (kKeyboardMapProperty, keyboardMapText_, nullptr);
    state.setProperty (kConcertPitchProperty, concertPitchHz_, nullptr);
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

    // DICEROLL's locks and strengths, which are state and never parameters --
    // a lock that was a parameter would be randomised by the button it
    // restrains, and reset by every preset the player loads. The roll history
    // is deliberately **not** saved: it is a session's worth of undo, and
    // writing 41 kB of snapshots into every project file to preserve it would
    // be paying for the wrong thing.
    state.setProperty (kDiceLocksProperty, static_cast<int> (diceLocks_), nullptr);
    state.setProperty (kDiceAmountProperty, diceAmount_, nullptr);
    state.setProperty (kDiceSpreadProperty, diceSpread_, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SonitusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state_.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    state_.replaceState (tree);

    abCompare_.restoreFromValueTree (tree.getChildWithName ("abCompare"));

    const juce::String name = tree.getProperty (kScaleNameProperty, "").toString();
    const juce::String text = tree.getProperty (kScaleTextProperty, "").toString();
    tooltipsEnabled_ = tree.getProperty (ui::stateIds::tooltipsEnabled, true);

    // A project saved before these existed gets the defaults, OUTPUT locked
    // included -- which is the right answer rather than an accident: an old
    // project reopening with the dice able to reach the master level would be
    // a surprise in the dangerous direction.
    const int storedLocks = tree.getProperty (kDiceLocksProperty,
                                              static_cast<int> (diceLocks_));
    diceLocks_ = static_cast<unsigned int> (std::max (0, storedLocks));

    setDiceAmount (static_cast<float> (double (tree.getProperty (kDiceAmountProperty, 1.0))));
    setDiceSpread (static_cast<float> (double (tree.getProperty (kDiceSpreadProperty, 1.0))));

    // The history describes a parameter set that the state just replaced, so
    // it cannot survive the load.
    diceHistory_.clear();
    diceCursor_ = -1;

    const juce::String map = tree.getProperty (kKeyboardMapProperty, "").toString();

    // The concert pitch first, so every publish below already carries it.
    concertPitchHz_ = std::clamp (double (tree.getProperty (kConcertPitchProperty, 440.0)),
                                  dsp::Tuning::kMinimumConcertHz,
                                  dsp::Tuning::kMaximumConcertHz);

    // Restore the tuning, and **fall back to 12-TET if it does not parse**
    // rather than leaving whatever was there. A project that reopens playing
    // the previous project's scale is worse than one that reopens in tune.
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

std::vector<float> SonitusProcessor::captureParameterSnapshot() const
{
    std::vector<float> snapshot;
    snapshot.reserve (static_cast<std::size_t> (getParameters().size()));

    for (const auto* parameter : getParameters())
        snapshot.push_back (parameter->getValue());

    return snapshot;
}

void SonitusProcessor::applyParameterSnapshot (const std::vector<float>& snapshot)
{
    const auto& parameters = getParameters();

    // A snapshot from a build with fewer parameters is applied as far as it
    // goes rather than refused: the ones it does not mention keep what they
    // have, which is the same rule a project saved before a parameter existed
    // already follows.
    const auto count = std::min (snapshot.size(), static_cast<std::size_t> (parameters.size()));

    for (std::size_t i = 0; i < count; ++i)
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameters[static_cast<int> (i)]))
            ranged->setValueNotifyingHost (snapshot[i]);
}

void SonitusProcessor::pushDiceHistory (std::vector<float> snapshot)
{
    // Anything ahead of the cursor is a future that this roll replaces --
    // ordinary undo semantics, and the alternative (a tree) is not what a
    // dice wants.
    if (diceCursor_ >= 0 && diceCursor_ + 1 < static_cast<int> (diceHistory_.size()))
        diceHistory_.erase (diceHistory_.begin() + diceCursor_ + 1, diceHistory_.end());

    diceHistory_.push_back (std::move (snapshot));

    while (diceHistory_.size() > kDiceHistoryLimit)
        diceHistory_.erase (diceHistory_.begin());

    diceCursor_ = static_cast<int> (diceHistory_.size()) - 1;
}

bool SonitusProcessor::stepDiceHistory (int direction)
{
    const int target = diceCursor_ + (direction < 0 ? -1 : 1);

    if (diceCursor_ < 0 || target < 0 || target >= static_cast<int> (diceHistory_.size()))
        return false;

    diceCursor_ = target;
    applyParameterSnapshot (diceHistory_[static_cast<std::size_t> (target)]);

    return true;
}

void SonitusProcessor::setDiceSectionLocked (DiceSection section, bool locked) noexcept
{
    const auto bit = 1u << static_cast<unsigned int> (section);

    diceLocks_ = locked ? (diceLocks_ | bit) : (diceLocks_ & ~bit);
}

void SonitusProcessor::soloDiceSection (DiceSection section) noexcept
{
    // Every section but this one. The mask is built from the seven real
    // sections; `unknown` is not one of them and is refused separately by the
    // roller, so it cannot be soloed into relevance.
    unsigned int everythingElse = 0u;

    for (int i = 0; i < numDiceSections; ++i)
        if (i != static_cast<int> (section))
            everythingElse |= 1u << static_cast<unsigned int> (i);

    // Pressing solo on the section that is already alone clears every lock, so
    // the same button is the way back out. Without this the player has to
    // remember which one they soloed to undo it, which is exactly the
    // "lock all the others over and over" tedium the button exists to remove.
    diceLocks_ = (diceLocks_ == everythingElse) ? 0u : everythingElse;
}

void SonitusProcessor::randomizeAllParameters()
{
    // A fresh seed per roll rather than one member generator: two rolls a
    // second apart must not be able to produce the same patch, and a member
    // seeded once at construction would make every session's first roll the
    // same one.
    std::mt19937 generator { std::random_device {} () };
    std::uniform_real_distribution<float> uniform { 0.0f, 1.0f };

    // **The state before the roll goes into the history first**, and only if
    // it is not already the entry the cursor is sitting on -- so hand edits
    // made between two rolls are recorded rather than lost, and pressing the
    // button twice does not fill the ring with duplicates.
    auto before = captureParameterSnapshot();

    if (diceCursor_ < 0 || diceHistory_[static_cast<std::size_t> (diceCursor_)] != before)
        pushDiceHistory (before);

    const float amount = diceAmount_;
    const float spread = diceSpread_;

    lastRollCount_ = 0;

    // Every parameter, minus the sections the player locked -- and there is
    // still no exclusion list of ids anywhere. Sonitus is an instrument, so it
    // has no bypass parameter to silence (the header leaves that button out;
    // muting the track is what a player reaches for). And the tuning was
    // deliberately never made a parameter, because a scale is a rig decision
    // presets must not reset -- so the scale and the concert pitch are
    // unreachable from here by construction rather than by a list somebody has
    // to remember to maintain.
    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (ranged == nullptr)
            continue;

        const auto section = diceSectionFor (ranged->paramID.toStdString());

        // `unknown` is treated as locked. A parameter added and not yet
        // classified is left alone rather than rolled at random, which is the
        // safe direction for a mistake nobody has noticed yet.
        if (section == DiceSection::unknown || isDiceSectionLocked (section))
            continue;

        // SPREAD is how many, not how far: a roll that changes six things is a
        // different creature from one that changes three hundred, however hard
        // each one is pushed.
        if (spread < 1.0f && uniform (generator) >= spread)
            continue;

        // Uniform on the NORMALISED range, which is what "0 to MAX" means for
        // a control whose own range is skewed: a skewed knob spends more of
        // its travel at the fine end, and rolling in its own units would land
        // in the coarse end nearly every time. Choices and switches come out
        // uniform over their entries for the same reason.
        const float target = uniform (generator);

        // AMOUNT drags each control towards that target rather than capping how
        // far it may move -- see `diceValueFor`, which is where the rule and
        // the reason for its shape live, and which the tests can reach.
        const float value = diceValueFor (ranged->getValue(), target, amount);

        ranged->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
        ++lastRollCount_;
    }

    // And the result, so PREV steps back to the patch this roll replaced and
    // NEXT returns to the roll.
    pushDiceHistory (captureParameterSnapshot());
}

juce::AudioProcessorEditor* SonitusProcessor::createEditor()
{
    return new SonitusEditor (*this);
}

} // namespace tezla::sonitus

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::sonitus::SonitusProcessor();
}
