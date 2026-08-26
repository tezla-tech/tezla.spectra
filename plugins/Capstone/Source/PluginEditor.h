#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::capstone
{

/// A level meter with honest ballistics: a VU bar for how loud it is and a peak
/// line for whether it is about to clip. The two disagree by 10 dB or more on
/// drums, and the disagreement is the information an engineer is reading.
class LevelMeter final : public juce::Component
{
public:
    void setValues (float vuDb, float peakDb) noexcept
    {
        vuDb_ = vuDb;
        peakDb_ = peakDb;
    }

    void paint (juce::Graphics&) override;

private:
    [[nodiscard]] float positionFor (float db) const noexcept;

    float vuDb_   { -100.0f };
    float peakDb_ { -100.0f };
};

/// The meter a limiter is actually watched on: how much is being taken away,
/// growing leftwards from the right-hand edge.
///
/// Two bars rather than one. The clipper and the limiter remove level in
/// completely different ways -- one cuts the waveform, the other moves the gain
/// -- and a single figure that added them would hide which was doing the work,
/// which is the one thing this display exists to show.
class ReductionMeter final : public juce::Component
{
public:
    explicit ReductionMeter (ui::Palette palette) : palette_ (palette) {}

    void setValues (float limiterDb, float clipDb) noexcept
    {
        // Held with a slow fall, so a transient stays readable long enough to
        // see. An instantaneous reading on a limiter is a flicker.
        limiterDb_ = std::min (limiterDb, limiterDb_ * 0.82f);
        clipDb_    = std::min (clipDb,    clipDb_ * 0.82f);
    }

    void paint (juce::Graphics&) override;

private:
    ui::Palette palette_;
    float limiterDb_ { 0.0f };
    float clipDb_    { 0.0f };
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
    void addToggle (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addGap();

    /// A line of guidance under the grid, for the things too important to leave
    /// in a tooltip nobody hovers over.
    void setNote (const juce::String& note);

    /// Greys a control out. Used for the controls a switch makes inert: a knob
    /// that moves and does nothing reads as a broken plugin rather than as a
    /// mode.
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

    struct Toggle
    {
        juce::String       id;
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Cell
    {
        enum class Kind { knob, choice, toggle, gap } kind {};
        int index {};
    };

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    int columns_;

    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<std::unique_ptr<Toggle>> toggles_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

class CapstoneEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit CapstoneEditor (CapstoneProcessor& processorToUse);
    ~CapstoneEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPages();
    void showPage (int index);

    /// Greys the controls the current switches make inert, and refreshes the
    /// status line. Only when something actually changed -- recomputing this
    /// thirty times a second would repaint the whole panel for nothing.
    void updateForSwitches();

    CapstoneProcessor& capstone_;

    juce::TooltipWindow tooltips_ { this, 500 };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;

    static constexpr int kNumPages = 3;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    LevelMeter inputMeter_;
    LevelMeter outputMeter_;
    std::unique_ptr<ReductionMeter> reductionMeter_;

    juce::Label inputMeterLabel_  { {}, "IN" };
    juce::Label outputMeterLabel_ { {}, "OUT" };
    juce::Label reductionLabel_   { {}, "GAIN REDUCTION" };

    WrappingLabel statusLabel_;

    /// What the panel is currently dressed for, so the greying is not
    /// recomputed every tick. Deliberately impossible starting values, so the
    /// first tick always applies the state rather than assuming it.
    int shownLimitOn_    { -1 };
    int shownClipOn_     { -1 };
    int shownLookahead_  { -1 };
    int shownLatency_    { -1 };
    int shownTruePeak_   { -1 };
    int shownOversample_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapstoneEditor)
};

} // namespace tezla::capstone
