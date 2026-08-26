#pragma once

// The MOD strip: three LFOs, a level follower, and the arm buttons that turn
// every knob in the plugin into a depth control for one of them.
//
// It is collapsible and starts closed. Halo's window is already a header, a tab
// strip, a control grid, a spectrum and a status line at 860x690; a modulation
// panel that is always open would take a fifth of that from a user who is not
// using modulation. Closed it is a 24 px bar that still shows all four sources
// moving, which is the part worth having at a glance.
//
// Nothing in here knows which plugin it is in. Every control is built from the
// parameter it drives -- the wave names, the note divisions and the ranges all
// come out of the parameters themselves -- so the strip cannot drift from what
// the plugin actually declared.

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ModulationView.hpp"
#include "Palette.hpp"

namespace tezla::ui
{

class ModStrip final : public juce::Component,
                       private juce::ChangeListener
{
public:
    explicit ModStrip (ModulationView& view);
    ~ModStrip() override;

    /// What the strip needs when closed, and when open. The editor asks rather
    /// than hard-coding, so the two cannot disagree.
    [[nodiscard]] static constexpr int getCollapsedHeight() noexcept { return 24; }
    [[nodiscard]] static constexpr int getExpandedHeight() noexcept { return 24 + 96; }

    [[nodiscard]] int getPreferredHeight() const noexcept
    {
        return open_ ? getExpandedHeight() : getCollapsedHeight();
    }

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    void setOpen (bool shouldBeOpen);

    /// Fired when the strip opens or closes, so the editor can find it the room.
    std::function<void()> onHeightChanged;

    /// Re-reads the live source values and repaints. Driven by the editor's
    /// timer alongside the meters and the spectrum.
    void refresh();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /// A slider in the strip: a bar with its name at one end and its value at
    /// the other, both painted by the strip rather than by a text box. At 18 px
    /// tall there is no room for a label above and a value below, and a bar with
    /// no name at all would be a mystery control.
    struct Control
    {
        juce::String name;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    /// One source's block of the panel.
    struct Panel
    {
        int source {};
        juce::TextButton arm;
        std::vector<std::unique_ptr<Control>> controls;
        std::vector<std::unique_ptr<Choice>>  choices;
        std::vector<std::unique_ptr<Toggle>>  toggles;

        /// What goes in each of the six cells, in reading order.
        struct Cell
        {
            enum class Kind { control, choice, toggle, empty } kind { Kind::empty };
            int index {};
        };

        std::vector<Cell> cells;

        /// Drawn across the panel's second row instead of controls. The level
        /// follower uses it for the sentence that stops people looking for a
        /// MIDI input that is never going to be there.
        juce::String caption;

        /// Where the live value bar goes, filled in by resized().
        juce::Rectangle<int> valueArea;
        /// What refresh() last drew there: the graph moves with all three.
        double shownValue { -99.0 };
        double shownPhase { -99.0 };
        int    shownWave  { -1 };
    };

    Control& addControl (Panel&, const char* parameterId, const juce::String& name,
                         const juce::String& tooltip);
    Choice&  addChoice (Panel&, const char* parameterId, const juce::String& tooltip);
    Toggle&  addToggle (Panel&, const char* parameterId, const juce::String& text,
                        const juce::String& tooltip);

    void buildLfoPanel (Panel&, int lfoIndex);
    void buildLevelPanel (Panel&);
    void layoutPanel (Panel&, juce::Rectangle<int> area);
    void paintPanel (juce::Graphics&, const Panel&) const;

    /// What a source is doing, drawn the same way open and closed.
    ///
    /// An LFO gets one cycle of its own waveform with a dot riding it, rather
    /// than a level bar. A level bar is dead until something is assigned --
    /// which is correct, because the sources genuinely do not run until then --
    /// and a control that looks broken while you are setting it up is worse than
    /// one that shows you the shape you just chose.
    void paintSource (juce::Graphics&, juce::Rectangle<float> area, int source) const;

    /// Where the closed row draws its four little graphs, so the summary text
    /// beside them can be given the space that is left rather than overlapping.
    juce::Rectangle<int> closedGraphs_;

    /// Greys the rate or the division, whichever the sync switch is not using.
    /// Both stay visible: a control that vanishes with a switch makes the switch
    /// harder to understand, not easier.
    void updateSyncStates();

    void updateArmButtons();

    [[nodiscard]] juce::String summaryText() const;

    ModulationView& view_;
    Palette palette_;

    juce::TextButton disclosure_;
    juce::Label      hint_;

    static constexpr int kNumPanels = ModulationView::numLfos + 1;
    std::array<Panel, kNumPanels> panels_;

    /// Panel bounds, so paint() can draw their backgrounds without recomputing
    /// the layout.
    std::array<juce::Rectangle<int>, kNumPanels> panelBounds_ {};

    bool open_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModStrip)
};

} // namespace tezla::ui
