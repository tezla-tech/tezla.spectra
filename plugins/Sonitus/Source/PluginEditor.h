#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/LevelMeter.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::sonitus
{

/// Wraps, so the "what this is doing right now" sentence is never truncated.
class WrappingLabel final : public juce::Label
{
public:
    void paint (juce::Graphics& g) override;
};

/// One page of the control surface. Holds its own controls and lays them out on
/// a grid; the editor decides which page is visible.
///
/// The same class as Anvil's, with two additions this instrument needs: a
/// toggle cell (there are five on/off controls here against Anvil's none), and
/// a heading cell, because a synth's pages carry three or four groups each and
/// an undivided grid of forty knobs is unreadable.
class ControlPage final : public juce::Component
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette, int columns)
        : state_ (state), palette_ (palette), columns_ (columns) {}

    void addKnob (const juce::String& parameterId, const juce::String& name,
                  const juce::String& tooltip);
    void addChoice (const juce::String& parameterId, const juce::String& name,
                    const juce::String& tooltip);
    void addToggle (const juce::String& parameterId, const juce::String& name,
                    const juce::String& tooltip);

    /// A row-spanning heading. Takes the rest of the current row with it, so a
    /// group always starts at the left edge.
    void addHeading (const juce::String& text);

    void addGap();

    /// A line of guidance under the grid, for the things too important to leave
    /// in a tooltip nobody hovers over.
    void setNote (const juce::String& note);

    /// Greys a control out. Used for the controls a switch makes inert: a knob
    /// that moves and does nothing reads as a broken plugin rather than a mode.
    void setControlEnabled (const juce::String& parameterId, bool enabled);

    /// How tall the grid wants to be, so a page taller than its window scrolls
    /// rather than squashing its knobs into unreadable stubs.
    [[nodiscard]] int getPreferredHeight() const;

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
        juce::Label        label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Heading
    {
        juce::Label label;
    };

    struct Cell
    {
        enum class Kind { knob, choice, toggle, heading, gap } kind {};
        int index {};
    };

    /// Where each cell lands, in grid coordinates. Headings take a whole row,
    /// so a cell's position is not its index divided by the column count and
    /// has to be worked out by walking the list.
    [[nodiscard]] int rowCount() const;

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;
    int columns_;

    std::vector<std::unique_ptr<Knob>>    knobs_;
    std::vector<std::unique_ptr<Choice>>  choices_;
    std::vector<std::unique_ptr<Toggle>>  toggles_;
    std::vector<std::unique_ptr<Heading>> headings_;
    std::vector<Cell> cells_;

    juce::String note_;
    juce::Rectangle<int> noteArea_;
};

/// The sixteen steps, as sixteen vertical faders with the playing one lit.
///
/// A step sequencer drawn as sixteen knobs is unusable -- the *shape* of the
/// pattern is the thing being edited, and a row of faders shows it at a glance
/// where a row of rotaries does not. The playing step is lit because with the
/// rate down at a quarter of a step per beat, "stopped" and "slow" look
/// identical for four seconds at a time.
class StepStrip final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    StepStrip (juce::AudioProcessorValueTreeState& state, ui::Palette palette);

    /// Which step is sounding, and how many of them are in the pattern.
    void setPlaying (int step, int length);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ui::Palette palette_;

    std::array<juce::Slider, dsp::StepSequencer::kMaxSteps> sliders_;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               dsp::StepSequencer::kMaxSteps> attachments_;

    int playing_ { -1 };
    int length_ { dsp::StepSequencer::kMaxSteps };
};

/// The tuning page: pick a built-in scale, or load a Scala file.
///
/// Its own component rather than a `ControlPage` because none of it is a
/// parameter -- a scale is text, and the two file buttons are the only things
/// in this plugin that touch a filesystem.
class TuningPage final : public juce::Component
{
public:
    TuningPage (SonitusProcessor& processorToUse, ui::Palette palette);

    /// Refreshes the description from the processor. Called when something has
    /// changed the tuning, including a state load from the host.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void loadScaleFile();
    void loadKeyboardMapFile();

    /// Says what went wrong, with the line number the parser gave. A tuning
    /// that half-loads is worse than one that will not load -- the `.tzref`
    /// lesson, from Transpectus.
    void reportFailure (const juce::String& what, const juce::String& reason);

    SonitusProcessor& sonitus_;
    ui::Palette palette_;

    juce::ComboBox   scaleBox_;
    juce::TextButton loadScaleButton_ { "Load .scl..." };
    juce::TextButton loadMapButton_   { "Load .kbm..." };
    juce::TextButton resetButton_     { "12-TET" };

    juce::Label      headingLabel_;
    WrappingLabel    descriptionLabel_;
    WrappingLabel    explanationLabel_;
    WrappingLabel    errorLabel_;

    std::unique_ptr<juce::FileChooser> chooser_;

    /// Guards the combo box's callback while the box is being repopulated or
    /// set from the processor, so restoring a selection does not read as the
    /// user choosing it.
    bool updating_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuningPage)
};

class SonitusEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit SonitusEditor (SonitusProcessor& processorToUse);
    ~SonitusEditor() override = default;

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

    SonitusProcessor& sonitus_;

    juce::TooltipWindow tooltips_ { this, 500 };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;

    static constexpr int kNumPages = 6;

    /// The MOD page is the one with the step strip under its grid, so it is
    /// named rather than numbered where the layout needs it.
    static constexpr int kModPage = 3;
    static constexpr int kTuningPage = 5;

    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    std::unique_ptr<StepStrip>  steps_;
    std::unique_ptr<TuningPage> tuning_;

    std::unique_ptr<ui::LevelMeter> outputMeter_;
    juce::Label outputMeterLabel_ { {}, "OUT" };

    WrappingLabel statusLabel_;

    /// What the panel is currently dressed for, so the greying and the notes
    /// are not recomputed every tick. Deliberately impossible starting values,
    /// so the first tick always applies the state rather than assuming it.
    int shownCombMode_   { -1 };
    int shownKeyMode_    { -1 };
    int shownOversample_ { -1 };
    int shownLatency_    { -1 };
    int shownSyncB_      { -1 };
    int shownShapeA_     { -1 };
    int shownShapeB_     { -1 };
    int shownNotch_      { -1 };
    juce::String shownScale_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SonitusEditor)
};

} // namespace tezla::sonitus
