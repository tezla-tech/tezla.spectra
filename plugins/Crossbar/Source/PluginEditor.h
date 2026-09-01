// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>

#include "PluginProcessor.h"

namespace tezla::crossbar {

/// Wraps, so the "what this is doing right now" sentence is never truncated.
class WrappingLabel final : public juce::Label
{
public:
    void paint (juce::Graphics& g) override;
};

/// The panel, and the picture of what the plugin is.
///
/// A DTMF keypad is literally a crossbar: four row frequencies down the left,
/// four column frequencies along the top, and a key is the point where one of
/// each crosses. So the frequencies are *drawn* on the edges rather than
/// hidden in a manual, and pressing a key lights its row, its column and the
/// two numbers -- which is the whole encoding explained without a sentence of
/// prose.
///
/// The call-progress tones sit under the keypad on the same grid, because they
/// are the other half of what a telephone makes and a key that only exists on
/// the MIDI map is a key nobody finds.
class KeypadView final : public juce::Component,
                         public juce::TooltipClient
{
public:
    KeypadView (ui::Palette palette, Region region);

    /// The tooltip is **per pad**, from whatever the pointer is over: one
    /// tooltip for a component this dense would have to say nothing in
    /// particular, and every key here has a specific answer -- its two
    /// frequencies, its cadence, and which MIDI note plays it.
    [[nodiscard]] juce::String getTooltip() override;

    /// Called on press and release, with the tone's index into the map.
    std::function<void (int, bool)> onKey;

    /// Which tones are sounding, one bit per `Tone`. Repaints only when it
    /// changes.
    void setSounding (std::uint64_t mask);

    /// The call-progress row's captions follow the region, because a UK
    /// engaged tone is not a North American busy one.
    void setRegion (Region region);

    /// Where the map starts, so a pad can say which key plays it.
    void setMapRoot (int root);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    struct Pad
    {
        Tone tone {};
        juce::Rectangle<int> bounds;
        juce::String caption;
        juce::String detail;
        bool large { false };
    };

    void press (int padIndex);
    void releaseHeld();
    [[nodiscard]] int padAt (juce::Point<int> position) const;
    [[nodiscard]] bool isSounding (Tone tone) const;

    void drawPad (juce::Graphics& g, const Pad& pad) const;
    [[nodiscard]] juce::String describe (Tone tone) const;

    ui::Palette palette_;
    Region region_;
    int mapRoot_ { kDefaultMapRoot };

    std::vector<Pad> pads_;
    int held_ { -1 };
    std::uint64_t sounding_ { 0 };

    juce::Rectangle<int> keypadArea_;
    juce::Rectangle<int> rowLabelArea_;
    juce::Rectangle<int> columnLabelArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeypadView)
};

/// One page of the control surface. The house grid, copied from Ferrite --
/// per-plugin rather than shared, because every plugin ends up wanting one
/// thing from it that the others do not.
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette, int columns)
        : state_ (state), palette_ (palette), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addChoice (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addGap();

    /// A line of guidance under the grid, for the things too important to
    /// leave in a tooltip nobody hovers over.
    void setNote (const juce::String& note);

    /// Greys a control out. A knob that moves and does nothing reads as a
    /// broken plugin.
    void setControlEnabled (const char* parameterId, bool enabled);

    /// The colour every name on this page was given, so switching a control off
    /// and on again returns it to the right one.
    [[nodiscard]] juce::Colour nameColour() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::String   id;
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Cell
    {
        enum class Kind { knob, choice, gap } kind {};
        int index {};
    };

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    int columns_;

    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

class CrossbarEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit CrossbarEditor (CrossbarProcessor& processorToUse);
    ~CrossbarEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPages();
    void showPage (int index);

    /// Greys the controls the current settings make inert, and refreshes the
    /// notes. Only when something actually changed -- recomputing this fifteen
    /// times a second would repaint the panel for nothing.
    void updateForSwitches();

    CrossbarProcessor& crossbar_;

    ui::TooltipHost tooltips_ { *this };
    ui::Palette palette_;

    /// The house look and feel. Declared after `palette_` so the initialiser
    /// list can hand it one, and installed on the editor so every page inherits
    /// it -- JUCE walks up the parent chain to find one.
    ui::KnobLookAndFeel knobLook_ { palette_ };

    std::unique_ptr<KeypadView> keypad_;

    /// The number, and the button that dials it. A text field rather than a
    /// parameter, because a phone number is text -- see PluginProcessor.h.
    juce::Label dialCaption_ { {}, "NUMBER" };
    juce::TextEditor dialField_;
    juce::TextButton dialButton_ { "DIAL" };

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label vendorLabel_;
    juce::Label voicesLabel_;
    juce::ToggleButton tooltipsButton_ { "?" };

    static constexpr int kNumPages = 3;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    /// What the panel is currently dressed for, so the greying and the notes
    /// are not recomputed every tick. Deliberately impossible starting values,
    /// so the first tick always applies the state.
    int shownCodec_ { -1 };
    int shownRate_ { -1 };
    int shownBand_ { -1 };
    int shownDialMode_ { -1 };
    int shownCadence_ { -1 };
    int shownRegion_ { -1 };
    double shownEffectiveRate_ { -1.0 };
    std::uint64_t shownSounding_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossbarEditor)
};

} // namespace tezla::crossbar
