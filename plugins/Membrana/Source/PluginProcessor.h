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

#include "MembranaEngine.hpp"

namespace tezla::membrana
{

/// Parameter IDs. These are permanent: renaming one silently resets that
/// parameter in every project that already uses the plugin. See CLAUDE.md
/// section 8. Frozen at birth, kSchemaV1, appended-only forever after.
namespace ids
{
inline constexpr auto micOn      = "micOn";
inline constexpr auto pattern    = "pattern";
inline constexpr auto capsuleMm  = "capsuleMm";
inline constexpr auto character  = "character";
inline constexpr auto grille     = "grille";
inline constexpr auto grilleHz   = "grilleHz";
inline constexpr auto distanceCm = "distanceCm";
inline constexpr auto axisDeg    = "axisDeg";
inline constexpr auto autoLevel  = "autoLevel";
inline constexpr auto lowLimitHz = "lowLimitHz";

inline constexpr auto presenceOn = "presenceOn";
inline constexpr auto presence   = "presence";
inline constexpr auto presHz     = "presHz";
inline constexpr auto presThresh = "presThresh";
inline constexpr auto track      = "track";

inline constexpr auto detailOn   = "detailOn";
inline constexpr auto detail     = "detail";
inline constexpr auto detHz      = "detHz";
inline constexpr auto detFloor   = "detFloor";

inline constexpr auto output     = "output";
inline constexpr auto bypass     = "bypass";
} // namespace ids

class MembranaProcessor final : public juce::AudioProcessor
{
public:
    MembranaProcessor();
    ~MembranaProcessor() override = default;

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

    /// What the panel's activity lanes read: the two lifts (what the
    /// dynamics stages are DOING, not what they are set to) and the level
    /// policy's current trim, plus the usual in/out peaks. Atomics, because
    /// the audio thread may not block.
    struct MeterValues
    {
        std::atomic<float> presenceLiftDb   { 0.0f };
        std::atomic<float> presenceTargetDb { 0.0f };
        std::atomic<float> detailLiftDb     { 0.0f };
        std::atomic<float> detailTargetDb   { 0.0f };
        std::atomic<float> capsuleTrimDb    { 0.0f };

        std::atomic<float> inputDb  { -100.0f };
        std::atomic<float> outputDb { -100.0f };
    };

    [[nodiscard]] MeterValues& getMeterValues() noexcept { return meters_; }

    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// Whether the panel shows its hover tooltips. Not a parameter -- it
    /// changes nothing about the sound. Lives here because the editor is
    /// destroyed whenever the window closes.
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }
    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }

    /// True when the current PARAMETERS make the stage the identity, bit
    /// for bit -- asked of the parameters and never of the engine (the
    /// Crossbar readout lesson: before the first callback the engine holds
    /// default-constructed neutral whatever the controls say).
    [[nodiscard]] bool isIdentity() const;

    /// The composed capsule curve from the same coefficients that play --
    /// the editor's curve pane reads this, so what is drawn is what sounds.
    /// Audio-thread-owned data read from the message thread: the values are
    /// plain doubles updated at control boundaries, and a frame-old curve
    /// drawn during a redesign is a display artefact, not a correctness
    /// problem -- the same judgement every meter here already makes.
    [[nodiscard]] double capsuleRenderedDbAt (double hz) const noexcept
    {
        return engine_.capsuleRenderedDbAt (hz);
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    void pullParameters();

    /// The same read into a fresh value, for isIdentity() from the message
    /// thread without racing the audio thread's copy.
    [[nodiscard]] MembranaEngine::Settings settingsFromParameters() const;

    juce::AudioProcessorValueTreeState state_;
    juce::AudioParameterBool* bypassParameter_ { nullptr };

    MembranaEngine engine_;
    MembranaEngine::Settings settings_;

    juce::AudioBuffer<double> scratch_;
    juce::AudioBuffer<double> dry_;

    dsp::BypassMixer bypassMixer_;

    ui::AbCompare abCompare_ { state_, { ids::bypass } };

    bool tooltipsEnabled_ { true };

    MeterValues meters_;

    double sampleRate_ { 44100.0 };
    int currentProgram_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MembranaProcessor)
};

} // namespace tezla::membrana
