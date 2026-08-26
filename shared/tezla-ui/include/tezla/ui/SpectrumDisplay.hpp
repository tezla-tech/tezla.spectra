#pragma once

// Draws two spectra over one another: what went in, and what is coming out.
//
// One curve on its own tells you what the signal looks like, which you already
// know. Two tell you what the plugin did to it -- and for an exciter that *is*
// the plugin, because the whole effect is the difference between them. The
// harmonics appear as the output curve lifting away from the input above the
// Focus frequency, which is the picture the controls are actually describing.
//
// The analysis is framework-free and lives in tezla-dsp; this only draws it, so
// the same analyser can run a standalone spectrum tool later without a GUI
// framework attached.

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/dsp/SpectrumAnalyser.hpp>

#include "Palette.hpp"

namespace tezla::ui
{

class SpectrumDisplay final : public juce::Component
{
public:
    explicit SpectrumDisplay (Palette palette);

    /// Rebuilds the analysers for a new rate. Allocates; message thread only.
    void prepare (double sampleRate, int fftOrder = 12, int numBins = 160);

    /// Pulls a frame out of both captures and redraws. Call from a timer.
    void update (const dsp::SpectrumCapture& input, const dsp::SpectrumCapture& output);

    /// Greys the curves and stops them moving, so a bypassed plugin does not
    /// look like a broken one.
    void setDimmed (bool shouldDim);

    /// Marks the Focus frequency, so the control and the picture agree.
    void setFocusFrequency (double hz, bool aboveMode);

    void paint (juce::Graphics&) override;

private:
    void drawGrid (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawCurve (juce::Graphics&, juce::Rectangle<float> area,
                    const std::vector<float>& bins, juce::Colour colour,
                    bool fill) const;

    [[nodiscard]] float xFor (juce::Rectangle<float> area, double hz) const;
    [[nodiscard]] float yFor (juce::Rectangle<float> area, float db) const;

    Palette palette_;

    dsp::SpectrumAnalyser inputAnalyser_;
    dsp::SpectrumAnalyser outputAnalyser_;

    double lowHz_    { 20.0 };
    double highHz_   { 20000.0 };
    double focusHz_  { 3000.0 };
    bool   aboveMode_ { true };
    bool   dimmed_   { false };
    bool   ready_    { false };

    static constexpr float kTopDb    = 6.0f;
    static constexpr float kBottomDb = -78.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};

} // namespace tezla::ui
