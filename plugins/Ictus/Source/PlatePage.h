// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A page of control plates: each plate a group with its own colour (the page
// accent rotated by `design::tintFor`), a heading, a spine, and one row of
// cells -- knobs at lead, normal or trim size, lamp switches, and displays
// that span several cells. Plates flow top to bottom, or sit beside each
// other in a band when asked to; a band's width is shared in proportion to
// its plates' cell counts, so cells come out the same size across it.
//
// The flat grid this replaced gave every control of a 29-knob page the same
// footprint and the same colour, and the eye had nowhere to land (the user's
// words). What survives from it: the house knob, the house lamp, the note
// under the page, and the greying of a control whose master is off.

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/PanelDesign.hpp>

namespace tezla::ictus {

class PlatePage final : public juce::Component
{
public:
    PlatePage (juce::AudioProcessorValueTreeState& state, ui::Palette palette);

    /// Starts a plate. `tintIndex` rotates the page accent by 18 degrees a
    /// step; `sameRow` puts this plate beside the previous one.
    void beginPlate (const juce::String& heading, const juce::String& detail,
                     int tintIndex, bool sameRow = false);

    void addKnob (const char* parameterId, const juce::String& name,
                  const juce::String& tooltip,
                  ui::design::Emphasis emphasis = ui::design::Emphasis::normal);

    /// `legend` is what the lamp itself says -- a word, since the cap is
    /// 64 px wide -- under the full `name` above it.
    void addLamp (const char* parameterId, const juce::String& name,
                  const juce::String& legend, const juce::String& tooltip);

    /// A choice parameter as a drop-down in the house look -- for the
    /// settings that are a pick from a short list (an output bus), where a
    /// knob would hide the names.
    void addChoice (const char* parameterId, const juce::String& name,
                    const juce::StringArray& options, const juce::String& tooltip);

    /// A display spanning `columns` cells of the current plate. The page
    /// owns it; the returned pointer is for the caller's refresh calls.
    juce::Component* addDisplay (std::unique_ptr<juce::Component> display, int columns);

    void setNote (const juce::String& note);

    /// Greys a control out. A knob that moves and does nothing reads as a
    /// broken plugin, so the controls a setting makes inert say so.
    void setControlEnabled (const char* parameterId, bool enabled);

    /// Replaces a control's tooltip and its label's -- for the ones that
    /// read live state.
    void setTooltip (const char* parameterId, const juce::String& tooltip);

    /// How a knob prints its value -- a Tune knob shows the note it snaps to.
    void setValueText (const char* parameterId, std::function<juce::String (double)> text);

    /// Re-renders a knob's value text after something other than its value
    /// changed what it should say.
    void refreshValueText (const char* parameterId);

    [[nodiscard]] juce::Colour tintOf (int tintIndex) const noexcept;

    /// The height below which the plates would overflow: every band at the
    /// smallest cell, plus the note. The editor scrolls the page when the
    /// window is shorter than this rather than letting the last rows fall off
    /// the bottom (the snare page grew to six rows at I4.5).
    [[nodiscard]] int minimumHeight() const noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label label;
        juce::Colour tint;
        ui::design::Emphasis emphasis {};
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Lamp
    {
        juce::String id;
        std::unique_ptr<ui::LampButton> button;
        juce::Label label;
        juce::Colour tint;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Choice
    {
        juce::String id;
        juce::ComboBox box;
        juce::Label label;
        juce::Colour tint;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Cell
    {
        enum class Kind { knob, lamp, display, choice } kind {};
        int index {};
        int columns { 1 };
    };

    struct Plate
    {
        juce::String heading;
        juce::String detail;
        juce::Colour tint;
        bool sameRow { false };
        std::vector<Cell> cells;
        juce::Rectangle<int> bounds;

        [[nodiscard]] int columns() const noexcept;
    };

    [[nodiscard]] juce::Colour nameColour (juce::Colour tint) const noexcept;
    void layoutBand (std::size_t first, std::size_t last, juce::Rectangle<int> band, int cellHeight);

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;

    std::vector<Plate> plates_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Lamp>> lamps_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<std::unique_ptr<juce::Component>> displays_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

} // namespace tezla::ictus
