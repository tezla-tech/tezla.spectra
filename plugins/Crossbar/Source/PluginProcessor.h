// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The JUCE wrapper around CrossbarEngine. Thin by design (CLAUDE.md section
// 4): parameters, state, MIDI, and one thing that is not a parameter.
//
// **The dial string is state, not a parameter.** A phone number is text, and
// a VST3 parameter is a float between 0 and 1 -- there is no honest way to
// automate "0208 811 8055". So it travels in the state tree beside the
// tooltip flag, exactly as Svarayantra's SoundFont path does, and the dial
// key reads whatever is currently there.
//
// Parameters are schema v1 from birth. Every choice list below is
// APPEND-ONLY forever (CLAUDE.md section 8): a choice parameter stores an
// INDEX, so inserting a region between the two present ones, or a rate
// between two others, would silently repoint every saved project.

#include <atomic>
#include <cstdint>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <CrossbarEngine.hpp>

namespace tezla::crossbar {

namespace dsp = tezla::dsp;

/// Parameter string IDs. **Frozen forever** -- renaming one resets that
/// control in every project that used it (CLAUDE.md section 8).
namespace ids
{
// TONE
inline constexpr auto region     = "region";
inline constexpr auto cadence    = "cadence";
inline constexpr auto twist      = "twist";
inline constexpr auto mapRoot    = "mapRoot";

// ENVELOPE
inline constexpr auto attack     = "attack";
inline constexpr auto decay      = "decay";
inline constexpr auto sustain    = "sustain";
inline constexpr auto release    = "release";

// LINE
inline constexpr auto codec      = "codec";
inline constexpr auto bits       = "bits";
inline constexpr auto rate       = "rate";
inline constexpr auto band       = "band";
inline constexpr auto noise      = "noise";

// DIAL
inline constexpr auto dialMode   = "dialMode";
inline constexpr auto dialDigit  = "dialDigit";
inline constexpr auto dialGap    = "dialGap";

// OUTPUT
inline constexpr auto level      = "level";
} // namespace ids

/// The choice lists. **APPEND-ONLY**, all of them, from birth. The order
/// below is the stored index forever, and each is married to its enum by a
/// static_assert in the .cpp.
namespace choices
{
inline constexpr const char* regions[] {
    "North America (Bell)",
    "United Kingdom (BT)",
};

inline constexpr const char* cadences[] {
    "From key",
    "Free running",
    "Steady",
};

inline constexpr const char* codecs[] {
    "Off",
    "Mu-law (G.711)",
    "A-law (G.711)",
    "Linear",
};

/// Named where they are standards, because the label is the documentation:
/// somebody reaching for "what did a phone sound like" should be able to read
/// the answer off the control.
inline constexpr const char* rates[] {
    "Off",
    "32 kHz",
    "24 kHz",
    "16 kHz (G.722)",
    "11 kHz",
    "8 kHz (G.711)",
    "6 kHz",
    "4 kHz",
    "3 kHz",
    "2 kHz",
    "1 kHz",
};

inline constexpr const char* bands[] {
    "Off",
    "Toll 300-3400 (G.712)",
    "Wideband 50-7000 (G.722)",
    "Handset 500-2800",
    "Speaker 700-2200",
};

inline constexpr const char* dialModes[] {
    "Tone (DTMF)",
    "Pulse (rotary)",
};
} // namespace choices

/// Where the dial string lives in the state tree.
inline constexpr auto kDialNumberProperty = "dialNumber";

class CrossbarProcessor final : public juce::AudioProcessor
{
public:
    CrossbarProcessor();
    ~CrossbarProcessor() override = default;

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

    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }

    // ---- the dial string (message thread) ---------------------------------

    /// The number the dial key will play. Kept as text because that is what a
    /// phone number is; see the header.
    void setDialNumber (const juce::String& text);
    [[nodiscard]] juce::String getDialNumber() const { return dialNumber_; }

    /// Plays it now, without waiting for a key -- what the editor's DIAL
    /// button does. Queued rather than acted on here, because this is the
    /// message thread and the engine belongs to the audio one.
    void triggerDial() noexcept { dialRequested_.store (true); }

    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }

    // ---- what the editor reads --------------------------------------------

    [[nodiscard]] int getActiveVoiceCount() const noexcept { return activeVoices_.load(); }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double getPreparedRate() const noexcept { return sampleRate_; }

    /// What the RATE control is actually doing at the current host rate, which
    /// is not always what its label says -- asking for 32 kHz in a 44.1 kHz
    /// session is a ratio of 1.38, and asking for it in a 192 kHz session is a
    /// ratio of 6.
    ///
    /// **Computed from the parameter and the prepared rate**, not read back
    /// from something the audio thread published. The first version did the
    /// latter and read 48000 Hz with the control set to 8 kHz whenever the
    /// transport had not run -- a panel that says the wrong thing until you
    /// press play is worse than one that says nothing.
    [[nodiscard]] double getEffectiveRateHz() const;

    /// Which tones are sounding, one bit per `Tone`, so the keypad can light
    /// the key -- and with it the row and column that are crossing. Published
    /// by the audio thread; a torn read costs a repaint, not a click.
    [[nodiscard]] std::uint64_t getSoundingToneMask() const noexcept
    {
        return soundingTones_.load();
    }

    // ---- keys pressed on the panel ----------------------------------------

    /// Presses or releases one of the map's tones from the editor.
    ///
    /// Two masks rather than one, and the reason is a fast click: `held_` is
    /// the current state and `pressed_` accumulates presses that the audio
    /// thread has not seen yet, so a key pressed and released between two
    /// blocks still produces a note-on and a note-off instead of nothing at
    /// all. Lock-free and allocation-free, which is what lets the audio thread
    /// read it.
    void setPanelKeyHeld (int toneIndex, bool held) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midi);

    void pullParameters();
    void handleMidi (const juce::MidiMessage& message);
    void servicePanelKeys();

    juce::AudioProcessorValueTreeState state_;

    CrossbarEngine engine_;
    CrossbarEngine::Parameters settings_;

    /// The dial string as the audio thread sees it: a fixed buffer swapped
    /// under a spin lock on the message thread, so `processBlock` never
    /// touches a juce::String.
    juce::String dialNumber_;
    juce::SpinLock dialLock_;
    char pendingDial_[Dialler::kMaxDigits + 1] {};
    std::atomic<bool> dialPending_ { false };
    std::atomic<bool> dialRequested_ { false };

    bool tooltipsEnabled_ { true };
    int currentProgram_ { 0 };

    juce::AudioBuffer<double> scratch_;

    bool prepared_ { false };
    std::atomic<int> activeVoices_ { 0 };
    std::atomic<std::uint64_t> soundingTones_ { 0 };
    double sampleRate_ { 48000.0 };

    /// Keys held on the panel, and presses not yet consumed. See
    /// `setPanelKeyHeld`.
    std::atomic<std::uint64_t> panelHeld_ { 0 };
    std::atomic<std::uint64_t> panelPressed_ { 0 };
    std::uint64_t panelPrevious_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossbarProcessor)
};

} // namespace tezla::crossbar
