#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/LevelMeter.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::anvil
{

/// What the amplifier is doing to itself, which is the whole claim of this
/// plugin made visible.
///
/// Three bars, and none of them is a level:
///
///   SAG    how far the rail has fallen under load
///   FLUX   how full the output transformer's core is -- past 1 it is
///          saturating, and the low end is going with it
///   BIAS   how far the first valve's operating point has drifted
///
/// A gain-reduction meter says how much quieter something got. These say what
/// the amplifier is *doing*, and they move on their own time constants -- tens
/// of milliseconds -- which is exactly the point being made. Watch FLUX while
/// playing a low note and a high one at the same level: it is twice as high for
/// the low one, and nothing in the code tests the frequency.
class WorkingMeter final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit WorkingMeter (ui::Palette palette) : palette_ (palette) {}

    void setValues (float sag, float flux, float bias) noexcept;

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;

    float sag_  { 0.0f };
    float flux_ { 0.0f };
    float bias_ { 0.0f };
};

/// Wraps, so the "what this is doing right now" sentence is never truncated.
class WrappingLabel final : public juce::Label
{
public:
    void paint (juce::Graphics& g) override;
};

/// One page of the control surface. Holds its own controls and lays them out on
/// a grid; the editor just decides which page is visible.
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette, int columns)
        : state_ (state), palette_ (palette), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addChoice (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addGap();

    /// A line of guidance under the grid, for the things too important to leave
    /// in a tooltip nobody hovers over.
    void setNote (const juce::String& note);

    /// Greys a control out. Used for the controls a switch makes inert: a knob
    /// that moves and does nothing reads as a broken plugin rather than a mode.
    void setControlEnabled (const char* parameterId, bool enabled);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::String   id;
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Cell
    {
        enum class Kind { knob, choice, gap } kind {};
        int index {};
    };

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    int columns_;

    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

class AnvilEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit AnvilEditor (AnvilProcessor& processorToUse);
    ~AnvilEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPages();
    void showPage (int index);

    /// Greys the controls the current switches make inert, and refreshes the
    /// notes. Only when something actually changed -- recomputing this thirty
    /// times a second would repaint the whole panel for nothing.
    void updateForSwitches();

    AnvilProcessor& anvil_;

    ui::TooltipHost tooltips_ { *this };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;

    static constexpr int kNumPages = 3;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    std::unique_ptr<ui::LevelMeter> inputMeter_;
    std::unique_ptr<ui::LevelMeter> outputMeter_;
    std::unique_ptr<WorkingMeter>   workingMeter_;

    juce::Label inputMeterLabel_   { {}, "IN" };
    juce::Label outputMeterLabel_  { {}, "OUT" };
    juce::Label workingLabel_      { {}, "WHAT THE AMPLIFIER IS DOING" };

    WrappingLabel statusLabel_;

    /// What the panel is currently dressed for, so the greying and the notes
    /// are not recomputed every tick. Deliberately impossible starting values,
    /// so the first tick always applies the state rather than assuming it.
    int shownVoicing_    { -1 };
    int shownCabinet_    { -1 };
    int shownOversample_ { -1 };
    int shownLatency_    { -1 };
    int shownCore_       { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnvilEditor)
};

} // namespace tezla::anvil
