// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The tooltip on/off switch, in one place so every plugin gets the same one.
//
// ---------------------------------------------------------------------------
// Why this is a class and not four lines in each editor
// ---------------------------------------------------------------------------
//
// It was four lines in each editor, and five of the six got them wrong by not
// having them at all: the TIPS button in the shared header called back into
// nothing, so the toggle lit up and the tooltips kept appearing. Sonitus was
// the only plugin wired, because it was the one the button was written for.
//
// The mechanism is also not the obvious one, which is the other reason to say
// it once. A `juce::TooltipWindow` is a top-level component that watches the
// mouse for as long as it exists, and there is no "stop" on it -- hiding it or
// setting a huge delay leaves it watching. **Not having one is the off
// switch**, so the window is created and destroyed rather than shown and
// hidden, and `isEnabled` is a null check rather than a second flag that could
// disagree with it.
//
// The delay is 500 ms everywhere. CLAUDE.md section 8 makes tooltips this
// workshop's documentation, so they should arrive when someone rests on a
// control and not while they are moving past it.

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/ui/StateIds.hpp>

namespace tezla::ui
{

/// Owns a plugin editor's tooltip window, and switches it off by not having one.
///
/// Held by value in the editor, constructed with the editor as its parent. The
/// parent must outlive it, which it does -- it is the thing that owns it.
class TooltipHost
{
public:
    /// How long the pointer has to rest before a tooltip appears.
    static constexpr int kDelayMs = 500;

    explicit TooltipHost (juce::Component& owner, bool enabled = true)
        : owner_ (owner)
    {
        setEnabled (enabled);
    }

    /// Creates or destroys the window. Cheap either way, and safe to call with
    /// the value it already has.
    void setEnabled (bool enabled)
    {
        if (enabled == isEnabled())
            return;

        window_ = enabled ? std::make_unique<juce::TooltipWindow> (&owner_, kDelayMs)
                          : nullptr;
    }

    [[nodiscard]] bool isEnabled() const noexcept { return window_ != nullptr; }

private:
    juce::Component& owner_;
    std::unique_ptr<juce::TooltipWindow> window_;
};

} // namespace tezla::ui
