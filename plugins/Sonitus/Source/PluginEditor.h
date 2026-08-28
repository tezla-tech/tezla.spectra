#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LevelMeter.hpp>
#include <tezla/ui/Palette.hpp>

#include "PluginProcessor.h"

namespace tezla::sonitus
{

/// Brushed metal, painted once and kept.
///
/// A vertical gradient, a specular band across the upper third, and fine
/// horizontal striations -- which is what brushed aluminium actually looks
/// like, and none of it is expensive *once*. Doing it live would be a few
/// thousand `drawLine` calls per repaint at thirty frames a second, so the
/// whole thing goes into an image and every paint is one blit. The image is
/// rebuilt only when the size changes.
///
/// The striations are hashed from their own row index rather than drawn from a
/// running generator, for the same reason `Lfo`'s sample-and-hold is: the
/// texture is then a property of the panel rather than of how many times it has
/// been resized, so it does not shimmer when the window is dragged.
class MetalBackground
{
public:
    /// `highlightAt` is where the specular band falls, as a fraction of the
    /// height.
    void paint (juce::Graphics& g, juce::Rectangle<int> bounds, float highlightAt = 0.30f);

private:
    void render (int width, int height, float highlightAt);

    /// splitmix64's finaliser, to [-1, 1]. See the definition.
    [[nodiscard]] static double hashed (std::uint64_t index);

    juce::Image image_;
    float highlight_ { -1.0f };
};

/// Wraps, so the "what this is doing right now" sentence is never truncated.
class WrappingLabel final : public juce::Label
{
public:
    void paint (juce::Graphics& g) override;
};

// ---------------------------------------------------------------------------
// Cells
// ---------------------------------------------------------------------------

/// One control and its name, as a component rather than as two components a
/// layout has to keep in step.
///
/// The earlier version laid every label and every widget out by hand inside the
/// page's grid loop, which is why the page could only ever be a grid: anything
/// that wanted a knob somewhere else had to reimplement the whole cell. Making
/// the cell own its own layout is what lets the envelope page put six of them
/// beside a graph without a line of duplicated code.
class ParameterCell : public juce::Component
{
public:
    ParameterCell (juce::String parameterId, const juce::String& name, ui::Palette palette);

    [[nodiscard]] const juce::String& parameterId() const noexcept { return id_; }

    virtual void setControlEnabled (bool enabled) = 0;

    void resized() override;

protected:
    /// Where the control goes: everything under the name.
    [[nodiscard]] juce::Rectangle<int> controlBounds() const;

    juce::String id_;
    ui::Palette  palette_;
    juce::Label  label_;
};

class KnobCell final : public ParameterCell
{
public:
    KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
              const juce::String& name, const juce::String& tooltip, ui::Palette palette);

    void setControlEnabled (bool enabled) override;
    void resized() override;

private:
    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

class ChoiceCell final : public ParameterCell
{
public:
    ChoiceCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                const juce::String& name, const juce::String& tooltip, ui::Palette palette);

    void setControlEnabled (bool enabled) override;
    void resized() override;

private:
    juce::ComboBox box_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;
};

class ToggleCell final : public ParameterCell
{
public:
    ToggleCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                const juce::String& name, const juce::String& tooltip, ui::Palette palette);

    void setControlEnabled (bool enabled) override;
    void resized() override;

private:
    juce::ToggleButton button_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment_;
};

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

/// Anything a tab can show. The viewport hosts all of them, including the
/// tuning panel -- which it did not before, and the special case that carved
/// out was where the blank black page came from.
class Page : public juce::Component
{
public:
    /// How tall the page wants to be, so one taller than its window scrolls
    /// rather than squashing its controls into unreadable stubs.
    [[nodiscard]] virtual int getPreferredHeight() const = 0;
};

/// A page of controls, arranged in **groups**. Each group has its own heading,
/// its own column count and its own panel behind it.
///
/// Per-group columns rather than one count for the page, because the groups
/// genuinely differ: an oscillator is ten controls that want five across, and a
/// modulation slot is three that want six so two slots share a row. Forcing
/// both onto one grid leaves a ragged edge on every page.
class ControlPage final : public Page
{
public:
    ControlPage (juce::AudioProcessorValueTreeState& state, ui::Palette palette)
        : state_ (state), palette_ (palette) {}

    /// Starts a group. Everything added after this lands in it.
    ///
    /// A `--` in the text splits the name from its explanation, and the two are
    /// drawn differently: the name is the thing being looked for, and setting
    /// the whole line in one weight makes a page of six headings read as six
    /// sentences rather than as six labels.
    /// `sameRow` puts this group **beside** the previous one rather than under
    /// it, sharing the band in proportion to their column counts. Three groups
    /// of two or three controls each stacked vertically is three headings and a
    /// page of air; side by side they are one band.
    void addHeading (const juce::String& text, int columns, bool sameRow = false);

    void addKnob (const juce::String& parameterId, const juce::String& name,
                  const juce::String& tooltip);
    void addChoice (const juce::String& parameterId, const juce::String& name,
                    const juce::String& tooltip);
    void addToggle (const juce::String& parameterId, const juce::String& name,
                    const juce::String& tooltip);

    /// Leaves a hole, so the cell after it starts where it should.
    void addGap();

    /// Greys a control out. Used for the ones a switch makes inert: a knob that
    /// moves and does nothing reads as a broken plugin rather than as a mode.
    void setControlEnabled (const juce::String& parameterId, bool enabled);

    [[nodiscard]] int getPreferredHeight() const override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Group
    {
        juce::String heading;
        juce::String detail;
        int columns { 5 };

        /// Shares its band with the group before it.
        bool sameRow { false };

        /// Null for a gap, so the grid can leave a hole without a placeholder
        /// component to own.
        std::vector<ParameterCell*> cells;

        juce::Rectangle<int> bounds;   ///< filled in by `resized`
    };

    [[nodiscard]] Group& currentGroup();
    [[nodiscard]] int rowsIn (const Group& group) const;
    [[nodiscard]] int totalRows() const;

    /// How many bands the page has -- a band being one group, or several
    /// sharing a row.
    [[nodiscard]] int bandCount() const;

    void add (std::unique_ptr<ParameterCell> cell);

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;

    std::vector<std::unique_ptr<ParameterCell>> owned_;
    std::vector<Group> groups_;

    /// How far down the last group reaches, so the panel behind the groups is
    /// painted over the content rather than over the whole page.
    int contentHeight_ { 0 };
};

/// An ADSR drawn as the shape it is, and dragged by its corners.
///
/// Five knobs describe an envelope completely and show it not at all. The curve
/// is the thing being edited -- how fast it opens, how far it falls, how long it
/// hangs on -- and none of that is legible as five numbers. So: a graph with
/// three handles, and the knobs kept underneath for the precision the graph
/// cannot give.
///
/// **The curve drawn is the curve that plays.** The segments are the same
/// exponentials `dsp::Adsr` runs, with the same overshoot mapping read from the
/// same constants, so the shape control bends the picture exactly as far as it
/// bends the sound. A graph drawn from straight lines would be a decoration.
///
/// **The horizontal axis is the knobs' own travel**, not seconds. Each segment
/// gets a fixed slice of the width and fills the fraction of it that its
/// parameter is along its range -- so a handle is exactly where the knob is,
/// dragging is the knob's own skew rather than a second scale to learn, and a
/// 5 ms attack beside a 5 s release is still visible. A linear time axis would
/// put every useful attack in the first three pixels.
class EnvelopeEditor final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    /// The eight parameters one envelope is made of.
    struct Ids
    {
        juce::String attack, hold, decay, sustain, release;
        juce::String attackTension, decayTension, releaseTension;
    };

    EnvelopeEditor (juce::AudioProcessorValueTreeState& state, ui::Palette palette, Ids ids);

    /// Re-reads the parameters and repaints if any of them moved. Driven by the
    /// editor's timer rather than one of its own -- the panel already ticks at
    /// 30 Hz, and a second timer per envelope is three more wakeups a frame for
    /// nothing.
    void refresh (double level);

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    /// Which corner is being dragged. The middle handle carries two parameters
    /// at once -- across is the decay time, up and down is the sustain level --
    /// which is what makes the graph quicker than the knobs rather than merely
    /// prettier.
    enum class Handle { none, attack, hold, decaySustain, release };

    struct Geometry
    {
        juce::Rectangle<float> plot;

        /// The five boundaries, left to right: the end of the attack, the end
        /// of the hold, the end of the decay, the end of the sustain's drawn
        /// stretch, and the end of the release.
        float attackX {}, holdX {}, decayX {}, sustainEndX {}, releaseX {};
        float sustainY {};
    };

    [[nodiscard]] Geometry geometry() const;
    [[nodiscard]] Handle handleAt (juce::Point<float> position) const;
    [[nodiscard]] juce::Point<float> handlePosition (Handle handle, const Geometry& g) const;

    [[nodiscard]] float normalised (const juce::String& id) const;
    [[nodiscard]] float plain (const juce::String& id) const;
    void setNormalised (const juce::String& id, float value, bool gesture);

    /// The level an `Adsr` segment is at, a fraction `u` of the way through it,
    /// travelling from `from` to `to` at `tension`. The library's arithmetic,
    /// not an approximation of it -- including the mirror for a negative
    /// tension, which is the half of the control a curve drawn by eye would
    /// get wrong.
    [[nodiscard]] static double segment (double u, double from, double to, double tension);

    void appendSegment (juce::Path& path, float x0, float x1,
                        double from, double to, double tension) const;

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;

    Ids ids_;

    Handle dragging_ { Handle::none };
    Handle hovered_ { Handle::none };

    /// What the graph was last drawn from, so a repaint only happens when
    /// something moved. Deliberately impossible starting values.
    float shown_[8] { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    float shownLevel_ { -1.0f };

    float level_ { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditor)
};

/// The three envelopes: a graph each, with its knobs beside it.
class EnvelopePage final : public Page
{
public:
    EnvelopePage (juce::AudioProcessorValueTreeState& state, ui::Palette palette);

    /// Pushes the live levels onto the three graphs.
    void refresh (const SonitusProcessor& processor);

    [[nodiscard]] int getPreferredHeight() const override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Block
    {
        juce::String heading;
        juce::String detail;
        std::unique_ptr<EnvelopeEditor> graph;
        std::vector<std::unique_ptr<ParameterCell>> knobs;
        juce::Rectangle<int> bounds;
    };

    void addBlock (juce::AudioProcessorValueTreeState& state, const juce::String& heading,
                   const juce::String& detail, const EnvelopeEditor::Ids& ids,
                   const char* extraId, const juce::String& extraName,
                   const juce::String& extraTooltip, const char* snapId = nullptr);

    ui::Palette palette_;
    std::vector<Block> blocks_;
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
class TuningPage final : public Page
{
public:
    TuningPage (SonitusProcessor& processorToUse, ui::Palette palette);

    /// Refreshes the description from the processor. Called when something has
    /// changed the tuning, including a state load from the host.
    void refresh();

    [[nodiscard]] int getPreferredHeight() const override { return 300; }

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
    ~SonitusEditor() override;

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

    ui::Palette palette_;

    /// Set on this editor before any child exists and cleared in the
    /// destructor, so nothing outlives it. A look and feel destroyed while a
    /// component still points at it is a use-after-free with no symptom until
    /// the host repaints.
    ui::KnobLookAndFeel lookAndFeel_;

    /// One per page, each carrying that page's accent.
    ///
    /// JUCE resolves a look and feel by walking *up* the component tree, so
    /// setting one on a page is enough to colour every knob, box and switch
    /// inside it -- there is nothing to pass down by hand and nothing to forget
    /// to pass. They are declared after the editor's own so they are destroyed
    /// before it, and cleared from their pages in the destructor either way.
    std::array<std::unique_ptr<ui::KnobLookAndFeel>, 6> pageLookAndFeels_;

    /// Held by pointer so it can be *destroyed*, which is the only reliable way
    /// to turn tooltips off: JUCE has no "disabled" state for one, and setting
    /// the delay enormous still shows a tip to anybody who rests on a control.
    ui::TooltipHost tooltips_ { *this };


    /// The brushed metal behind everything, cached at the window's size.
    MetalBackground metal_;

    std::unique_ptr<ui::HeaderBar> header_;

    static constexpr int kNumPages = 6;

    /// The MOD page is the one with the step strip under its grid, and the ENV
    /// page is the bespoke one, so both are named rather than numbered where
    /// the layout needs them.
    static constexpr int kOscPage    = 0;
    static constexpr int kFilterPage = 1;
    static constexpr int kEnvPage    = 2;
    static constexpr int kModPage    = 3;
    static constexpr int kManglePage = 4;
    static constexpr int kTuningPage = 5;

    std::array<std::unique_ptr<Page>, kNumPages> pages_;
    std::array<juce::TextButton, kNumPages> tabs_;
    int currentPage_ { 0 };

    /// Pages scroll rather than squash. A knob compressed to nothing is
    /// unusable in a way a scroll bar is not, so the grid keeps its row height
    /// and the viewport takes the difference.
    juce::Viewport viewport_;

    std::unique_ptr<StepStrip> steps_;

    /// The guidance line for each page, shown in a fixed strip under the
    /// viewport. On the page it lived below the fold exactly where it was
    /// wanted -- the two pages long enough to scroll are the two whose notes
    /// carry the live readings.
    std::array<juce::String, kNumPages> notes_;
    WrappingLabel noteLabel_;

    std::unique_ptr<ui::LevelMeter> outputMeter_;
    juce::Label outputMeterLabel_ { {}, "OUT" };

    WrappingLabel statusLabel_;

    /// The `ControlPage`s, by index, for the greying. Null for the pages that
    /// are not one, which is how a lookup that would have been a bad cast
    /// becomes a null check.
    [[nodiscard]] ControlPage* controlPage (int index) const;

    /// What the panel is currently dressed for, so the greying and the notes
    /// are not recomputed every tick. Deliberately impossible starting values,
    /// so the first tick always applies the state rather than assuming it.
    int shownCombMode_   { -1 };
    int shownKeyMode_    { -1 };
    int shownOversample_ { -1 };
    int shownLatency_    { -1 };
    int shownSyncB_      { -1 };
    int shownLfoSync_    { -1 };
    int shownShapeA_     { -1 };
    int shownShapeB_     { -1 };
    int shownNotch_      { -1 };
    juce::String shownScale_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SonitusEditor)
};

} // namespace tezla::sonitus
