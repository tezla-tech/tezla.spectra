// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <tezla/dsp/Divisions.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/VuMeter.hpp>
#include <tezla/ui/AbCompare.hpp>
#include <tezla/ui/TuningHost.hpp>

#include "SonitusEngine.hpp"

namespace tezla::sonitus
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin. CLAUDE.md section 8.
namespace ids
{
// ---- oscillator A ----------------------------------------------------------
inline constexpr auto shapeA      = "shapeA";
inline constexpr auto octaveA     = "octaveA";
inline constexpr auto semitonesA  = "semitonesA";
inline constexpr auto centsA      = "centsA";
inline constexpr auto widthA      = "widthA";
inline constexpr auto morphA      = "morphA";
inline constexpr auto unisonA     = "unisonA";
inline constexpr auto detuneA     = "detuneA";
inline constexpr auto spreadA     = "spreadA";
inline constexpr auto driftA      = "driftA";
inline constexpr auto levelA      = "levelA";

// ---- oscillator B ----------------------------------------------------------
inline constexpr auto shapeB      = "shapeB";
inline constexpr auto octaveB     = "octaveB";
inline constexpr auto semitonesB  = "semitonesB";
inline constexpr auto centsB      = "centsB";
inline constexpr auto widthB      = "widthB";
inline constexpr auto morphB      = "morphB";

/// Phase 4. Appended, never reordered -- CLAUDE.md section 8.
inline constexpr auto feedbackA   = "feedbackA";
inline constexpr auto feedbackB   = "feedbackB";
inline constexpr auto pmReverse   = "pmReverse";
inline constexpr auto unisonB     = "unisonB";
inline constexpr auto detuneB     = "detuneB";
inline constexpr auto spreadB     = "spreadB";
inline constexpr auto driftB      = "driftB";
inline constexpr auto levelB      = "levelB";
inline constexpr auto syncB       = "syncB";
inline constexpr auto pmIndex     = "pmIndex";

// ---- sub and destruction ---------------------------------------------------
inline constexpr auto subShape    = "subShape";
inline constexpr auto subOctave   = "subOctave";
inline constexpr auto subLevel    = "subLevel";
inline constexpr auto ringAmount  = "ringAmount";
inline constexpr auto foldAmount  = "foldAmount";

// ---- kargyraa --------------------------------------------------------------
inline constexpr auto kargyraa        = "kargyraa";
inline constexpr auto kargyraaRasp    = "kargyraaRasp";
inline constexpr auto kargyraaDivisor = "kargyraaDivisor";

// ---- filter ----------------------------------------------------------------
inline constexpr auto filterMode  = "filterMode";
inline constexpr auto cutoff      = "cutoff";
inline constexpr auto resonance   = "resonance";
inline constexpr auto filterDrive = "filterDrive";
inline constexpr auto filterTrack = "filterTrack";
inline constexpr auto filterFm    = "filterFm";
inline constexpr auto filterVel   = "filterVel";

// ---- envelopes -------------------------------------------------------------
//
// AHDSR: a **hold** between the attack and the decay, and a **bipolar tension**
// on each of the three timed segments in place of the single one-way shape the
// three used to share. The old `<x>Shape` ids are gone rather than kept as
// aliases -- nothing has shipped and nobody has a project with them in, and a
// dead id that silently does nothing is worse than one that is simply absent.
inline constexpr auto ampAttack    = "ampAttack";
inline constexpr auto ampHold      = "ampHold";
inline constexpr auto ampDecay     = "ampDecay";
inline constexpr auto ampSustain   = "ampSustain";
inline constexpr auto ampRelease   = "ampRelease";
inline constexpr auto ampAttackT   = "ampAttackT";
inline constexpr auto ampSnap     = "ampSnap";
inline constexpr auto ampDecayT    = "ampDecayT";
inline constexpr auto ampReleaseT  = "ampReleaseT";
inline constexpr auto ampVelocity  = "ampVelocity";

inline constexpr auto env1Attack   = "env1Attack";
inline constexpr auto env1Hold     = "env1Hold";
inline constexpr auto env1Decay    = "env1Decay";
inline constexpr auto env1Sustain  = "env1Sustain";
inline constexpr auto env1Release  = "env1Release";
inline constexpr auto env1AttackT  = "env1AttackT";
inline constexpr auto env1DecayT   = "env1DecayT";
inline constexpr auto env1ReleaseT = "env1ReleaseT";
inline constexpr auto env1Snap    = "env1Snap";

inline constexpr auto env2Attack   = "env2Attack";
inline constexpr auto env2Hold     = "env2Hold";
inline constexpr auto env2Decay    = "env2Decay";
inline constexpr auto env2Sustain  = "env2Sustain";
inline constexpr auto env2Release  = "env2Release";
inline constexpr auto env2AttackT  = "env2AttackT";
inline constexpr auto env2DecayT   = "env2DecayT";
inline constexpr auto env2ReleaseT = "env2ReleaseT";
inline constexpr auto env2Snap    = "env2Snap";

// ---- keyboard --------------------------------------------------------------
inline constexpr auto keyMode     = "keyMode";
inline constexpr auto polyphony   = "polyphony";
inline constexpr auto glide       = "glide";
inline constexpr auto bendRange   = "bendRange";

// ---- global modulation sources ---------------------------------------------
inline constexpr auto lfo1Wave    = "lfo1Wave";
inline constexpr auto lfo1Rate    = "lfo1Rate";
inline constexpr auto lfo1Sync    = "lfo1Sync";
inline constexpr auto lfo1Div     = "lfo1Div";
inline constexpr auto lfo1Smooth  = "lfo1Smooth";
inline constexpr auto lfo1Retrig  = "lfo1Retrig";
inline constexpr auto lfo1Key     = "lfo1Key";
inline constexpr auto lfo1Att     = "lfo1Att";
inline constexpr auto lfo2Wave    = "lfo2Wave";
inline constexpr auto lfo2Rate    = "lfo2Rate";
inline constexpr auto lfo2Sync    = "lfo2Sync";
inline constexpr auto lfo2Div     = "lfo2Div";
inline constexpr auto lfo2Smooth  = "lfo2Smooth";
inline constexpr auto lfo2Retrig  = "lfo2Retrig";
inline constexpr auto lfo2Key     = "lfo2Key";
inline constexpr auto lfo2Att     = "lfo2Att";

inline constexpr auto seqRate     = "seqRate";
inline constexpr auto seqLength   = "seqLength";
inline constexpr auto seqGlide    = "seqGlide";
inline constexpr auto seqToLfoRate = "seqToLfoRate";

/// `seq1` .. `seq16`. Built rather than listed, because sixteen near-identical
/// names invite a typo that would silently point one step at nothing.
[[nodiscard]] inline juce::String step (int index)
{
    return "seq" + juce::String (index + 1);
}

/// `adv1Enable`, `adv2T5`, and the rest of the ninety ADV-envelope names --
/// built for the same reason as `step`: ninety hand-typed identifiers invite
/// the typo that silently points one at nothing. `field` is e.g. "Enable",
/// "T5", "L2", "C8". These are parameter IDs and therefore frozen forever.
[[nodiscard]] inline juce::String adv (int envelope, const juce::String& field)
{
    return "adv" + juce::String (envelope + 1) + field;
}

/// `modSource1` / `modDest1` / `modDepth1`, and so on.
[[nodiscard]] inline juce::String modSource (int slot) { return "modSource" + juce::String (slot + 1); }
[[nodiscard]] inline juce::String modDest (int slot)   { return "modDest" + juce::String (slot + 1); }
[[nodiscard]] inline juce::String modDepth (int slot)  { return "modDepth" + juce::String (slot + 1); }

/// The global matrix, which drives the mangle rather than the voice.
[[nodiscard]] inline juce::String globalSource (int slot) { return "gmodSource" + juce::String (slot + 1); }
[[nodiscard]] inline juce::String globalDest (int slot)   { return "gmodDest" + juce::String (slot + 1); }
[[nodiscard]] inline juce::String globalDepth (int slot)  { return "gmodDepth" + juce::String (slot + 1); }

// ---- the split and the mangle ----------------------------------------------
inline constexpr auto splitHz     = "splitHz";
inline constexpr auto subMono     = "subMono";
inline constexpr auto subSplit    = "subSplit";

inline constexpr auto order       = "order";
inline constexpr auto tubeDrive   = "tubeDrive";
inline constexpr auto combMode    = "combMode";
inline constexpr auto combTime    = "combTime";
inline constexpr auto combTrack   = "combTrack";
inline constexpr auto combFeed    = "combFeed";
inline constexpr auto combDamp    = "combDamp";
inline constexpr auto combSpread  = "combSpread";
inline constexpr auto combMix     = "combMix";
inline constexpr auto combInvert  = "combInvert";
inline constexpr auto phaseFreq   = "phaseFreq";
inline constexpr auto phaseStages = "phaseStages";
inline constexpr auto formantMorph = "formantMorph";
inline constexpr auto formantSharp = "formantSharp";
inline constexpr auto formantMix  = "formantMix";
inline constexpr auto formantHarmonic = "formantHarmonic";
inline constexpr auto formantLock = "formantLock";
inline constexpr auto formantNotch = "formantNotch";
inline constexpr auto formantNotchDepth = "formantNotchDepth";
inline constexpr auto tilt        = "tilt";

// ---- global ----------------------------------------------------------------
inline constexpr auto output      = "output";
inline constexpr auto oversampling = "oversampling";
} // namespace ids

/// The option lists behind the choice parameters.
///
/// **Append-only, forever.** A choice parameter stores an *index*, not a name,
/// so inserting or reordering an entry silently repoints every saved use of it
/// -- the plugin still loads, still runs, and quietly plays a different sound.
/// CLAUDE.md section 8.
///
/// Each is checked against the enum it is read back as at compile time, so a
/// list that drifts out of step with its enum stops the build rather than the
/// project.
namespace choices
{
inline const juce::StringArray shape { "Saw", "Pulse", "Triangle", "Sine",
                                       "Vintage", "Dome", "Double saw",
                                       "Harmonic", "Noise" };
inline const juce::StringArray subShape { "Sine", "Square" };
inline const juce::StringArray filterMode { "Lowpass", "Bandpass", "Highpass", "Notch" };
inline const juce::StringArray keyMode { "Poly", "Mono", "Legato" };
inline const juce::StringArray combMode { "Off", "Flange", "Phase" };
inline const juce::StringArray order { "Tube then comb", "Comb then tube" };
inline const juce::StringArray lfoWave { "Sine", "Triangle", "Saw up", "Saw down",
                                         "Square", "Sample & hold", "Smooth random" };

/// Built from the dsp division table rather than typed beside it, so the two
/// cannot drift: an entry appended there appears here, at the same index.
inline const juce::StringArray lfoDivision = []
{
    juce::StringArray names;

    for (const auto& division : dsp::divisions)
        names.add (division.name);

    return names;
}();
inline const juce::StringArray oversampling { "Auto", "Off", "x2", "x4", "x8" };

/// The modulation sources, in the order the matrix indexes them.
inline const juce::StringArray modSource { "Off", "Amp env", "Mod env 1", "Mod env 2",
                                           "Velocity", "Key track", "Note random",
                                           "LFO 1", "LFO 2", "Sequencer",
                                           "ADV 1", "ADV 2", "ADV 3" };

/// The modulation destinations, likewise. **Continuous controls only** -- a
/// choice or a switch reconfigures rather than adjusts, so modulating one would
/// mean a filter rebuild per chunk rather than a value change.
inline const juce::StringArray modDest { "Off", "Cutoff", "Resonance", "Filter drive",
                                         "PM index", "Width A", "Width B",
                                         "Detune A", "Detune B", "Osc mix",
                                         "Sub level", "Ring", "Fold",
                                         "Pitch", "Pitch B", "Level",
                                         "Kargyraa", "Morph A", "Morph B",
                                         "Feedback A", "Feedback B", "PM reverse" };

/// How many oscillator cycles one kargyraa modulator cycle spans.
///
/// The stored value is an **index**, so this is append-only like every other
/// choice list here -- CLAUDE.md section 8.
inline const juce::StringArray kargyraaDivisor { "/2  true kargyraa", "/3", "/4" };

/// The global matrix's sources: the three that exist once rather than once per
/// note. Pointing an amp envelope at a global control has no answer when eight
/// notes are down, which is why the voice's list is not reused here.
inline const juce::StringArray globalSource { "Off", "LFO 1", "LFO 2", "Sequencer",
                                             "Amp env", "Mod env 1", "Mod env 2", "Velocity",
                                             "ADV 1", "ADV 2", "ADV 3" };

/// The global matrix's destinations: the mangle's continuous controls. **Comb
/// time is the one this instrument exists for** -- the brief's flanger-at-rate-
/// zero trick with something better than an automation lane behind it.
inline const juce::StringArray globalDest { "Off", "Comb time", "Comb feedback", "Comb mix",
                                            "Phase centre", "Vowel", "Tube", "Output",
                                            "Harmonic", "Notch" };

static_assert (static_cast<int> (dsp::OscShape::saw)       == 0
            && static_cast<int> (dsp::OscShape::pulse)     == 1
            && static_cast<int> (dsp::OscShape::triangle)  == 2
            && static_cast<int> (dsp::OscShape::sine)      == 3
            && static_cast<int> (dsp::OscShape::vintage)   == 4
            && static_cast<int> (dsp::OscShape::dome)      == 5
            && static_cast<int> (dsp::OscShape::doubleSaw) == 6
            && static_cast<int> (dsp::OscShape::harmonic)  == 7
            && static_cast<int> (dsp::OscShape::noise)     == 8
            && static_cast<int> (dsp::OscShape::count)     == 9,
               "the shape option list is indexed straight into OscShape");

static_assert (static_cast<int> (SubShape::sine)   == 0
            && static_cast<int> (SubShape::square) == 1
            && static_cast<int> (SubShape::count)  == 2,
               "the sub shape option list is indexed straight into SubShape");

static_assert (static_cast<int> (dsp::SvfMode::lowpass)  == 0
            && static_cast<int> (dsp::SvfMode::bandpass) == 1
            && static_cast<int> (dsp::SvfMode::highpass) == 2
            && static_cast<int> (dsp::SvfMode::notch)    == 3
            && static_cast<int> (dsp::SvfMode::count)    == 4,
               "the filter mode option list is indexed straight into SvfMode");

static_assert (static_cast<int> (KeyboardMode::poly)   == 0
            && static_cast<int> (KeyboardMode::mono)   == 1
            && static_cast<int> (KeyboardMode::legato) == 2
            && static_cast<int> (KeyboardMode::count)  == 3,
               "the keyboard option list is indexed straight into KeyboardMode");

static_assert (static_cast<int> (CombMode::off)    == 0
            && static_cast<int> (CombMode::flange) == 1
            && static_cast<int> (CombMode::phase)  == 2
            && static_cast<int> (CombMode::count)  == 3,
               "the comb option list is indexed straight into CombMode");

static_assert (static_cast<int> (MangleOrder::tubeThenComb) == 0
            && static_cast<int> (MangleOrder::combThenTube) == 1
            && static_cast<int> (MangleOrder::count)        == 2,
               "the order option list is indexed straight into MangleOrder");

static_assert (static_cast<int> (dsp::Lfo::Wave::sine)         == 0
            && static_cast<int> (dsp::Lfo::Wave::smoothRandom) == 6
            && dsp::Lfo::kNumWaves == 7,
               "the LFO wave option list is indexed straight into Lfo::Wave");

static_assert (static_cast<int> (dsp::OversamplingMode::Auto) == 0
            && static_cast<int> (dsp::OversamplingMode::Off)  == 1
            && static_cast<int> (dsp::OversamplingMode::X8)   == 4,
               "the oversampling option list is indexed straight into OversamplingMode");

static_assert (static_cast<int> (ModSource::none)      == 0
            && static_cast<int> (ModSource::sequencer) == 9
            && static_cast<int> (ModSource::advEnv1)   == 10
            && static_cast<int> (ModSource::advEnv3)   == 12
            && static_cast<int> (ModSource::count)     == 13,
               "the modulation source list is indexed straight into ModSource");

static_assert (static_cast<int> (ModDestination::none)     == 0
            && static_cast<int> (ModDestination::level)    == 15
            && static_cast<int> (ModDestination::kargyraa) == 16
            && static_cast<int> (ModDestination::morphA)   == 17
            && static_cast<int> (ModDestination::morphB)    == 18
            && static_cast<int> (ModDestination::feedbackA) == 19
            && static_cast<int> (ModDestination::feedbackB) == 20
            && static_cast<int> (ModDestination::pmReverse) == 21
            && static_cast<int> (ModDestination::count)     == 22,
               "the modulation destination list is indexed straight into ModDestination");

static_assert (static_cast<int> (GlobalSource::none)         == 0
            && static_cast<int> (GlobalSource::sequencer)    == 3
            && static_cast<int> (GlobalSource::ampEnvelope)  == 4
            && static_cast<int> (GlobalSource::velocity)     == 7
            && static_cast<int> (GlobalSource::advEnv1)      == 8
            && static_cast<int> (GlobalSource::advEnv3)      == 10
            && static_cast<int> (GlobalSource::count)        == 11,
               "the global source list is indexed straight into GlobalSource");

static_assert (static_cast<int> (GlobalDestination::none)            == 0
            && static_cast<int> (GlobalDestination::output)          == 7
            && static_cast<int> (GlobalDestination::formantHarmonic) == 8
            && static_cast<int> (GlobalDestination::formantNotch)    == 9
            && static_cast<int> (GlobalDestination::count)           == 10,
               "the global destination list is indexed straight into GlobalDestination");
} // namespace choices

// ui::TuningHost so the shared tuning panel can drive it -- the methods
// below already had exactly the interface's names and signatures.
class SonitusProcessor final : public juce::AudioProcessor,
                               public ui::TuningHost
{
public:
    SonitusProcessor();
    ~SonitusProcessor() override = default;

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

    /// The longest a note can ring after the last note-off: the amp release,
    /// plus the filter's own decay at full resonance. Hosts use this to decide
    /// how long to keep rendering after a stop, and reporting zero from an
    /// instrument truncates every tail.
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }

    /// **DICEROLL.** Sets every sound parameter to a uniform random value over
    /// its whole range.
    ///
    /// Every one, at both extremes -- this is not a nudge and is not meant to
    /// be usable most of the time. The interesting rolls are the one in twenty
    /// that are, and the point of the control is to find them faster than a
    /// person can turn a hundred and forty knobs.
    ///
    /// Two things it does **not** touch, for different reasons:
    ///
    ///  - **Bypass**, because it is a transport control rather than a sound. A
    ///    dice that silenced the plugin half the time would read as broken
    ///    rather than as random.
    ///  - **The tuning** -- the scale and the concert pitch -- and that one
    ///    needed no code at all: they were deliberately never made parameters
    ///    (see `setConcertPitch`), because a scale is a rig decision that
    ///    presets must not reset. A parameter randomiser cannot reach them by
    ///    construction, which is the same property that keeps presets off them.
    ///
    /// There is no undo. The A/B slots in the header are how a patch worth
    /// keeping survives a roll: COPY it across first.
    void randomizeAllParameters();

    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// What the output is doing, for the panel's meter.
    ///
    /// An instrument has no input to meter, so there is one of these rather than
    /// a pair -- but it still gets both readings. A VU and a peak differ by ten
    /// decibels or more on a reese, which is exactly the kind of signal where
    /// "it looks loud enough" and "it is clipping the converter" are different
    /// questions. CLAUDE.md section 7.
    struct MeterValues
    {
        std::atomic<float> outputVuDb   { -100.0f };
        std::atomic<float> outputPeakDb { -100.0f };
    };

    [[nodiscard]] MeterValues& getMeterValues() noexcept { return meters_; }

    /// What the panel shows: where the LFOs and the sequencer are, and where
    /// the comb's notch is actually sitting. Published by the audio thread
    /// through atomics, because the message thread reads them while it moves.
    [[nodiscard]] const Engine::Readouts& getReadouts() const noexcept
    {
        return engine_.readouts();
    }

    [[nodiscard]] int getSequencerStep() const noexcept
    {
        return engine_.readouts().sequencerStep.load (std::memory_order_relaxed);
    }

    /// Where the comb's first notch is actually sitting -- key tracking and the
    /// global matrix included, rather than worked out from the knob.
    [[nodiscard]] double getCombNotchHz() const noexcept
    {
        return engine_.readouts().combNotchHz.load (std::memory_order_relaxed);
    }

    /// The tracked note's envelope level, 0 for the amplitude envelope and 1 or
    /// 2 for the mod ones. The panel draws it as a playhead on the curve.
    /// Whether the panel shows its tooltips. Not a parameter -- it changes
    /// nothing about the sound and a host has no business automating it -- but
    /// it is saved with the state, because a preference that resets every time
    /// the plugin is reopened is not a preference.
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }
    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }

    [[nodiscard]] double getEnvelopeLevel (int index) const noexcept
    {
        if (index < 0 || index > 2)
            return 0.0;

        return engine_.readouts().envelopeLevels[index].load (std::memory_order_relaxed);
    }

    // ---- tuning ------------------------------------------------------------

    /// Loads a Scala scale file's **text**. Returns an empty string on success
    /// and the reason with its line number otherwise.
    ///
    /// Text rather than a path, so the DSP layer never touches the filesystem
    /// and the whole thing stays testable. The editor reads the file.
    juce::String loadScalaText (const juce::String& text, const juce::String& name);

    /// The same for a `.kbm` keyboard map.
    juce::String loadKeyboardMapText (const juce::String& text);

    /// Selects one of the built-in scales by name. Empty string on success.
    juce::String selectBuiltInScale (const juce::String& name);

    /// Back to twelve-tone equal, with no keyboard map.
    void resetTuning();

    [[nodiscard]] juce::String getScaleName() const;

    /// The pitch standard, as what A440 is moved to -- one ratio over the
    /// whole tuning, .kbm reference included. Clamped to Tuning's 380-500 Hz.
    /// Deliberately not a parameter: like the scale it is a rig decision that
    /// presets must not reset, saved beside the scale in the plugin state.
    void setConcertPitch (double hz);
    [[nodiscard]] double getConcertPitch() const noexcept { return concertPitchHz_; }

    /// Degree 0's note and sounding frequency, for the panel's Hz column --
    /// middle C without a map, the map's middle note with one.
    [[nodiscard]] int getRootNote() const;
    [[nodiscard]] double getRootHz() const;

    /// The scale as loaded -- degrees, repeat, and for the built-ins the
    /// construction and story the tuning panel shows. Message thread only,
    /// like every other panel accessor: the engine has its own copy.
    [[nodiscard]] const dsp::Scale& getScale() const noexcept { return scale_; }

    /// A sentence describing the tuning currently loaded -- how many notes, what
    /// it repeats at, and whether that is an octave.
    [[nodiscard]] juce::String describeTuning() const;

    // ---- what the panel reads ----------------------------------------------

    [[nodiscard]] juce::String describeOversampling() const;
    [[nodiscard]] juce::String describeLatency() const;
    [[nodiscard]] juce::String describeComb() const;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] int getActiveVoiceCount() const noexcept { return activeVoices_.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi);

    void pullParameters();
    void handleMidi (const juce::MidiMessage& message);

    /// Message thread. Copies the loaded tuning into the hand-off slot and
    /// raises the flag; the audio thread picks it up at the top of its next
    /// block.
    void publishTuning();

    /// Audio thread. Takes the hand-off if there is one and the message thread
    /// is not mid-write. **Never blocks** -- a failed try-lock simply leaves
    /// the flag up and the next block gets it, which at 512 samples is 11 ms
    /// later and inaudible for something a human just clicked.
    void collectTuning() noexcept;

    juce::AudioProcessorValueTreeState state_;

    Engine engine_;
    EngineParameters parameters_;

    /// The tuning lives here rather than in the engine's own `Tuning`, because
    /// it has to survive a `prepare()` and be saved with the state. The engine's
    /// copy is refreshed from this one.
    dsp::Scale scale_;
    double concertPitchHz_ { 440.0 };
    double pendingConcertHz_ { 440.0 };
    dsp::KeyboardMap keyboardMap_;
    bool hasKeyboardMap_ { false };

    /// **The hand-off, and it is not optional.** A scale is a `std::vector`, and
    /// the audio thread reads it inside `noteOn` to work out the frequency. A
    /// host calls `setStateInformation` with audio running, so assigning
    /// straight into the engine's tuning from the message thread means a
    /// reallocation under a pointer the audio thread is dereferencing. Rare,
    /// and a crash when it happens.
    ///
    /// So the message thread fills these and raises `tuningPending_`; the audio
    /// thread *swaps* them into the engine, which allocates nothing and leaves
    /// the old vectors here to be freed on the message thread next time.
    dsp::Scale pendingScale_;
    dsp::KeyboardMap pendingMap_;
    std::atomic<bool> tuningPending_ { false };

    /// Held properly by the message thread and only ever *tried* by the audio
    /// thread, which is what keeps `processBlock` lock-free -- CLAUDE.md
    /// section 2.2. A try-lock that fails is not a wait.
    juce::SpinLock tuningLock_;

    /// The `.scl` text as loaded, kept verbatim so the state can save it. A
    /// project that reopens on another machine has to reproduce the tuning
    /// without needing the file, which means storing the file.
    juce::String scalaText_;

    bool tooltipsEnabled_ { true };
    juce::String keyboardMapText_;
    juce::String scaleName_;

    /// Double-precision scratch: the DSP is double throughout, so a float host
    /// buffer is converted here rather than compromising the processing.
    juce::AudioBuffer<double> scratch_;
    std::array<double*, 2> channelPointers_ {};

    dsp::VuMeter outputMeter_[2];
    MeterValues meters_;

    int reportedLatency_ { 0 };
    bool prepared_ { false };

    std::atomic<int> activeVoices_ { 0 };

    ui::AbCompare abCompare_ { state_, {} };

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SonitusProcessor)
};

} // namespace tezla::sonitus
