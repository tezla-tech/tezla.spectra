#pragma once

// A level meter you can actually read a number off.
//
// Three things a bar alone cannot tell you, and this adds all three:
//
//   what the level *is*     a numeric readout, in decibels, at the top
//   what the worst was      it holds the maximum until you clear it
//   where the marks are     a labelled dB scale beside the bar
//
// The conventions are Ardour's mixer strip and the peak-hold readout every
// mastering limiter has had since the L1: a number that latches the worst peak,
// turns red when it goes over, and clears when you click it. None of that is
// anybody's code -- it is how the control has worked for thirty years, and a
// meter that behaved differently would be the surprising one.
//
// **The scale runs to +6 dB, and that is display range rather than headroom.**
// Worth being precise about, because the two get confused: inside a float bus
// there is about 770 dB above full scale, and inside this project's double path
// about 6165, so an overshoot is entirely recoverable while it stays in
// floating point. At a converter or in a fixed-point file there is exactly
// **zero** -- 0 dBFS is a wall. The +6 exists so an overshoot that is still
// recoverable can be *measured*, and so you know how far to pull back.
//
// In fact the safe level at a converter is below 0 dBFS, not at it: samples all
// under full scale still reconstruct above it. Measured in this project at
// +1.506 dB on dense near-Nyquist content and +3.011 dB on a tone at a quarter
// of the sample rate. That is what the reference level is for -- an output
// meter is given its plugin's Ceiling rather than 0, so "over" means over the
// thing that was promised.

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.hpp"

namespace tezla::ui
{

class LevelMeter final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (Palette palette);

    /// Top and bottom of the scale, in dBFS. +6 matches Ardour's mixer meters.
    static constexpr float kTopDb   = 6.0f;
    static constexpr float kFloorDb = -60.0f;

    /// The bar itself. Fixed and centred, whatever the component is given.
    static constexpr int kBarWidth = 18;

    /// The labelled scale, when it is shown.
    static constexpr int kScaleWidth = 26;

    /// The numeric readout across the top.
    static constexpr int kReadoutHeight = 19;

    /// The narrowest the component can be and still render its readout: a
    /// number like "-12.3" at 14 point. Give it less and the digits are clipped,
    /// which is the failure the readout exists to prevent.
    static constexpr int kMinimumWidth = 46;

    /// Feed it the current readings, once per editor tick. `peakDb` also feeds
    /// the hold.
    void setValues (float vuDb, float peakDb) noexcept;

    /// The level an overshoot is measured against: 0 for an input meter, the
    /// plugin's Ceiling for its output meter. The readout turns red above it.
    void setReferenceDb (float referenceDb) noexcept;

    /// Draws the labelled scale down the right-hand side. Off by default: a
    /// pair of meters flanking a panel only needs one scale between them, and a
    /// narrow meter has no room for one.
    void setScaleVisible (bool shouldBeVisible);

    /// Clears the held peak and the over indicator. Clicking does this too.
    void resetHold() noexcept;

    /// The worst peak since the last clear, in dB.
    [[nodiscard]] float getHeldDb() const noexcept { return heldDb_; }

    void paint (juce::Graphics&) override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseDown  (const juce::MouseEvent&) override;

private:
    /// Where a level sits in the meter, 0 at the floor and 1 at the top.
    [[nodiscard]] static float positionFor (float db) noexcept;

    [[nodiscard]] juce::Rectangle<float> barBounds() const;

    void paintScale (juce::Graphics&, juce::Rectangle<float> bar) const;
    void paintReadout (juce::Graphics&, juce::Rectangle<float> area) const;

    Palette palette_;

    float vuDb_    { -100.0f };
    float peakDb_  { -100.0f };
    float heldDb_  { -100.0f };
    float referenceDb_ { 0.0f };

    bool scaleVisible_ { false };
    bool hovering_     { false };
};

} // namespace tezla::ui
