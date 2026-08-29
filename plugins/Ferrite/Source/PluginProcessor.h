// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/AbCompare.hpp>

#include "FerriteEngine.hpp"

namespace tezla::ferrite
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin. See CLAUDE.md §8.
namespace ids
{
inline constexpr auto input        = "input";
inline constexpr auto drive        = "drive";
inline constexpr auto saturation   = "saturation";
inline constexpr auto bias         = "bias";

inline constexpr auto speed        = "speed";
inline constexpr auto bump         = "bump";

inline constexpr auto wow          = "wow";
inline constexpr auto flutter      = "flutter";
inline constexpr auto wowRate      = "wowRate";
inline constexpr auto flutterRate  = "flutterRate";

inline constexpr auto spacing      = "spacing";
inline constexpr auto thickness    = "thickness";
inline constexpr auto gap          = "gap";

inline constexpr auto hiss         = "hiss";
inline constexpr auto autoTrim     = "autoTrim";
inline constexpr auto mix          = "mix";
inline constexpr auto output       = "output";
inline constexpr auto oversampling = "oversampling";
inline constexpr auto bypass       = "bypass";
} // namespace ids

/// The option lists behind the two choice parameters.
///
/// **Append-only, forever.** A choice parameter stores an *index*, not a
/// name, so inserting or reordering an entry silently repoints every saved
/// use of it. CLAUDE.md §8.
namespace choices
{
inline const juce::StringArray speed { "3.75 ips", "7.5 ips", "15 ips", "30 ips" };
inline const juce::StringArray oversampling { "Auto", "Off", "x2", "x4", "x8" };

static_assert (static_cast<int> (dsp::OversamplingMode::Auto) == 0
            && static_cast<int> (dsp::OversamplingMode::Off)  == 1
            && static_cast<int> (dsp::OversamplingMode::X8)   == 4,
               "the oversampling option list is indexed straight into OversamplingMode");
} // namespace choices

class FerriteProcessor final : public juce::AudioProcessor
{
public:
    FerriteProcessor();
    ~FerriteProcessor() override = default;

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

    /// Meter values, written by the audio thread and read by the editor.
    /// The input pair reads AFTER the input gain -- the level going onto the
    /// tape, which is what the needle on a machine shows -- and the output
    /// pair reads what leaves the plugin.
    struct MeterValues
    {
        std::atomic<float> inputVuDb    { -100.0f };
        std::atomic<float> inputPeakDb  { -100.0f };
        std::atomic<float> outputVuDb   { -100.0f };
        std::atomic<float> outputPeakDb { -100.0f };
    };

    [[nodiscard]] MeterValues& getMeterValues() noexcept { return meters_; }

    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// Whether the panel shows its hover tooltips. Not a parameter -- it
    /// changes nothing about the sound -- but it must survive the editor
    /// being closed, so it lives here. See Anvil for the longer argument.
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }
    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }

    /// What the oversampling control is costing and buying, in plain words,
    /// at the host's actual rate. CLAUDE.md §6.
    [[nodiscard]] juce::String describeOversampling() const;

    [[nodiscard]] juce::String describeLatency() const;

    /// Whether prepareToPlay has run, so the panel can tell "no latency"
    /// apart from "no figure yet".
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    void pullParameters();

    juce::AudioProcessorValueTreeState state_;
    juce::AudioParameterBool* bypassParameter_ { nullptr };

    Engine engine_;
    Parameters parameters_;

    /// Double-precision scratch: the DSP is double throughout, so a float
    /// host buffer is converted here rather than compromising the processing.
    juce::AudioBuffer<double> scratch_;
    std::array<double*, Engine::kMaxChannels> channelPointers_ {};

    int reportedLatency_ { 0 };
    bool prepared_ { false };

    ui::AbCompare abCompare_ { state_, { ids::bypass } };

    /// See getTooltipsEnabled. Defaults on: the tooltips are how this
    /// workshop documents itself, so a new user gets them.
    bool tooltipsEnabled_ { true };

    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FerriteProcessor)
};

} // namespace tezla::ferrite
