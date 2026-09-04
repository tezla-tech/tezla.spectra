// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace tezla::stryda {

namespace {

[[nodiscard]] juce::String rateText (double hz)
{
    return juce::String (hz / 1000.0, hz < 100000.0 ? 1 : 0) + " kHz";
}

/// The house shape for a knob's range: enough resolution that a value can be
/// typed, skewed so the useful part sits mid-travel.
[[nodiscard]] juce::NormalisableRange<float> skewed (float low, float high, float centre)
{
    juce::NormalisableRange<float> range { low, high };
    range.setSkewForCentre (centre);
    return range;
}

[[nodiscard]] juce::ParameterID versioned (const juce::String& id, int schema = kSchemaV1)
{
    return { id, schema };
}

constexpr const char* kScaleNameProperty = "scaleName";
constexpr const char* kScaleTextProperty = "scaleText";
constexpr const char* kKeyboardMapProperty = "keyboardMap";
constexpr const char* kConcertPitchProperty = "concertPitch";

/// The tempo divisions as a JUCE list, built once from the shared table.
///
/// **Append-only for the same reason every other choice list is**: a synced
/// sequencer stores which division it chose, not what it means, and
/// `dsp::divisions` carries a static_assert saying so.
[[nodiscard]] const juce::StringArray& divisionNames()
{
    static const juce::StringArray names = []
    {
        juce::StringArray list;

        for (int i = 0; i < dsp::numDivisions; ++i)
            list.add (dsp::divisions[static_cast<std::size_t> (i)].name);

        return list;
    }();

    return names;
}

} // namespace

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout StrydaProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // **The formatter is not decoration.** Without one, a skewed range
    // round-trips its default through the 0-1 normalisation and comes back as
    // 0.999999, which JUCE then prints in full -- so a Ratio of 1 reads
    // "0.99999..." on the panel and in the host's automation lane, and looks
    // exactly like a bug. Two places, and the unit beside the number.
    const auto add = [&layout] (const juce::String& id,
                                const juce::String& name,
                                juce::NormalisableRange<float> range,
                                float defaultValue,
                                const juce::String& suffix = {},
                                int places = 2,
                                int schema = kSchemaV1)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            versioned (id, schema), name, range, defaultValue,
            juce::AudioParameterFloatAttributes()
                .withLabel (suffix)
                .withStringFromValueFunction (
                    [suffix, places] (float value, int)
                    {
                        auto text = juce::String (value, places);
                        return suffix.isEmpty() ? text : text + " " + suffix;
                    })));
    };

    for (int op = 0; op < kNumOperators; ++op)
    {
        const juce::String label = "Op " + juce::String (op + 1) + " \xc2\xb7 ";

        // Ratio to the note. 32 at the top because a modulator at 32x a bass
        // note is 1.8 kHz, which is where the metallic end of the range lives.
        add (ids::op (op, "Ratio"), label + "Ratio", skewed (0.25f, 32.0f, 4.0f), 1.0f);
        add (ids::op (op, "Fine"), label + "Fine", { -100.0f, 100.0f }, 0.0f, "c", 1);

        // 0 = classic phase modulation, 1 = ModFM. See dsp/FmOperator.hpp.
        add (ids::op (op, "Character"), label + "Character", { 0.0f, 1.0f }, 0.0f);

        // Only operator 1 carries the output by default, which is what makes a
        // fresh instance a single clean sine rather than six of them at once.
        add (ids::op (op, "Level"), label + "Level", { 0.0f, 1.0f }, op == 0 ? 1.0f : 0.0f);
        add (ids::op (op, "Pan"), label + "Pan", { -1.0f, 1.0f }, 0.0f);
        add (ids::op (op, "Feedback"), label + "Feedback", { 0.0f, 1.0f }, 0.0f, "cyc");

        add (ids::op (op, "Attack"), label + "Attack", skewed (0.0f, 4.0f, 0.05f), 0.002f, "s", 3);
        add (ids::op (op, "Decay"), label + "Decay", skewed (0.005f, 12.0f, 0.6f), 1.5f, "s", 3);
        add (ids::op (op, "Sustain"), label + "Sustain", { 0.0f, 1.0f }, 1.0f);
        add (ids::op (op, "Release"), label + "Release", skewed (0.005f, 12.0f, 0.4f), 0.25f, "s", 3);

        // ---- F4, appended at schema 2 --------------------------------------

        add (ids::op (op, "Fold"), label + "Fold", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);

        add (ids::op (op, "Formant"), label + "Formant",
             skewed (50.0f, 8000.0f, 900.0f), 800.0f, "Hz", 0, kSchemaV2);
        add (ids::op (op, "Width"), label + "Formant width",
             skewed (0.0f, 8.0f, 1.0f), 1.0f, "cyc", 2, kSchemaV2);

        add (ids::op (op, "KeyBreak"), label + "Key break",
             { 21.0f, 108.0f }, 60.0f, {}, 0, kSchemaV2);
        add (ids::op (op, "KeyLeft"), label + "Key below", { -1.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);
        add (ids::op (op, "KeyRight"), label + "Key above", { -1.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);

        add (ids::op (op, "VelLevel"), label + "Vel level", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);
        add (ids::op (op, "VelIndex"), label + "Vel index", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV2);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            versioned (ids::op (op, "Mode"), kSchemaV2), label + "Mode",
            choices::operatorMode, 0));

        // F5 appended the extras; F6 appends the ratio mode. Free at index 0
        // returns the ratio bit for bit, so this is inert until asked for.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            versioned (ids::op (op, "RatioMode"), kSchemaV4), label + "Ratio mode",
            choices::ratioMode, 0));
    }

    // The thirty off-diagonal cells. The diagonal is Feedback, above.
    for (int to = 0; to < kNumOperators; ++to)
        for (int from = 0; from < kNumOperators; ++from)
            if (to != from)
                add (ids::cell (to, from),
                     juce::String (from + 1) + " \xe2\x86\x92 " + juce::String (to + 1),
                     skewed (0.0f, 8.0f, 1.0f), 0.0f, "cyc");

    for (int to = 0; to < kNumOperators; ++to)
        add (ids::noise (to), "Noise \xe2\x86\x92 " + juce::String (to + 1),
             skewed (0.0f, 4.0f, 0.5f), 0.0f, "cyc");

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::oversampling), "Oversampling", choices::oversampling, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::renderOversampling), "Render quality", choices::render, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::indexCap), "Index cap", choices::indexCap, 0));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        versioned (ids::polyphony), "Voices", 1, StrydaEngine::kMaxVoices, 8));

    add (ids::master, "Master", { -60.0f, 12.0f }, 0.0f, "dB", 1);

    // ---- F5, appended at schema 3 ------------------------------------------
    //
    // Twenty-two, and every one of them neutral by default. The filter is above
    // its bypass corner and therefore skipped bit-exactly (StrydaVoice's
    // kFilterBypassHz); the sub is at zero level; unison is a stack of one, at
    // which the detune, spread and index-spread amounts have nothing to spread.
    // A project saved at F4 reopens sounding identical, which is the whole
    // point of appending rather than inserting (CLAUDE.md section 8).

    add (ids::filterCutoff, "Filter cutoff",
         skewed (20.0f, 20000.0f, 1200.0f), 20000.0f, "Hz", 0, kSchemaV3);
    add (ids::filterReso, "Filter resonance", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV3);
    add (ids::filterMorph, "Filter morph", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV3);
    add (ids::filterKeyTrack, "Filter key track", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV3);
    add (ids::filterEnv, "Filter envelope", { -6.0f, 6.0f }, 0.0f, "oct", 2, kSchemaV3);
    add (ids::filterDrive, "Filter drive", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV3);
    add (ids::filterSing, "Filter sing", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV3);

    add (ids::filterAttack, "Filter attack",
         skewed (0.0f, 4.0f, 0.05f), 0.002f, "s", 3, kSchemaV3);
    add (ids::filterDecay, "Filter decay",
         skewed (0.005f, 12.0f, 0.6f), 0.5f, "s", 3, kSchemaV3);
    add (ids::filterSustain, "Filter sustain", { 0.0f, 1.0f }, 1.0f, {}, 2, kSchemaV3);
    add (ids::filterRelease, "Filter release",
         skewed (0.005f, 12.0f, 0.4f), 0.25f, "s", 3, kSchemaV3);

    add (ids::subLevel, "Sub level", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV3);

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::subOctave, kSchemaV3), "Sub octave", choices::subOctave, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::subShape, kSchemaV3), "Sub shape", choices::subShape, 0));

    add (ids::subAttack, "Sub attack", skewed (0.0f, 4.0f, 0.05f), 0.002f, "s", 3, kSchemaV3);
    add (ids::subDecay, "Sub decay", skewed (0.005f, 12.0f, 0.6f), 2.0f, "s", 3, kSchemaV3);
    add (ids::subSustain, "Sub sustain", { 0.0f, 1.0f }, 1.0f, {}, 2, kSchemaV3);
    add (ids::subRelease, "Sub release", skewed (0.005f, 12.0f, 0.4f), 0.25f, "s", 3, kSchemaV3);

    layout.add (std::make_unique<juce::AudioParameterInt> (
        versioned (ids::unison, kSchemaV3), "Unison", 1, StrydaEngine::kMaxVoices, 1));

    add (ids::unisonDetune, "Unison detune", skewed (0.0f, 50.0f, 12.0f), 12.0f, "c", 1, kSchemaV3);
    add (ids::unisonSpread, "Unison spread", { 0.0f, 1.0f }, 0.6f, {}, 2, kSchemaV3);
    add (ids::unisonIndex, "Unison index spread",
         skewed (0.0f, 2.0f, 0.3f), 0.0f, "cyc", 2, kSchemaV3);

    // ---- F6, appended at schema 4 ------------------------------------------
    //
    // Sixteen steps whose value is a RATIO, plus the five controls that decide
    // what the pattern does with them. Off by default and with no destination,
    // so nothing here can touch an F5 project.

    layout.add (std::make_unique<juce::AudioParameterBool> (
        versioned (ids::seqOn, kSchemaV4), "Sequencer", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::seqTarget, kSchemaV4), "Sequencer target",
        choices::seqTarget, 0));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        versioned (ids::seqLength, kSchemaV4), "Sequencer steps",
        1, RatioSequencer::kMaxSteps, 8));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::seqDivision, kSchemaV4), "Sequencer division",
        divisionNames(), 7));   // 1/16

    add (ids::seqGlide, "Sequencer glide", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV4);

    // Every step starts at 1.0 -- the ratio the operator would have had anyway,
    // so switching the sequencer on with an untouched pattern changes nothing
    // audible and the player hears the rhythm appear as they move steps.
    for (int i = 0; i < RatioSequencer::kMaxSteps; ++i)
        add (ids::step (i), "Step " + juce::String (i + 1),
             skewed (0.25f, 32.0f, 4.0f), 1.0f, {}, 2, kSchemaV4);

    // ---- F7, appended at schema 5 ------------------------------------------
    //
    // Thirty-two, and every default is a value the stage is SKIPPED at rather
    // than merely transparent at -- Split off, vowel mix 0, fold 0, 16 bits,
    // downsample 1, comb and phaser mix 0, drive 0, compressor ratio 1 (which
    // is `CompressorCore::isIdentity`). An F6 project is bit-identical.

    add (ids::split, "Split", skewed (0.0f, 800.0f, 140.0f), 0.0f, "Hz", 0, kSchemaV5);

    add (ids::vowelMix, "Vowel mix", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);
    add (ids::vowelMorph, "Vowel", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);
    add (ids::vowelTract, "Vowel tract", { 0.0f, 1.0f }, 0.5f, {}, 2, kSchemaV5);
    add (ids::vowelSharp, "Vowel sharpness", { 0.0f, 1.0f }, 0.5f, {}, 2, kSchemaV5);

    layout.add (std::make_unique<juce::AudioParameterBool> (
        versioned (ids::vowelSeqOn, kSchemaV5), "Vowel sequencer", false));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        versioned (ids::vowelSeqLength, kSchemaV5), "Vowel steps",
        1, RatioSequencer::kMaxSteps, 8));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        versioned (ids::vowelSeqDiv, kSchemaV5), "Vowel division", divisionNames(), 6));

    add (ids::vowelSeqGlide, "Vowel glide", { 0.0f, 1.0f }, 0.3f, {}, 2, kSchemaV5);

    for (int i = 0; i < RatioSequencer::kMaxSteps; ++i)
        add (ids::vowelStep (i), "Vowel step " + juce::String (i + 1),
             { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);

    add (ids::fold, "Fold", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);
    add (ids::crushBits, "Crush bits", { 2.0f, 16.0f }, 16.0f, "bit", 1, kSchemaV5);
    add (ids::crushAmount, "Crush", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);
    add (ids::downsample, "Downsample", skewed (1.0f, 64.0f, 8.0f), 1.0f, "x", 1, kSchemaV5);

    add (ids::combMix, "Comb mix", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);
    add (ids::combHz, "Comb", skewed (20.0f, 4000.0f, 220.0f), 220.0f, "Hz", 0, kSchemaV5);
    add (ids::combFeedback, "Comb feedback", { -0.95f, 0.95f }, 0.0f, {}, 2, kSchemaV5);

    add (ids::phaserMix, "Phaser mix", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);
    add (ids::phaserHz, "Phaser", skewed (60.0f, 8000.0f, 600.0f), 600.0f, "Hz", 0, kSchemaV5);
    add (ids::phaserFeedback, "Phaser feedback", { 0.0f, 0.95f }, 0.0f, {}, 2, kSchemaV5);

    add (ids::mangleDrive, "Drive", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV5);

    add (ids::compThreshold, "Comp threshold", { -48.0f, 0.0f }, 0.0f, "dB", 1, kSchemaV5);
    add (ids::compRatio, "Comp ratio", skewed (1.0f, 20.0f, 4.0f), 1.0f, ": 1", 2, kSchemaV5);
    add (ids::compAttack, "Comp attack", skewed (0.1f, 200.0f, 10.0f), 10.0f, "ms", 1, kSchemaV5);
    add (ids::compRelease, "Comp release",
         skewed (5.0f, 2000.0f, 150.0f), 120.0f, "ms", 0, kSchemaV5);
    add (ids::compMakeup, "Comp makeup", { 0.0f, 24.0f }, 0.0f, "dB", 1, kSchemaV5);

    // ---- F8, appended at schema 6 ------------------------------------------
    //
    // Two ADV envelopes, two LFOs, four macros and eight slots. Every slot's
    // source and destination default to Off, so the whole layer is skipped and
    // an F7 project is bit-identical.

    // Sixteen breakpoints each, and the defaults are a shape rather than a
    // flat line -- an envelope assigned to a slot and never edited should do
    // something audible, or the first thing anyone learns about the layer is
    // that it appears not to work.
    for (int e = 0; e < 2; ++e)
    {
        const juce::String label = "ADV " + juce::String (e + 1) + " \xc2\xb7 ";

        layout.add (std::make_unique<juce::AudioParameterInt> (
            versioned (ids::adv (e, "Points"), kSchemaV6), label + "Points",
            2, dsp::MultiEnvelope::kMaxPoints, 4));

        // Displayed 1-based, as on Sonitus's ADV page; the engine subtracts
        // one. **Without a sustain point an ADV envelope is a one-shot**, and
        // a held bass note is exactly what this instrument is for.
        layout.add (std::make_unique<juce::AudioParameterInt> (
            versioned (ids::adv (e, "Sustain"), kSchemaV6), label + "Sustain point",
            1, dsp::MultiEnvelope::kMaxPoints, 3));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            versioned (ids::adv (e, "Loop"), kSchemaV6), label + "Loop", false));

        layout.add (std::make_unique<juce::AudioParameterInt> (
            versioned (ids::adv (e, "LoopStart"), kSchemaV6), label + "Loop from",
            1, dsp::MultiEnvelope::kMaxPoints, 1));

        // Points past the fourth are flat, level-0 legs, so raising Points
        // adds time rather than a shape nobody asked for.
        constexpr float defaultSeconds[] { 0.004f, 0.18f, 0.05f, 0.25f,
                                           0.1f, 0.1f, 0.1f, 0.1f,
                                           0.1f, 0.1f, 0.1f, 0.1f,
                                           0.1f, 0.1f, 0.1f, 0.1f };
        constexpr float defaultLevel[]   { 1.0f, 0.45f, 0.45f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f };
        constexpr float defaultTension[] { 0.35f, 0.35f, 0.0f, 0.35f,
                                           0.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 0.0f, 0.0f };

        static_assert (static_cast<int> (std::size (defaultSeconds)) == dsp::MultiEnvelope::kMaxPoints);
        static_assert (static_cast<int> (std::size (defaultLevel)) == dsp::MultiEnvelope::kMaxPoints);
        static_assert (static_cast<int> (std::size (defaultTension)) == dsp::MultiEnvelope::kMaxPoints);

        for (int p = 0; p < dsp::MultiEnvelope::kMaxPoints; ++p)
        {
            const juce::String point = label + juce::String (p + 1) + " ";
            const auto i = static_cast<std::size_t> (p);

            add (ids::advPoint (e, p, "Time"), point + "time",
                 skewed (0.0f, 20.0f, 0.3f), defaultSeconds[i], "s", 3, kSchemaV6);

            add (ids::advPoint (e, p, "Level"), point + "level",
                 { 0.0f, 1.0f }, defaultLevel[i], {}, 2, kSchemaV6);

            add (ids::advPoint (e, p, "Tens"), point + "tension",
                 { -1.0f, 1.0f }, defaultTension[i], {}, 2, kSchemaV6);
        }
    }

    for (int l = 0; l < 2; ++l)
    {
        const juce::String label = "LFO " + juce::String (l + 1) + " \xc2\xb7 ";

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            versioned (ids::lfo (l, "Wave"), kSchemaV6), label + "Wave",
            choices::lfoWave, 0));

        add (ids::lfo (l, "Rate"), label + "Rate",
             skewed (0.01f, 40.0f, 2.0f), 2.0f, "Hz", 2, kSchemaV6);

        layout.add (std::make_unique<juce::AudioParameterBool> (
            versioned (ids::lfo (l, "Sync"), kSchemaV6), label + "Sync", false));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            versioned (ids::lfo (l, "Div"), kSchemaV6), label + "Division",
            divisionNames(), 6));

        add (ids::lfo (l, "Smooth"), label + "Smooth", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV6);
        add (ids::lfo (l, "Phase"), label + "Phase", { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV6);

        layout.add (std::make_unique<juce::AudioParameterBool> (
            versioned (ids::lfo (l, "Retrig"), kSchemaV6), label + "Retrigger", true));
    }

    for (int m = 0; m < kNumMacros; ++m)
        add (ids::macro (m), "Macro " + juce::String (m + 1),
             { 0.0f, 1.0f }, 0.0f, {}, 2, kSchemaV6);

    for (int s = 0; s < kNumSlots; ++s)
    {
        const juce::String label = "Mod " + juce::String (s + 1) + " \xc2\xb7 ";

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            versioned (ids::modSlot (s, "Src"), kSchemaV6), label + "Source",
            choices::modSources, 0));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            versioned (ids::modSlot (s, "Dst"), kSchemaV6), label + "Destination",
            choices::modDests, 0));

        add (ids::modSlot (s, "Amt"), label + "Amount",
             { -1.0f, 1.0f }, 0.0f, {}, 2, kSchemaV6);
    }

    return layout;
}

// ---------------------------------------------------------------------------

StrydaProcessor::StrydaProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output",
                                                          juce::AudioChannelSet::stereo(),
                                                          true)),
      state_ (*this, nullptr, "STRYDA", createParameterLayout())
{
    // Build the bandwidth predictor's order tables here, on the thread that
    // constructs the plugin, rather than leaving them to whichever thread
    // happens to ask first. They cost about 80 ms once per process and are
    // shared by every instance; `prepareToPlay` is not guaranteed to be off the
    // audio thread in every host, and 80 ms there is a dropout.
    (void) dsp::fm::significantOrder (1.0);
    (void) dsp::fm::feedbackOrder (0.5);
}

bool StrydaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
             && layouts.getMainInputChannelSet().isDisabled();
}

void StrydaProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    engine_.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    scratch_.setSize (2, juce::jmax (1, maximumExpectedSamplesPerBlock), false, false, true);

    prepared_ = true;
    reportedLatency_ = -1;
}

void StrydaProcessor::pullParameters()
{
    for (int op = 0; op < kNumOperators; ++op)
    {
        auto& settings = parameters_.operators[static_cast<std::size_t> (op)];

        settings.ratio = raw (ids::op (op, "Ratio"));
        settings.fineCents = raw (ids::op (op, "Fine"));
        settings.character = raw (ids::op (op, "Character"));
        settings.level = raw (ids::op (op, "Level"));
        settings.pan = raw (ids::op (op, "Pan"));
        settings.feedback = raw (ids::op (op, "Feedback"));

        settings.attack = raw (ids::op (op, "Attack"));
        settings.decay = raw (ids::op (op, "Decay"));
        settings.sustain = raw (ids::op (op, "Sustain"));
        settings.release = raw (ids::op (op, "Release"));

        settings.fold = raw (ids::op (op, "Fold"));
        settings.mode = static_cast<int> (std::lround (raw (ids::op (op, "Mode"))));
        settings.formantHz = raw (ids::op (op, "Formant"));
        settings.formantDepth = raw (ids::op (op, "Width"));

        settings.keyBreak = raw (ids::op (op, "KeyBreak"));
        settings.keyLeft = raw (ids::op (op, "KeyLeft"));
        settings.keyRight = raw (ids::op (op, "KeyRight"));

        settings.velLevel = raw (ids::op (op, "VelLevel"));
        settings.velIndex = raw (ids::op (op, "VelIndex"));

        settings.ratioMode = static_cast<int> (std::lround (raw (ids::op (op, "RatioMode"))));

        for (int from = 0; from < kNumOperators; ++from)
            parameters_.indices[static_cast<std::size_t> (op)][static_cast<std::size_t> (from)]
                = op == from ? 0.0 : raw (ids::cell (op, from));

        parameters_.noiseIndices[static_cast<std::size_t> (op)] = raw (ids::noise (op));
    }

    parameters_.masterLevel = juce::Decibels::decibelsToGain (raw (ids::master), -60.0f);

    // Off / Soft / Hard, as the amount the prediction is leaned on.
    const int cap = static_cast<int> (std::lround (raw (ids::indexCap)));
    parameters_.indexCap = cap == 0 ? 0.0 : (cap == 1 ? 0.6 : 1.0);

    auto& extras = parameters_.extras;

    extras.cutoffHz = raw (ids::filterCutoff);
    extras.resonance = raw (ids::filterReso);
    extras.morph = raw (ids::filterMorph);
    extras.keyTrack = raw (ids::filterKeyTrack);
    extras.envAmount = raw (ids::filterEnv);
    extras.drive = raw (ids::filterDrive);
    extras.sing = raw (ids::filterSing);

    extras.filterAttack = raw (ids::filterAttack);
    extras.filterDecay = raw (ids::filterDecay);
    extras.filterSustain = raw (ids::filterSustain);
    extras.filterRelease = raw (ids::filterRelease);

    extras.subLevel = raw (ids::subLevel);

    // The choice stores an index into `choices::subOctave`, which is frozen:
    // 0 is two octaves down, 1 is one, 2 is the note. Mapping it here rather
    // than storing the octave itself is what lets the list grow later.
    extras.subOctave = static_cast<int> (std::lround (raw (ids::subOctave))) - 2;
    extras.subShape = static_cast<int> (std::lround (raw (ids::subShape)));

    extras.subAttack = raw (ids::subAttack);
    extras.subDecay = raw (ids::subDecay);
    extras.subSustain = raw (ids::subSustain);
    extras.subRelease = raw (ids::subRelease);

    extras.unisonCount = static_cast<int> (std::lround (raw (ids::unison)));
    extras.unisonDetuneCents = raw (ids::unisonDetune);
    extras.unisonSpread = raw (ids::unisonSpread);
    extras.unisonIndexSpread = raw (ids::unisonIndex);

    auto& sequencer = engine_.getSequencer();

    sequencer.setEnabled (raw (ids::seqOn) > 0.5f);

    // Index 0 of the target list is "Off", so the operator index is one less --
    // which is how a destination-less state is spelt without a magic number.
    sequencer.setTarget (static_cast<int> (std::lround (raw (ids::seqTarget))) - 1);
    sequencer.setLength (static_cast<int> (std::lround (raw (ids::seqLength))));
    sequencer.setGlide (raw (ids::seqGlide));

    engine_.setSequencerDivision (static_cast<int> (std::lround (raw (ids::seqDivision))));

    for (int i = 0; i < RatioSequencer::kMaxSteps; ++i)
        sequencer.setStep (i, raw (ids::step (i)));

    MangleParameters mangle;

    mangle.splitHz = raw (ids::split);

    mangle.vowelMix = raw (ids::vowelMix);
    mangle.vowelMorph = raw (ids::vowelMorph);
    mangle.vowelTract = raw (ids::vowelTract);
    mangle.vowelSharpness = raw (ids::vowelSharp);

    mangle.fold = raw (ids::fold);
    mangle.crushBits = raw (ids::crushBits);
    mangle.crushAmount = raw (ids::crushAmount);
    mangle.downsample = raw (ids::downsample);

    mangle.combMix = raw (ids::combMix);
    mangle.combHz = raw (ids::combHz);
    mangle.combFeedback = raw (ids::combFeedback);

    mangle.phaserMix = raw (ids::phaserMix);
    mangle.phaserHz = raw (ids::phaserHz);
    mangle.phaserFeedback = raw (ids::phaserFeedback);

    mangle.drive = raw (ids::mangleDrive);

    mangle.compressThresholdDb = raw (ids::compThreshold);
    mangle.compressRatio = raw (ids::compRatio);
    mangle.compressAttackMs = raw (ids::compAttack);
    mangle.compressReleaseMs = raw (ids::compRelease);
    mangle.compressMakeupDb = raw (ids::compMakeup);

    engine_.setMangleParameters (mangle);

    engine_.setVowelSequence (raw (ids::vowelSeqOn) > 0.5f,
                              static_cast<int> (std::lround (raw (ids::vowelSeqLength))),
                              static_cast<int> (std::lround (raw (ids::vowelSeqDiv))),
                              raw (ids::vowelSeqGlide));

    for (int i = 0; i < RatioSequencer::kMaxSteps; ++i)
        engine_.setVowelStep (i, raw (ids::vowelStep (i)));

    auto& modulation = parameters_.modulation;

    for (int e = 0; e < 2; ++e)
    {
        auto& shape = modulation.envelopes[static_cast<std::size_t> (e)];

        shape.pointCount = static_cast<int> (std::lround (raw (ids::adv (e, "Points"))));
        shape.loop = raw (ids::adv (e, "Loop")) > 0.5f;

        // Both displayed 1-based; the envelope counts from zero.
        shape.sustain = static_cast<int> (std::lround (raw (ids::adv (e, "Sustain")))) - 1;
        shape.loopStart = static_cast<int> (std::lround (raw (ids::adv (e, "LoopStart")))) - 1;

        for (int p = 0; p < dsp::MultiEnvelope::kMaxPoints; ++p)
            shape.points[static_cast<std::size_t> (p)] = {
                raw (ids::advPoint (e, p, "Time")),
                raw (ids::advPoint (e, p, "Level")),
                raw (ids::advPoint (e, p, "Tens"))
            };
    }

    for (int l = 0; l < 2; ++l)
    {
        auto& shape = modulation.lfos[static_cast<std::size_t> (l)];

        shape.wave = static_cast<int> (std::lround (raw (ids::lfo (l, "Wave"))));
        shape.synced = raw (ids::lfo (l, "Sync")) > 0.5f;
        shape.division = static_cast<int> (std::lround (raw (ids::lfo (l, "Div"))));
        shape.smooth = raw (ids::lfo (l, "Smooth"));
        shape.phaseOffset = raw (ids::lfo (l, "Phase"));
        shape.retrigger = raw (ids::lfo (l, "Retrig")) > 0.5f;

        // A synced LFO's rate is the division at the session's tempo, resolved
        // here rather than in the voice so every voice agrees about it.
        shape.rateHz = shape.synced ? dsp::divisionRateHz (shape.division, lastBpm_)
                                    : raw (ids::lfo (l, "Rate"));
    }

    for (int m = 0; m < kNumMacros; ++m)
        modulation.macros[static_cast<std::size_t> (m)] = raw (ids::macro (m));

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& slot = modulation.slots[static_cast<std::size_t> (s)];

        slot.source = static_cast<int> (std::lround (raw (ids::modSlot (s, "Src"))));
        slot.destination = static_cast<int> (std::lround (raw (ids::modSlot (s, "Dst"))));
        slot.amount = raw (ids::modSlot (s, "Amt"));
    }

    engine_.setPolyphony (static_cast<int> (std::lround (raw (ids::polyphony))));
    engine_.setOversamplingMode (static_cast<dsp::OversamplingMode> (
        static_cast<int> (std::lround (raw (ids::oversampling)))));
    engine_.setRenderOversampling (static_cast<dsp::RenderOversampling> (
        static_cast<int> (std::lround (raw (ids::renderOversampling)))));
}

void StrydaProcessor::handleMidi (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        lastNote_ = message.getNoteNumber();
        engine_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        engine_.noteOff (message.getNoteNumber());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        engine_.allNotesOff();
    }
}

template <typename FloatType>
void StrydaProcessor::processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (numSamples <= 0 || ! prepared_)
        return;

    collectTuning();

    // **The transport first**, because `pullParameters` resolves a synced
    // LFO's rate from the tempo: reading it afterwards would leave every
    // synced LFO one block behind the session.
    //
    // Once per block, so the ratio sequencer locks to the bar. The engine
    // keeps its rate between anchors and free-runs, which is what
    // makes the step edges sample-accurate inside the block rather than only
    // at its head -- and a growl that is not in time with the drums is a
    // mistake, not a texture.
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = false;

    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            ppq = position->getPpqPosition().orFallback (0.0);
            bpm = position->getBpm().orFallback (120.0);
            playing = position->getIsPlaying();
        }

    lastBpm_ = bpm;
    engine_.setTransport (ppq, bpm, playing);

    // **Before `setParameters`, not after.** The ratio modes are resolved
    // against the scale inside `setParameters`, so a tuning that arrived this
    // block has to be in place first or the ratios spend one block snapped to
    // the previous scale.
    pullParameters();
    engine_.setParameters (parameters_);
    engine_.updateFactor (isNonRealtime());

    if (scratch_.getNumSamples() < numSamples)
        scratch_.setSize (2, numSamples, false, false, true);

    double* const engineLeft = scratch_.getWritePointer (0);
    double* const engineRight = scratch_.getWritePointer (1);

    // Sample-accurate MIDI: the render is cut at every event, so a note lands
    // where it was played. The engine cuts again at its own control grid, which
    // is why 64- and 512-sample blocks are bit-identical.
    int rendered = 0;

    const auto renderSpan = [&] (int from, int count)
    {
        engine_.process (engineLeft, engineRight, count);

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

    activeVoices_.store (engine_.getActiveVoiceCount());

    // The bandwidth readout, computed once per block on the note last played --
    // never per sample, and never on the message thread, which cannot see the
    // engine's current tuning.
    {
        dsp::FmBandwidth bandwidth;
        bandwidth.setOperatorCount (kNumOperators);

        const double fundamental = engine_.getTuning().frequencyFor (lastNote_);

        for (int op = 0; op < kNumOperators; ++op)
        {
            const auto& settings = parameters_.operators[static_cast<std::size_t> (op)];
            bandwidth.setOperatorFrequency (op, fundamental * settings.ratio);
            bandwidth.setFeedback (op, settings.feedback);

            for (int from = 0; from < kNumOperators; ++from)
                bandwidth.setIndex (from, op,
                                    parameters_.indices[static_cast<std::size_t> (op)]
                                                       [static_cast<std::size_t> (from)]);
        }

        predictedTopHz_.store (bandwidth.topSidebandHz());
        internalNyquist_.store (0.5 * engine_.getInternalRate());
    }

    const int latency = engine_.getLatencySamples();

    if (latency != reportedLatency_)
    {
        reportedLatency_ = latency;
        setLatencySamples (latency);
    }
}

void StrydaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

void StrydaProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processInternal (buffer, midi);
}

// ---------------------------------------------------------------------------
// Tooltips that read the live state
// ---------------------------------------------------------------------------

juce::String StrydaProcessor::describeOversampling() const
{
    const int mode = static_cast<int> (std::lround (raw (ids::oversampling)));
    const int factor = engine_.getFactor();
    const double internal = engine_.getInternalRate();

    const juce::String latency = factor > 1
        ? " Latency " + juce::String (engine_.getLatencySamples()) + " samples, compensated."
        : juce::String (" No latency.");

    if (mode == 0)
    {
        juce::String text = "Auto -- your session is at " + rateText (sampleRate_) + ", so this is ";

        text += factor == 1 ? juce::String ("off: the headroom is already there.")
                            : "running x" + juce::String (factor) + ", giving "
                                + rateText (internal) + " internally.";

        text += " It matters more here than anywhere else in the suite: measured, a two-operator "
                "pair at index 16 and ratio 7 reads +2.6 dB of aliasing at the host rate and "
                "-114.6 dB at x4.";

        return text + latency;
    }

    return "x" + juce::String (factor) + " -- " + rateText (internal) + " internally, "
             + juce::String (factor) + " times the CPU of Off. Auto would pick x"
             + juce::String (dsp::autoOversamplingFactor (sampleRate_)) + " here." + latency;
}

juce::String StrydaProcessor::describeRenderQuality() const
{
    const int render = static_cast<int> (std::lround (raw (ids::renderOversampling)));

    if (render == 0)
        return "Same as live -- a bounce is the session graph, bit for bit. Change this and an "
               "offline render can afford a factor the session could not.";

    return "An offline render uses this instead of the live setting, and the latency is "
           "re-declared to match. Playback goes back to the live setting.";
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

const std::vector<Preset>& StrydaProcessor::getPresets()
{
    static const std::vector<Preset> presets = [] {
        std::vector<Preset> list;

        list.push_back ({
            "Init",
            "One operator, one sine, nothing else. This is the neutral patch and it is "
            "bit-exactly a sine wave -- the same numbers std::sin would give you, asserted "
            "in the tests. Start here and turn one matrix cell up.\n\n"
            "To learn the instrument: set 2 -> 1 to about 1.0 and sweep Op 2's Ratio. "
            "Integer ratios fuse into one tone; 4.76 is a bell.",
            {} });

        list.push_back ({
            "Neuro Growl",
            "The reason this plugin exists. Op 2 modulates Op 1 hard at a ratio of 2, with "
            "Op 3 modulating Op 2 on top -- a three-deep stack, which is what gives a growl "
            "its edge rather than its brightness.\n\n"
            "Automate Op 2's Character: at 0 the index steps through the Bessel nulls and "
            "the timbre flickers; at 1 it opens like a filter. Automate 2 -> 1 in sixteenths "
            "against the drums. Op 1's Feedback adds teeth without another operator.\n\n"
            "The sub lane is doing the work below 100 Hz and goes through none of it -- not "
            "the matrix, not the filter. That is why the growl can bite as hard as you like "
            "without the low end going with it. Three copies of unison with a little index "
            "spread give the top a reese that moves; turn Unison INDEX up and the copies "
            "start differing in timbre rather than only in pitch.",
            { { ids::op (1, "Ratio"), 2.0f },
              { ids::op (2, "Ratio"), 3.0f },
              { ids::cell (0, 1), 1.2f },
              { ids::cell (1, 2), 0.7f },
              { ids::op (0, "Feedback"), 0.18f },
              { ids::op (1, "Character"), 0.55f },
              { ids::op (0, "Decay"), 6.0f },
              { ids::op (1, "Decay"), 2.0f },
              { ids::op (1, "Sustain"), 0.55f },
              { ids::op (2, "Decay"), 0.9f },
              { ids::op (2, "Sustain"), 0.3f },
              { ids::subLevel, 0.55f },
              { ids::subOctave, 1.0f },
              { ids::filterCutoff, 4200.0f },
              { ids::filterMorph, 0.0f },
              { ids::filterEnv, 1.6f },
              { ids::filterDecay, 0.35f },
              { ids::filterSustain, 0.35f },
              { ids::unison, 3.0f },
              { ids::unisonDetune, 11.0f },
              { ids::unisonSpread, 0.7f },
              { ids::unisonIndex, 0.35f },
              { ids::master, -4.0f } } });

        list.push_back ({
            "Bell",
            "Chowning's own territory: an inharmonic carrier-to-modulator ratio, a modulator "
            "that decays faster than the carrier, and nothing else. The ratio 1 : 3.5 is what "
            "makes it a bell rather than a horn -- rational, so the spectrum is harmonic, but "
            "with the fundamental missing.\n\n"
            "Turn Op 2's Character up and listen to the decay: at 0 the partials break into "
            "dashes as each Bessel term crosses zero; at 1 they fall away in order. That is "
            "the whole difference between the two techniques, and it is most audible here.",
            { { ids::op (1, "Ratio"), 3.5f },
              { ids::cell (0, 1), 1.8f },
              { ids::op (0, "Decay"), 4.0f },
              { ids::op (0, "Sustain"), 0.0f },
              { ids::op (1, "Decay"), 1.2f },
              { ids::op (1, "Sustain"), 0.0f },
              { ids::master, -3.0f } } });

        list.push_back ({
            "Sub Stack",
            "A bass that stays a bass. Op 1 carries alone at ratio 1 with a long decay; Op 2 "
            "sits at ratio 1 as well, so every sideband lands on a harmonic of the note and "
            "nothing lands below it.\n\n"
            "This is the patch to check the Index cap on: play it four octaves up with the cap "
            "Off and then on, and the readout tells you what it had to do. Hold the modulation "
            "index below about 2 and it will never bite at all.\n\n"
            "And it is where to hear what the sub lane is for. Pull SUB LEVEL to zero and back "
            "up with the growl presets loud in the same session: the lane is one oscillator "
            "that nothing in the instrument can touch, so the fundamental stays exactly where "
            "you put it however hard the operators are driven. One octave down on a triangle "
            "is the setting here -- the triangle's quiet odd harmonics are what makes a sub "
            "audible on a speaker that cannot reproduce the fundamental at all.",
            { { ids::op (1, "Ratio"), 1.0f },
              { ids::cell (0, 1), 1.1f },
              { ids::op (1, "Character"), 1.0f },
              { ids::op (0, "Decay"), 8.0f },
              { ids::op (0, "Sustain"), 0.9f },
              { ids::op (1, "Decay"), 3.0f },
              { ids::op (1, "Sustain"), 0.5f },
              { ids::subLevel, 0.7f },
              { ids::subOctave, 1.0f },
              { ids::subShape, 1.0f },
              { ids::subDecay, 8.0f },
              { ids::master, -2.0f } } });

        return list;
    }();

    return presets;
}

int StrydaProcessor::getNumPrograms() { return static_cast<int> (getPresets().size()); }

const juce::String StrydaProcessor::getProgramName (int index)
{
    const auto& presets = getPresets();
    return juce::isPositiveAndBelow (index, static_cast<int> (presets.size()))
             ? juce::String (presets[static_cast<std::size_t> (index)].name)
             : juce::String();
}

void StrydaProcessor::setCurrentProgram (int index)
{
    const auto& presets = getPresets();

    if (! juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        return;

    currentProgram_ = index;

    // Every preset starts from the defaults, so a setting a preset does not
    // mention is the default rather than whatever the last patch left there.
    for (const auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (
                const_cast<juce::AudioProcessorParameter*> (parameter)))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& setting : presets[static_cast<std::size_t> (index)].settings)
        if (auto* parameter = state_.getParameter (setting.id))
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (setting.value));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void StrydaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state_.copyState();
    tree.setProperty ("schemaVersion", kStateSchemaVersion, nullptr);
    tree.setProperty ("tooltips", tooltipsEnabled_, nullptr);

    // The scale travels with the project as .scl text, so a session opened on
    // another machine does not need the file that made it.
    tree.setProperty (kScaleNameProperty, scaleName_, nullptr);
    tree.setProperty (kScaleTextProperty, scalaText_, nullptr);
    tree.setProperty (kKeyboardMapProperty, keyboardMapText_, nullptr);
    tree.setProperty (kConcertPitchProperty, concertPitchHz_, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void StrydaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state_.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            tooltipsEnabled_ = tree.getProperty ("tooltips", true);

            concertPitchHz_ = tree.getProperty (kConcertPitchProperty, 440.0);

            const juce::String scaleText = tree.getProperty (kScaleTextProperty, juce::String());
            const juce::String name = tree.getProperty (kScaleNameProperty, juce::String());

            if (scaleText.isNotEmpty())
                loadScalaText (scaleText, name);
            else if (name.isNotEmpty())
                selectBuiltInScale (name);
            else
                resetTuning();

            const juce::String mapText = tree.getProperty (kKeyboardMapProperty, juce::String());

            if (mapText.isNotEmpty())
                loadKeyboardMapText (mapText);

            publishTuning();
            state_.replaceState (tree);
        }
}

// ---------------------------------------------------------------------------
// The tuning
// ---------------------------------------------------------------------------
//
// Message-thread side, handed to the audio thread under a spin lock -- the
// pattern Malleus established and Ictus follows. What is new here is what the
// engine does with it: `resolveRatio` snaps an operator's RATIO to the scale's
// degrees, so the whole sideband ladder lands in the session's tuning at every
// key rather than only the notes doing so.

void StrydaProcessor::publishTuning()
{
    const juce::SpinLock::ScopedLockType lock (tuningLock_);

    pendingScale_ = scale_;
    pendingMap_ = hasKeyboardMap_ ? keyboardMap_ : dsp::KeyboardMap {};
    pendingConcertHz_ = concertPitchHz_;

    tuningPending_.store (true, std::memory_order_release);
}

void StrydaProcessor::collectTuning() noexcept
{
    if (! tuningPending_.load (std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock (tuningLock_);

    // A try-lock, not a lock: the audio thread never waits on the message
    // thread. Missing a swap costs one block of the old tuning and the flag
    // stays set, so the next block takes it.
    if (! lock.isLocked())
        return;

    auto& tuning = engine_.getTuning();

    tuning.setScale (pendingScale_);
    tuning.setKeyboardMap (pendingMap_);
    tuning.setConcertPitch (pendingConcertHz_);

    tuningPending_.store (false, std::memory_order_release);
}

juce::String StrydaProcessor::loadScalaText (const juce::String& text,
                                             const juce::String& name)
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

juce::String StrydaProcessor::loadKeyboardMapText (const juce::String& text)
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

juce::String StrydaProcessor::selectBuiltInScale (const juce::String& name)
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

void StrydaProcessor::resetTuning()
{
    scale_ = dsp::scales::twelveToneEqual();
    scaleName_ = scale_.name;
    scalaText_.clear();
    keyboardMap_ = {};
    keyboardMapText_.clear();
    hasKeyboardMap_ = false;

    publishTuning();
}

void StrydaProcessor::setConcertPitch (double hz)
{
    concertPitchHz_ = std::clamp (hz, dsp::Tuning::kMinimumConcertHz,
                                  dsp::Tuning::kMaximumConcertHz);
    publishTuning();
}

double StrydaProcessor::getRootHz() const noexcept
{
    dsp::Tuning preview;
    preview.setScale (scale_);
    preview.setConcertPitch (concertPitchHz_);

    return preview.frequencyFor (preview.getRootNote());
}

juce::String StrydaProcessor::describeTuning() const
{
    return scaleName_ + "  --  " + juce::String (scale_.ratios.size())
             + " degrees, A4 = " + juce::String (concertPitchHz_, 2) + " Hz";
}

double StrydaProcessor::resolvedRatio (int op) const
{
    if (op < 0 || op >= kNumOperators)
        return 1.0;

    const auto value = [this] (const juce::String& id)
    {
        const auto* parameter = state_.getRawParameterValue (id);
        return parameter != nullptr ? static_cast<double> (parameter->load()) : 0.0;
    };

    const auto mode = static_cast<RatioMode> (
        static_cast<int> (std::lround (value (ids::op (op, "RatioMode")))));

    return resolveRatio (value (ids::op (op, "Ratio")), mode, scale_);
}

void StrydaProcessor::applyBraid (int index)
{
    if (index < 0 || index >= braids::kCount)
        return;

    const auto& braid = braids::table()[static_cast<std::size_t> (index)];

    const auto set = [this] (const juce::String& id, float value)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (value));
    };

    for (int to = 0; to < kNumOperators; ++to)
    {
        for (int from = 0; from < kNumOperators; ++from)
        {
            const auto amount = static_cast<float> (
                braid.cells[static_cast<std::size_t> (to)][static_cast<std::size_t> (from)]);

            // The diagonal is the operator's own feedback, which is the same
            // idea as a cell but a different parameter -- OperatorMatrix keeps
            // it on `Oscillator::setFeedback` because the recursion is
            // bounded there and nowhere else.
            if (to == from)
                set (ids::op (to, "Feedback"), amount);
            else
                set (ids::cell (to, from), amount);
        }

        set (ids::op (to, "Level"),
             static_cast<float> (braid.levels[static_cast<std::size_t> (to)]));
    }
}

juce::AudioProcessorEditor* StrydaProcessor::createEditor()
{
    return new StrydaEditor (*this);
}

} // namespace tezla::stryda

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tezla::stryda::StrydaProcessor();
}
