// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/BypassMixer.hpp>
#include <tezla/ui/AbCompare.hpp>

#include "SyrinxEngine.hpp"

namespace tezla::syrinx
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin. See CLAUDE.md
/// section 8.
///
/// Grouped and named by stage, because a strip's controls only make sense as
/// stages -- `comp1Attack` and `comp2Attack` are the same control on two
/// different boxes and nothing but the prefix says which.
namespace ids
{
inline constexpr auto inputTrim      = "inputTrim";
inline constexpr auto highpass       = "highpass";

inline constexpr auto gateOn         = "gateOn";
inline constexpr auto gateThreshold  = "gateThreshold";
inline constexpr auto gateHysteresis = "gateHysteresis";
inline constexpr auto gateRange      = "gateRange";
inline constexpr auto gateAttack     = "gateAttack";
inline constexpr auto gateHold       = "gateHold";
inline constexpr auto gateRelease    = "gateRelease";
inline constexpr auto gateSidechain  = "gateSidechain";

inline constexpr auto deEssOn        = "deEssOn";
inline constexpr auto deEssCorner    = "deEssCorner";
inline constexpr auto deEssThreshold = "deEssThreshold";
inline constexpr auto deEssRatio     = "deEssRatio";
inline constexpr auto deEssKnee      = "deEssKnee";
inline constexpr auto deEssRange     = "deEssRange";
inline constexpr auto deEssAttack    = "deEssAttack";
inline constexpr auto deEssRelease   = "deEssRelease";
inline constexpr auto deEssListen    = "deEssListen";

inline constexpr auto comp1On        = "comp1On";
inline constexpr auto comp1Threshold = "comp1Threshold";
inline constexpr auto comp1Ratio     = "comp1Ratio";
inline constexpr auto comp1Knee      = "comp1Knee";
inline constexpr auto comp1Attack    = "comp1Attack";
inline constexpr auto comp1Release   = "comp1Release";
inline constexpr auto comp1Makeup    = "comp1Makeup";
inline constexpr auto comp1Mix       = "comp1Mix";
inline constexpr auto comp1Sidechain = "comp1Sidechain";
inline constexpr auto comp1Auto      = "comp1Auto";

inline constexpr auto comp2On        = "comp2On";
inline constexpr auto comp2Threshold = "comp2Threshold";
inline constexpr auto comp2Ratio     = "comp2Ratio";
inline constexpr auto comp2Knee      = "comp2Knee";
inline constexpr auto comp2Attack    = "comp2Attack";
inline constexpr auto comp2Release   = "comp2Release";
inline constexpr auto comp2Makeup    = "comp2Makeup";
inline constexpr auto comp2Mix       = "comp2Mix";
inline constexpr auto comp2Sidechain = "comp2Sidechain";
inline constexpr auto comp2Auto      = "comp2Auto";

inline constexpr auto eqOn           = "eqOn";
inline constexpr auto eqLowHz        = "eqLowHz";
inline constexpr auto eqLowDb        = "eqLowDb";
inline constexpr auto eqMidHz        = "eqMidHz";
inline constexpr auto eqMidQ         = "eqMidQ";
inline constexpr auto eqMidDb        = "eqMidDb";
inline constexpr auto eqHighHz       = "eqHighHz";
inline constexpr auto eqHighDb       = "eqHighDb";

inline constexpr auto outputTrim     = "outputTrim";
inline constexpr auto bypass         = "bypass";
} // namespace ids

class SyrinxProcessor final : public juce::AudioProcessor
{
public:
    SyrinxProcessor();
    ~SyrinxProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }

    /// What each stage is doing, written by the audio thread and read by the
    /// editor. Atomics rather than a lock, because the audio thread may not
    /// block.
    ///
    /// Per stage, and that is the whole point of a strip's display: one meter
    /// showing the total reduction would hide *which* box is doing the work,
    /// and the answer to "why does this sound squashed" is almost always that
    /// one of them is doing all of it.
    struct MeterValues
    {
        std::atomic<float> gateDb      { 0.0f };
        std::atomic<float> deEssDb     { 0.0f };
        std::atomic<float> comp1Db     { 0.0f };
        std::atomic<float> comp2Db     { 0.0f };

        /// The de-esser's detector reading: high-band energy against body
        /// energy, in dB. Its own identity display, since this is the number
        /// that makes the stage different from an HF threshold.
        std::atomic<float> sibilanceDb { -120.0f };

        std::atomic<float> inputDb     { -100.0f };
        std::atomic<float> outputDb    { -100.0f };
    };

    [[nodiscard]] MeterValues& getMeterValues() noexcept { return meters_; }

    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// Whether the panel shows its hover tooltips. Not a parameter -- it
    /// changes nothing about the sound, so it has no business in an automation
    /// lane or a preset. It lives here rather than in the editor because the
    /// editor is destroyed every time the window closes.
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }
    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }

    /// True when the current **parameters** make the strip the identity, bit
    /// for bit -- what the *Neutral* preset has to be rather than merely sound
    /// like. The panel reads this to say so, which is why it must not be asked
    /// of the engine: see the comment on the definition.
    [[nodiscard]] bool isIdentity() const;

    /// The high-pass corner in words, including the fact that 0 is off rather
    /// than "very low". A control whose bottom end changes kind rather than
    /// degree needs saying out loud.
    [[nodiscard]] juce::String describeHighpass() const;

    /// What the de-esser is measuring, in plain language, with its current
    /// threshold and the reading right now. This is the control most likely to
    /// be misread as a level.
    [[nodiscard]] juce::String describeSibilance() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    /// Reads every parameter into `settings_`. The per-stage switches are
    /// applied here by forcing the stage to its own neutral value rather than
    /// by branching in the engine: the engine already proves those settings
    /// are bit-exact identities, so a switched-off stage costs its arithmetic
    /// and changes nothing, and there is one definition of "off" instead of
    /// two.
    void pullParameters();

    /// The same read, into a fresh value rather than into the member the audio
    /// thread is using. What `isIdentity()` asks about, from the message
    /// thread, without racing the audio thread's copy.
    [[nodiscard]] SyrinxEngine::Settings settingsFromParameters() const;

    juce::AudioProcessorValueTreeState state_;
    juce::AudioParameterBool* bypassParameter_ { nullptr };

    SyrinxEngine engine_;
    SyrinxEngine::Settings settings_;

    /// Double-precision scratch: the DSP is double throughout, so a float host
    /// buffer is converted here rather than compromising the processing. The
    /// second buffer holds the untouched input for the bypass crossfade.
    juce::AudioBuffer<double> scratch_;
    juce::AudioBuffer<double> dry_;

    dsp::BypassMixer bypassMixer_;

    ui::AbCompare abCompare_ { state_, { ids::bypass } };

    /// See getTooltipsEnabled. Defaults on: the tooltips are how this workshop
    /// documents itself, so a new user gets them.
    bool tooltipsEnabled_ { true };

    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyrinxProcessor)
};

} // namespace tezla::syrinx
