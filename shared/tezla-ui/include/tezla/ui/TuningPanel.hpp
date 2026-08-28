// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The microtuning panel, shared between the instruments.
//
// Grown inside Sonitus and extracted once Svarayantra needed the identical
// page: the built-in scale menu, the .scl/.kbm loaders that refuse a partial
// parse, the degree table with exact fractions and live Hz, the construction
// and story of the selected scale, the pitch-standard lore with its Apply
// button, and the A4 control. One implementation, so the two instruments
// cannot drift apart in what a scale means.
//
// The panel talks to its plugin through TuningHost -- the same method names
// the processors already had -- so the component depends on no processor and
// each processor stays testable without a UI.

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include <tezla/dsp/Tuning.hpp>

#include "Palette.hpp"
#include "TuningHost.hpp"

namespace tezla::ui {

/// A label that wraps its text instead of eliding it.
class TuningWrappingLabel final : public juce::Label
{
public:
    void paint (juce::Graphics& g) override;
};

/// Every degree of the loaded scale: number, exact fraction (only when it IS
/// one -- a tempered degree shows a dash and speaks through its cents), the
/// step to the next degree, and the sounding frequency, which follows the A4
/// control live. The repeat interval is the final row.
class TuningDegreeTable final : public juce::Component
{
public:
    explicit TuningDegreeTable (Palette palette) : palette_ (palette) {}

    void setScale (const tezla::dsp::Scale& scale, double rootHz);

    void paint (juce::Graphics& g) override;

    [[nodiscard]] int preferredHeight() const noexcept
    {
        return kHeaderHeight + kRowHeight * static_cast<int> (rows_.size());
    }

    static constexpr int kRowHeight = 15;
    static constexpr int kHeaderHeight = 17;

private:
    struct Row
    {
        juce::String degree, ratio, cents, step, hz;
        bool isRepeat { false };
    };

    Palette palette_;
    std::vector<Row> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningDegreeTable)
};

/// The tuning page itself. The host plugin supplies its own explanation
/// paragraph -- why microtuning matters in THIS instrument -- and everything
/// else is common.
class TuningPanel final : public juce::Component
{
public:
    TuningPanel (TuningHost& host, Palette palette, juce::String explanationText);

    /// Refreshes every readout from the host. Call when anything has changed
    /// the tuning, including a state load.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void loadScaleFile();
    void loadKeyboardMapFile();
    void reportFailure (const juce::String& what, const juce::String& reason);

    TuningHost& host_;
    Palette palette_;

    juce::ComboBox   scaleBox_;
    juce::TextButton loadScaleButton_ { "Load .scl..." };
    juce::TextButton loadMapButton_   { "Load .kbm..." };
    juce::TextButton resetButton_     { "12-TET" };

    juce::Label         headingLabel_;
    TuningWrappingLabel descriptionLabel_;
    TuningWrappingLabel explanationLabel_;
    TuningWrappingLabel errorLabel_;

    TuningWrappingLabel constructionLabel_;
    TuningWrappingLabel storyLabel_;
    TuningDegreeTable   degreeTable_;
    juce::Viewport      tableViewport_;

    TuningWrappingLabel pitchLoreLabel_;
    juce::TextButton    applyPitchButton_;
    juce::Slider        concertSlider_;
    juce::Label         concertLabel_;

    std::unique_ptr<juce::FileChooser> chooser_;

    /// Guards the combo box's callback while it is being set from the host,
    /// so restoring a selection does not read as the user choosing it.
    bool updating_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningPanel)
};

} // namespace tezla::ui
