#pragma once

// The stereo image as a picture rather than a number.
//
// A correlation reading answers "do the channels agree"; this answers "how do
// they disagree", and those are different questions with different fixes. The
// analysis is framework-free and lives in tezla-dsp's StereoScope; this only
// draws it.

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/dsp/Correlation.hpp>

#include "Palette.hpp"

namespace tezla::ui
{

/// A goniometer: the sample pairs themselves, rotated so mono is vertical.
///
/// The correlation number says how much the channels agree; this says *how*
/// they disagree, and those are different questions. A wide mix and a
/// hard-panned pair can read the same r and need opposite fixes -- one is a
/// stereo image, the other is two mono tracks that never met.
///
/// Rotated 45 degrees, which is the convention and is worth the trouble:
///
///   x = (R - L) / sqrt(2)      y = -(L + R) / sqrt(2)
///
/// so a mono signal draws a vertical line, a polarity-inverted one draws a
/// horizontal line, and hard left and hard right lean into their own corners.
/// Un-rotated L-against-R puts mono on a diagonal, where the eye is much worse
/// at spotting that it has tilted.
class Goniometer final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    /// How many points are drawn. Enough to look continuous on tonal material,
    /// few enough to redraw at the editor's tick without costing a core.
    static constexpr int kPoints = 900;

    /// The trail is drawn in chunks, oldest dimmest, which is what makes a
    /// moving image read as motion rather than as a smear.
    static constexpr int kChunks = 10;

    explicit Goniometer (ui::Palette palette) : palette_ (palette) {}

    /// Message thread. Pulls the most recent window from the scope, striding so
    /// the picture spans the same slice of time at every sample rate.
    void update (const dsp::StereoScope& scope);

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;

    std::array<float, kPoints> x_ {}, y_ {};
    int filled_ { 0 };
};

} // namespace tezla::ui
