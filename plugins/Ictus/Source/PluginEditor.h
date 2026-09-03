// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The Ictus editor at I3: the shared header (output, oversampling, render
// quality, A/B, tooltips), a pad strip with a HIT button that strikes the
// page's pad so a drum can be auditioned without a keyboard, a BASS lamp and
// three page tabs -- Kick 1's page, Snare 1's page, and the shared tuning
// panel. The pad grid and the other pads' pages arrive with the editor
// close-out (I9).

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/TuningPanel.hpp>

#include "PluginProcessor.h"

namespace tezla::ictus {

/// A grid of house controls, each attached to a parameter: knobs, choices and
/// lamp switches (never a tick box -- CLAUDE.md section 8).
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                 int columns)
        : state_ (state), palette_ (palette), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name,
                  const juce::String& tooltip);
    /// `legend` is what the lamp itself says -- a word, since the cap is
    /// 64 px wide -- under the full `name` above it.
    void addSwitch (const char* parameterId, const juce::String& name,
                    const juce::String& legend, const juce::String& tooltip);
    void addGap();

    void setNote (const juce::String& note);
    void setControlEnabled (const char* parameterId, bool enabled);

    /// Replaces a control's tooltip and its label's -- for the ones that
    /// read live state (which scale a key plays through).
    void setTooltip (const char* parameterId, const juce::String& tooltip);

    [[nodiscard]] juce::Colour nameColour() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Switch
    {
        juce::String id;
        std::unique_ptr<ui::LampButton> button;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Cell
    {
        enum class Kind { knob, lamp, gap } kind {};
        int index {};
    };

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    int columns_;

    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Switch>> switches_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

class IctusEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit IctusEditor (IctusProcessor& processor);
    ~IctusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildKickPage();
    void buildSnarePage();
    void buildTuningPage();
    void showPage (int index);
    void refreshPadStrip();
    void styleTab (juce::TextButton& tab, bool active);
    void refreshHeaderTooltips();
    void refreshKeyTooltips();
    void updateGreying();

    IctusProcessor& ictus_;

    ui::TooltipHost tooltips_ { *this };
    ui::Palette palette_;
    ui::KnobLookAndFeel knobLook_ { palette_ };

    std::unique_ptr<ui::HeaderBar> header_;

    // The pad strip: a HIT button, the pad's name and note, the BASS lamp
    // (a global: every key plays Kick 1), the two page tabs, and how many
    // hits are sounding.
    juce::Label padLabel_;
    juce::TextButton hitButton_ { "HIT" };
    ui::LampButton bassButton_ { "Bass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bassAttachment_;
    juce::TextButton kickTab_ { "KICK" };
    juce::TextButton snareTab_ { "SNARE" };
    juce::TextButton tuningTab_ { "TUNING" };
    juce::Label hitsLabel_;

    std::unique_ptr<ControlPage> kickPage_;
    std::unique_ptr<ControlPage> snarePage_;
    std::unique_ptr<ui::TuningPanel> tuningPage_;
    int currentPage_ { 0 };

    /// The pad HIT strikes and the strip names: the page's drum, and the
    /// last drum page's while the tuning page is up.
    PadIndex currentPad_ { PadIndex::kick1 };

    int shownFactor_ { -1 };
    bool shownOffline_ { false };
    int shownRender_ { -1 };
    int shownOversampling_ { -1 };
    int shownRateHz_ { 0 };
    bool shownToneOn_ { true };
    bool shownHarmonics_ { true };
    bool shownTail_ { true };
    bool shownGate_ { true };
    bool shownKeyed_ { true };

    bool shownSnareWires_ { true };
    bool shownSnareCrack_ { true };
    bool shownSnareNoise_ { true };
    bool shownSnareGate_ { true };
    bool shownSnareKeyed_ { true };

    // The live key tooltips are rebuilt only when what they describe moves.
    juce::String shownScale_;
    bool shownBass_ { false };
    int shownPadNote_ { -1 };

    // The NOTE lamps' tooltips name what Tune snaps to: rebuilt when a Tune
    // or a lamp moves (as tenths of a Hz and the two lamp bits, packed).
    juce::int64 shownSnapKey_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IctusEditor)
};

} // namespace tezla::ictus
