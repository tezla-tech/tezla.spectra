#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/LevelMeter.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::transpectus
{

/// One number with its name above it, and a band of interpretation under it.
///
/// The interpretation is what makes a metering plugin worth having over a
/// number in a status bar: -8 LUFS means nothing on its own, and "6 dB louder
/// than Spotify plays things" means something immediately.
class Readout final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    Readout (ui::Palette palette, juce::String caption, juce::String unit);

    /// `note` is the line under the number. Empty hides it.
    void setValue (juce::String text, juce::String note = {});

    /// Draws the number in the warning colour. For a reading that has gone
    /// somewhere it should not.
    void setWarning (bool shouldWarn);

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;
    juce::String caption_, unit_, value_ { "--" }, note_;
    bool warning_ { false };
};

/// A correlation meter: -1 to +1, with the mono-safe region marked.
class CorrelationBar final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit CorrelationBar (ui::Palette palette) : palette_ (palette) {}

    void setValue (float correlation, bool warn) noexcept
    {
        correlation_ = correlation;
        warning_ = warn;
    }

    void setCaption (juce::String caption) { caption_ = std::move (caption); }

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;
    juce::String caption_;
    float correlation_ { 1.0f };
    bool warning_ { false };
};

/// The spectrum, with the two honest references: a pink-noise slope, and a
/// curve captured from a track you already like.
///
/// No target curve ships with it. Genre curves are folklore -- they vary by
/// track, by era and by who drew them -- and baking one into a tool somebody
/// trusts is worse than shipping nothing. Pink noise is physics; the other
/// reference is whatever you point it at.
class SpectrumView final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    SpectrumView (ui::Palette palette, dsp::ReferenceCurve& reference);

    /// Folds the latest capture onto the display bins. Returns true if there
    /// was anything new to draw.
    bool update (const dsp::SpectrumCapture& capture);

    void setShowPinkSlope (bool shouldShow);
    void setShowDifference (bool shouldShow);

    /// Feeds the capture in progress, if there is one.
    void pushToCapture();

    [[nodiscard]] dsp::SpectrumAnalyser& getAnalyser() noexcept { return analyser_; }

    void paint (juce::Graphics&) override;

private:
    /// Where a frequency sits across the width, 0 to 1. Log, so an octave takes
    /// the same room wherever it is.
    [[nodiscard]] float positionFor (double hz) const noexcept;

    void paintGrid (juce::Graphics&, juce::Rectangle<float>) const;
    void paintCurve (juce::Graphics&, juce::Rectangle<float>,
                     const std::vector<float>& db, juce::Colour, float thickness, bool fill) const;

    ui::Palette palette_;
    dsp::ReferenceCurve& reference_;
    dsp::SpectrumAnalyser analyser_;

    std::vector<double> difference_;

    bool showPinkSlope_ { true };
    bool showDifference_ { false };
};

class TranspectusEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit TranspectusEditor (TranspectusProcessor& processorToUse);
    ~TranspectusEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildControls();

    /// Formats a loudness for display, with a real "silent" rather than a large
    /// negative number that looks like a reading.
    [[nodiscard]] static juce::String formatLufs (double lufs);

    TranspectusProcessor& transpectus_;

    juce::TooltipWindow tooltips_ { this, 500 };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;

    // ---- the numbers ---------------------------------------------------------

    std::unique_ptr<Readout> integrated_, shortTerm_, momentary_;
    std::unique_ptr<Readout> truePeak_, plr_, psr_;
    std::unique_ptr<Readout> delta_;

    std::unique_ptr<ui::LevelMeter> inputMeter_;

    std::unique_ptr<CorrelationBar> fullCorrelation_;
    std::unique_ptr<CorrelationBar> lowCorrelation_;

    std::unique_ptr<SpectrumView> spectrum_;

    juce::TextButton captureButton_ { "CAPTURE REFERENCE" };
    juce::TextButton clearReferenceButton_ { "CLEAR" };
    juce::ToggleButton pinkButton_ { "Pink slope" };
    juce::ToggleButton differenceButton_ { "Difference" };

    // ---- the controls --------------------------------------------------------

    juce::ComboBox targetBox_, truePeakBox_;
    juce::Label targetLabel_ { {}, "TARGET" };
    juce::Label truePeakLabel_ { {}, "TRUE PEAK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> truePeakAttachment_;

    juce::TextButton resetButton_ { "RESET MEASUREMENT" };

    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranspectusEditor)
};

} // namespace tezla::transpectus
