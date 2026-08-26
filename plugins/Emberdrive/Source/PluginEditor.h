#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::emberdrive
{

/// A meter with honest ballistics: a VU bar for how loud it is, a peak line for
/// whether it is about to clip, and a separate gain-reduction bar. Those two
/// readings disagree by 10 dB or more on drums, and the disagreement is the
/// information an engineer is actually reading.
class LevelMeter final : public juce::Component
{
public:
    enum class Style { level, gainReduction };

    explicit LevelMeter (Style style) : style_ (style) {}

    void setValues (float vuDb, float peakDb) noexcept
    {
        vuDb_ = vuDb;
        peakDb_ = peakDb;
    }

    void paint (juce::Graphics&) override;

private:
    [[nodiscard]] float positionFor (float db) const noexcept;

    Style style_;
    float vuDb_   { -100.0f };
    float peakDb_ { -100.0f };
};

/// Wraps, so the "what Auto is doing right now" sentence is never truncated.
class WrappingLabel final : public juce::Label
{
public:
    void paint (juce::Graphics& g) override;
};

/// A rotary control with its name above and its value below, plus the tooltip
/// that is this plugin's only documentation.
struct Knob
{
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/// One page of the control surface. Holds its own knobs and lays them out on a
/// grid; the editor just decides which page is visible.
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, int columns)
        : state_ (state), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addChoice (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addToggle (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addBreak();

    /// A line of guidance shown under the grid. For the things that are too
    /// important to leave in a tooltip nobody hovers over.
    void setNote (const juce::String& note) { note_ = note; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Choice
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    /// What sits in each grid cell, in the order it was added.
    struct Cell
    {
        enum class Kind { knob, choice, toggle, gap } kind {};
        int index {};
    };

    juce::AudioProcessorValueTreeState& state_;
    int columns_;

    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<std::unique_ptr<Toggle>> toggles_;
    std::vector<Cell> cells_;
    juce::String note_;
    int gridBottom_ { 0 };
};

class EmberdriveEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit EmberdriveEditor (EmberdriveProcessor& processorToUse);
    ~EmberdriveEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPages();
    void showPage (int index);

    EmberdriveProcessor& processor_;

    juce::TooltipWindow tooltips_ { this, 500 };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;

    static constexpr int kNumPages = 4;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    LevelMeter inputMeter_     { LevelMeter::Style::level };
    LevelMeter outputMeter_    { LevelMeter::Style::level };
    LevelMeter reductionMeter_ { LevelMeter::Style::gainReduction };

    juce::Label inputMeterLabel_     { {}, "IN" };
    juce::Label outputMeterLabel_    { {}, "OUT" };
    juce::Label reductionMeterLabel_ { {}, "GR" };

    WrappingLabel statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberdriveEditor)
};

} // namespace tezla::emberdrive
