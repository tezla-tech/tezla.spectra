// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The Svarayantra panel: a soundfont on the left, the microtuning on its own
// page -- the SAME tuning page as Sonitus, from shared/tezla-ui, because the
// whole point of this instrument is that the two agree about what a scale
// means. The header sets the name in both scripts: SVARAYANTRA and
// its Sanskrit self.

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/TuningPanel.hpp>

#include "PluginProcessor.h"

namespace tezla::svarayantra {

namespace ui = tezla::ui;

/// The preset list: every bank:program of the loaded font, the sounding one
/// highlighted. A click chooses; MIDI program changes move the highlight.
class PresetListModel final : public juce::ListBoxModel
{
public:
    PresetListModel (SvarayantraProcessor& processorToUse, ui::Palette palette,
                     std::function<void()> onChoice)
        : svarayantra_ (processorToUse), palette_ (palette),
          onChoice_ (std::move (onChoice)) {}

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool rowIsSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

private:
    SvarayantraProcessor& svarayantra_;
    ui::Palette palette_;
    std::function<void()> onChoice_;
};

class SvarayantraEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit SvarayantraEditor (SvarayantraProcessor& processorToUse);
    ~SvarayantraEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int index);
    void loadFontDialog();
    void refreshFontLabels();

    SvarayantraProcessor& svarayantra_;
    ui::Palette palette_;
    ui::KnobLookAndFeel lookAndFeel_;

    // ---- header ----
    juce::Label titleLabel_;
    juce::Label devanagariLabel_;
    juce::Label vendorLabel_;
    juce::TextButton tooltipsButton_ { "?" };
    juce::TextButton pageButtons_[2];

    // ---- FONT page ----
    juce::Component fontPage_;
    juce::TextButton loadButton_ { "Load .sf2..." };
    juce::TextButton clearButton_ { "Clear" };
    juce::Label fontNameLabel_;
    juce::Label fontPathLabel_;
    juce::Label fontErrorLabel_;
    juce::Label presetsHeading_;
    std::unique_ptr<PresetListModel> presetModel_;
    juce::ListBox presetList_;

    juce::Slider trimSlider_;
    juce::Label trimLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimAttachment_;
    juce::Slider bendSlider_;
    juce::Label bendLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bendAttachment_;
    juce::Label voicesLabel_;

    // ---- TUNING page ----
    ui::TuningPanel tuningPanel_;

    std::unique_ptr<juce::FileChooser> chooser_;
    ui::TooltipHost tooltips_ { *this };

    int currentPage_ { 0 };
    int lastSeenProgram_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SvarayantraEditor)
};

} // namespace tezla::svarayantra
