// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The JUCE wrapper around SvaraEngine. Thin by design (CLAUDE.md section 4):
// parameters, state, MIDI translation, and the two hand-offs that keep the
// audio thread lock-free -- the tuning (same pattern as Sonitus) and the
// loaded soundfont.
//
// ---------------------------------------------------------------------------
// The state stores the PATH to the soundfont, never the data
// ---------------------------------------------------------------------------
//
// A soundfont is tens or hundreds of megabytes; a project that embedded one
// would balloon and every save would copy it. So the state stores the file's
// path, the chosen bank/program, and the tuning -- and a project opened where
// the file is missing reports the path it wanted and plays nothing, rather
// than guessing. This is the one deliberate exception to "the project must
// reproduce everything" (the Scala text IS embedded, it is a few hundred
// bytes): a sample library is an installed asset, like the DAW itself.
//
// ---------------------------------------------------------------------------
// The font hand-off
// ---------------------------------------------------------------------------
//
// Loading parses the file and resolves the model on the MESSAGE thread, then
// publishes the finished object through an atomic pointer; the audio thread
// picks it up at the top of a block, points the engine at it, and parks the
// previous font in a retirement slot the message thread frees later. The
// audio thread never allocates, frees, parses, or waits. At most one font is
// pending and one retired at any moment, because the message thread drains
// both slots before publishing -- bounded memory, no queue.

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/Scales.hpp>
#include <tezla/dsp/Tuning.hpp>

#include <Sf2File.hpp>
#include <Sf2Model.hpp>
#include <SvaraEngine.hpp>

namespace tezla::svarayantra {

namespace dsp = tezla::dsp;

namespace ids
{
inline constexpr auto outputTrim = "outputTrim";
inline constexpr auto bendRange = "bendRange";
} // namespace ids

class SvarayantraProcessor final : public juce::AudioProcessor
{
public:
    SvarayantraProcessor();
    ~SvarayantraProcessor() override;

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

    /// The longest tail the loaded font can produce: its largest volume
    /// envelope release, measured at load time. Zero fonts have no tail.
    double getTailLengthSeconds() const override;

    /// The host's program list is the font's preset list, so a DAW-side
    /// preset picker works before (and beside) the editor's own browser.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }

    // ---- the soundfont (message thread) -----------------------------------

    /// Parses and resolves the file, publishes it to the audio thread, and
    /// rebuilds the preset list. Returns an empty string on success, else
    /// the reason the file was refused (which is also kept for the editor).
    juce::String loadFontFile (const juce::File& file);

    /// Unloads: the engine falls silent, the state forgets the path.
    void clearFont();

    [[nodiscard]] juce::String getFontPath() const noexcept { return fontPath_; }
    [[nodiscard]] juce::String getFontError() const noexcept { return fontError_; }
    [[nodiscard]] juce::String getFontName() const noexcept { return fontName_; }

    struct PresetChoice
    {
        juce::String label;
        int bank { 0 };
        int program { 0 };
    };

    [[nodiscard]] const std::vector<PresetChoice>& getPresetChoices() const noexcept
    {
        return presetChoices_;
    }

    /// Chooses the sounding preset. The change reaches the engine at the top
    /// of the next audio block.
    void setPresetChoice (int bank, int program);

    [[nodiscard]] int getCurrentBank() const noexcept { return bank_; }
    [[nodiscard]] int getCurrentProgramNumber() const noexcept { return program_; }

    // ---- the tuning (message thread; same workflow as Sonitus) ------------

    juce::String loadScalaText (const juce::String& text, const juce::String& name);
    juce::String loadKeyboardMapText (const juce::String& text);
    bool selectBuiltInScale (const juce::String& name);
    void resetTuning();

    [[nodiscard]] const dsp::Scale& getScale() const noexcept { return scale_; }
    [[nodiscard]] juce::String getScaleName() const noexcept { return scaleName_; }
    [[nodiscard]] bool hasScalaFileLoaded() const noexcept { return scalaText_.isNotEmpty(); }

    void setConcertPitch (double hz);
    [[nodiscard]] double getConcertPitch() const noexcept { return concertPitchHz_; }

    /// Root frequency preview for the tuning table: what the current tuning
    /// plays for a key, computed on a message-thread copy.
    [[nodiscard]] double previewFrequencyFor (int midiNote) const;

    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] int getActiveVoiceCount() const noexcept { return activeVoices_.load(); }

private:
    /// One loaded soundfont, built whole on the message thread.
    struct FontData
    {
        Sf2File file;
        Sf2Model model;
        double tailSeconds { 0.0 };
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi);

    void pullParameters();
    void handleMidi (const juce::MidiMessage& message);

    /// Message thread: hands the built font to the audio thread, draining
    /// whatever previous font is still parked in either slot.
    void publishFont (std::unique_ptr<FontData> font);

    /// Audio thread: adopts a pending font and parks the old one.
    void collectFont() noexcept;

    void publishTuning();
    void collectTuning() noexcept;

    /// Rebuilds the message-thread preset list from the current model.
    void rebuildPresetChoices (const Sf2Model& model);

    juce::AudioProcessorValueTreeState state_;

    SvaraEngine engine_;

    // ---- font hand-off ----------------------------------------------------
    std::atomic<FontData*> pendingFont_ { nullptr };   // owns what it holds
    std::atomic<FontData*> retiredFont_ { nullptr };   // owns what it holds
    FontData* currentFont_ { nullptr };                // audio thread only
    std::atomic<double> tailSeconds_ { 0.0 };

    juce::String fontPath_, fontError_, fontName_;
    std::vector<PresetChoice> presetChoices_;

    // ---- preset choice hand-off -------------------------------------------
    std::atomic<int> desiredBank_ { 0 };
    std::atomic<int> desiredProgram_ { 0 };
    std::atomic<bool> presetDirty_ { false };
    int bank_ { 0 };
    int program_ { 0 };

    // ---- tuning (mirrors Sonitus) -----------------------------------------
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

    juce::AudioBuffer<double> scratch_;
    std::array<double*, 2> channelPointers_ {};

    bool prepared_ { false };
    std::atomic<int> activeVoices_ { 0 };
    double sampleRate_ { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SvarayantraProcessor)
};

} // namespace tezla::svarayantra
