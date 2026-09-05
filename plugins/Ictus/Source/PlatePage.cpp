// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PlatePage.h"

#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/Plate.hpp>

namespace tezla::ictus {

namespace
{
constexpr int kLabelHeight = 15;
constexpr int kCellHeightMin = 84;
constexpr int kCellHeightMax = 122;
constexpr int kPlateGap = 8;
constexpr int kNoteHeight = 34;

/// A display keeps its cells' full height; a knob cell is the label row plus
/// the rotary and its value.
constexpr int kSpineInset = 5;
} // namespace

PlatePage::PlatePage (juce::AudioProcessorValueTreeState& state, ui::Palette palette)
    : state_ (state), palette_ (palette)
{
}

int PlatePage::Plate::columns() const noexcept
{
    int total = 0;

    for (const auto& cell : cells)
        total += cell.columns;

    return total;
}

juce::Colour PlatePage::tintOf (int tintIndex) const noexcept
{
    return ui::design::tintFor (palette_.accent, tintIndex);
}

juce::Colour PlatePage::nameColour (juce::Colour tint) const noexcept
{
    return palette_.dimText.interpolatedWith (tint, ui::design::kLabelTint);
}

void PlatePage::beginPlate (const juce::String& heading, const juce::String& detail,
                            int tintIndex, bool sameRow)
{
    Plate plate;
    plate.heading = heading.toUpperCase();
    plate.detail = detail;
    plate.tint = tintOf (tintIndex);
    plate.sameRow = sameRow && ! plates_.empty();
    plates_.push_back (std::move (plate));
}

void PlatePage::addKnob (const char* parameterId, const juce::String& name,
                         const juce::String& tooltip, ui::design::Emphasis emphasis)
{
    if (plates_.empty())
        beginPlate ("", "", 0);

    auto knob = std::make_unique<Knob>();
    knob->tint = plates_.back().tint;
    knob->emphasis = emphasis;

    knob->slider.setComponentID (parameterId);
    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    ui::styleKnob (knob->slider, palette_, knob->tint, emphasis);
    ui::resetsToDefault (knob->slider, state_, parameterId);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    ui::styleName (knob->label, palette_, knob->tint);
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->id = parameterId;
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    plates_.back().cells.push_back ({ Cell::Kind::knob, static_cast<int> (knobs_.size()), 1 });
    knobs_.push_back (std::move (knob));
}

void PlatePage::addLamp (const char* parameterId, const juce::String& name,
                         const juce::String& legend, const juce::String& tooltip)
{
    if (plates_.empty())
        beginPlate ("", "", 0);

    auto lamp = std::make_unique<Lamp>();
    lamp->tint = plates_.back().tint;

    lamp->button = std::make_unique<ui::LampButton> (legend);
    lamp->button->setComponentID (parameterId);
    lamp->button->setClickingTogglesState (true);
    lamp->button->setTooltip (tooltip);
    addAndMakeVisible (*lamp->button);

    lamp->label.setText (name, juce::dontSendNotification);
    ui::styleName (lamp->label, palette_, lamp->tint);
    lamp->label.setTooltip (tooltip);
    addAndMakeVisible (lamp->label);

    lamp->id = parameterId;
    lamp->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, *lamp->button);

    plates_.back().cells.push_back ({ Cell::Kind::lamp, static_cast<int> (lamps_.size()), 1 });
    lamps_.push_back (std::move (lamp));
}

void PlatePage::addChoice (const char* parameterId, const juce::String& name,
                           const juce::StringArray& options, const juce::String& tooltip)
{
    if (plates_.empty())
        beginPlate ("", "", 0);

    auto choice = std::make_unique<Choice>();
    choice->tint = plates_.back().tint;

    // Item ids from 1: the attachment maps a choice's index to id + 1.
    for (int i = 0; i < options.size(); ++i)
        choice->box.addItem (options[i], i + 1);

    choice->box.setComponentID (parameterId);
    choice->box.setTooltip (tooltip);
    choice->box.setJustificationType (juce::Justification::centred);
    ui::styleChoice (choice->box, palette_, choice->tint);
    ui::noWheel (choice->box);
    addAndMakeVisible (choice->box);

    choice->label.setText (name, juce::dontSendNotification);
    ui::styleName (choice->label, palette_, choice->tint);
    choice->label.setTooltip (tooltip);
    addAndMakeVisible (choice->label);

    choice->id = parameterId;
    choice->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state_, parameterId, choice->box);

    plates_.back().cells.push_back ({ Cell::Kind::choice, static_cast<int> (choices_.size()), 1 });
    choices_.push_back (std::move (choice));
}

juce::Component* PlatePage::addDisplay (std::unique_ptr<juce::Component> display, int columns)
{
    if (plates_.empty())
        beginPlate ("", "", 0);

    auto* raw = display.get();
    addAndMakeVisible (*raw);

    plates_.back().cells.push_back ({ Cell::Kind::display, static_cast<int> (displays_.size()),
                                      juce::jmax (1, columns) });
    displays_.push_back (std::move (display));

    return raw;
}

void PlatePage::setNote (const juce::String& note)
{
    if (note_ == note)
        return;

    note_ = note;
    resized();
    repaint();
}

void PlatePage::setControlEnabled (const char* parameterId, bool enabled)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
        {
            knob->slider.setEnabled (enabled);
            knob->label.setColour (juce::Label::textColourId,
                                   enabled ? nameColour (knob->tint) : nameColour (knob->tint).withAlpha (0.35f));
            knob->label.repaint();
        }

    for (auto& lamp : lamps_)
        if (lamp->id == id)
        {
            lamp->button->setEnabled (enabled);
            lamp->button->setAlpha (enabled ? 1.0f : 0.45f);
            lamp->label.setColour (juce::Label::textColourId,
                                   enabled ? nameColour (lamp->tint) : nameColour (lamp->tint).withAlpha (0.35f));
            lamp->label.repaint();
        }

    for (auto& choice : choices_)
        if (choice->id == id)
        {
            choice->box.setEnabled (enabled);
            choice->box.setAlpha (enabled ? 1.0f : 0.45f);
            choice->label.setColour (juce::Label::textColourId,
                                     enabled ? nameColour (choice->tint) : nameColour (choice->tint).withAlpha (0.35f));
            choice->label.repaint();
        }
}

void PlatePage::setTooltip (const char* parameterId, const juce::String& tooltip)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
        {
            knob->slider.setTooltip (tooltip);
            knob->label.setTooltip (tooltip);
        }

    for (auto& lamp : lamps_)
        if (lamp->id == id)
        {
            lamp->button->setTooltip (tooltip);
            lamp->label.setTooltip (tooltip);
        }
}

void PlatePage::setValueText (const char* parameterId, std::function<juce::String (double)> text)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
        {
            knob->slider.textFromValueFunction = std::move (text);
            knob->slider.updateText();
            return;
        }
}

void PlatePage::refreshValueText (const char* parameterId)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
            knob->slider.updateText();
}

void PlatePage::paint (juce::Graphics& g)
{
    const auto plateColour = palette_.panel.brighter (0.22f);

    for (const auto& plate : plates_)
    {
        if (plate.bounds.isEmpty())
            continue;

        ui::paintPlate (g, plate.bounds, plateColour, plate.tint);

        if (plate.heading.isNotEmpty())
            ui::paintPlateHeading (g, palette_, plate.bounds.withHeight (ui::kPlateHeadingHeight),
                                   plate.heading, plate.detail, plate.tint);
    }

    if (note_.isNotEmpty() && ! noteArea_.isEmpty())
    {
        g.setColour (palette_.dimText);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (note_, noteArea_, juce::Justification::centredLeft, 3, 1.0f);
    }
}

void PlatePage::resized()
{
    if (plates_.empty())
        return;

    auto bounds = getLocalBounds().reduced (2, 2);

    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (8, 0);
    else
        noteArea_ = {};

    // The bands: a plate starts one unless it asked to sit in its
    // predecessor's.
    int bands = 0;

    for (const auto& plate : plates_)
        if (! plate.sameRow)
            ++bands;

    const int chrome = ui::kPlateHeadingHeight + 6 + kPlateGap;
    const int available = bounds.getHeight() - bands * chrome;
    const int cellHeight = juce::jlimit (kCellHeightMin, kCellHeightMax,
                                         available / juce::jmax (1, bands));

    int y = bounds.getY();

    for (std::size_t first = 0; first < plates_.size();)
    {
        std::size_t last = first + 1;

        while (last < plates_.size() && plates_[last].sameRow)
            ++last;

        const int bandHeight = ui::kPlateHeadingHeight + cellHeight + 6;

        layoutBand (first, last, { bounds.getX(), y, bounds.getWidth(), bandHeight }, cellHeight);

        y += bandHeight + kPlateGap;
        first = last;
    }
}

int PlatePage::minimumHeight() const noexcept
{
    int bands = 0;

    for (const auto& plate : plates_)
        if (! plate.sameRow)
            ++bands;

    // Mirrors resized(): the 2 px margins, the note, and per band the heading,
    // the smallest cell, its 6 px and the gap.
    const int chrome = ui::kPlateHeadingHeight + 6 + kPlateGap;
    return 4 + (note_.isEmpty() ? 0 : kNoteHeight) + bands * (chrome + kCellHeightMin);
}

void PlatePage::layoutBand (std::size_t first, std::size_t last, juce::Rectangle<int> band,
                            int cellHeight)
{
    int totalColumns = 0;

    for (std::size_t i = first; i < last; ++i)
        totalColumns += plates_[i].columns();

    int x = band.getX();

    for (std::size_t i = first; i < last; ++i)
    {
        auto& plate = plates_[i];
        const bool lastInBand = i + 1 == last;
        const int width = lastInBand ? band.getRight() - x
                                     : band.getWidth() * plate.columns() / juce::jmax (1, totalColumns);

        plate.bounds = { x, band.getY(), width, band.getHeight() };

        auto inner = plate.bounds.reduced (4, 2).withTrimmedTop (ui::kPlateHeadingHeight);
        inner.removeFromLeft (kSpineInset);

        // Knob cells stop widening at the house maximum; whatever is left
        // goes to the displays, which want every pixel they can get, or
        // centres the knobs when there are none.
        int displayColumns = 0;

        for (const auto& cell : plate.cells)
            if (cell.kind == Cell::Kind::display)
                displayColumns += cell.columns;

        const int columns = juce::jmax (1, plate.columns());
        const int cellWidth = juce::jmin (ui::design::kCellWidthMax, inner.getWidth() / columns);
        const int leftover = inner.getWidth() - cellWidth * columns;
        const int extraPerDisplayColumn = displayColumns > 0 ? leftover / displayColumns : 0;

        int cx = inner.getX() + (displayColumns > 0 ? 0 : leftover / 2);

        for (const auto& cell : plate.cells)
        {
            const int cellSpan = cell.columns * cellWidth
                               + (cell.kind == Cell::Kind::display ? cell.columns * extraPerDisplayColumn : 0);

            juce::Rectangle<int> area { cx, inner.getY(), cellSpan, cellHeight };

            switch (cell.kind)
            {
                case Cell::Kind::knob:
                {
                    auto& knob = *knobs_[static_cast<std::size_t> (cell.index)];
                    knob.label.setBounds (area.removeFromTop (kLabelHeight));
                    knob.slider.setBounds (ui::emphasised (area.reduced (3, 0), knob.emphasis));
                    break;
                }

                case Cell::Kind::lamp:
                {
                    auto& lamp = *lamps_[static_cast<std::size_t> (cell.index)];
                    lamp.label.setBounds (area.removeFromTop (kLabelHeight));
                    lamp.button->setBounds (ui::LampButton::sized (64, 26).withCentre (area.getCentre()));
                    break;
                }

                case Cell::Kind::choice:
                {
                    auto& choice = *choices_[static_cast<std::size_t> (cell.index)];
                    choice.label.setBounds (area.removeFromTop (kLabelHeight));
                    const int boxWidth = juce::jmin (area.getWidth() - 8, 104);
                    choice.box.setBounds (juce::Rectangle<int> (boxWidth, 26).withCentre (area.getCentre()));
                    break;
                }

                case Cell::Kind::display:
                    displays_[static_cast<std::size_t> (cell.index)]->setBounds (area.reduced (4, 1));
                    break;
            }

            cx += cellSpan;
        }

        x += width;
    }
}

} // namespace tezla::ictus
