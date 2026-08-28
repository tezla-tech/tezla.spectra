#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/LevelMeter.hpp>
#include <tezla/ui/ModRing.hpp>
#include <tezla/ui/ModStrip.hpp>
#include <tezla/ui/ModulationView.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::emberdrive
{

/// A meter with honest ballistics: a VU bar for how loud it is, a peak line for
/// whether it is about to clip, and a separate gain-reduction bar. Those two
/// readings disagree by 10 dB or more on drums, and the disagreement is the
/// information an engineer is actually reading.
class ReductionMeter final : public juce::Component
{
public:
    void setValue (float reductionDb) noexcept { reductionDb_ = reductionDb; }

    void paint (juce::Graphics&) override;

private:
    [[nodiscard]] static float positionFor (float db) noexcept;

    float reductionDb_ { 0.0f };
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
    juce::String id;
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    /// Null for a knob nothing can be pointed at -- Bypass and the mode
    /// switches, which reconfigure rather than adjust.
    std::unique_ptr<ui::ModRing> ring;
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

    /// Gives every knob that is a modulation destination a ring around it.
    ///
    /// Called once, after the page is built: the rings are added last so they
    /// sit above the knobs they overlay, and they stay out of the way until a
    /// source is armed.
    void attachModulation (ui::ModulationView& view);

    /// Re-reads what modulation is doing and repaints the rings that moved.
    void refreshModulation();

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

    /// Finds the MOD strip the room it needs when it opens, and gives it back
    /// when it closes. The window grows rather than the pages shrinking: the
    /// EXPERT page already fills its grid, and taking 96 px out of it would
    /// crop the controls rather than tighten them.
    void updateStripSpace();

    EmberdriveProcessor& processor_;

    ui::TooltipHost tooltips_ { *this };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;
    std::unique_ptr<ui::ModulationView> modulation_;
    std::unique_ptr<ui::ModStrip> modStrip_;

    /// How much height the strip is currently being given, so opening and
    /// closing it is a delta rather than a recomputation of the whole window.
    int stripHeight_ { ui::ModStrip::getCollapsedHeight() };

    static constexpr int kNumPages = 4;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    std::unique_ptr<ui::LevelMeter> inputMeter_;
    std::unique_ptr<ui::LevelMeter> outputMeter_;

    /// What Ceiling the output meter is referenced to, so it is not pushed
    /// thirty times a second for no reason.
    float shownCeilingDb_ { 1000.0f };
    ReductionMeter reductionMeter_;

    juce::Label inputMeterLabel_     { {}, "IN" };
    juce::Label outputMeterLabel_    { {}, "OUT" };
    juce::Label reductionMeterLabel_ { {}, "GR" };

    WrappingLabel statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberdriveEditor)
};

} // namespace tezla::emberdrive
