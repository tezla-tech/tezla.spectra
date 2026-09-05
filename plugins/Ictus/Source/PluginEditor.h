// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The Ictus editor: the shared header (output, oversampling, render quality,
// A/B, tooltips); a pad strip whose eight pads light when struck and open
// their pages; a HIT button for the page's pad; the BASS lamp; the TUNING
// tab; then the page -- Kick 1's, Snare 1's or the ghost's control plates in
// the house look, each plate a group with its own colour, its lead control
// drawn larger, its set-and-forget controls smaller, and a picture of what
// the group does drawn from the knobs (Displays.h); or the shared tuning
// panel. The snare page is built once, for either snare-engine pad; the
// ghost's adds LINK, and greys what LINK borrows from the main snare. The
// hats have ONE page for both pads -- they are one pair of cymbals struck two
// ways -- and the clap has its own.
//
// The pages, in order: 0 kick, 1 snare, 2 ghost, 3 tuning, 4 hats, 5 clap.
// The order is the editor's own and nothing saves it, so it can grow.

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/TuningPanel.hpp>

#include "Displays.h"
#include "PadStrip.h"
#include "PlatePage.h"
#include "PluginProcessor.h"

namespace tezla::ictus {

class IctusEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit IctusEditor (IctusProcessor& processor);
    ~IctusEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /// The three pictures a snare page carries, for the timer's refresh.
    struct SnareViews
    {
        ModesView* modes { nullptr };
        WiresView* wires { nullptr };
        EnvelopeView* envelope { nullptr };
    };

    /// What a snare page's greying was last set to.
    struct SnareShown
    {
        bool wires { true }, crack { true }, noise { true }, gate { true }, keyed { true }, linked { false };
        bool thump { true };
        bool rattle { true }, room { true }, clap { true };
    };

    void timerCallback() override;
    void buildKickPage();
    SnareViews buildSnarePage (PlatePage& page, const SnareIds& ids, PadIndex pad);
    void buildHatPage();
    void buildClapPage();
    void buildMixPage();
    void buildTuningPage();
    void updateSnareGreying (PlatePage& page, const SnareIds& ids, SnareShown& shown);
    void showPage (int index);
    void selectPad (PadIndex pad);
    void styleTab (juce::TextButton& tab, bool active);
    void refreshHeaderTooltips();
    void refreshKeyTooltips();
    void refreshPadStrip();
    void refreshDisplays();
    void updateGreying();

    /// The plate page the viewport shows, sized to the viewport or to its own
    /// minimum height, whichever is taller -- the taller one scrolls.
    void layoutViewedPage();

    IctusProcessor& ictus_;

    ui::TooltipHost tooltips_ { *this };
    ui::Palette palette_;
    ui::KnobLookAndFeel knobLook_ { palette_ };

    std::unique_ptr<ui::HeaderBar> header_;

    // The strip: HIT for the page's pad, the eight pads, BASS, TUNING, and
    // how many hits are sounding.
    juce::TextButton hitButton_ { "HIT" };
    std::unique_ptr<PadStrip> padStrip_;
    ui::LampButton bassButton_ { "Bass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bassAttachment_;
    juce::TextButton tuningTab_ { "TUNING" };
    juce::TextButton mixTab_ { "MIX" };
    juce::Label hitsLabel_;

    std::unique_ptr<PlatePage> kickPage_;
    std::unique_ptr<PlatePage> snarePage_;
    std::unique_ptr<PlatePage> ghostPage_;
    std::unique_ptr<PlatePage> hatPage_;
    std::unique_ptr<PlatePage> clapPage_;
    std::unique_ptr<PlatePage> mixPage_;
    std::unique_ptr<ui::TuningPanel> tuningPage_;

    /// One viewport shows whichever plate page is current; a page taller than
    /// the window scrolls in it (the wheel scrolls the panel, never a knob).
    juce::Viewport pageView_;

    // The pictures, owned by their pages; kept for the timer's refresh.
    PitchView* pitchView_ { nullptr };
    EnvelopeView* kickEnvelope_ { nullptr };
    SnareViews snareViews_;
    SnareViews ghostViews_;
    PartialsView* partialsView_ { nullptr };
    BurstView* burstView_ { nullptr };
    FieldView* fieldView_ { nullptr };

    int currentPage_ { 0 };

    /// The pad HIT strikes and the strip highlights: the page's drum, and
    /// the last drum page's while the tuning page is up.
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
    bool shownUnder_ { true };
    bool shownKnock_ { true };
    bool shownKickRoom_ { true };

    SnareShown shownSnare_;
    SnareShown shownGhost_;

    /// What the hat and clap pages' greying was last set to.
    bool shownHatAir_ { true };
    bool shownHatGate_ { true };
    bool shownHatHoldLink_ { false };
    bool shownClapBody_ { true };
    bool shownClapNoise_ { true };
    bool shownClapGate_ { true };
    bool shownClapRoom_ { true };
    bool shownHatPlate_ { true };

    /// Per pad, whether the MIX page's Width and Mono below were last shown
    /// live -- a pad with nothing spread and no room has no side for them.
    bool shownPadField_[kPadCount] { true, true, true, true, true, true, true, true };

    // The live key tooltips and the Tune readouts are rebuilt only when
    // what they describe moves.
    juce::String shownScale_;
    bool shownBass_ { false };
    int shownPadNote_ { -1 };
    juce::int64 shownSnapKey_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IctusEditor)
};

} // namespace tezla::ictus
