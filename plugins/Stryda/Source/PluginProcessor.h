// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The JUCE wrapper around the Stryda engine. Thin by design (CLAUDE.md
// section 4): parameters, state, MIDI, and the oversampling plumbing every
// oversampling plugin in the suite shares.
//
// F3 shape: the six operators, the thirty off-diagonal matrix cells, the noise
// row, and five globals -- 101 parameters, all born at schema 1. Everything
// later is APPENDED with the schema version it was born at, and nothing here is
// ever reordered (CLAUDE.md section 8).

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/FmBandwidth.hpp>
#include <tezla/dsp/FmOperator.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/dsp/ScalaFile.hpp>
#include <tezla/dsp/Scales.hpp>
#include <tezla/ui/AbCompare.hpp>
#include <tezla/ui/TuningHost.hpp>

#include <Braids.hpp>
#include <StrydaEngine.hpp>

namespace tezla::stryda {

namespace dsp = tezla::dsp;

inline constexpr int kNumOperators = OperatorMatrix::kNumOperators;

/// Parameter string IDs.
///
/// **The naming rule is frozen, not just the strings.** Per-operator ids are
/// `o<n><Name>` with n from 1 to 6; matrix cells are `m<to><from>`, both 1-based
/// and never equal (the diagonal is `o<n>Feedback`); the noise row is
/// `n<to>`. Renaming one -- or changing the rule that builds it -- resets that
/// control in every project that used it (CLAUDE.md section 8), so the
/// generator below is as load-bearing as a literal would be.
namespace ids
{
[[nodiscard]] inline juce::String op (int index, const char* name)
{
    return "o" + juce::String (index + 1) + name;
}

[[nodiscard]] inline juce::String cell (int to, int from)
{
    return "m" + juce::String (to + 1) + juce::String (from + 1);
}

[[nodiscard]] inline juce::String noise (int to)
{
    return "n" + juce::String (to + 1);
}

inline constexpr auto oversampling       = "oversampling";
inline constexpr auto renderOversampling = "renderOversampling";
inline constexpr auto polyphony          = "polyphony";
inline constexpr auto master             = "master";
inline constexpr auto indexCap           = "indexCap";

// ---- F5, appended at schema 3 ---------------------------------------------

inline constexpr auto filterCutoff   = "filterCutoff";
inline constexpr auto filterReso     = "filterReso";
inline constexpr auto filterMorph    = "filterMorph";
inline constexpr auto filterKeyTrack = "filterKeyTrack";
inline constexpr auto filterEnv      = "filterEnv";
inline constexpr auto filterDrive    = "filterDrive";
inline constexpr auto filterSing     = "filterSing";
inline constexpr auto filterAttack   = "filterAttack";
inline constexpr auto filterDecay    = "filterDecay";
inline constexpr auto filterSustain  = "filterSustain";
inline constexpr auto filterRelease  = "filterRelease";

inline constexpr auto subLevel   = "subLevel";
inline constexpr auto subOctave  = "subOctave";
inline constexpr auto subShape   = "subShape";
inline constexpr auto subAttack  = "subAttack";
inline constexpr auto subDecay   = "subDecay";
inline constexpr auto subSustain = "subSustain";
inline constexpr auto subRelease = "subRelease";

inline constexpr auto unison       = "unison";
inline constexpr auto unisonDetune = "unisonDetune";
inline constexpr auto unisonSpread = "unisonSpread";
inline constexpr auto unisonIndex  = "unisonIndex";

// ---- F6, appended at schema 4 ---------------------------------------------

/// A step's ratio. `s<n>` with n from 1 to 16, one-based like every other
/// generated id here, and the RULE is frozen exactly as much as the strings.
[[nodiscard]] inline juce::String step (int index)
{
    return "s" + juce::String (index + 1);
}

inline constexpr auto seqOn       = "seqOn";
inline constexpr auto seqTarget   = "seqTarget";
inline constexpr auto seqLength   = "seqLength";
inline constexpr auto seqDivision = "seqDivision";
inline constexpr auto seqGlide    = "seqGlide";

// ---- F7, appended at schema 5 ---------------------------------------------

inline constexpr auto split = "split";

inline constexpr auto vowelMix       = "vowelMix";
inline constexpr auto vowelMorph     = "vowelMorph";
inline constexpr auto vowelTract     = "vowelTract";
inline constexpr auto vowelSharp     = "vowelSharp";
inline constexpr auto vowelSeqOn     = "vowelSeqOn";
inline constexpr auto vowelSeqLength = "vowelSeqLength";
inline constexpr auto vowelSeqDiv    = "vowelSeqDiv";
inline constexpr auto vowelSeqGlide  = "vowelSeqGlide";

/// A vowel step, `v<n>` with n from 1 to 16. Same frozen rule as `step`.
[[nodiscard]] inline juce::String vowelStep (int index)
{
    return "v" + juce::String (index + 1);
}

inline constexpr auto fold        = "mangleFold";
inline constexpr auto crushBits   = "crushBits";
inline constexpr auto crushAmount = "crushAmount";
inline constexpr auto downsample  = "downsample";

inline constexpr auto combMix      = "combMix";
inline constexpr auto combHz       = "combHz";
inline constexpr auto combFeedback = "combFeedback";

inline constexpr auto phaserMix      = "phaserMix";
inline constexpr auto phaserHz       = "phaserHz";
inline constexpr auto phaserFeedback = "phaserFeedback";

inline constexpr auto mangleDrive = "mangleDrive";

inline constexpr auto compThreshold = "compThreshold";
inline constexpr auto compRatio     = "compRatio";
inline constexpr auto compAttack    = "compAttack";
inline constexpr auto compRelease   = "compRelease";
inline constexpr auto compMakeup    = "compMakeup";

// ---- F8, appended at schema 6 ---------------------------------------------
//
// The generated ids are as frozen as the literals: `e<n>p<m>{Time,Level,Tens}`
// for an ADV envelope's breakpoints, `l<n><Name>` for an LFO, `mac<n>` for a
// macro, and `mod<n>{Src,Dst,Amt}` for a slot.

[[nodiscard]] inline juce::String advPoint (int envelope, int point, const char* field)
{
    return "e" + juce::String (envelope + 1) + "p" + juce::String (point + 1) + field;
}

[[nodiscard]] inline juce::String adv (int envelope, const char* field)
{
    return "e" + juce::String (envelope + 1) + field;
}

[[nodiscard]] inline juce::String lfo (int index, const char* field)
{
    return "l" + juce::String (index + 1) + field;
}

[[nodiscard]] inline juce::String macro (int index)
{
    return "mac" + juce::String (index + 1);
}

[[nodiscard]] inline juce::String modSlot (int index, const char* field)
{
    return "mod" + juce::String (index + 1) + field;
}
} // namespace ids

/// Every parameter Stryda has ever had was born at one of these. A live
/// parameter keeps its version forever: the hint feeds the VST3 parameter ID,
/// and bumping it on an existing control is indistinguishable from renaming it.
inline constexpr int kSchemaV1 = 1;

/// F4: Fold, the formant mode, key scaling and velocity. **Appended**, never
/// inserted, and every parameter born at V1 keeps V1 forever -- the hint feeds
/// the VST3 parameter ID, so bumping it on a live control is indistinguishable
/// from renaming it.
inline constexpr int kSchemaV2 = 2;

/// F5: the per-voice filter, the protected sub lane and unison. **Appended**,
/// and every one of the twenty-two defaults to neutral -- the filter wide open
/// and bypassed bit-exactly, the sub at zero level, unison at a count of one --
/// so a project saved before F5 reopens sounding identical.
inline constexpr int kSchemaV3 = 3;

/// F6: the three ratio modes, the ratio sequencer, and the tuning. Appended.
/// Every operator's ratio mode defaults to **Free**, which returns its input
/// bit for bit, and the sequencer defaults to off -- so an F5 project is
/// untouched.
inline constexpr int kSchemaV4 = 4;

/// F7: Split, the vowel lane and the mangle chain. Appended, and every one of
/// the thirty-two is at a value the stage is SKIPPED at -- not merely
/// transparent at. An F6 project is bit-identical.
inline constexpr int kSchemaV5 = 5;

/// F8: two ADV envelopes, two LFOs, four macros and eight modulation slots.
/// Appended, and every slot's source and destination default to Off -- so the
/// whole layer is skipped and an F7 project is bit-identical.
inline constexpr int kSchemaV6 = 6;

/// F9: a waveform choice per operator. **Sine is index 0 and is bit-exact**,
/// so every project saved before shapes existed reopens sounding the same.
inline constexpr int kSchemaV7 = 7;

inline constexpr int kStateSchemaVersion = kSchemaV7;

namespace choices
{
/// **Append-only.** A choice parameter stores an index, not a name.
inline const juce::StringArray oversampling { "Auto", "Off", "x2", "x4", "x8" };
inline const juce::StringArray render { "Same as live", "Auto", "x2", "x4", "x8" };
inline const juce::StringArray indexCap { "Off", "Soft", "Hard" };

/// **Append-only.** `normal` must stay index 0 so every project saved before
/// F4 reopens with its operators unchanged.
inline const juce::StringArray operatorMode { "Normal", "Formant" };

/// **Append-only**, and the order is the stored index: 0 is two octaves down,
/// 1 is one, 2 is the note itself. The default is 1, which is where a sub
/// under a bass patch usually wants to sit.
inline const juce::StringArray subOctave { "-2 oct", "-1 oct", "Unison" };

/// **Append-only.** Sine first, because a sub that is not a sine is a choice
/// rather than a default.
inline const juce::StringArray subShape { "Sine", "Triangle" };

/// **Append-only**, and `Free` must stay index 0: it is the default and it is
/// the only one that leaves a ratio exactly as the patch set it.
inline const juce::StringArray ratioMode { "Free", "Harmonic", "Scale" };

/// Which operator the ratio sequencer drives. Index 0 is "none", so the
/// sequencer has a destination-less state that is not a magic number.
inline const juce::StringArray seqTarget { "Off", "Op 1", "Op 2", "Op 3",
                                           "Op 4", "Op 5", "Op 6" };

/// **Append-only**, and built from the frozen tables rather than retyped: a
/// slot stores an index, so a list that drifts from `source::names` or
/// `dest::names` repoints every saved modulation.
[[nodiscard]] inline juce::StringArray fromTable (const char* const* names, int count)
{
    juce::StringArray list;

    for (int i = 0; i < count; ++i)
        list.add (names[i]);

    return list;
}

inline const juce::StringArray modSources = fromTable (source::names, source::count);
inline const juce::StringArray modDests = fromTable (dest::names, dest::count);

/// **Append-only.** Matches `dsp::Lfo::Wave` index for index.
/// **Append-only**, built from `dsp::fmShapeNames` rather than retyped: an
/// operator stores an index, so a list that drifts from the enum repoints every
/// saved operator's waveform.
inline const juce::StringArray fmShape
    = fromTable (dsp::fmShapeNames, static_cast<int> (dsp::FmShape::count));

inline const juce::StringArray lfoWave { "Sine", "Triangle", "Saw up", "Saw down",
                                         "Square", "Random", "Smooth random" };
} // namespace choices

static_assert (static_cast<int> (RatioMode::free) == 0
                 && static_cast<int> (RatioMode::harmonic) == 1
                 && static_cast<int> (RatioMode::scale) == 2,
               "choices::ratioMode must match RatioMode, index for index");

static_assert (static_cast<int> (dsp::OversamplingMode::Auto) == 0
                 && static_cast<int> (dsp::OversamplingMode::Off) == 1
                 && static_cast<int> (dsp::OversamplingMode::X2) == 2
                 && static_cast<int> (dsp::OversamplingMode::X4) == 3
                 && static_cast<int> (dsp::OversamplingMode::X8) == 4,
               "choices::oversampling must match dsp::OversamplingMode, index for index");

static_assert (static_cast<int> (dsp::FmOperator::Mode::normal) == 0
                 && static_cast<int> (dsp::FmOperator::Mode::formant) == 1,
               "choices::operatorMode must match dsp::FmOperator::Mode, index for index");

static_assert (static_cast<int> (dsp::RenderOversampling::sameAsLive) == 0
                 && static_cast<int> (dsp::RenderOversampling::Auto) == 1
                 && static_cast<int> (dsp::RenderOversampling::X2) == 2
                 && static_cast<int> (dsp::RenderOversampling::X4) == 3
                 && static_cast<int> (dsp::RenderOversampling::X8) == 4,
               "choices::render must match dsp::RenderOversampling, index for index");

/// A preset carries its own notes beside the settings they describe, and the
/// field is not defaultable -- a preset added without them does not compile.
struct Setting
{
    juce::String id;
    float value {};
};

struct Preset
{
    const char* name;
    const char* notes;
    std::vector<Setting> settings;
};

class StrydaProcessor final : public juce::AudioProcessor,
                             public ui::TuningHost
{
public:
    StrydaProcessor();
    ~StrydaProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }
    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// What the panel needs to say out loud.
    [[nodiscard]] juce::String describeOversampling() const;
    [[nodiscard]] juce::String describeRenderQuality() const;

    // ---- the tuning (message thread; mirrors Malleus and Ictus) ------------
    //
    // Stryda uses it for one thing nothing else here does: an operator's RATIO
    // can snap to the loaded scale's degrees. In FM the ratio is the interval,
    // so the whole sideband ladder then lands on that scale rather than on
    // 12-TET's -- at every key, because a ratio is an interval and not a pitch.

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

    /// What an operator's ratio actually becomes, after its mode. For the
    /// panel's readout, so a snapped ratio shows what it snapped to rather
    /// than what was asked for.
    [[nodiscard]] double resolvedRatio (int op) const;

    /// Write a braid's matrix and levels into the parameters. The player's
    /// ratios, envelopes and Character are left alone -- a topology is a
    /// starting point, not a patch.
    void applyBraid (int index);

    /// The live bandwidth readout: the predicted top sideband for the note last
    /// played, against the internal Nyquist.
    [[nodiscard]] double getPredictedTopHz() const noexcept { return predictedTopHz_.load(); }
    [[nodiscard]] double getInternalNyquistHz() const noexcept { return internalNyquist_.load(); }
    [[nodiscard]] double getIndexScale() const noexcept { return indexScale_.load(); }
    [[nodiscard]] int getActiveVoices() const noexcept { return activeVoices_.load(); }

    [[nodiscard]] bool areTooltipsEnabled() const noexcept { return tooltipsEnabled_; }
    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static const std::vector<Preset>& getPresets();

private:
    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi);

    void pullParameters();
    void handleMidi (const juce::MidiMessage& message);

    [[nodiscard]] float raw (const juce::String& id) const noexcept
    {
        const auto* value = state_.getRawParameterValue (id);
        return value != nullptr ? value->load() : 0.0f;
    }

    juce::AudioProcessorValueTreeState state_;

    // ---- the tuning, message-thread side ----------------------------------
    //
    // The engine's `dsp::Tuning` is the audio-thread copy; these are what the
    // panel edits and what the state saves. `publishTuning` hands them across
    // under a spin lock, exactly as Malleus and Ictus do.
    dsp::Scale scale_ { dsp::scales::twelveToneEqual() };
    juce::String scaleName_ { scale_.name };
    juce::String scalaText_;
    juce::String keyboardMapText_;
    dsp::KeyboardMap keyboardMap_ {};
    bool hasKeyboardMap_ { false };
    double concertPitchHz_ { 440.0 };

    /// The host tempo from the last block, so a synced LFO can resolve its
    /// division without every voice having to be told the tempo separately.
    double lastBpm_ { 120.0 };

    juce::SpinLock tuningLock_;
    std::atomic<bool> tuningPending_ { false };
    dsp::Scale pendingScale_;
    dsp::KeyboardMap pendingMap_;
    double pendingConcertHz_ { 440.0 };

    void publishTuning();
    void collectTuning() noexcept;
    ui::AbCompare abCompare_ { state_, {} };

    StrydaEngine engine_;
    VoiceParameters parameters_ {};
    juce::AudioBuffer<double> scratch_;

    double sampleRate_ { 48000.0 };
    bool prepared_ { false };
    int reportedLatency_ { 0 };
    int currentProgram_ { 0 };
    bool tooltipsEnabled_ { true };
    int lastNote_ { 60 };

    std::atomic<double> predictedTopHz_ { 0.0 };
    std::atomic<double> internalNyquist_ { 96000.0 };
    std::atomic<double> indexScale_ { 1.0 };
    std::atomic<int> activeVoices_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrydaProcessor)
};

} // namespace tezla::stryda
