// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// What a house control *is*, in one place.
//
// ---------------------------------------------------------------------------
// Why a helper rather than nine copies of five lines
// ---------------------------------------------------------------------------
//
// `KnobLookAndFeel` knows how to draw a knob in relief with a skirt and a
// tinted track, but only if the knob asks -- the properties are opt-in, because
// the same look and feel draws the header bar's sliders and a meter's scale,
// and those are not knobs on a plate. So every knob in the suite carries the
// same five properties, and "every" is the word that goes wrong: eleven
// editors, a couple of hundred controls, and one that missed a line reads as a
// knob drawn flat next to ten in relief.
//
// Which is exactly the shape of a bug this workshop has already had twice --
// `VoiceParameters` broke a target nobody built, and `BypassMixer::setLatency`
// was guarded at one of its call sites. The rule from both: put the thing that
// must be consistent in the object that knows, and call it.
//
// So: one function says what a knob looks like, one says what a dropdown looks
// like, one says what a name above a control looks like. A new plugin gets the
// house design by calling three functions, and a change to the design lands on
// all of them at once.
//
// The switch is not here because it is a whole component -- see LampButton.hpp.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/ui/Palette.hpp>
#include <tezla/ui/PanelDesign.hpp>
#include <tezla/ui/ScrollWheel.hpp>

namespace tezla::ui
{

/// Dresses a rotary as a house knob and turns its wheel off.
///
/// **Sets the value size before `setTextBoxStyle`**, and that order is load
/// bearing: `setTextBoxStyle` is what calls `createSliderTextBox`, so the label
/// is built exactly once and reads the property as it is built. Setting the
/// size afterwards leaves a label already made at the default.
inline void styleKnob (juce::Slider& slider, Palette palette, juce::Colour tint,
                       design::Emphasis emphasis = design::Emphasis::normal)
{
    auto& properties = slider.getProperties();

    properties.set ("tezlaTint", static_cast<juce::int64> (tint.getARGB()));
    properties.set ("tezlaRelief", true);
    properties.set ("tezlaSkirt", true);
    properties.set ("tezlaTintTrack", true);
    properties.set ("tezlaValueBold", true);
    properties.set ("tezlaValueSize", emphasis == design::Emphasis::lead
                                        ? design::kValueSizeLead
                                        : design::kValueSize);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, design::kValueHeight);
    slider.setColour (juce::Slider::textBoxTextColourId, palette.text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    noWheel (slider);
}

/// Double-click goes back to **the parameter's own default**, not to the middle
/// of its range -- which on a skewed range is somewhere else entirely, and on a
/// drive control is usually somewhere loud.
inline void resetsToDefault (juce::Slider& slider,
                             juce::AudioProcessorValueTreeState& state,
                             const juce::String& parameterId)
{
    if (auto* parameter = state.getParameter (parameterId))
        slider.setDoubleClickReturnValue (
            true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
}

/// Dresses a choice box in its group's colour and turns its wheel off.
inline void styleChoice (juce::ComboBox& box, Palette palette, juce::Colour tint)
{
    box.setColour (juce::ComboBox::backgroundColourId, palette.panel.brighter (0.10f));
    box.setColour (juce::ComboBox::textColourId, palette.text);
    box.setJustificationType (juce::Justification::centredLeft);

    // The drawing is `KnobLookAndFeel::drawComboBox`; this only says which hue,
    // and its presence is what opts the box into the tinted treatment at all.
    box.getProperties().set ("tezlaTint", static_cast<juce::int64> (tint.getARGB()));

    noWheel (box);
}

/// A rotary's bounds, grown for a lead control and shrunk for a set-once trim.
///
/// **The cell keeps its footprint; only the control inside it moves.** The grid
/// is a grid -- a lead knob that pushed its neighbours sideways would make the
/// columns depend on which control happened to be in them.
///
/// The value row keeps its height whatever the knob does, so a row of mixed
/// sizes still has its numbers on one line, and the knob grows *upward* from
/// that row rather than about the cell's centre.
[[nodiscard]] inline juce::Rectangle<int> emphasised (juce::Rectangle<int> area,
                                                      design::Emphasis emphasis,
                                                      int valueHeight = design::kValueHeight)
{
    float scale = 1.0f;

    switch (emphasis)
    {
        case design::Emphasis::lead:   scale = design::kLeadScale; break;
        case design::Emphasis::trim:   scale = design::kTrimScale; break;
        case design::Emphasis::normal: break;
    }

    if (juce::approximatelyEqual (scale, 1.0f) || area.getHeight() <= valueHeight)
        return area;

    const auto value = area.removeFromBottom (valueHeight);

    const int wanted = juce::roundToInt (static_cast<float> (area.getHeight()) * scale);
    const int width = juce::jmin (area.getWidth(),
                                  juce::roundToInt (static_cast<float> (area.getWidth()) * scale));

    auto rotary = juce::Rectangle<int> { width, juce::jmin (wanted, area.getHeight()) }
                    .withCentre ({ area.getCentreX(), area.getCentreY() })
                    .withBottom (area.getBottom());

    return rotary.getUnion (value.withX (rotary.getX()).withWidth (rotary.getWidth()));
}

/// The name above a control: upper case, small, bold, warmed toward its group's
/// colour.
///
/// Upper case because a name set beside a number wants to be read as a label
/// rather than as a word, and at ten pixels the capitals are the legible half
/// of the alphabet anyway. Warmed rather than coloured -- a page of saturated
/// labels is a page where the loudest thing is the words, and the words are the
/// part you already know after a week.
///
/// The caller sets the text first; this reads it back and shouts it.
inline void styleName (juce::Label& label, Palette palette, juce::Colour tint)
{
    label.setText (label.getText().toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId,
                     palette.dimText.interpolatedWith (tint, design::kLabelTint));
    label.setFont (juce::FontOptions (design::kLabelSize, juce::Font::bold));
    label.setMinimumHorizontalScale (0.65f);
}

} // namespace tezla::ui
