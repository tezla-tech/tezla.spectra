// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The JUCE wrapper around MalleusEngine. Thin by design (CLAUDE.md section
// 4): parameters, state, MIDI, and the one hand-off that keeps the audio
// thread lock-free -- the tuning, exactly as Sonitus and Svarayantra do it.
//
// The tuning hand-off matters more here than in either of them. In an
// ordinary microtuned synth the scale decides what pitch a key plays; in
// Malleus it also decides where every PARTIAL of the object lands (Overtone
// Lock) and how the sympathetic strings are tuned. One scale, three
// readers, and all three read the single copy inside the engine's
// dsp::Tuning -- so the swap that installs a new scale updates the lot at
// once and cannot leave them disagreeing.
//
// Parameters are schema v1 from birth. Every choice list below is
// APPEND-ONLY forever (CLAUDE.md section 8): a choice parameter stores an
// INDEX, so inserting "Bow" between "Pluck" and "Roll" one day would
// silently repoint every saved patch's exciter. New entries go on the end,
// even when the order reads badly.

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/Scales.hpp>
#include <tezla/dsp/Tuning.hpp>
#include <tezla/ui/TuningHost.hpp>

#include <MalleusEngine.hpp>

namespace tezla::malleus {

namespace dsp = tezla::dsp;

/// Parameter string IDs. **Frozen forever** -- renaming one resets that
/// control in every project that used it (CLAUDE.md section 8).
namespace ids
{
// OBJECT
inline constexpr auto material     = "material";
inline constexpr auto stretch      = "stretch";
inline constexpr auto lockAmount   = "lockAmount";
inline constexpr auto partials     = "partials";
inline constexpr auto decay        = "decay";
inline constexpr auto tilt         = "tilt";
inline constexpr auto position     = "position";

// EXCITE
inline constexpr auto exciter      = "exciter";
inline constexpr auto hardness     = "hardness";
inline constexpr auto noiseAmount  = "noiseAmount";
inline constexpr auto dropDepth    = "dropDepth";
inline constexpr auto dropTime     = "dropTime";
inline constexpr auto bowPressure  = "bowPressure";
inline constexpr auto bowSpeed     = "bowSpeed";
inline constexpr auto rollStart    = "rollStart";
inline constexpr auto rollRatio    = "rollRatio";
inline constexpr auto rollMinimum  = "rollMinimum";
inline constexpr auto rollHumanise = "rollHumanise";

// RESONANCE
inline constexpr auto sympCount      = "sympCount";
inline constexpr auto sympRoot       = "sympRoot";
inline constexpr auto sympLevel      = "sympLevel";
inline constexpr auto sympCoupling   = "sympCoupling";
inline constexpr auto sympDrone      = "sympDrone";
inline constexpr auto sympDecay      = "sympDecay";
inline constexpr auto sympBrightness = "sympBrightness";

// OUTPUT
inline constexpr auto outputTrim   = "outputTrim";
} // namespace ids

/// The exciter choice list. **APPEND-ONLY** -- the order below is the
/// stored index, forever. It matches Exciter's enum order by construction
/// (a static_assert in the .cpp keeps them married).
namespace exciterNames
{
inline constexpr const char* list[] { "Mallet", "Pluck", "Roll", "Bow" };
}

class MalleusProcessor final : public juce::AudioProcessor,
                               public tezla::ui::TuningHost
{
public:
    MalleusProcessor();
    ~MalleusProcessor() override = default;

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

    /// The longest thing this can still be sounding after the last note
    /// off: the sympathetic bank's decay, which outlives any single voice.
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }

    // ---- the tuning (message thread; mirrors Sonitus/Svarayantra) --------

    juce::String loadScalaText (const juce::String& text, const juce::String& name) override;
    juce::String loadKeyboardMapText (const juce::String& text) override;
    juce::String selectBuiltInScale (const juce::String& name) override;
    void resetTuning() override;

    [[nodiscard]] const dsp::Scale& getScale() const noexcept override { return scale_; }
    [[nodiscard]] juce::String getScaleName() const noexcept override { return scaleName_; }
    [[nodiscard]] juce::String describeTuning() const override;
    [[nodiscard]] double getRootHz() const noexcept override;
    [[nodiscard]] bool hasScalaFileLoaded() const noexcept { return scalaText_.isNotEmpty(); }

    void setConcertPitch (double hz) override;
    [[nodiscard]] double getConcertPitch() const noexcept override { return concertPitchHz_; }

    /// What the current tuning plays for a key, from a message-thread twin.
    [[nodiscard]] double previewFrequencyFor (int midiNote) const;

    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }

    // ---- what the editor reads ------------------------------------------

    [[nodiscard]] int getActiveVoiceCount() const noexcept { return activeVoices_.load(); }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double getPreparedRate() const noexcept { return sampleRate_; }

    /// What the mode-stack picture should draw.
    struct ModeStack
    {
        std::vector<double> frequencies;   ///< audible partials, Hz, ascending
        bool sounding { false };           ///< a real voice, or a preview
    };

    /// The sounding voice's stack, or -- when nothing is playing -- the
    /// object the current settings describe, at the tuning's root. Copied
    /// under no lock: it is a picture, and a torn one costs a redraw rather
    /// than a click.
    [[nodiscard]] ModeStack snapshotModeStack() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi);

    void pullParameters();
    void handleMidi (const juce::MidiMessage& message);

    void publishTuning();
    void collectTuning() noexcept;

    juce::AudioProcessorValueTreeState state_;

    MalleusEngine engine_;

    // ---- tuning hand-off --------------------------------------------------
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

    /// Message-thread twin of the engine's tuning, for table previews.
    dsp::Tuning previewTuning_;

    bool tooltipsEnabled_ { true };
    int currentProgram_ { 0 };

    juce::AudioBuffer<double> scratch_;

    bool prepared_ { false };
    std::atomic<int> activeVoices_ { 0 };
    double sampleRate_ { 48000.0 };
    double outputGain_ { 1.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MalleusProcessor)
};

} // namespace tezla::malleus
