// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The wheel scrolls the panel. It never moves a control.
//
// ---------------------------------------------------------------------------
// Why this is a rule rather than a preference
// ---------------------------------------------------------------------------
//
// These panels scroll: six pages of sixty controls do not fit a window, and the
// viewport is how you reach the bottom of one. On a scrolling panel a wheel that
// also edits is a trap with no feedback -- the pointer happens to be over Detune
// on the way past, the page does not move, and a patch has silently changed by
// three cents. The user finds out later, from the sound, with no idea what
// happened or how to undo it.
//
// It is worse here than on a fixed panel for a second reason: the thing the
// wheel *would* have done is the thing the user asked for. A scroll that edits
// instead of scrolling is not a different feature, it is the feature failing.
//
// So every `Slider` and `ComboBox` in this workshop is built with the wheel
// off. JUCE then falls through to `Component::mouseWheelMove`, which hands the
// event to the parent, and it arrives at the viewport -- which is where it was
// always going.
//
// ---------------------------------------------------------------------------
// Two calls, and the second one is a net rather than the rule
// ---------------------------------------------------------------------------
//
// `noWheel` at the point of construction is the rule: the code that makes a
// control says what the control does, and that is readable where it matters.
//
// `sweepNoWheel` walks a finished tree and turns the wheel off on anything it
// finds. It exists because a page can build controls long after the editor's
// constructor has run -- an ADV envelope unfolding when its Enable is switched
// on, a modulation row appearing when a slot is filled -- and one forgotten
// call site in a plugin with sixty of them is a silent regression of exactly
// the kind above. Call it as a net; do not rely on it as the rule.
//
// ---------------------------------------------------------------------------
// The one exception: a graph is not a control
// ---------------------------------------------------------------------------
//
// The rule above is about *editing*. A wheel that edits a parameter is a trap
// because the parameter changed and nothing said so. A wheel that changes what
// part of a graph you are looking at changes nothing at all -- no parameter
// moves, no sound changes, and the graph in front of you visibly answers what
// happened.
//
// So a display may take the wheel as a view gesture, and Sonitus's ADV envelope
// graphs do: the wheel zooms the time axis, shift-wheel scrolls it, and a
// double-click puts the whole envelope back. Sixteen points across 900 pixels
// is 56 of them a leg, and a point's time, level and tension have no knobs --
// that graph is the only way to reach them, so it has to be reachable.
//
// Two conditions on doing this anywhere else, and they are what keep it from
// being a hole in the rule:
//
//  - **Nothing it does is a parameter change.** The moment a wheel gesture
//    writes to the value tree it is the trap again, whatever it is drawn on.
//  - **There is a way out, and the wheel is never dead.** A graph zoomed fully
//    out passes the next notch to its parent, so the page scrolls -- a control
//    that swallowed the event and did nothing with it would read as broken, and
//    a graph with no way back to 1x would have swallowed its own exit.

#include <juce_gui_basics/juce_gui_basics.h>

namespace tezla::ui
{

inline void noWheel (juce::Slider& slider) noexcept
{
    slider.setScrollWheelEnabled (false);
}

inline void noWheel (juce::ComboBox& box) noexcept
{
    box.setScrollWheelEnabled (false);
}

/// Turns the wheel off on every slider and combo box in a tree, including the
/// component itself. Idempotent and cheap -- it touches a bool per control --
/// so calling it again after a page has grown is free.
inline void sweepNoWheel (juce::Component& component) noexcept
{
    if (auto* slider = dynamic_cast<juce::Slider*> (&component))
        noWheel (*slider);

    if (auto* box = dynamic_cast<juce::ComboBox*> (&component))
        noWheel (*box);

    for (auto* child : component.getChildren())
        if (child != nullptr)
            sweepNoWheel (*child);
}

} // namespace tezla::ui
