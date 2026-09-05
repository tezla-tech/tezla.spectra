// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The JUCE wrapper around the Ictus engine. Thin by design (CLAUDE.md
// section 4): parameters, state, MIDI, the HIT button's hand-off, and the
// oversampling and render-quality plumbing every oversampling plugin in the
// suite shares.
//
// I2 shape: Kick 1's controls and the three globals. The other pads'
// parameters are APPENDED as their engines arrive (CLAUDE.md section 8), each
// with the schema version it was born at; nothing here is ever reordered.
//
// The pad's MIDI note is a state-tree property, not a parameter: it is not
// automatable and a preset must not remap the player's notes
// (plugins/Ictus/PLAN.md). Note-learn writes it from the message thread.
//
// Schema 2, from the rig's first ear round: Gate and Release on the kick,
// Bass mode (every key plays Kick 1 at the key's pitch), and the shared
// tuning page behind both -- the processor is the ui::TuningHost, exactly as
// Malleus is, with the scale handed to the audio thread by a swap.
//
// Schema 3 (I3): Snare 1's 24 controls. Snare 2 and the Perc pad run the
// same engine on their defaults until their pages arrive with the editor
// close-out (I9).
//
// Schema 11 (I4.4, the rig's fourth round): the hats' Air tilt, Air attack,
// Grain, Vel > Air and the open pad's own Hold behind a LINK; the kick's Under
// and Knock; the snares' Ring and Thump; and a Pan per pad, the first control
// every pad has whether or not its page exists yet. Every one neutral by
// default, so a schema-10 project reopens bit for bit (the round's golden
// render is in plugins/Ictus/PLAN.md).

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/Correlation.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/Scales.hpp>
#include <tezla/dsp/Tuning.hpp>
#include <tezla/ui/AbCompare.hpp>
#include <tezla/ui/TuningHost.hpp>

#include <IctusEngine.hpp>

namespace tezla::ictus {

namespace dsp = tezla::dsp;

/// Parameter string IDs. **Frozen forever** -- renaming one resets that
/// control in every project that used it (CLAUDE.md section 8). The two-letter
/// prefix is the pad; a host's parameter list groups by it.
namespace ids
{
// KICK 1 -- pitch
inline constexpr auto k1Tune      = "k1Tune";
inline constexpr auto k1FollowKey = "k1FollowKey";
inline constexpr auto k1Start     = "k1Start";
inline constexpr auto k1Drop      = "k1Drop";
inline constexpr auto k1Sigh      = "k1Sigh";
inline constexpr auto k1SighTime  = "k1SighTime";
inline constexpr auto k1Phase     = "k1Phase";

// KICK 1 -- colour
inline constexpr auto k1Harmonics = "k1Harmonics";
inline constexpr auto k1Even      = "k1Even";
inline constexpr auto k1ToneOn    = "k1ToneOn";
inline constexpr auto k1Tone      = "k1Tone";

// KICK 1 -- click
inline constexpr auto k1Click     = "k1Click";
inline constexpr auto k1ClickTone = "k1ClickTone";
inline constexpr auto k1Noise     = "k1Noise";
inline constexpr auto k1NoiseTime = "k1NoiseTime";

// KICK 1 -- amplitude
inline constexpr auto k1Attack    = "k1Attack";
inline constexpr auto k1Hold      = "k1Hold";
inline constexpr auto k1Decay     = "k1Decay";
inline constexpr auto k1Shape     = "k1Shape";
inline constexpr auto k1Tail      = "k1Tail";
inline constexpr auto k1TailTime  = "k1TailTime";
inline constexpr auto k1Level     = "k1Level";

// KICK 1 -- velocity
inline constexpr auto k1VelLevel  = "k1VelLevel";
inline constexpr auto k1VelClick  = "k1VelClick";
inline constexpr auto k1VelDrop   = "k1VelDrop";
inline constexpr auto k1VelDecay  = "k1VelDecay";

// GLOBAL
inline constexpr auto output             = "output";
inline constexpr auto oversampling       = "oversampling";
inline constexpr auto renderOversampling = "renderOversampling";

// ---- schema 2: the rig's first ear round -----------------------------------
// KICK 1 -- gate
inline constexpr auto k1Gate      = "k1Gate";
inline constexpr auto k1Release   = "k1Release";

// GLOBAL -- Bass mode
inline constexpr auto bassMode    = "bassMode";

// ---- schema 3: the snare (I3) ------------------------------------------------
// SNARE 1 -- shell
inline constexpr auto s1Tune       = "s1Tune";
inline constexpr auto s1FollowKey  = "s1FollowKey";
inline constexpr auto s1Spread     = "s1Spread";
inline constexpr auto s1Tone       = "s1Tone";
inline constexpr auto s1Decay      = "s1Decay";
inline constexpr auto s1Start      = "s1Start";
inline constexpr auto s1Drop       = "s1Drop";
inline constexpr auto s1Body       = "s1Body";

// SNARE 1 -- wires
inline constexpr auto s1Wires      = "s1Wires";
inline constexpr auto s1Snappy     = "s1Snappy";
inline constexpr auto s1Snap       = "s1Snap";
inline constexpr auto s1WiresDecay = "s1WiresDecay";
inline constexpr auto s1Rattle     = "s1Rattle";

// SNARE 1 -- crack
inline constexpr auto s1Crack      = "s1Crack";
inline constexpr auto s1CrackTone  = "s1CrackTone";
inline constexpr auto s1Noise      = "s1Noise";
inline constexpr auto s1NoiseTime  = "s1NoiseTime";

// SNARE 1 -- level and gate
inline constexpr auto s1Level      = "s1Level";
inline constexpr auto s1Gate       = "s1Gate";
inline constexpr auto s1Release    = "s1Release";

// SNARE 1 -- velocity
inline constexpr auto s1VelLevel   = "s1VelLevel";
inline constexpr auto s1VelWires   = "s1VelWires";
inline constexpr auto s1VelCrack   = "s1VelCrack";
inline constexpr auto s1VelDrop    = "s1VelDrop";

// ---- schema 4: note snap -- the drums in the key of the bass line ----------
inline constexpr auto k1NoteSnap   = "k1NoteSnap";
inline constexpr auto s1NoteSnap   = "s1NoteSnap";

// ---- schema 5: the GHOST snare, on the second snare pad (E1) ---------------
// The same 24 controls as Snare 1, plus LINK: lit, the ghost is the main
// snare's drum -- Tune, Key, Note, Spread, Tone, Snappy and Shape follow it
// -- and only the stroke is its own.
inline constexpr auto g1Tune       = "g1Tune";
inline constexpr auto g1FollowKey  = "g1FollowKey";
inline constexpr auto g1NoteSnap   = "g1NoteSnap";
inline constexpr auto g1Spread     = "g1Spread";
inline constexpr auto g1Tone       = "g1Tone";
inline constexpr auto g1Decay      = "g1Decay";
inline constexpr auto g1Start      = "g1Start";
inline constexpr auto g1Drop       = "g1Drop";
inline constexpr auto g1Body       = "g1Body";
inline constexpr auto g1Wires      = "g1Wires";
inline constexpr auto g1Snappy     = "g1Snappy";
inline constexpr auto g1Snap       = "g1Snap";
inline constexpr auto g1WiresDecay = "g1WiresDecay";
inline constexpr auto g1Rattle     = "g1Rattle";
inline constexpr auto g1Crack      = "g1Crack";
inline constexpr auto g1CrackTone  = "g1CrackTone";
inline constexpr auto g1Noise      = "g1Noise";
inline constexpr auto g1NoiseTime  = "g1NoiseTime";
inline constexpr auto g1Level      = "g1Level";
inline constexpr auto g1Gate       = "g1Gate";
inline constexpr auto g1Release    = "g1Release";
inline constexpr auto g1VelLevel   = "g1VelLevel";
inline constexpr auto g1VelWires   = "g1VelWires";
inline constexpr auto g1VelCrack   = "g1VelCrack";
inline constexpr auto g1VelDrop    = "g1VelDrop";
inline constexpr auto g1Link       = "g1Link";

// HATS -- one pair of cymbals, struck two ways: every control is shared by
// the closed and open pads except the decay, which each has its own of.
inline constexpr auto htTune      = "htTune";
inline constexpr auto htHarmonics = "htHarmonics";
inline constexpr auto htSpread    = "htSpread";
inline constexpr auto htColour    = "htColour";
inline constexpr auto htAir       = "htAir";
inline constexpr auto hcDecay     = "hcDecay";
inline constexpr auto hoDecay     = "hoDecay";
inline constexpr auto htLevel     = "htLevel";
inline constexpr auto htChoke     = "htChoke";
inline constexpr auto htVelLevel  = "htVelLevel";
inline constexpr auto htVelDecay  = "htVelDecay";
inline constexpr auto htVelColour = "htVelColour";

// CLAP
inline constexpr auto cpFlam      = "cpFlam";
inline constexpr auto cpColour    = "cpColour";
inline constexpr auto cpTail      = "cpTail";
inline constexpr auto cpLevel     = "cpLevel";
inline constexpr auto cpVelLevel  = "cpVelLevel";

// HATS, schema 7 -- the noise made a layer of its own and the metal given
// something to do besides ring at one brightness for its whole life.
inline constexpr auto htRing      = "htRing";
inline constexpr auto htDrive     = "htDrive";
inline constexpr auto htSizzle    = "htSizzle";
inline constexpr auto htAirTone   = "htAirTone";
inline constexpr auto htAirDecay  = "htAirDecay";
inline constexpr auto htWidth     = "htWidth";
inline constexpr auto htHighpass  = "htHighpass";
inline constexpr auto htDamp      = "htDamp";
inline constexpr auto htStrike    = "htStrike";
inline constexpr auto htHold      = "htHold";
inline constexpr auto htShape     = "htShape";
inline constexpr auto htVelStrike = "htVelStrike";
inline constexpr auto htGate      = "htGate";
inline constexpr auto htRelease   = "htRelease";

// CLAP, schema 7
inline constexpr auto cpBursts    = "cpBursts";
inline constexpr auto cpSkew      = "cpSkew";
inline constexpr auto cpSnap      = "cpSnap";
inline constexpr auto cpNoise     = "cpNoise";
inline constexpr auto cpNoiseTone = "cpNoiseTone";
inline constexpr auto cpBody      = "cpBody";
inline constexpr auto cpBodyPitch = "cpBodyPitch";
inline constexpr auto cpBodyRing  = "cpBodyRing";
inline constexpr auto cpWidth     = "cpWidth";
inline constexpr auto cpTailTone  = "cpTailTone";
inline constexpr auto cpDrive     = "cpDrive";

// CLAP, schema 8 -- the gate the other pads had
inline constexpr auto cpGate      = "cpGate";
inline constexpr auto cpRelease   = "cpRelease";

// SNARES, schema 9 -- a hold on the wires, so a buzz has a length of its own
inline constexpr auto s1WiresHold = "s1WiresHold";
inline constexpr auto g1WiresHold = "g1WiresHold";

// HATS, schema 10 -- the plate the six pulses could never be, and the grit of
// a low-resolution sample path (I4.3, the rig's "thin and tinny")
inline constexpr auto htPlate      = "htPlate";
inline constexpr auto htGrit       = "htGrit";

// ---- schema 11: the rig's fourth round (I4.4) --------------------------------
// HATS -- more say over the hiss, and the open pad's own hold
inline constexpr auto htAirTilt    = "htAirTilt";
inline constexpr auto htAirAttack  = "htAirAttack";
inline constexpr auto htGrain      = "htGrain";
inline constexpr auto htVelAir     = "htVelAir";
inline constexpr auto hoHold       = "hoHold";
inline constexpr auto htHoldLink   = "htHoldLink";

// KICK 1 -- the sub under the body, and the beater's low contact
inline constexpr auto k1Under         = "k1Under";
inline constexpr auto k1UnderInterval = "k1UnderInterval";
inline constexpr auto k1UnderDecay    = "k1UnderDecay";
inline constexpr auto k1UnderAttack   = "k1UnderAttack";
inline constexpr auto k1Knock         = "k1Knock";
inline constexpr auto k1KnockTone     = "k1KnockTone";
inline constexpr auto k1KnockTime     = "k1KnockTime";

// SNARE 1 and GHOST -- a low mode under the shell, and the upper modes' ring
inline constexpr auto s1Ring       = "s1Ring";
inline constexpr auto s1Thump      = "s1Thump";
inline constexpr auto s1ThumpTone  = "s1ThumpTone";
inline constexpr auto s1ThumpDecay = "s1ThumpDecay";
inline constexpr auto g1Ring       = "g1Ring";
inline constexpr auto g1Thump      = "g1Thump";
inline constexpr auto g1ThumpTone  = "g1ThumpTone";
inline constexpr auto g1ThumpDecay = "g1ThumpDecay";

// PAN -- one per pad, the first per-pad control the pads without a page get
inline constexpr auto k1Pan = "k1Pan";
inline constexpr auto s1Pan = "s1Pan";
inline constexpr auto hcPan = "hcPan";
inline constexpr auto hoPan = "hoPan";
inline constexpr auto cpPan = "cpPan";
inline constexpr auto pcPan = "pcPan";
inline constexpr auto k2Pan = "k2Pan";
inline constexpr auto g1Pan = "g1Pan";

// ---- schema 12: the rig's fifth round (I4.5) --------------------------------
// HATS -- the hiss and the metal placed across the field, and the plate washed
inline constexpr auto htAirStereo   = "htAirStereo";
inline constexpr auto htMetalStereo = "htMetalStereo";
inline constexpr auto htWash        = "htWash";

// SNARE 1 and GHOST -- the head's ratios, the wires' colour and placement, and
// the rattle's own decay, tone and tension
inline constexpr auto s1Head          = "s1Head";
inline constexpr auto s1WiresTilt     = "s1WiresTilt";
inline constexpr auto s1Bed           = "s1Bed";
inline constexpr auto s1WiresStereo   = "s1WiresStereo";
inline constexpr auto s1RattleDecay   = "s1RattleDecay";
inline constexpr auto s1RattleTone    = "s1RattleTone";
inline constexpr auto s1RattleTension = "s1RattleTension";
inline constexpr auto g1Head          = "g1Head";
inline constexpr auto g1WiresTilt     = "g1WiresTilt";
inline constexpr auto g1Bed           = "g1Bed";
inline constexpr auto g1WiresStereo   = "g1WiresStereo";
inline constexpr auto g1RattleDecay   = "g1RattleDecay";
inline constexpr auto g1RattleTone    = "g1RattleTone";
inline constexpr auto g1RattleTension = "g1RattleTension";

// CLAP -- the bursts placed across the field
inline constexpr auto cpStereo = "cpStereo";

// KICK 1 -- the drop's shape
inline constexpr auto k1DropCurve = "k1DropCurve";

// WIDTH -- one per pad, a gain on the pad's side signal. The id says "Side"
// because the hats and the clap already spend "Width" on the width of their
// band-pass; the knob on the MIX page reads Width, since that is what a mixer
// calls it.
inline constexpr auto k1Side = "k1Side";
inline constexpr auto s1Side = "s1Side";
inline constexpr auto hcSide = "hcSide";
inline constexpr auto hoSide = "hoSide";
inline constexpr auto cpSide = "cpSide";
inline constexpr auto pcSide = "pcSide";
inline constexpr auto k2Side = "k2Side";
inline constexpr auto g1Side = "g1Side";

// MONO BELOW -- one per pad, the corner under which the side is removed
inline constexpr auto k1MonoBelow = "k1MonoBelow";
inline constexpr auto s1MonoBelow = "s1MonoBelow";
inline constexpr auto hcMonoBelow = "hcMonoBelow";
inline constexpr auto hoMonoBelow = "hoMonoBelow";
inline constexpr auto cpMonoBelow = "cpMonoBelow";
inline constexpr auto pcMonoBelow = "pcMonoBelow";
inline constexpr auto k2MonoBelow = "k2MonoBelow";
inline constexpr auto g1MonoBelow = "g1MonoBelow";

// ROOM -- early reflections on the kick, the snare, the ghost and the clap
inline constexpr auto k1Room     = "k1Room";
inline constexpr auto k1RoomSize = "k1RoomSize";
inline constexpr auto k1RoomTone = "k1RoomTone";
inline constexpr auto s1Room     = "s1Room";
inline constexpr auto s1RoomSize = "s1RoomSize";
inline constexpr auto s1RoomTone = "s1RoomTone";
inline constexpr auto g1Room     = "g1Room";
inline constexpr auto g1RoomSize = "g1RoomSize";
inline constexpr auto g1RoomTone = "g1RoomTone";
inline constexpr auto cpRoom     = "cpRoom";
inline constexpr auto cpRoomSize = "cpRoomSize";
inline constexpr auto cpRoomTone = "cpRoomTone";

// SNARE 1 -- the clap engine layered under it
inline constexpr auto s1Clap       = "s1Clap";
inline constexpr auto s1ClapOffset = "s1ClapOffset";
} // namespace ids

/// The pans, indexed by PadIndex -- the order the engine reads them in.
inline constexpr const char* kPanIds[kPadCount] {
    ids::k1Pan, ids::s1Pan, ids::hcPan, ids::hoPan, ids::cpPan, ids::pcPan, ids::k2Pan, ids::g1Pan
};

static_assert (kPadCount == 8, "a ninth pad needs a pan id appended to kPanIds");

/// The widths (a gain on each pad's side signal) and the Mono below corners,
/// indexed by PadIndex like the pans.
inline constexpr const char* kWidthIds[kPadCount] {
    ids::k1Side, ids::s1Side, ids::hcSide, ids::hoSide, ids::cpSide, ids::pcSide, ids::k2Side, ids::g1Side
};

inline constexpr const char* kMonoBelowIds[kPadCount] {
    ids::k1MonoBelow, ids::s1MonoBelow, ids::hcMonoBelow, ids::hoMonoBelow,
    ids::cpMonoBelow, ids::pcMonoBelow, ids::k2MonoBelow, ids::g1MonoBelow
};

static_assert (kPadCount == 8, "a ninth pad needs a width and a mono-below id appended");

/// One room's three parameter IDs, indexed by RoomIndex.
struct RoomIds
{
    const char* level;
    const char* size;
    const char* tone;
};

inline constexpr RoomIds kRoomIds[kRoomCount] {
    { ids::k1Room, ids::k1RoomSize, ids::k1RoomTone },
    { ids::s1Room, ids::s1RoomSize, ids::s1RoomTone },
    { ids::g1Room, ids::g1RoomSize, ids::g1RoomTone },
    { ids::cpRoom, ids::cpRoomSize, ids::cpRoomTone },
};

static_assert (kRoomCount == 4, "a fifth room needs its ids appended to kRoomIds");

/// One snare-engine pad's parameter IDs, so the snare page, its pictures and
/// the parameter pull are written once and run for Snare 1 and the ghost
/// alike. `link` is null for a pad that is nobody's ghost.
struct SnareIds
{
    const char* tune;
    const char* followKey;
    const char* noteSnap;
    const char* spread;
    const char* tone;
    const char* decay;
    const char* start;
    const char* drop;
    const char* body;
    const char* wires;
    const char* snappy;
    const char* snap;
    const char* wiresHold;
    const char* wiresDecay;
    const char* rattle;
    const char* crack;
    const char* crackTone;
    const char* noise;
    const char* noiseTime;
    const char* level;
    const char* gate;
    const char* release;
    const char* velLevel;
    const char* velWires;
    const char* velCrack;
    const char* velDrop;
    const char* ring;
    const char* thump;
    const char* thumpTone;
    const char* thumpDecay;
    const char* head;
    const char* wiresTilt;
    const char* bed;
    const char* wiresStereo;
    const char* rattleDecay;
    const char* rattleTone;
    const char* rattleTension;
    const char* link;
};

inline constexpr SnareIds kSnare1Ids {
    ids::s1Tune, ids::s1FollowKey, ids::s1NoteSnap, ids::s1Spread, ids::s1Tone,
    ids::s1Decay, ids::s1Start, ids::s1Drop, ids::s1Body,
    ids::s1Wires, ids::s1Snappy, ids::s1Snap, ids::s1WiresHold, ids::s1WiresDecay, ids::s1Rattle,
    ids::s1Crack, ids::s1CrackTone, ids::s1Noise, ids::s1NoiseTime,
    ids::s1Level, ids::s1Gate, ids::s1Release,
    ids::s1VelLevel, ids::s1VelWires, ids::s1VelCrack, ids::s1VelDrop,
    ids::s1Ring, ids::s1Thump, ids::s1ThumpTone, ids::s1ThumpDecay,
    ids::s1Head, ids::s1WiresTilt, ids::s1Bed, ids::s1WiresStereo,
    ids::s1RattleDecay, ids::s1RattleTone, ids::s1RattleTension,
    nullptr
};

inline constexpr SnareIds kGhostIds {
    ids::g1Tune, ids::g1FollowKey, ids::g1NoteSnap, ids::g1Spread, ids::g1Tone,
    ids::g1Decay, ids::g1Start, ids::g1Drop, ids::g1Body,
    ids::g1Wires, ids::g1Snappy, ids::g1Snap, ids::g1WiresHold, ids::g1WiresDecay, ids::g1Rattle,
    ids::g1Crack, ids::g1CrackTone, ids::g1Noise, ids::g1NoiseTime,
    ids::g1Level, ids::g1Gate, ids::g1Release,
    ids::g1VelLevel, ids::g1VelWires, ids::g1VelCrack, ids::g1VelDrop,
    ids::g1Ring, ids::g1Thump, ids::g1ThumpTone, ids::g1ThumpDecay,
    ids::g1Head, ids::g1WiresTilt, ids::g1Bed, ids::g1WiresStereo,
    ids::g1RattleDecay, ids::g1RattleTone, ids::g1RattleTension,
    ids::g1Link
};

/// Choice lists. **APPEND-ONLY**: a choice parameter stores an index.
namespace choices
{
inline const juce::StringArray oversampling { "Auto", "Off", "x2", "x4", "x8" };
inline const juce::StringArray renderOversampling { "Same as live", "Auto", "x2", "x4", "x8" };
} // namespace choices

static_assert (static_cast<int> (dsp::OversamplingMode::Auto) == 0
            && static_cast<int> (dsp::OversamplingMode::Off)  == 1
            && static_cast<int> (dsp::OversamplingMode::X8)   == 4,
               "the oversampling option list is indexed straight into OversamplingMode");

static_assert (static_cast<int> (dsp::RenderOversampling::sameAsLive) == 0
            && static_cast<int> (dsp::RenderOversampling::Auto)       == 1
            && static_cast<int> (dsp::RenderOversampling::X8)         == 4,
               "the render option list is indexed straight into RenderOversampling");

/// The snare-engine pad's IDs: the ghost's for the second snare pad, Snare
/// 1's for anything else.
[[nodiscard]] inline const SnareIds& snareIdsFor (PadIndex pad) noexcept
{
    return pad == PadIndex::snare2 ? kGhostIds : kSnare1Ids;
}

class IctusProcessor final : public juce::AudioProcessor,
                             public ui::TuningHost
{
public:
    IctusProcessor();
    ~IctusProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /// The longest a hit can still be sounding: the decay's top plus the
    /// tail's, and the decimators after them.
    double getTailLengthSeconds() const override { return 6.1; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }
    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    // ---- the message thread's hand-offs ---------------------------------

    /// The HIT button: strikes the pad at full velocity at the top of the
    /// next block. One bit per pad, so a click is never lost or doubled.
    void triggerHit (PadIndex pad) noexcept;

    /// The pad's MIDI note (message thread; a state-tree property).
    void setPadNote (PadIndex pad, int note) noexcept;
    [[nodiscard]] int getPadNote (PadIndex pad) const noexcept;

    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }

    // ---- the tuning (message thread; mirrors Malleus) ----------------------

    juce::String loadScalaText (const juce::String& text, const juce::String& name) override;
    juce::String loadKeyboardMapText (const juce::String& text) override;
    juce::String selectBuiltInScale (const juce::String& name) override;
    void resetTuning() override;

    [[nodiscard]] const dsp::Scale& getScale() const noexcept override { return scale_; }
    [[nodiscard]] juce::String getScaleName() const noexcept override { return scaleName_; }
    [[nodiscard]] juce::String describeTuning() const override;
    [[nodiscard]] double getRootHz() const noexcept override;

    void setConcertPitch (double hz) override;
    [[nodiscard]] double getConcertPitch() const noexcept override { return concertPitchHz_; }

    /// What the current tuning plays for a key, from a message-thread twin.
    [[nodiscard]] double previewFrequencyFor (int midiNote) const;

    /// Live tooltips for the BASS lamp and the KEY switch: what a key plays
    /// right now, through which scale, in Hz -- not something to work out.
    [[nodiscard]] juce::String describeKeying() const;
    [[nodiscard]] juce::String describeFollowKey (PadIndex pad) const;

    /// The NOTE lamp's live tooltip: what this pad's Tune snaps to right now.
    [[nodiscard]] juce::String describeNoteSnap (PadIndex pad) const;

    /// Whether the ghost is linked to the main snare's drum right now.
    [[nodiscard]] bool isGhostLinked() const noexcept
    {
        return state_.getRawParameterValue (ids::g1Link)->load() > 0.5f;
    }

    /// A frequency as the nearest note of the current tuning with its cents
    /// offset, "G#1 +3c" -- the readout under a Tune knob.
    [[nodiscard]] juce::String noteNameFor (double hz) const;

    /// What Tune would snap to, from the message-thread twin of the tuning.
    [[nodiscard]] double previewSnappedHz (double hz) const;

    /// The message-thread twin itself, for the displays' note rulers.
    [[nodiscard]] const dsp::Tuning& previewTuning() const noexcept { return previewTuning_; }

    /// Per pad: hits since prepare and the last velocity, copied out of the
    /// engine at the end of every block for the pad lamps.
    [[nodiscard]] std::uint32_t getPadHitCount (PadIndex pad) const noexcept
    {
        return padHits_[static_cast<int> (pad)].load (std::memory_order_relaxed);
    }

    [[nodiscard]] float getPadLastVelocity (PadIndex pad) const noexcept
    {
        return padVelocity_[static_cast<int> (pad)].load (std::memory_order_relaxed);
    }

    // ---- what the editor reads ------------------------------------------

    [[nodiscard]] int getActiveHitCount() const noexcept { return activeHits_.load(); }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double getPreparedRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int getOversamplingFactor() const noexcept { return engine_.getOversamplingFactor(); }

    /// The Main output's correlation over the last 400 ms, full band and the
    /// band under 120 Hz, from the suite's own StereoAnalyser -- what the MIX
    /// page's readout shows. +1 is mono, 0 uncorrelated, -1 out of phase.
    [[nodiscard]] float getCorrelation() const noexcept { return correlationFull_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getLowCorrelation() const noexcept { return correlationLow_.load (std::memory_order_relaxed); }

    /// The output level over the same window, so the readout can go quiet
    /// when there is nothing to correlate.
    [[nodiscard]] float getOutputRms() const noexcept { return outputRms_.load (std::memory_order_relaxed); }

    /// Live tooltips for the header's OS and RENDER boxes: what Auto is doing
    /// at this session's actual rate, and whether a render override is in
    /// force right now.
    [[nodiscard]] juce::String describeOversampling() const;
    [[nodiscard]] juce::String describeRenderQuality() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi);

    void pullParameters();
    void pullSnare (SnareSettings& settings, const SnareIds& ids);
    void handleMidi (const juce::MidiMessage& message);

    void publishTuning();
    void collectTuning() noexcept;

    juce::AudioProcessorValueTreeState state_;
    ui::AbCompare abCompare_ { state_, {} };

    Engine engine_;
    EngineParameters parameters_;

    std::atomic<unsigned> pendingHits_ { 0 };
    std::atomic<int> padNotes_[kPadCount];
    std::atomic<std::uint32_t> padHits_[kPadCount] {};
    std::atomic<float> padVelocity_[kPadCount] {};

    bool tooltipsEnabled_ { true };
    int currentProgram_ { 0 };

    juce::AudioBuffer<double> scratch_;
    bool prepared_ { false };
    double sampleRate_ { 48000.0 };
    int reportedLatency_ { 0 };
    std::atomic<int> activeHits_ { 0 };

    // The field readout: the Main output through the stereo analyser at the
    // host rate, published per block.
    dsp::StereoAnalyser analyser_;
    std::atomic<float> correlationFull_ { 1.0f };
    std::atomic<float> correlationLow_ { 1.0f };
    std::atomic<float> outputRms_ { 0.0f };
    double rmsAccumulator_ { 0.0 };
    int rmsSamples_ { 0 };

    // ---- tuning hand-off (the Malleus arrangement) ---------------------------
    dsp::Scale scale_;
    double concertPitchHz_ { 440.0 };
    double pendingConcertHz_ { 440.0 };
    dsp::KeyboardMap keyboardMap_;
    bool hasKeyboardMap_ { false };

    dsp::Scale pendingScale_;
    dsp::KeyboardMap pendingMap_;
    std::atomic<bool> tuningPending_ { false };
    juce::SpinLock tuningLock_;

    juce::String scalaText_;
    juce::String keyboardMapText_;
    juce::String scaleName_;

    /// Message-thread twin of the engine's tuning, for the tooltips and the
    /// panel's table.
    dsp::Tuning previewTuning_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IctusProcessor)
};

} // namespace tezla::ictus
