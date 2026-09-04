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
#include <tezla/ui/AbCompare.hpp>

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

inline constexpr int kStateSchemaVersion = kSchemaV3;

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
} // namespace choices

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

class StrydaProcessor final : public juce::AudioProcessor
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
