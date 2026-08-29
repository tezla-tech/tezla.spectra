// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

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

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/dsp/SpectrumAnalyser.hpp>

#include "Palette.hpp"

namespace tezla::ui
{

class SpectrumDisplay final : public juce::Component
{
public:
    explicit SpectrumDisplay (Palette palette);

    /// Where a drag is in its life. The phases exist so the editor can wrap the
    /// whole gesture in beginChangeGesture/endChangeGesture: without that a host
    /// records a drag as several hundred unrelated jumps rather than as one
    /// move, and the automation lane is unusable afterwards.
    enum class DragPhase { began, moved, ended };

    /// Fired while the Focus line is being dragged. Frequency is in Hz, taken
    /// from where the pointer is; clamping to the parameter's own range is the
    /// editor's job, because the display does not know what that range is.
    std::function<void (double hz, DragPhase phase)> onFocusDragged;

    /// Rebuilds the analysers for a new rate. Allocates; message thread only.
    void prepare (double sampleRate, int fftOrder = 12, int numBins = 160);

    /// Pulls a frame out of both captures and redraws. Call from a timer.
    void update (const dsp::SpectrumCapture& input, const dsp::SpectrumCapture& output);

    /// Greys the curves and stops them moving, so a bypassed plugin does not
    /// look like a broken one.
    void setDimmed (bool shouldDim);

    /// Marks the Focus frequency, so the control and the picture agree.
    void setFocusFrequency (double hz, bool aboveMode);

    /// Marks where the generated harmonics are allowed to live. Each end is only
    /// drawn when its filter is switched in, so the picture shows what is
    /// actually happening rather than what could be.
    void setHarmonicLimits (bool floorOn, double floorHz, bool ceilingOn, double ceilingHz);

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /// Only offers the drag cursor when there is something to drag, so a plugin
    /// that shows a spectrum without a Focus control does not advertise a
    /// gesture that does nothing.
    [[nodiscard]] juce::MouseCursor getMouseCursor() override;

    /// The frequency drawn at a pixel. Public because it is the inverse of where
    /// the display puts a marker, and the two agreeing is exactly what makes a
    /// drag land where the pointer is -- which is a claim worth being able to
    /// check from outside. `tezla-ui-preview focus-drag` does.
    [[nodiscard]] double frequencyAt (int x) const;

private:
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    void drawGrid (juce::Graphics&, juce::Rectangle<float> area,
                   juce::Rectangle<float> axis) const;
    void drawMarker (juce::Graphics&, juce::Rectangle<float> area, double hz,
                     const juce::String& label, juce::Colour colour, bool dashed) const;
    void drawAddedRegion (juce::Graphics&, juce::Rectangle<float> area) const;
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

    /// What the analysers were last built for, so prepare() can be called
    /// from a timer and cost nothing when the host rate has not moved. The
    /// requested axis is kept separately from the settled lowHz_/highHz_
    /// above: rebuilding from the settled values would creep the axis by
    /// half a bin per rebuild.
    int    preparedRateHz_ { 0 };
    int    preparedOrder_  { 0 };
    int    preparedBins_   { 0 };
    double requestedLowHz_  { 20.0 };
    double requestedHighHz_ { 20000.0 };
    double focusHz_  { 3000.0 };
    bool   aboveMode_ { true };

    bool   floorOn_    { false };
    double floorHz_    { 200.0 };
    bool   ceilingOn_  { true };
    double ceilingHz_  { 16000.0 };
    bool   dimmed_   { false };
    bool   ready_    { false };
    bool   hovered_  { false };
    bool   dragging_ { false };

    static constexpr float kTopDb    = 6.0f;
    static constexpr float kBottomDb = -78.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};

} // namespace tezla::ui
