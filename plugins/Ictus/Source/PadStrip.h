// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The pad strip: the eight pads as small lit plates. Each names its drum and
// its MIDI note, lights when the pad is struck -- as bright as the hit was
// hard, fading over a quarter of a second -- and, for a pad that has a page,
// selects it. The one place on the panel that shows the instrument being
// PLAYED rather than set, and the precursor of the pad grid (I9).

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::ictus {

class PadLamp final : public juce::Button
{
public:
    PadLamp (PadIndex pad, const juce::String& name, ui::Palette palette, bool hasPage);

    void setNote (int note);
    void setSelected (bool selected);
    void flash (float velocity);

    /// One timer tick: the glow fades. Returns whether a repaint is due.
    bool fade();

    [[nodiscard]] PadIndex pad() const noexcept { return pad_; }
    [[nodiscard]] bool hasPage() const noexcept { return hasPage_; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    PadIndex pad_;
    juce::String name_;
    juce::String noteText_;
    ui::Palette palette_;
    bool hasPage_;
    bool selected_ { false };
    float glow_ { 0.0f };
};

class PadStrip final : public juce::Component
{
public:
    PadStrip (IctusProcessor& processor, ui::Palette palette);

    /// Reads each pad's hit count and lights what was struck since the
    /// last call; fades the rest. From the editor's timer.
    void refresh();

    void setSelected (PadIndex pad);

    std::function<void (PadIndex)> onPadSelected;

    void resized() override;

private:
    IctusProcessor& processor_;
    std::vector<std::unique_ptr<PadLamp>> lamps_;
    std::array<std::uint32_t, kPadCount> seenHits_ {};
};

} // namespace tezla::ictus
