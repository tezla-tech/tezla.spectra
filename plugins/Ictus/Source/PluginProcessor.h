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

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

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
} // namespace ids

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
    const char* link;
};

inline constexpr SnareIds kSnare1Ids {
    ids::s1Tune, ids::s1FollowKey, ids::s1NoteSnap, ids::s1Spread, ids::s1Tone,
    ids::s1Decay, ids::s1Start, ids::s1Drop, ids::s1Body,
    ids::s1Wires, ids::s1Snappy, ids::s1Snap, ids::s1WiresDecay, ids::s1Rattle,
    ids::s1Crack, ids::s1CrackTone, ids::s1Noise, ids::s1NoiseTime,
    ids::s1Level, ids::s1Gate, ids::s1Release,
    ids::s1VelLevel, ids::s1VelWires, ids::s1VelCrack, ids::s1VelDrop,
    nullptr
};

inline constexpr SnareIds kGhostIds {
    ids::g1Tune, ids::g1FollowKey, ids::g1NoteSnap, ids::g1Spread, ids::g1Tone,
    ids::g1Decay, ids::g1Start, ids::g1Drop, ids::g1Body,
    ids::g1Wires, ids::g1Snappy, ids::g1Snap, ids::g1WiresDecay, ids::g1Rattle,
    ids::g1Crack, ids::g1CrackTone, ids::g1Noise, ids::g1NoiseTime,
    ids::g1Level, ids::g1Gate, ids::g1Release,
    ids::g1VelLevel, ids::g1VelWires, ids::g1VelCrack, ids::g1VelDrop,
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
