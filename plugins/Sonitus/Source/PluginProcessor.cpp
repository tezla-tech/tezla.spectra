#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
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

constexpr int kStateSchemaVersion = kSchemaV2;
constexpr auto kStateTypeName = "SonitusState";

/// Where the tuning is stashed inside the state tree.
///
/// A property rather than a parameter, because a scale is text and a parameter
/// is a number. It has to be *in* the state, though: a project that reopens on
/// another machine has to sound the same without needing the `.scl` file to be
/// there, which means the file travels with the project.
constexpr auto kScaleTextProperty = "scalaText";
constexpr auto kScaleNameProperty = "scaleName";
constexpr auto kKeyboardMapProperty = "keyboardMapText";

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
                                          const char* widthId, const char* unisonId,
                                          const char* detuneId, const char* spreadId,
                                          const char* driftId, const char* levelId,
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
    };

    addOscillator (ids::shapeA, ids::octaveA, ids::semitonesA, ids::centsA, ids::widthA,
                   ids::unisonA, ids::detuneA, ids::spreadA, ids::driftA, ids::levelA,
                   "Osc A", 1.0f);

    addOscillator (ids::shapeB, ids::octaveB, ids::semitonesB, ids::centsB, ids::widthB,
                   ids::unisonB, ids::detuneB, ids::spreadB, ids::driftB, ids::levelB,
                   "Osc B", 0.0f);

    layout.add (std::make_unique<Boolean> (
        juce::ParameterID { ids::syncB, kSchemaV1 }, "Sync B to A", false));

    layout.add (std::make_unique<Parameter> (
        juce::ParameterID { ids::pmIndex, kSchemaV1 }, "PM index",
        skewedRange (0.0f, 8.0f, 1.5f), 0.0f, percentAttributes()));

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
    scale_ = dsp::Tuning::twelveToneEqual();
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
    v.unisonB = indexOf (state_, ids::unisonB);
    v.detuneB = valueOf (state_, ids::detuneB);
    v.spreadB = valueOf (state_, ids::spreadB);
    v.driftB = valueOf (state_, ids::driftB);
    v.levelB = valueOf (state_, ids::levelB);
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
            // so half the range was unreachable from the matrix.
            case ModDestination::pmIndex:     v.slots[slot].depth = depth * 16.0; break;

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

            engine_.setTransport (ppq.orFallback (-1.0), bpm.orFallback (120.0),
                                  position->getIsPlaying());
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
    scale_ = dsp::Tuning::twelveToneEqual();
    scaleName_ = scale_.name;
    scalaText_.clear();
    keyboardMapText_.clear();
    hasKeyboardMap_ = false;

    publishTuning();
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
                { ids::output, -9.0f },
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
    state.setProperty (ui::stateIds::tooltipsEnabled, tooltipsEnabled_, nullptr);

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

    const juce::String map = tree.getProperty (kKeyboardMapProperty, "").toString();

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

juce::AudioProcessorEditor* SonitusProcessor::createEditor()
{
    return new SonitusEditor (*this);
}

} // namespace tezla::sonitus

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::sonitus::SonitusProcessor();
}
