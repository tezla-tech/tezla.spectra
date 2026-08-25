#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace tezla::halo
{

/// A meter with honest ballistics: a VU bar for how loud it is and a peak line
/// for whether it is about to clip. The two disagree by 10 dB or more on drums,
/// and the disagreement is the information an engineer is actually reading.
///
/// The harmonics style is a different measurement entirely -- how much harmonic
/// energy is being added relative to the source -- so it gets its own scale and
/// its own colour rather than being squeezed onto a level scale where it would
/// read as nonsense.
class LevelMeter final : public juce::Component
{
public:
    enum class Style { level, harmonics };

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

/// One page of the control surface. Holds its own controls and lays them out on
/// a grid; the editor just decides which page is visible.
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, int columns)
        : state_ (state), columns_ (columns) {}

    void addKnob (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addChoice (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addToggle (const char* parameterId, const juce::String& name, const juce::String& tooltip);
    void addBreak();

    /// A line of guidance shown under the grid, for the things too important to
    /// leave in a tooltip nobody hovers over.
    void setNote (const juce::String& note) { note_ = note; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

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

class HaloEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit HaloEditor (HaloProcessor& processorToUse);
    ~HaloEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPages();
    void showPage (int index);

    HaloProcessor& halo_;

    juce::TooltipWindow tooltips_ { this, 500 };

    static constexpr int kNumPages = 2;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    LevelMeter inputMeter_     { LevelMeter::Style::level };
    LevelMeter outputMeter_    { LevelMeter::Style::level };
    LevelMeter harmonicsMeter_ { LevelMeter::Style::harmonics };

    juce::Label inputMeterLabel_     { {}, "IN" };
    juce::Label outputMeterLabel_    { {}, "OUT" };
    juce::Label harmonicsMeterLabel_ { {}, "HARM" };

    WrappingLabel statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaloEditor)
};

} // namespace tezla::halo
