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
/// Schema 4: Note snap on the kick and the snare.
constexpr int kSchemaV4 = 4;
/// Schema 5: the ghost snare on the second snare pad, with its LINK.
constexpr int kSchemaV5 = 5;

/// Schema 6: the hats and the clap (I4).
constexpr int kSchemaV6 = 6;

/// Schema 7: the depth the first hats and clap did not have (I4.1).
constexpr int kSchemaV7 = 7;

/// Schema 8: the clap's gate, which every other pad already had.
constexpr int kSchemaV8 = 8;

/// Schema 9: a hold on the snares' wires.
constexpr int kSchemaV9 = 9;

/// Schema 10: the hats' Plate and Grit (I4.3, the rig's "thin and tinny").
constexpr int kSchemaV10 = 10;

/// Schema 11: the rig's fourth round (I4.4) -- the hiss's tilt, attack and
/// grain, the open pad's own hold, the kick's Under and Knock, the snares'
/// Ring and Thump, and a Pan per pad.
constexpr int kSchemaV11 = 11;

/// Schema 12: the rig's fifth round (I4.5) -- the pads in the field for real
/// (the hiss, the metal, the wires and the bursts spread; Width and Mono
/// below per pad), a room on four pads, the plate's wash, the head's ratios,
/// the wires' colour, the rattle's own decay, tone and tension, the drop's
/// curve, and the clap layered under the snare.
constexpr int kSchemaV12 = 12;

/// Schema 13: the output buses (I7) -- an Output choice per pad, Main by
/// default, so the kit stays on the one output it always had until told.
constexpr int kSchemaV13 = 13;
constexpr int kStateSchemaVersion = kSchemaV13;

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
                // The ghost: the same drum, a lighter, shorter stroke --
                // mostly wire, a little crack, well under the main hit.
                { ids::g1Decay, 90.0f },
                { ids::g1Start, 3.0f },
                { ids::g1Drop, 8.0f },
                { ids::g1Body, 45.0f },
                { ids::g1Wires, 85.0f },
                { ids::g1WiresDecay, 70.0f },
                { ids::g1Rattle, 20.0f },
                { ids::g1Crack, 30.0f },
                { ids::g1Level, 55.0f },
                // The hats: tight and bright, a short closed and a medium open,
                // a little air so the sixteenths breathe.
                { ids::htTune, 240.0f },
                { ids::htHarmonics, 0.0f },
                { ids::htSpread, 20.0f },
                { ids::htColour, 5200.0f },
                { ids::htAir, 25.0f },
                { ids::hcDecay, 38.0f },
                { ids::hoDecay, 320.0f },
                { ids::htLevel, 55.0f },
                { ids::htRing, 30.0f },
                { ids::htDrive, 20.0f },
                { ids::htSizzle, 55.0f },
                { ids::htAir, 40.0f },
                { ids::htAirTone, 6500.0f },
                { ids::htAirDecay, 80.0f },
                { ids::htWidth, 55.0f },
                { ids::htHighpass, 1800.0f },
                { ids::htDamp, 45.0f },
                { ids::htStrike, 55.0f },
                // The clap, layered under the snare: quick flam, mid colour,
                // a little body so it has a pitch of its own under the snare.
                { ids::cpFlam, 9.0f },
                { ids::cpBursts, 4.0f },
                { ids::cpSkew, -20.0f },
                { ids::cpSnap, 3.0f },
                { ids::cpColour, 1500.0f },
                { ids::cpWidth, 55.0f },
                { ids::cpBody, 25.0f },
                { ids::cpBodyPitch, 1100.0f },
                { ids::cpBodyRing, 45.0f },
                { ids::cpTail, 120.0f },
                { ids::cpTailTone, 65.0f },
                { ids::cpLevel, 55.0f },
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
                // The ghost: softer and shorter, the wires still rattling
                // with the shell.
                { ids::g1Decay, 150.0f },
                { ids::g1Body, 55.0f },
                { ids::g1Wires, 70.0f },
                { ids::g1WiresDecay, 110.0f },
                { ids::g1Rattle, 40.0f },
                { ids::g1Level, 50.0f },
                // The hats: darker and looser, out of the sub's way.
                { ids::htTune, 180.0f },
                { ids::htHarmonics, 1.0f },
                { ids::htSpread, 45.0f },
                { ids::htColour, 3200.0f },
                { ids::htAir, 40.0f },
                { ids::hcDecay, 70.0f },
                { ids::hoDecay, 700.0f },
                { ids::htLevel, 50.0f },
                { ids::htRing, 45.0f },
                { ids::htDrive, 35.0f },
                { ids::htSizzle, 75.0f },
                { ids::htAir, 55.0f },
                { ids::htAirTone, 3500.0f },
                { ids::htAirDecay, 160.0f },
                { ids::htWidth, 75.0f },
                { ids::htHighpass, 900.0f },
                { ids::htDamp, 65.0f },
                { ids::htStrike, 30.0f },
                { ids::htHold, 12.0f },
                { ids::cpFlam, 14.0f },
                { ids::cpBursts, 5.0f },
                { ids::cpSnap, 6.0f },
                { ids::cpColour, 900.0f },
                { ids::cpWidth, 70.0f },
                { ids::cpNoiseTone, 500.0f },
                { ids::cpBody, 35.0f },
                { ids::cpBodyPitch, 600.0f },
                { ids::cpBodyRing, 90.0f },
                { ids::cpTail, 260.0f },
                { ids::cpTailTone, 55.0f },
                { ids::cpLevel, 50.0f },
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
                // The ghost: the tightest of them, nearly all wire.
                { ids::g1Decay, 70.0f },
                { ids::g1Start, 5.0f },
                { ids::g1Drop, 6.0f },
                { ids::g1Body, 35.0f },
                { ids::g1Wires, 90.0f },
                { ids::g1WiresDecay, 50.0f },
                { ids::g1Crack, 40.0f },
                { ids::g1Noise, 30.0f },
                { ids::g1Level, 60.0f },
                // The hats: trashy and wide, the ratio set morphed past Bell
                // towards Trash, so the sixteenths have some grit in them.
                { ids::htTune, 300.0f },
                { ids::htHarmonics, 1.7f },
                { ids::htSpread, 70.0f },
                { ids::htColour, 6800.0f },
                { ids::htAir, 15.0f },
                { ids::hcDecay, 30.0f },
                { ids::hoDecay, 240.0f },
                { ids::htLevel, 60.0f },
                { ids::htVelColour, 70.0f },
                { ids::htRing, 65.0f },
                { ids::htDrive, 55.0f },
                { ids::htSizzle, 40.0f },
                { ids::htAir, 35.0f },
                { ids::htAirTone, 8000.0f },
                { ids::htAirDecay, 60.0f },
                { ids::htWidth, 35.0f },
                { ids::htHighpass, 2600.0f },
                { ids::htDamp, 25.0f },
                { ids::htStrike, 75.0f },
                { ids::htShape, 35.0f },
                // The clap: six hands, spreading, and a long room.
                { ids::cpFlam, 18.0f },
                { ids::cpBursts, 6.0f },
                { ids::cpSkew, 45.0f },
                { ids::cpSnap, 2.0f },
                { ids::cpColour, 1800.0f },
                { ids::cpWidth, 40.0f },
                { ids::cpNoiseTone, 1600.0f },
                { ids::cpBody, 15.0f },
                { ids::cpDrive, 30.0f },
                { ids::cpTail, 300.0f },
                { ids::cpTailTone, 85.0f },
                { ids::cpLevel, 60.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The lush end of the hats, and the reason the ring modulator and
            // the sizzle bank exist: a long open hat with the noise rung
            // through the metal's own partials, damped so it falls from
            // bright to dark, gated so a key lift ends it. Appended --
            // presets are recalled by index.
            "Lush Hats",
            {
                { ids::htTune, 320.0f },
                { ids::htHarmonics, 0.8f },
                { ids::htSpread, 55.0f },
                { ids::htRing, 70.0f },
                { ids::htDrive, 45.0f },
                { ids::htAir, 70.0f },
                { ids::htAirTone, 4000.0f },
                { ids::htAirDecay, 200.0f },
                { ids::htSizzle, 85.0f },
                { ids::htColour, 4200.0f },
                { ids::htWidth, 90.0f },
                { ids::htHighpass, 700.0f },
                { ids::htDamp, 70.0f },
                { ids::htStrike, 45.0f },
                { ids::htHold, 20.0f },
                { ids::hcDecay, 110.0f },
                { ids::hoDecay, 1400.0f },
                { ids::htGate, 1.0f },
                { ids::htRelease, 90.0f },
                { ids::htLevel, 75.0f },
                // A wide, soft clap under it, mostly body.
                { ids::cpBursts, 5.0f },
                { ids::cpFlam, 16.0f },
                { ids::cpSkew, -35.0f },
                { ids::cpSnap, 8.0f },
                { ids::cpNoise, 70.0f },
                { ids::cpNoiseTone, 400.0f },
                { ids::cpBody, 55.0f },
                { ids::cpBodyPitch, 700.0f },
                { ids::cpBodyRing, 140.0f },
                { ids::cpColour, 1000.0f },
                { ids::cpWidth, 85.0f },
                { ids::cpTail, 420.0f },
                { ids::cpTailTone, 50.0f },
                { ids::cpLevel, 65.0f },
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
        // -------------------------------------------------------------------
        {
            // The chunky hat the rig asked for (I4.3): the plate nearly all
            // the way, a little of the six pulses left in for sheen, the
            // steps of a six-bit sample path, the bands opened and dropped so
            // the body gets through, a hard stick, a short closed and a
            // medium open. Appended -- presets are recalled by index.
            "Fat Hats",
            {
                { ids::htPlate, 85.0f },
                { ids::htGrit, 55.0f },
                { ids::htTune, 250.0f },
                { ids::htHarmonics, 0.0f },
                { ids::htSpread, 35.0f },
                { ids::htRing, 20.0f },
                { ids::htDrive, 40.0f },
                { ids::htAir, 55.0f },
                { ids::htAirTone, 2500.0f },
                { ids::htAirDecay, 90.0f },
                { ids::htSizzle, 60.0f },
                { ids::htColour, 1800.0f },
                { ids::htWidth, 100.0f },
                { ids::htHighpass, 350.0f },
                { ids::htDamp, 50.0f },
                { ids::htStrike, 65.0f },
                { ids::htHold, 4.0f },
                { ids::hcDecay, 60.0f },
                { ids::hoDecay, 450.0f },
                { ids::htLevel, 85.0f },
                { ids::htVelColour, 30.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The layering trick built in (I4.4): a punchy 48 Hz kick with a
            // clean octave-down sub that blooms in behind it and outlives it,
            // and the beater's knock under the click. Appended -- presets are
            // recalled by index.
            "Sub Kick",
            {
                { ids::k1Tune, 48.0f },
                { ids::k1Start, 24.0f },
                { ids::k1Drop, 25.0f },
                { ids::k1Sigh, 1.0f },
                { ids::k1Harmonics, 25.0f },
                { ids::k1Even, 70.0f },
                { ids::k1Click, 35.0f },
                { ids::k1ClickTone, 2500.0f },
                { ids::k1Noise, 15.0f },
                { ids::k1Decay, 380.0f },
                { ids::k1Shape, 10.0f },
                { ids::k1Under, 70.0f },
                { ids::k1UnderInterval, 12.0f },
                { ids::k1UnderDecay, 160.0f },
                { ids::k1UnderAttack, 12.0f },
                { ids::k1Knock, 45.0f },
                { ids::k1KnockTone, 300.0f },
                { ids::k1KnockTime, 30.0f },
                { ids::k1Level, 60.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // A snare with a kick in it (I4.4): a low thump under the shell,
            // the upper pair damped towards a thud, wires held then let go,
            // and a ghost that carries a little of the same thump. Appended.
            "Thump Snare",
            {
                { ids::s1Tune, 190.0f },
                { ids::s1Tone, 60.0f },
                { ids::s1Decay, 220.0f },
                { ids::s1Body, 80.0f },
                { ids::s1Ring, -30.0f },
                { ids::s1Thump, 60.0f },
                { ids::s1ThumpTone, 90.0f },
                { ids::s1ThumpDecay, 150.0f },
                { ids::s1Wires, 65.0f },
                { ids::s1Snappy, 3500.0f },
                { ids::s1WiresHold, 20.0f },
                { ids::s1WiresDecay, 160.0f },
                { ids::s1Rattle, 25.0f },
                { ids::s1Crack, 45.0f },
                { ids::s1CrackTone, 4000.0f },
                { ids::s1Noise, 25.0f },
                { ids::g1Decay, 100.0f },
                { ids::g1Start, 3.0f },
                { ids::g1Body, 40.0f },
                { ids::g1Thump, 30.0f },
                { ids::g1ThumpTone, 90.0f },
                { ids::g1ThumpDecay, 90.0f },
                { ids::g1Wires, 55.0f },
                { ids::g1WiresDecay, 80.0f },
                { ids::g1Level, 45.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The washy open hat (I4.4): the plate most of the way, the hiss
            // swelling up behind the strike and tilted dark, and an open pad
            // that holds for a quarter of a second on its own before it
            // falls. Appended.
            "Wash Hats",
            {
                { ids::htPlate, 60.0f },
                { ids::htGrit, 20.0f },
                { ids::htTune, 230.0f },
                { ids::htAir, 70.0f },
                { ids::htAirTone, 3000.0f },
                { ids::htAirTilt, -20.0f },
                { ids::htAirAttack, 120.0f },
                { ids::htAirDecay, 140.0f },
                { ids::htSizzle, 50.0f },
                { ids::htColour, 2500.0f },
                { ids::htWidth, 90.0f },
                { ids::htHighpass, 500.0f },
                { ids::htDamp, 45.0f },
                { ids::htStrike, 50.0f },
                { ids::hcDecay, 70.0f },
                { ids::hoDecay, 900.0f },
                { ids::hoHold, 250.0f },
                { ids::htHoldLink, 0.0f },
                { ids::htLevel, 80.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The sandy hat (I4.4): the hiss thinned to a crackle and rung
            // hard through the partials, tilted bright, with harder hits
            // getting more of it. Appended.
            "Crackle Hats",
            {
                { ids::htPlate, 40.0f },
                { ids::htGrit, 45.0f },
                { ids::htAir, 65.0f },
                { ids::htAirTone, 4000.0f },
                { ids::htAirTilt, 25.0f },
                { ids::htGrain, 80.0f },
                { ids::htSizzle, 85.0f },
                { ids::htColour, 3400.0f },
                { ids::htWidth, 70.0f },
                { ids::htHighpass, 900.0f },
                { ids::htDamp, 50.0f },
                { ids::htStrike, 60.0f },
                { ids::htVelAir, 50.0f },
                { ids::hcDecay, 45.0f },
                { ids::hoDecay, 400.0f },
                { ids::htLevel, 80.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The kit placed in the field (I4.5): the hiss and the metal
            // spread, the wires as a second stream on the side, the clap's
            // bursts across the pair, a little room on the snare, and the kick
            // left exactly where it was -- centred, mono under 150 Hz.
            // Appended.
            "Wide Kit",
            {
                { ids::htAirStereo, 70.0f },
                { ids::htMetalStereo, 55.0f },
                { ids::htAir, 45.0f },
                { ids::htPlate, 50.0f },
                { ids::s1WiresStereo, 60.0f },
                { ids::s1Room, 22.0f },
                { ids::s1RoomSize, 60.0f },
                { ids::s1RoomTone, 5000.0f },
                { ids::g1WiresStereo, 40.0f },
                { ids::cpStereo, 70.0f },
                { ids::hcPan, 12.0f },
                { ids::hoPan, 18.0f },
                { ids::pcPan, -20.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // Four rooms (I4.5): a tight booth on the kick, kept mono under
            // 150 Hz; a wooden room on the snare and the ghost; a longer,
            // duller one on the clap, whose whole character is its room.
            // Appended.
            "Room Kit",
            {
                { ids::k1Room, 18.0f },
                { ids::k1RoomSize, 40.0f },
                { ids::k1RoomTone, 2500.0f },
                { ids::s1Room, 40.0f },
                { ids::s1RoomSize, 120.0f },
                { ids::s1RoomTone, 4000.0f },
                { ids::g1Room, 30.0f },
                { ids::g1RoomSize, 120.0f },
                { ids::g1RoomTone, 4000.0f },
                { ids::cpRoom, 55.0f },
                { ids::cpRoomSize, 200.0f },
                { ids::cpRoomTone, 3000.0f },
                { ids::htAirStereo, 35.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The snares' chatter (I4.5): a long-ringing shell whose throw on
            // the wires is tensioned so the snares lift and strike at the
            // head's period, tuned low and dark, and cut before the shell.
            // Head halfway to a tom's ratios, a bed under the wires. Appended.
            "Chatter Snare",
            {
                { ids::s1Tune, 190.0f },
                { ids::s1Decay, 420.0f },
                { ids::s1Ring, 30.0f },
                { ids::s1Head, 35.0f },
                { ids::s1Body, 85.0f },
                { ids::s1Wires, 70.0f },
                { ids::s1Snappy, 2600.0f },
                { ids::s1WiresDecay, 120.0f },
                { ids::s1Rattle, 65.0f },
                { ids::s1RattleTension, 70.0f },
                { ids::s1RattleTone, -9.0f },
                { ids::s1RattleDecay, 150.0f },
                { ids::s1Bed, 35.0f },
                { ids::s1WiresTilt, -20.0f },
                { ids::s1Crack, 30.0f },
                { ids::s1Thump, 30.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The snare-plus-clap of the classic sampled drum machines as one
            // pad (I4.5): the CLAP page's sound under Snare 1, 8 ms behind,
            // its bursts across the field with the wires. Appended.
            "Clap Snare",
            {
                { ids::s1Clap, 70.0f },
                { ids::s1ClapOffset, 8.0f },
                { ids::s1WiresStereo, 40.0f },
                { ids::s1Wires, 55.0f },
                { ids::s1Body, 75.0f },
                { ids::cpBursts, 4.0f },
                { ids::cpFlam, 9.0f },
                { ids::cpStereo, 60.0f },
                { ids::cpTail, 120.0f },
                { ids::cpColour, 1600.0f },
            }
        },
        // -------------------------------------------------------------------
        {
            // The laser (I4.5): a four-octave drop landing on a straight
            // line over 80 ms rather than the exponential's early fall, a
            // little booth behind it. Appended.
            "Laser Kick",
            {
                { ids::k1Tune, 48.0f },
                { ids::k1Start, 48.0f },
                { ids::k1Drop, 80.0f },
                { ids::k1DropCurve, -100.0f },
                { ids::k1Decay, 420.0f },
                { ids::k1Harmonics, 25.0f },
                { ids::k1Click, 30.0f },
                { ids::k1Room, 12.0f },
                { ids::k1RoomSize, 35.0f },
                { ids::k1RoomTone, 2000.0f },
                { ids::k1Level, 62.0f },
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
                                // Every bus enabled from the start: FL Studio skips outputs a
                                // plugin marks inactive unless told to process them
                                // (docs/DSP-REFERENCES.md, the Image-Line rows). Main is the
                                // fallback and is never disabled.
                                .withOutput (kBusNames[0], juce::AudioChannelSet::stereo(), true)
                                .withOutput (kBusNames[1], juce::AudioChannelSet::stereo(), true)
                                .withOutput (kBusNames[2], juce::AudioChannelSet::stereo(), true)
                                .withOutput (kBusNames[3], juce::AudioChannelSet::stereo(), true)
                                .withOutput (kBusNames[4], juce::AudioChannelSet::stereo(), true)),
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

    // The display name says "Wire shape" (the ID stays `s1Snap`, frozen), so
    // it cannot be read as the note snap that arrived at schema 4.
    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Snap, kSchemaV3 }, "Snare 1 Wire shape",
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

    // ---- schema 4: note snap -- APPENDED ---------------------------------------

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::k1NoteSnap, kSchemaV4 }, "Kick 1 Note snap", false));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::s1NoteSnap, kSchemaV4 }, "Snare 1 Note snap", false));

    // ---- schema 5: the GHOST snare -- APPENDED ------------------------------
    // Its defaults are a ghost's: linked to the main snare's drum, a lighter
    // and shorter stroke that is mostly wire, well under the main hit.

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Tune, kSchemaV5 }, "Ghost Tune",
        skewed (60.0f, 800.0f, 0.1f, 200.0f), 180.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::g1FollowKey, kSchemaV5 }, "Ghost Follow key", false));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::g1NoteSnap, kSchemaV5 }, "Ghost Note snap", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Spread, kSchemaV5 }, "Ghost Spread",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Tone, kSchemaV5 }, "Ghost Tone",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Decay, kSchemaV5 }, "Ghost Decay",
        skewed (50.0f, 2000.0f, 1.0f, 300.0f), 110.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Start, kSchemaV5 }, "Ghost Start",
        Range (0.0f, 24.0f, 0.1f), 3.0f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Drop, kSchemaV5 }, "Ghost Drop",
        skewed (2.0f, 200.0f, 0.1f, 30.0f), 12.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Body, kSchemaV5 }, "Ghost Body",
        Range (0.0f, 100.0f, 0.1f), 50.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Wires, kSchemaV5 }, "Ghost Wires",
        Range (0.0f, 100.0f, 0.1f), 80.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Snappy, kSchemaV5 }, "Ghost Snappy",
        skewed (1000.0f, 8000.0f, 1.0f, 3000.0f), 3000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Snap, kSchemaV5 }, "Ghost Wire shape",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1WiresDecay, kSchemaV5 }, "Ghost Wires decay",
        skewed (50.0f, 400.0f, 1.0f, 150.0f), 80.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Rattle, kSchemaV5 }, "Ghost Rattle",
        Range (0.0f, 100.0f, 0.1f), 20.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Crack, kSchemaV5 }, "Ghost Crack",
        Range (0.0f, 100.0f, 0.1f), 20.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1CrackTone, kSchemaV5 }, "Ghost Crack tone",
        skewed (200.0f, 8000.0f, 1.0f, 2000.0f), 4000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Noise, kSchemaV5 }, "Ghost Noise",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1NoiseTime, kSchemaV5 }, "Ghost Noise time",
        skewed (0.5f, 8.0f, 0.01f, 2.0f), 1.5f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Level, kSchemaV5 }, "Ghost Level",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::g1Gate, kSchemaV5 }, "Ghost Gate", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1Release, kSchemaV5 }, "Ghost Release",
        skewed (0.0f, 2000.0f, 1.0f, 100.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1VelLevel, kSchemaV5 }, "Ghost Velocity to level",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1VelWires, kSchemaV5 }, "Ghost Velocity to wires",
        Range (0.0f, 100.0f, 0.1f), 40.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1VelCrack, kSchemaV5 }, "Ghost Velocity to crack",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1VelDrop, kSchemaV5 }, "Ghost Velocity to drop",
        Range (0.0f, 100.0f, 0.1f), 30.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::g1Link, kSchemaV5 }, "Ghost Link", true));

    // ---- schema 6: the HATS and the CLAP -- APPENDED ------------------------
    //
    // Unlike every earlier addition these do NOT default to neutral, and they
    // do not have to: no project has ever had a hat or a clap from this
    // plugin, so there is nothing saved for a neutral default to protect.
    // They default to a sound.

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htTune, kSchemaV6 }, "Hats Tune",
        skewed (60.0f, 1200.0f, 0.1f, 300.0f), 205.3f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htHarmonics, kSchemaV6 }, "Hats Harmonics",
        Range (0.0f, static_cast<float> (HatEngine::kMaxHarmonicsPosition), 0.01f), 0.0f,
        attributes ("")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htSpread, kSchemaV6 }, "Hats Spread",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htColour, kSchemaV6 }, "Hats Colour",
        skewed (800.0f, 12000.0f, 1.0f, 3500.0f), 3440.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htAir, kSchemaV6 }, "Hats Air",
        Range (0.0f, 100.0f, 0.1f), 45.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::hcDecay, kSchemaV6 }, "Hat closed Decay",
        skewed (10.0f, 300.0f, 0.1f, 60.0f), 55.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::hoDecay, kSchemaV6 }, "Hat open Decay",
        skewed (100.0f, 2000.0f, 1.0f, 500.0f), 450.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htLevel, kSchemaV6 }, "Hats Level",
        Range (0.0f, 100.0f, 0.1f), 70.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::htChoke, kSchemaV6 }, "Hats Choke", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htVelLevel, kSchemaV6 }, "Hats Velocity to level",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htVelDecay, kSchemaV6 }, "Hats Velocity to decay",
        Range (0.0f, 100.0f, 0.1f), 30.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htVelColour, kSchemaV6 }, "Hats Velocity to colour",
        Range (0.0f, 100.0f, 0.1f), 40.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpFlam, kSchemaV6 }, "Clap Flam",
        Range (4.0f, 30.0f, 0.1f), 11.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpColour, kSchemaV6 }, "Clap Colour",
        skewed (300.0f, 5000.0f, 1.0f, 1400.0f), 1200.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpTail, kSchemaV6 }, "Clap Tail",
        skewed (30.0f, 1000.0f, 1.0f, 200.0f), 180.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpLevel, kSchemaV6 }, "Clap Level",
        Range (0.0f, 100.0f, 0.1f), 75.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpVelLevel, kSchemaV6 }, "Clap Velocity to level",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    // ---- schema 7: what the first hats and clap were missing ---------------
    //
    // The hats read as a metallic chord rather than a cymbal, because six
    // pulses through a band-pass is a sparse comb with no transient and one
    // uniform decay. Ring and Drive make it dense, Damp makes it fall from
    // bright to dark the way a plate does, Strike gives it a stick, and
    // Sizzle rings the hiss through the metal's own partials so the noise
    // and the harmonics are one instrument rather than two. The clap gains
    // its burst pattern, a body that rings, and a room with its own tone.
    //
    // Appended, like everything else here. Nothing saved moves.

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htRing, kSchemaV7 }, "Hats Ring",
        Range (0.0f, 100.0f, 0.1f), 35.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htDrive, kSchemaV7 }, "Hats Drive",
        Range (0.0f, 100.0f, 0.1f), 25.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htSizzle, kSchemaV7 }, "Hats Sizzle",
        Range (0.0f, 100.0f, 0.1f), 60.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htAirTone, kSchemaV7 }, "Hats Air tone",
        skewed (200.0f, 12000.0f, 1.0f, 3000.0f), 5000.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htAirDecay, kSchemaV7 }, "Hats Air decay",
        Range (10.0f, 300.0f, 1.0f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htWidth, kSchemaV7 }, "Hats Width",
        Range (0.0f, 100.0f, 0.1f), 50.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htHighpass, kSchemaV7 }, "Hats Highpass",
        skewed (200.0f, 8000.0f, 1.0f, 1500.0f), 1200.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htDamp, kSchemaV7 }, "Hats Damp",
        Range (0.0f, 100.0f, 0.1f), 40.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htStrike, kSchemaV7 }, "Hats Strike",
        Range (0.0f, 100.0f, 0.1f), 40.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htHold, kSchemaV7 }, "Hats Hold",
        Range (0.0f, 200.0f, 0.1f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htShape, kSchemaV7 }, "Hats Shape",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htVelStrike, kSchemaV7 }, "Hats Velocity to strike",
        Range (0.0f, 100.0f, 0.1f), 50.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::htGate, kSchemaV7 }, "Hats Gate", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htRelease, kSchemaV7 }, "Hats Release",
        skewed (0.0f, 2000.0f, 1.0f, 100.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpBursts, kSchemaV7 }, "Clap Bursts",
        Range (2.0f, 6.0f, 1.0f), 4.0f, attributes ("")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpSkew, kSchemaV7 }, "Clap Skew",
        Range (-100.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpSnap, kSchemaV7 }, "Clap Snap",
        skewed (1.0f, 20.0f, 0.1f, 5.0f), 3.5f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpNoise, kSchemaV7 }, "Clap Noise",
        Range (0.0f, 100.0f, 0.1f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpNoiseTone, kSchemaV7 }, "Clap Noise tone",
        skewed (200.0f, 8000.0f, 1.0f, 1200.0f), 800.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpBody, kSchemaV7 }, "Clap Body",
        Range (0.0f, 100.0f, 0.1f), 20.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpBodyPitch, kSchemaV7 }, "Clap Body pitch",
        skewed (200.0f, 2500.0f, 1.0f, 900.0f), 900.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpBodyRing, kSchemaV7 }, "Clap Body ring",
        skewed (10.0f, 500.0f, 0.1f, 80.0f), 60.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpWidth, kSchemaV7 }, "Clap Width",
        Range (0.0f, 100.0f, 0.1f), 50.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpTailTone, kSchemaV7 }, "Clap Tail tone",
        Range (25.0f, 150.0f, 0.1f), 70.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpDrive, kSchemaV7 }, "Clap Drive",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    // ---- schema 8: the clap's gate -----------------------------------------

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::cpGate, kSchemaV8 }, "Clap Gate", false));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpRelease, kSchemaV8 }, "Clap Release",
        skewed (0.0f, 2000.0f, 1.0f, 100.0f), 0.0f, attributes ("ms")));

    // ---- schema 9: a hold on the wires -------------------------------------
    //
    // Neutral at 0, so every project saved before it existed reopens
    // sounding exactly as it did (CLAUDE.md section 8).

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1WiresHold, kSchemaV9 }, "Snare 1 Wires hold",
        skewed (0.0f, 300.0f, 0.1f, 40.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::g1WiresHold, kSchemaV9 }, "Ghost Wires hold",
        skewed (0.0f, 300.0f, 0.1f, 40.0f), 0.0f, attributes ("ms")));

    // ---- schema 10: the plate, and the grit (I4.3) ---------------------------
    //
    // The rig heard the hats as thin and tinny and asked for the chunky hat of
    // a sampled real pair of cymbals. Six pulses cannot be that whatever the
    // filters do, so Plate crossfades them against a 64-mode modal cymbal
    // placed by the published cymbal mode law, and Grit quantises the layers
    // the way a low-resolution sample path did. Both 0 by default: a project
    // saved before they existed reopens bit for bit (CLAUDE.md section 8; the
    // golden render is in plugins/Ictus/PLAN.md).

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htPlate, kSchemaV10 }, "Hats Plate",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htGrit, kSchemaV10 }, "Hats Grit",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    // ---- schema 11: the rig's fourth round (I4.4) ------------------------------
    //
    // More say over the hiss, an open pad that can hold longer than the closed
    // one, a thicker kick and snare, and the pads placed in the field. Every
    // one of these is neutral by default -- 0, 100 % (a multiplier of one),
    // or LINK lit -- so a project saved at schema 10 reopens bit for bit
    // (CLAUDE.md section 8; the round's golden render is in
    // plugins/Ictus/PLAN.md).

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htAirTilt, kSchemaV11 }, "Hats Air tilt",
        Range (-100.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htAirAttack, kSchemaV11 }, "Hats Air attack",
        skewed (0.0f, 500.0f, 0.1f, 80.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htGrain, kSchemaV11 }, "Hats Grain",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htVelAir, kSchemaV11 }, "Hats Velocity to air",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::hoHold, kSchemaV11 }, "Hat open Hold",
        skewed (0.0f, 1000.0f, 0.1f, 150.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Switch> (
        juce::ParameterID { ids::htHoldLink, kSchemaV11 }, "Hats Hold link", true));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Under, kSchemaV11 }, "Kick 1 Under",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1UnderInterval, kSchemaV11 }, "Kick 1 Under interval",
        Range (0.0f, 24.0f, 0.01f), 12.0f, attributes ("st")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1UnderDecay, kSchemaV11 }, "Kick 1 Under decay",
        skewed (25.0f, 400.0f, 0.1f, 100.0f), 100.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1UnderAttack, kSchemaV11 }, "Kick 1 Under attack",
        skewed (0.0f, 200.0f, 0.1f, 30.0f), 0.0f, attributes ("ms")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1Knock, kSchemaV11 }, "Kick 1 Knock",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1KnockTone, kSchemaV11 }, "Kick 1 Knock tone",
        skewed (150.0f, 800.0f, 1.0f, 350.0f), 350.0f, attributes ("Hz")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1KnockTime, kSchemaV11 }, "Kick 1 Knock time",
        skewed (5.0f, 80.0f, 0.1f, 25.0f), 25.0f, attributes ("ms")));

    for (const auto* snare : { &kSnare1Ids, &kGhostIds })
    {
        const juce::String who = snare->link == nullptr ? "Snare 1 " : "Ghost ";

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->ring, kSchemaV11 }, who + "Ring",
            Range (-100.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->thump, kSchemaV11 }, who + "Thump",
            Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->thumpTone, kSchemaV11 }, who + "Thump tone",
            skewed (40.0f, 200.0f, 0.1f, 100.0f), 100.0f, attributes ("Hz")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->thumpDecay, kSchemaV11 }, who + "Thump decay",
            skewed (30.0f, 500.0f, 0.1f, 120.0f), 120.0f, attributes ("ms")));
    }

    // One pan per pad, in PadIndex order. -100 is hard left, +100 hard right,
    // on a balance law: the centre is both channels at unity, the dual mono
    // the instrument always rendered.
    static const char* const panNames[kPadCount] {
        "Kick 1 Pan", "Snare 1 Pan", "Hat closed Pan", "Hat open Pan",
        "Clap Pan", "Perc Pan", "Kick 2 Pan", "Ghost Pan"
    };

    for (int pad = 0; pad < kPadCount; ++pad)
        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { kPanIds[pad], kSchemaV11 }, panNames[pad],
            Range (-100.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    // ---- schema 12: the rig's fifth round (I4.5) ------------------------------
    //
    // The field, for real: the sources spread, a Width and a Mono below per
    // pad, a room on four pads, and the plate washed; the snare's head, the
    // wires' colour and the rattle's own decay, tone and tension; the drop's
    // curve; the clap under the snare. Every one is neutral by default -- 0,
    // 100 % (a gain of one), or the value the engine already used -- so a
    // project saved at schema 11 reopens bit for bit (the round's golden
    // render is in plugins/Ictus/PLAN.md).

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htAirStereo, kSchemaV12 }, "Hats Air stereo",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htMetalStereo, kSchemaV12 }, "Hats Metal stereo",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::htWash, kSchemaV12 }, "Hats Wash",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    for (const auto* snare : { &kSnare1Ids, &kGhostIds })
    {
        const juce::String who = snare->link == nullptr ? "Snare 1 " : "Ghost ";

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->head, kSchemaV12 }, who + "Head",
            Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->wiresTilt, kSchemaV12 }, who + "Wires tilt",
            Range (-100.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->bed, kSchemaV12 }, who + "Bed",
            Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->wiresStereo, kSchemaV12 }, who + "Wires stereo",
            Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

        // 0 is "the shell's" -- the follower for as long as the drum rings.
        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->rattleDecay, kSchemaV12 }, who + "Rattle decay",
            skewed (0.0f, 2000.0f, 1.0f, 200.0f), 0.0f, attributes ("ms")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->rattleTone, kSchemaV12 }, who + "Rattle tone",
            Range (-24.0f, 24.0f, 0.1f), 0.0f, attributes ("st")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { snare->rattleTension, kSchemaV12 }, who + "Rattle tension",
            Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));
    }

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::cpStereo, kSchemaV12 }, "Clap Stereo",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::k1DropCurve, kSchemaV12 }, "Kick 1 Drop curve",
        Range (-100.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    // Width and Mono below, one per pad in PadIndex order. Width is a gain on
    // the pad's side alone: 100 % is the field the pad's own spread controls
    // make, exactly, and 0 folds it to mono. Mono below is the corner under
    // which the side is removed; 0 is off.
    static const char* const padNames[kPadCount] {
        "Kick 1", "Snare 1", "Hat closed", "Hat open", "Clap", "Perc", "Kick 2", "Ghost"
    };

    for (int pad = 0; pad < kPadCount; ++pad)
        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { kWidthIds[pad], kSchemaV12 }, juce::String (padNames[pad]) + " Width",
            Range (0.0f, 200.0f, 0.1f), 100.0f, attributes ("%")));

    for (int pad = 0; pad < kPadCount; ++pad)
        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { kMonoBelowIds[pad], kSchemaV12 }, juce::String (padNames[pad]) + " Mono below",
            skewed (0.0f, 500.0f, 1.0f, 150.0f), 150.0f, attributes ("Hz")));

    // The rooms, in RoomIndex order. 20 kHz on the tone is off.
    static const char* const roomNames[kRoomCount] { "Kick 1", "Snare 1", "Ghost", "Clap" };

    for (int room = 0; room < kRoomCount; ++room)
    {
        const juce::String who = roomNames[room];

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { kRoomIds[room].level, kSchemaV12 }, who + " Room",
            Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { kRoomIds[room].size, kSchemaV12 }, who + " Room size",
            skewed (10.0f, 250.0f, 1.0f, 80.0f), 80.0f, attributes ("ms")));

        parameters.push_back (std::make_unique<Parameter> (
            juce::ParameterID { kRoomIds[room].tone, kSchemaV12 }, who + " Room tone",
            skewed (500.0f, 20000.0f, 1.0f, 4000.0f), 4000.0f, attributes ("Hz")));
    }

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1Clap, kSchemaV12 }, "Snare 1 Clap",
        Range (0.0f, 100.0f, 0.1f), 0.0f, attributes ("%")));

    parameters.push_back (std::make_unique<Parameter> (
        juce::ParameterID { ids::s1ClapOffset, kSchemaV12 }, "Snare 1 Clap offset",
        Range (0.0f, 50.0f, 0.1f), 0.0f, attributes ("ms")));

    // ---- schema 13: the output buses (I7) -------------------------------------
    //
    // One Output per pad, Main by default: a project saved before the buses
    // existed keeps the whole kit on the one output it always had.
    for (int pad = 0; pad < kPadCount; ++pad)
        parameters.push_back (std::make_unique<ChoiceParameter> (
            juce::ParameterID { kOutputIds[pad], kSchemaV13 }, juce::String (padNames[pad]) + " Output",
            choices::output, 0));

    return { parameters.begin(), parameters.end() };
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

bool IctusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // No inputs; Main stereo, never disabled; every other bus stereo or
    // disabled -- a pad routed to a bus the host has turned off falls back
    // to Main in processBlock, so nothing goes silent.
    if (! layouts.inputBuses.isEmpty())
        for (const auto& input : layouts.inputBuses)
            if (! input.isDisabled())
                return false;

    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
    {
        const auto& set = layouts.outputBuses.getReference (bus);

        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
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

    scratch_.setSize (2 * kBusCount, std::max (maximumExpectedSamplesPerBlock, 1), false, false, true);

    analyser_.prepare (sampleRate_, 0.4);
    rmsAccumulator_ = 0.0;
    rmsSamples_ = 0;

    reportedLatency_ = engine_.getLatencySamples();
    setLatencySamples (reportedLatency_);

    prepared_ = true;
}

void IctusProcessor::pullParameters()
{
    auto& k = parameters_.kick1;

    k.tuneHz = valueOf (state_, ids::k1Tune);
    k.followKey = valueOf (state_, ids::k1FollowKey) > 0.5f;
    k.noteSnap = valueOf (state_, ids::k1NoteSnap) > 0.5f;
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

    k.under = valueOf (state_, ids::k1Under) * 0.01;
    k.underSemitones = valueOf (state_, ids::k1UnderInterval);
    k.underDecay = valueOf (state_, ids::k1UnderDecay) * 0.01;
    k.underAttackSeconds = valueOf (state_, ids::k1UnderAttack) * 0.001;
    k.knock = valueOf (state_, ids::k1Knock) * 0.01;
    k.knockHz = valueOf (state_, ids::k1KnockTone);
    k.knockSeconds = valueOf (state_, ids::k1KnockTime) * 0.001;
    k.dropCurve = valueOf (state_, ids::k1DropCurve) * 0.01;

    pullSnare (parameters_.snare1, kSnare1Ids);
    pullSnare (parameters_.snare2, kGhostIds);

    // LINK: the ghost is the main snare's drum -- the same shell and the
    // same wire colour -- and only its stroke is its own.
    if (valueOf (state_, ids::g1Link) > 0.5f)
    {
        auto& ghost = parameters_.snare2;
        const auto& main = parameters_.snare1;

        ghost.tuneHz = main.tuneHz;
        ghost.followKey = main.followKey;
        ghost.noteSnap = main.noteSnap;
        ghost.spread = main.spread;
        ghost.tone = main.tone;
        ghost.snappyHz = main.snappyHz;
        ghost.snap = main.snap;

        // I4.5: the head's ratios and the wires' colour are the drum too.
        ghost.head = main.head;
        ghost.wiresTilt = main.wiresTilt;
        ghost.bed = main.bed;
    }

    // ---- the hats: one instrument, two pads ----
    auto& h = parameters_.hat;

    h.tuneHz = valueOf (state_, ids::htTune);
    h.harmonics = valueOf (state_, ids::htHarmonics);
    h.spread = valueOf (state_, ids::htSpread) * 0.01;
    h.colourHz = valueOf (state_, ids::htColour);
    h.ring = valueOf (state_, ids::htRing) * 0.01;
    h.drive = valueOf (state_, ids::htDrive) * 0.01;
    h.plate = valueOf (state_, ids::htPlate) * 0.01;
    h.grit = valueOf (state_, ids::htGrit) * 0.01;
    h.air = valueOf (state_, ids::htAir) * 0.01;
    h.airToneHz = valueOf (state_, ids::htAirTone);
    h.airDecay = valueOf (state_, ids::htAirDecay) * 0.01;
    h.sizzle = valueOf (state_, ids::htSizzle) * 0.01;
    h.width = valueOf (state_, ids::htWidth) * 0.01;
    h.highpassHz = valueOf (state_, ids::htHighpass);
    h.damp = valueOf (state_, ids::htDamp) * 0.01;
    h.strike = valueOf (state_, ids::htStrike) * 0.01;
    h.holdSeconds = valueOf (state_, ids::htHold) * 0.001;
    h.shape = valueOf (state_, ids::htShape) * 0.01;
    h.velocityStrike = valueOf (state_, ids::htVelStrike) * 0.01;
    h.gate = valueOf (state_, ids::htGate) > 0.5f;
    h.releaseSeconds = valueOf (state_, ids::htRelease) * 0.001;
    h.decayClosedSeconds = valueOf (state_, ids::hcDecay) * 0.001;
    h.decayOpenSeconds = valueOf (state_, ids::hoDecay) * 0.001;
    h.level = valueOf (state_, ids::htLevel) * 0.01;
    h.choke = valueOf (state_, ids::htChoke) > 0.5f;
    h.velocityLevel = valueOf (state_, ids::htVelLevel) * 0.01;
    h.velocityDecay = valueOf (state_, ids::htVelDecay) * 0.01;
    h.velocityColour = valueOf (state_, ids::htVelColour) * 0.01;

    h.airTilt = valueOf (state_, ids::htAirTilt) * 0.01;
    h.airAttackSeconds = valueOf (state_, ids::htAirAttack) * 0.001;
    h.grain = valueOf (state_, ids::htGrain) * 0.01;
    h.velocityAir = valueOf (state_, ids::htVelAir) * 0.01;
    h.holdOpenSeconds = valueOf (state_, ids::hoHold) * 0.001;
    h.holdLink = valueOf (state_, ids::htHoldLink) > 0.5f;

    h.airStereo = valueOf (state_, ids::htAirStereo) * 0.01;
    h.metalStereo = valueOf (state_, ids::htMetalStereo) * 0.01;
    h.wash = valueOf (state_, ids::htWash) * 0.01;

    // ---- the clap ----
    auto& c = parameters_.clap;

    c.bursts = static_cast<int> (std::lround (valueOf (state_, ids::cpBursts)));
    c.flamSeconds = valueOf (state_, ids::cpFlam) * 0.001;
    c.skew = valueOf (state_, ids::cpSkew) * 0.01;
    c.snapSeconds = valueOf (state_, ids::cpSnap) * 0.001;
    c.noise = valueOf (state_, ids::cpNoise) * 0.01;
    c.noiseToneHz = valueOf (state_, ids::cpNoiseTone);
    c.body = valueOf (state_, ids::cpBody) * 0.01;
    c.bodyHz = valueOf (state_, ids::cpBodyPitch);
    c.bodyRingSeconds = valueOf (state_, ids::cpBodyRing) * 0.001;
    c.width = valueOf (state_, ids::cpWidth) * 0.01;
    c.drive = valueOf (state_, ids::cpDrive) * 0.01;
    c.colourHz = valueOf (state_, ids::cpColour);
    c.tailSeconds = valueOf (state_, ids::cpTail) * 0.001;
    c.tailTone = valueOf (state_, ids::cpTailTone) * 0.01;
    c.gate = valueOf (state_, ids::cpGate) > 0.5f;
    c.releaseSeconds = valueOf (state_, ids::cpRelease) * 0.001;
    c.level = valueOf (state_, ids::cpLevel) * 0.01;
    c.velocityLevel = valueOf (state_, ids::cpVelLevel) * 0.01;
    c.stereo = valueOf (state_, ids::cpStereo) * 0.01;

    for (int pad = 0; pad < kPadCount; ++pad)
    {
        parameters_.padNotes[pad] = padNotes_[pad].load();
        parameters_.pan[pad] = valueOf (state_, kPanIds[pad]) * 0.01;
        parameters_.width[pad] = valueOf (state_, kWidthIds[pad]) * 0.01;
        parameters_.monoBelowHz[pad] = valueOf (state_, kMonoBelowIds[pad]);
    }

    for (int room = 0; room < kRoomCount; ++room)
    {
        auto& r = parameters_.room[room];
        r.level = valueOf (state_, kRoomIds[room].level) * 0.01;
        r.seconds = valueOf (state_, kRoomIds[room].size) * 0.001;

        // The top of the range is off: the engine treats 0 as no filter.
        const float tone = valueOf (state_, kRoomIds[room].tone);
        r.toneHz = tone >= 19999.0f ? 0.0 : tone;
    }

    parameters_.snareClap = valueOf (state_, ids::s1Clap) * 0.01;
    parameters_.snareClapOffsetSeconds = valueOf (state_, ids::s1ClapOffset) * 0.001;

    for (int pad = 0; pad < kPadCount; ++pad)
        parameters_.output[pad] = juce::jlimit (0, kBusCount - 1,
                                                static_cast<int> (std::lround (valueOf (state_, kOutputIds[pad]))));

    parameters_.masterDb = valueOf (state_, ids::output);
    parameters_.bassMode = valueOf (state_, ids::bassMode) > 0.5f;

    parameters_.oversampling = static_cast<dsp::OversamplingMode> (
        juce::jlimit (0, static_cast<int> (dsp::OversamplingMode::X8),
                      static_cast<int> (std::lround (valueOf (state_, ids::oversampling)))));

    parameters_.renderOversampling = static_cast<dsp::RenderOversampling> (
        juce::jlimit (0, static_cast<int> (dsp::RenderOversampling::X8),
                      static_cast<int> (std::lround (valueOf (state_, ids::renderOversampling)))));
}

void IctusProcessor::pullSnare (SnareSettings& n, const SnareIds& p)
{
    n.tuneHz = valueOf (state_, p.tune);
    n.followKey = valueOf (state_, p.followKey) > 0.5f;
    n.noteSnap = valueOf (state_, p.noteSnap) > 0.5f;
    n.spread = valueOf (state_, p.spread) * 0.01;
    n.tone = valueOf (state_, p.tone) * 0.01;
    n.decaySeconds = valueOf (state_, p.decay) * 0.001;
    n.startSemitones = valueOf (state_, p.start);
    n.dropSeconds = valueOf (state_, p.drop) * 0.001;
    n.body = valueOf (state_, p.body) * 0.01;

    n.ring = valueOf (state_, p.ring) * 0.01;
    n.thump = valueOf (state_, p.thump) * 0.01;
    n.thumpHz = valueOf (state_, p.thumpTone);
    n.thumpDecaySeconds = valueOf (state_, p.thumpDecay) * 0.001;

    n.wires = valueOf (state_, p.wires) * 0.01;
    n.snappyHz = valueOf (state_, p.snappy);
    n.snap = valueOf (state_, p.snap) * 0.01;
    n.wiresHoldSeconds = valueOf (state_, p.wiresHold) * 0.001;
    n.wiresDecaySeconds = valueOf (state_, p.wiresDecay) * 0.001;
    n.rattle = valueOf (state_, p.rattle) * 0.01;

    n.head = valueOf (state_, p.head) * 0.01;
    n.wiresTilt = valueOf (state_, p.wiresTilt) * 0.01;
    n.bed = valueOf (state_, p.bed) * 0.01;
    n.wiresStereo = valueOf (state_, p.wiresStereo) * 0.01;
    n.rattleDecaySeconds = valueOf (state_, p.rattleDecay) * 0.001;
    n.rattleToneOctaves = valueOf (state_, p.rattleTone) / 12.0;
    n.rattleTension = valueOf (state_, p.rattleTension) * 0.01;

    n.crack = valueOf (state_, p.crack) * 0.01;
    n.crackToneHz = valueOf (state_, p.crackTone);
    n.crackNoise = valueOf (state_, p.noise) * 0.01;
    n.crackNoiseSeconds = valueOf (state_, p.noiseTime) * 0.001;

    n.level = valueOf (state_, p.level) * 0.01;
    n.gate = valueOf (state_, p.gate) > 0.5f;
    n.releaseSeconds = valueOf (state_, p.release) * 0.001;

    n.velocityLevel = valueOf (state_, p.velLevel) * 0.01;
    n.velocityWires = valueOf (state_, p.velWires) * 0.01;
    n.velocityCrack = valueOf (state_, p.velCrack) * 0.01;
    n.velocityDrop = valueOf (state_, p.velDrop) * 0.01;
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
        scratch_.setSize (2 * kBusCount, numSamples, false, false, true);

    double* engineOut[2 * kBusCount];

    for (int channel = 0; channel < 2 * kBusCount; ++channel)
        engineOut[channel] = scratch_.getWritePointer (channel);

    // Where each bus lands in the host's buffer: its own channels when the
    // host gave it any, else folded into Main. A tool that hands us a
    // two-channel buffer, or a host that disabled a bus, hears everything.
    const int hostChannels = buffer.getNumChannels();
    int busChannel[kBusCount];

    for (int bus = 0; bus < kBusCount; ++bus)
    {
        const auto* layout = getBus (false, bus);
        const int first = layout != nullptr && layout->isEnabled() ? getChannelIndexInProcessBlockBuffer (false, bus, 0) : -1;
        busChannel[bus] = first >= 0 && first + 1 < hostChannels ? first : -1;
    }

    busChannel[0] = hostChannels >= 2 ? 0 : -1;

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
        engine_.processBuses (engineOut, count);

        // Every bus to its channels, or into Main when it has none.
        for (int bus = 0; bus < kBusCount; ++bus)
        {
            const int target = busChannel[bus] >= 0 ? busChannel[bus] : busChannel[0];

            if (target < 0)
                continue;

            for (int channel = 0; channel < 2; ++channel)
            {
                const double* source = engineOut[2 * bus + channel];
                auto* destination = buffer.getWritePointer (target + channel) + from;

                if (busChannel[bus] >= 0)
                    for (int i = 0; i < count; ++i)
                        destination[i] = static_cast<FloatType> (source[i]);
                else
                    for (int i = 0; i < count; ++i)
                        destination[i] += static_cast<FloatType> (source[i]);
            }
        }

        // The field readout reads the whole kit -- every bus summed, the mix
        // as it would be with the mixer channels at unity.
        double* const sumLeft = engineOut[0];
        double* const sumRight = engineOut[1];

        for (int bus = 1; bus < kBusCount; ++bus)
            for (int i = 0; i < count; ++i)
            {
                sumLeft[i] += engineOut[2 * bus][i];
                sumRight[i] += engineOut[2 * bus + 1][i];
            }

        const double* const analysed[2] { sumLeft, sumRight };
        analyser_.process (analysed, 2, count);

        for (int i = 0; i < count; ++i)
            rmsAccumulator_ += 0.5 * (sumLeft[i] * sumLeft[i] + sumRight[i] * sumRight[i]);

        rmsSamples_ += count;
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

    correlationFull_.store (static_cast<float> (analyser_.getCorrelation()), std::memory_order_relaxed);
    correlationLow_.store (static_cast<float> (analyser_.getBandCorrelation (dsp::StereoAnalyser::low)),
                           std::memory_order_relaxed);

    // The level behind the readout, over about a tenth of a second.
    if (rmsSamples_ >= static_cast<int> (0.1 * sampleRate_))
    {
        outputRms_.store (static_cast<float> (std::sqrt (rmsAccumulator_ / static_cast<double> (rmsSamples_))),
                          std::memory_order_relaxed);
        rmsAccumulator_ = 0.0;
        rmsSamples_ = 0;
    }

    for (int pad = 0; pad < kPadCount; ++pad)
    {
        padHits_[pad].store (engine_.getPadHitCount (static_cast<PadIndex> (pad)), std::memory_order_relaxed);
        padVelocity_[pad].store (static_cast<float> (engine_.getPadLastVelocity (static_cast<PadIndex> (pad))),
                                 std::memory_order_relaxed);
    }

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

juce::String IctusProcessor::noteNameFor (double hz) const
{
    if (! (hz > 0.0))
        return {};

    // The nearest keyboard note at the current concert pitch, and how far
    // the frequency sits from it in cents. A snapped Tune on 12-TET reads
    // +0c; on a loaded scale the degree's own offset shows.
    const double a4 = concertPitchHz_;
    const double semitones = 12.0 * std::log2 (hz / a4) + 69.0;
    const int nearest = juce::jlimit (0, 127, juce::roundToInt (semitones));
    const int cents = juce::roundToInt (100.0 * (semitones - nearest));

    return juce::MidiMessage::getMidiNoteName (nearest, true, true, 3)
             + (cents == 0 ? juce::String (" +0c")
                           : juce::String (cents > 0 ? " +" : " ") + juce::String (cents) + "c");
}

double IctusProcessor::previewSnappedHz (double hz) const
{
    return previewTuning_.nearestScaleHz (hz);
}

juce::String IctusProcessor::describeNoteSnap (PadIndex pad) const
{
    const bool kick = pad == PadIndex::kick1 || pad == PadIndex::kick2;
    const auto& snare = snareIdsFor (pad);

    if (! kick && snare.link != nullptr && isGhostLinked())
        return "LINK is lit, so the ghost's Tune and Note snap are SNARE's: "
               + describeNoteSnap (PadIndex::snare1);

    const double tune = state_.getRawParameterValue (kick ? ids::k1Tune : snare.tune)->load();
    const bool lit = state_.getRawParameterValue (kick ? ids::k1NoteSnap : snare.noteSnap)->load() > 0.5f;
    const double snapped = previewSnappedHz (tune);
    const juce::String scale = scaleName_ + " at A4 = " + juce::String (concertPitchHz_, 1) + " Hz";

    juce::String text = (lit ? "Lit -- " : "Dark -- Tune is free. Lit: ")
                      + juce::String ("Tune snaps to the nearest degree of the TUNING page's scale (")
                      + scale + "), so the drum sits in the key of the bass line: right now "
                      + juce::String (tune, 1) + " Hz " + (lit ? "becomes " : "would become ")
                      + juce::String (snapped, 2) + " Hz, " + noteNameFor (snapped)
                      + ". Costs nothing: one lookup per hit.";

    if (! kick)
        text += " Snaps the shell's fundamental; the upper modes keep their ratios to it.";

    return text;
}

juce::String IctusProcessor::describeFollowKey (PadIndex pad) const
{
    const int note = getPadNote (pad);
    const juce::String scale = scaleName_ + " at A4 = " + juce::String (concertPitchHz_, 1) + " Hz";
    const bool kick = pad == PadIndex::kick1 || pad == PadIndex::kick2;

    if (pad == PadIndex::snare2 && isGhostLinked())
        return "LINK is lit, so the ghost follows SNARE's Follow key. Its own note is "
               + juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note)
               + "): it always sounds there, whatever sets the pitch.";

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
