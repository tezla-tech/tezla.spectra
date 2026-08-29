// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/ReferenceCurve.hpp>
#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/ui/AbCompare.hpp>

#include "TranspectusEngine.hpp"

namespace tezla::transpectus
{

/// Parameter IDs. Permanent: renaming one silently resets that parameter in
/// every project that already uses the plugin. See CLAUDE.md §8.
namespace ids
{
inline constexpr auto target      = "target";
inline constexpr auto truePeak    = "truePeak";
inline constexpr auto monoCheckHz = "monoCheckHz";
inline constexpr auto bypass      = "bypass";
inline constexpr auto resolution  = "resolution";   // appended at schema v2
} // namespace ids

namespace choices
{
/// **Append-only, forever.** A choice parameter stores an index, so inserting
/// or reordering an entry silently repoints every saved use of it -- and this
/// one is generated from the engine's table, so the two cannot drift apart.
[[nodiscard]] juce::StringArray targetNames();

inline const juce::StringArray truePeak { "Off", "Standard", "Strict" };

/// Spectrum resolution. Fast is one 2048-point transform (the original
/// behaviour); Balanced one 4096; Fine adds a 16384-point transform below
/// 500 Hz, crossfaded in -- sharp bass and a responsive top at once.
inline const juce::StringArray resolution { "Fast", "Balanced", "Fine" };
} // namespace choices

class TranspectusProcessor final : public juce::AudioProcessor
{
public:
    /// How many display bins the spectrum is folded onto. The editor's analyser
    /// and the reference curve have to agree on this, so it lives in one place.
    static constexpr int kSpectrumBins = Engine::kNumBins;

    TranspectusProcessor();
    ~TranspectusProcessor() override = default;

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    [[nodiscard]] juce::AudioProcessorValueTreeState& getState() noexcept { return state_; }
    [[nodiscard]] Engine& getEngine() noexcept { return engine_; }
    [[nodiscard]] ui::AbCompare& getAbCompare() noexcept { return abCompare_; }

    /// Whether the panel shows its hover tooltips.
    ///
    /// Not a parameter -- it changes nothing about the sound, so it has no
    /// business in an automation lane or in a preset. It lives here rather than
    /// in the editor because the editor is destroyed every time the window is
    /// closed, and a setting that forgot itself on every close would be worse
    /// than not having the switch.
    [[nodiscard]] bool getTooltipsEnabled() const noexcept { return tooltipsEnabled_; }
    void setTooltipsEnabled (bool enabled) noexcept { tooltipsEnabled_ = enabled; }

    /// The captured reference, shared with the editor. Lives here rather than in
    /// the editor so it survives the window being closed and is saved with the
    /// project.
    [[nodiscard]] dsp::ReferenceCurve& getReferenceCurve() noexcept { return reference_; }

    /// The permanent per-bin maximum behind the spectrum's peak-hold ghost, in
    /// dB, since the last reset.
    ///
    /// It lives here rather than in the editor because it is a measurement, and
    /// a measurement that vanishes when you close the window is not one -- the
    /// true-peak hold and the integrated loudness behave the same way. Only the
    /// message thread ever touches it: the display bins are computed in the
    /// editor, so this is storage that outlives the editor, not something the
    /// audio thread writes.
    [[nodiscard]] std::vector<float>& getSpectrumPeakHold() noexcept { return spectrumPeakHold_; }

    void resetSpectrumPeakHold() noexcept;

    /// The goniometer's excursion hold: per angular sector, the widest the
    /// image has been. Same contract as the spectrum hold above -- storage
    /// that outlives the editor, written only by the message thread; the
    /// editor sizes it to the goniometer's sector count on attach.
    [[nodiscard]] std::vector<float>& getImageExcursionHold() noexcept
    {
        return imageExcursionHold_;
    }

    void resetImageExcursionHold() noexcept
    {
        std::fill (imageExcursionHold_.begin(), imageExcursionHold_.end(), 0.0f);
    }

    /// Clears the integration, the true-peak hold and the peak-hold bars.
    void resetMeasurement() noexcept { engine_.resetMeasurement(); }

    /// Whether prepareToPlay has run, so the panel can tell "nothing yet" from
    /// "nothing there".
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename FloatType>
    void processInternal (juce::AudioBuffer<FloatType>& buffer);

    void pullParameters();

    juce::AudioProcessorValueTreeState state_;

    Engine engine_;
    Parameters parameters_;

    /// Double-precision scratch: the measurement runs in double, so a float
    /// host buffer is converted rather than measured at lower precision.
    juce::AudioBuffer<double> scratch_;
    std::array<const double*, Engine::kMaxChannels> channelPointers_ {};

    dsp::ReferenceCurve reference_;

    /// kSpectrumBins of it, every entry at the floor until something louder
    /// arrives.
    std::vector<float> spectrumPeakHold_;
    std::vector<float> imageExcursionHold_;

    ui::AbCompare abCompare_ { state_, { ids::bypass } };


    /// See getTooltipsEnabled. Defaults on: the tooltips are how this

    /// workshop documents itself, so a new user gets them.

    bool tooltipsEnabled_ { true };

    bool prepared_ { false };
    double sampleRate_ { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranspectusProcessor)
};

} // namespace tezla::transpectus
