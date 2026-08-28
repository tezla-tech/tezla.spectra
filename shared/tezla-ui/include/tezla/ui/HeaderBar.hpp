// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The strip across the top of every plugin: name, a bypass that lights up, and
// A/B compare.
//
// These live in the header rather than on a page because they are the two
// controls a user reaches for while listening, and a control you have to change
// tabs to find is a control you stop using. Bypass in particular has to be
// reachable from wherever you happen to be.

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Palette.hpp"

namespace tezla::ui
{

/// A button that is lit rather than ticked.
///
/// A tick box tells you a state if you look for it. This tells you across the
/// room: when bypass is engaged the whole button glows, with a soft halo around
/// it so it reads even in peripheral vision. That matters because a plugin left
/// bypassed by accident is a mix decision made by accident.
class GlowButton final : public juce::Button
{
public:
    GlowButton (const juce::String& name, Palette palette);

    /// The lit colour. Defaults to the palette's bypass orange.
    void setGlowColour (juce::Colour colour);

    void paintButton (juce::Graphics&, bool shouldDrawHighlighted, bool shouldDrawDown) override;

private:
    Palette palette_;
    juce::Colour glow_;
};

/// Name on the left, bypass and A/B on the right, a rule underneath in the
/// plugin's accent colour.
class HeaderBar final : public juce::Component
{
public:
    /// `title` is the plugin's name in capitals; `subtitle` the one-line
    /// description that sits under the vendor name.
    ///
    /// `bypassParameterId` may be null or empty, and then the bypass button is
    /// left out rather than shown dead -- an instrument has nothing to bypass,
    /// and muting the track is what a player reaches for anyway.
    HeaderBar (juce::AudioProcessorValueTreeState& state,
               const juce::String& title,
               const juce::String& subtitle,
               const char* bypassParameterId,
               Palette palette);

    /// Called when the user asks to swap between the A and B settings, and when
    /// they ask to copy the current one across to the other slot. The header
    /// only knows that the buttons were pressed; what a snapshot *is* belongs to
    /// the processor.
    std::function<void()> onSwapRequested;
    std::function<void()> onCopyRequested;

    /// Called when the tooltip switch is flipped, with its new state.
    ///
    /// The header only owns the button. Whether a tooltip window exists at all
    /// is the editor's business, and what the setting is worth remembering
    /// between sessions is the processor's.
    std::function<void (bool)> onTooltipsToggled;

    /// Sets the switch without calling back, for restoring a saved state.
    void setTooltipsEnabled (bool enabled);

    /// Puts the plugin's **output trim** and **oversampling** in the header,
    /// beside A/B and COPY.
    ///
    /// These two are the same control on every plugin in the suite and they are
    /// the last thing anybody touches, so they belong somewhere fixed rather
    /// than at the bottom of whichever page happened to have room. Finding the
    /// output trim on the MANGLE tab of one plugin and the DRIVE tab of the next
    /// is the sort of thing that is only annoying twenty times a day.
    ///
    /// The dry/wet **mix** joins them, for the same reason: on an effect it is
    /// the control you reach for from whichever page you happen to be on.
    ///
    /// Any id may be null, and then that control is left out. Capstone has no
    /// global oversampling, Transpectus has none of the three, and neither Halo
    /// nor Capstone has a dry/wet at all -- Halo's `amount` is how much
    /// harmonic content is *generated*, which is a drive and not a blend, and
    /// putting it here labelled MIX would be a lie about what it does.
    void attachSuiteControls (juce::AudioProcessorValueTreeState& state,
                              const char* mixParameterId,
                              const char* outputParameterId,
                              const char* oversamplingParameterId);

    /// Which slot is live, so the button can show it.
    void setActiveSlot (bool isSlotB);

    /// Whether the inactive slot holds anything yet, so Copy can explain itself
    /// rather than looking broken on first use.
    void setOtherSlotFilled (bool filled);

    void paint (juce::Graphics&) override;
    void resized() override;

    /// How tall the bar wants to be.
    [[nodiscard]] static constexpr int getPreferredHeight() noexcept { return 46; }

private:
    Palette palette_;
    juce::String title_;
    juce::String subtitle_;

    GlowButton bypassButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;

    juce::TextButton swapButton_ { "A / B" };
    juce::TextButton copyButton_ { "COPY" };
    juce::TextButton tipsButton_ { "TIPS" };

    juce::Slider mixSlider_;
    juce::Label mixLabel_ { {}, "MIX" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment_;

    juce::Slider outputSlider_;
    juce::Label outputLabel_ { {}, "OUT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment_;

    juce::ComboBox oversamplingBox_;
    juce::Label oversamplingLabel_ { {}, "OS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment_;

    bool hasMix_ { false };
    bool hasOutput_ { false };
    bool hasOversampling_ { false };

    bool hasBypass_ { true };
    bool slotB_ { false };
    bool otherFilled_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace tezla::ui
