#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/ModRing.hpp>
#include <tezla/ui/ModStrip.hpp>
#include <tezla/ui/ModulationView.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/SpectrumDisplay.hpp>

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

    /// Greys a control out. Used for the three knobs the Chebyshev generator
    /// replaces: leaving Drive, Colour and Track live but inert would be worse
    /// than hiding them, because a knob that moves and does nothing reads as a
    /// broken plugin rather than as a mode.
    void setControlEnabled (const char* parameterId, bool enabled);

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
    struct Knob
    {
        juce::String id;
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        /// Null for a knob nothing can be pointed at -- there are none on Halo
        /// today, but the destination list excludes switches by construction and
        /// a future control could land here.
        std::unique_ptr<ui::ModRing> ring;
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
    juce::Rectangle<int> noteArea_;
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

    /// Finds the MOD strip the room it needs when it opens, and gives it back
    /// when it closes. The window grows rather than the spectrum shrinking:
    /// squeezing 96 px out of the analyser would leave it too short to read, and
    /// the strip is a panel the user opened on purpose.
    void updateStripSpace();

    /// Greys the controls the current generator does not use, and updates the
    /// note that says what the other one is doing instead.
    void updateForGenerator();

    HaloProcessor& halo_;

    juce::TooltipWindow tooltips_ { this, 500 };

    ui::Palette palette_;
    std::unique_ptr<ui::HeaderBar> header_;
    std::unique_ptr<ui::SpectrumDisplay> spectrum_;
    std::unique_ptr<ui::ModulationView> modulation_;
    std::unique_ptr<ui::ModStrip> modStrip_;

    /// How much height the strip is currently being given, so opening and
    /// closing it is a delta rather than a recomputation of the whole window.
    int stripHeight_ { ui::ModStrip::getCollapsedHeight() };

    static constexpr int kNumPages = 3;
    std::array<std::unique_ptr<ControlPage>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    /// Which generator the panel is currently dressed for. -1 so the first
    /// timer tick always applies the state rather than assuming it, and so the
    /// greying is not recomputed and repainted thirty times a second.
    int shownGenerator_ { -1 };

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
