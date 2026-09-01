// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/TooltipHost.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/LevelMeter.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/TuningPanel.hpp>

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

    /// The group's colour, for a design that gives each group its own.
    ///
    /// The base tints the *name*, which every cell has; an override adds
    /// whatever else that cell can colour and calls this first.
    virtual void setTint (juce::Colour tint);

    void resized() override;

protected:
    /// Where the control goes: everything under the name.
    [[nodiscard]] juce::Rectangle<int> controlBounds() const;

    /// The name's colour, group tint mixed in. Held as a method rather than a
    /// value because the enabled/disabled paths both need it and neither
    /// should have to remember the mix.
    [[nodiscard]] juce::Colour labelColour() const;

    /// The height the value row wants, which a design may grow.
    [[nodiscard]] static int valueHeight();

    juce::String id_;
    ui::Palette  palette_;
    juce::Label  label_;
    juce::Colour tint_ { palette_.accent };
};

class KnobCell final : public ParameterCell
{
public:
    KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
              const juce::String& name, const juce::String& tooltip, ui::Palette palette);

    void setControlEnabled (bool enabled) override;
    void setTint (juce::Colour tint) override;
    void resized() override;

private:
    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

/// A one-cycle picture of the oscillator's waveform, live.
///
/// Drawn from `Oscillator::naiveShapeSample` -- the same function the DSP's
/// uncorrected path uses -- so the picture and the sound cannot drift apart.
/// Listens to the shape, width and morph parameters and repaints on change;
/// no timer, no attachment.
class WaveCell final : public ParameterCell,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    WaveCell (juce::AudioProcessorValueTreeState& state, const juce::String& shapeId,
              const juce::String& widthId, const juce::String& morphId,
              ui::Palette palette);
    ~WaveCell() override;

    void setControlEnabled (bool) override {}
    void paint (juce::Graphics&) override;

private:
    void parameterChanged (const juce::String&, float) override;

    juce::AudioProcessorValueTreeState& state_;
    juce::String shapeId_, widthId_, morphId_;
};

/// A small horizontal slider, for the shape's Morph -- the "small sliders by
/// the control" of the brief. No text box; the value lives in the drag popup
/// and the tooltip.
class MorphCell final : public ParameterCell
{
public:
    MorphCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
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
    void setTint (juce::Colour tint) override;
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
    /// Held by base pointer because the attachment does not care which button
    /// is under it, and because the cell's own code never needs the switch's
    /// interface -- only `Button`'s.
    std::unique_ptr<juce::Button> button_;
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
/// The DICEROLL page: one button, and it is not a subtle one.
///
/// Rainbow rather than a page accent, and that is the point rather than a
/// decoration: every other tab on this panel is one hue because it does one
/// coherent thing. This one does all of them at once, so it gets all of them
/// at once, and it is the only control on the panel that can throw away work.
/// Being unmissable is a feature.
class DicePage final : public Page,
                       private juce::Timer
{
public:
    /// The button paints itself, because a `TextButton` covers whatever is
    /// behind it -- the first draft drew the spectrum onto the page and the
    /// button sat on top of it as a grey plate, with only the halo showing.
    class RainbowButton final : public juce::Button
    {
    public:
        RainbowButton() : juce::Button ("R A N D O M I Z E") {}

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    /// One section's row: a LOCK toggle and a SOLO button.
    ///
    /// SOLO is a **button of its own** rather than a modifier on the lock, and
    /// deliberately: a modifier is a thing you have to know about, and the one
    /// gesture worth optimising here -- "roll only the filter" -- should not
    /// need a manual. It locks everything else; pressing it again on the
    /// section that is already alone clears the locks, so it is its own way
    /// back out.
    struct SectionRow
    {
        DiceSection section { DiceSection::osc };
        std::unique_ptr<juce::TextButton> lock;
        std::unique_ptr<juce::TextButton> solo;
        juce::Rectangle<int> bounds;
    };

    DicePage (SonitusProcessor& processorToUse, ui::Palette palette);
    ~DicePage() override;

    [[nodiscard]] int getPreferredHeight() const override { return 470; }

    void paint (juce::Graphics&) override;
    void resized() override;

    /// The rolling hue, so the tab can glow in step with the page.
    [[nodiscard]] static float hueNow();

private:
    void timerCallback() override;

    /// Repaints the lock and solo faces from the processor's mask, and greys
    /// PREV/NEXT at the ends of the ring. One function rather than each button
    /// tracking its own idea of the state -- SOLO changes six other buttons.
    void refreshControls();

    void addSectionRow (DiceSection section);

    SonitusProcessor& processor_;
    ui::Palette palette_;

    RainbowButton roll_;
    juce::Label caption_;
    juce::Label count_;

    std::array<SectionRow, numDiceSections> sections_;

    /// HISTORY / STRENGTH / WHAT ROLLS, laid out by resized() and drawn by
    /// paint() -- headings are not components here, they are three strings.
    std::array<juce::Rectangle<int>, 3> headings_;

    juce::TextButton previous_ { "< PREV" };
    juce::TextButton next_ { "NEXT >" };
    juce::Label history_;

    juce::Slider amount_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider spread_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label amountLabel_;
    juce::Label spreadLabel_;

    int rolls_ { 0 };

    /// The lock mask the buttons are currently drawn from, so the tick can
    /// tell "nothing changed" from "a project load moved it" without
    /// repainting seven buttons thirty times a second.
    unsigned int shownLocks_ { 0 };
};

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
    void addWave (const juce::String& shapeId, const juce::String& widthId,
                  const juce::String& morphId);
    void addMorph (const juce::String& parameterId, const juce::String& name,
                   const juce::String& tooltip);

    /// Leaves a hole, so the cell after it starts where it should.
    void addGap();

    /// Replaces one group's explanation text, found by its heading.
    ///
    /// For a readout that has to be live -- the FM ratio the two oscillators
    /// are actually running -- without giving the page a component whose only
    /// job is to hold a string. Repaints just that heading, and only when the
    /// text has actually changed: this is called thirty times a second and the
    /// answer changes when a knob moves.
    void setGroupDetail (const juce::String& headingName, const juce::String& detail);

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

        /// This group's own colour, rotated off the page accent. Held rather
        /// than recomputed, because the cells are tinted once at build time and
        /// the plate is painted thirty times a second.
        juce::Colour tint;

        juce::Rectangle<int> bounds;   ///< filled in by `resized`
    };

    [[nodiscard]] Group& currentGroup();
    /// How many columns a group actually gets, which is **not** the number its
    /// call site asked for: a group may be widened until its row is full, down
    /// to `design::kCellWidthMin`. See PanelDesign.hpp.
    [[nodiscard]] int columnsFor (const Group& group, int width) const;

    [[nodiscard]] int rowsIn (const Group& group, int width) const;
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

/// A multi-stage envelope drawn as the polyline it is, dragged by its points.
///
/// The ADV envelopes' graph. Points drag in both axes -- x is that segment's
/// time, y its level -- and a segment's *midpoint* drags vertically for its
/// tension, which is the FL gesture and needs no third handle. Curves are
/// drawn with EnvelopeEditor::segment, the shared tension arithmetic, so the
/// picture is the DSP's own curve. The sustain point wears a ring; the loop
/// region is shaded while Loop is on.
class MultiEnvelopeEditor final : public juce::Component
{
public:
    MultiEnvelopeEditor (juce::AudioProcessorValueTreeState& state, int envelopeIndex,
                         ui::Palette palette);

    /// The tempo the ruler is drawn against, pushed in by the page each tick.
    void setTempo (double bpm, int beatsPerBar) noexcept;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Layout
    {
        /// kMaxPoints + 1: x[0] is the gate's own start, x[i] is point i-1.
        /// Sized by the constant rather than typed, because it moved from 8 to
        /// 16 and a hard 9 here would have been an out-of-bounds write with
        /// nothing to notice it -- exactly the shape of the DICEROLL crash.
        static constexpr std::size_t kSlots =
            static_cast<std::size_t> (dsp::MultiEnvelope::kMaxPoints) + 1;

        double total { 1.0 };
        std::array<float, kSlots> x {};
        std::array<float, kSlots> y {};

        /// Each leg's duration **as it will be played** -- snapped when Snap is
        /// on. The graph drew the raw parameters before, which meant a synced
        /// envelope showed a shape the engine was not running; the ruler made
        /// that visible the first time it was drawn.
        std::array<double, kSlots> seconds {};

        /// Which note length each leg snapped to, or -1 for one that passed
        /// through. Straight from the DSP's own chooser, so the label and the
        /// sound cannot disagree.
        std::array<int, kSlots> division {};

        int points { 2 };
        int sustain { 0 };
        int loopStart { 0 };
        bool loop { false };
        bool snap { false };
    };

    /// One tier of the musical grid: how far apart its lines are and how
    /// loudly to draw them.
    struct GridTier
    {
        double seconds { 0.0 };
        float alpha { 0.0f };
        float thickness { 1.0f };
    };

    [[nodiscard]] Layout layoutNow() const;
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    [[nodiscard]] juce::Rectangle<float> rulerArea() const;

    void paintMusicalRuler (juce::Graphics&, const Layout&);
    void paintSecondsRuler (juce::Graphics&, const Layout&);
    void paintLegLabels (juce::Graphics&, const Layout&);
    void paintLengthReadout (juce::Graphics&, const Layout&);

    /// "1 bar", "3 beats", "1.4 beats", or a time in seconds when there is no
    /// grid to measure against.
    [[nodiscard]] juce::String musicalLength (double seconds) const;

    [[nodiscard]] float plain (const juce::String& field) const;
    void setPlain (const juce::String& field, float value, bool gesture);

    juce::AudioProcessorValueTreeState& state_;
    int envelope_ { 0 };
    ui::Palette palette_;

    double bpm_ { 120.0 };
    int beatsPerBar_ { 4 };

    int dragPoint_ { -1 };
    int dragSegment_ { -1 };
    float dragStartTension_ { 0.0f };
    float dragStartY_ { 0.0f };
    juce::String gestureField_;
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
    /// One tension segment's level at progress u, the DSP's own arithmetic.
    /// Public because MultiEnvelopeEditor draws with the same curve.
    [[nodiscard]] static double segment (double u, double from, double to, double tension);

    /// The eight parameters one envelope is made of.
    struct Ids
    {
        juce::String attack, hold, decay, sustain, release;
        juce::String attackTension, decayTension, releaseTension;
    };

    EnvelopeEditor (juce::AudioProcessorValueTreeState& state, ui::Palette palette, Ids ids);

    /// The Snap toggle this envelope answers to, and the tempo it is snapping
    /// against. Both optional: an envelope with no Snap parameter draws its
    /// plain stage letters as it always did.
    ///
    /// **Why the axis does not become a ruler here.** This graph's horizontal
    /// axis is the knobs' own travel, not seconds (see the class comment), so
    /// a grid drawn on it would be measuring the wrong thing. What Snap
    /// actually does is turn each stage's *duration* into a note length -- so
    /// the honest indication is to name the note under the stage, which is
    /// what the axis marks do when this is set. The ADV graph, whose axis
    /// really is time, gets the ruler.
    void setSnapSource (juce::String snapId) { snapId_ = std::move (snapId); }
    void setTempo (double bpm, int beatsPerBar) noexcept;

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
    void appendSegment (juce::Path& path, float x0, float x1,
                        double from, double to, double tension) const;

    /// "A" on its own, or "A 1/16" when Snap is on and the stage is long
    /// enough to have snapped to something.
    [[nodiscard]] juce::String stageLabel (const char* letter, const juce::String& timeId) const;

    juce::AudioProcessorValueTreeState& state_;
    ui::Palette palette_;

    Ids ids_;
    juce::String snapId_;
    double bpm_ { 120.0 };
    int beatsPerBar_ { 4 };

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

    /// One ADV envelope's row: a compact strip until enabled, a full graph
    /// block after. The page re-lays itself out when an enable flips.
    struct AdvRow
    {
        int index { 0 };
        juce::String heading;
        std::unique_ptr<MultiEnvelopeEditor> graph;
        std::vector<std::unique_ptr<ParameterCell>> cells;   ///< enable first
        juce::Rectangle<int> bounds;
        bool shownEnabled { false };
    };

    void addAdvRow (juce::AudioProcessorValueTreeState& state, int index);

    void addBlock (juce::AudioProcessorValueTreeState& state, const juce::String& heading,
                   const juce::String& detail, const EnvelopeEditor::Ids& ids,
                   const char* extraId, const juce::String& extraName,
                   const juce::String& extraTooltip, const char* snapId = nullptr);

    ui::Palette palette_;
    std::vector<Block> blocks_;
    std::array<AdvRow, 3> advRows_;

public:
    /// Fired when a row's enable flips and the page's preferred height with
    /// it; the editor wires this to its own resized().
    std::function<void()> onHeightChanged;

private:
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

/// The tuning page: the shared microtuning panel (scale menu, Scala
/// loaders, degree table, pitch-standard lore, A4 control), which grew up
/// here and now lives in tezla-ui so Svarayantra shows the identical page.
/// This wrapper is what keeps it a Page.
class TuningPage final : public Page
{
public:
    TuningPage (SonitusProcessor& processorToUse, ui::Palette palette)
        : panel_ (processorToUse, palette,
                  "Microtuning is built in because the comb key-tracks onto harmonics of the "
                  "played note: a just interval locks against it where a tempered one churns. "
                  "The scale travels with the project -- .scl text is saved into the plugin's "
                  "state. Detune and glide stay in cents; they are a spread around a pitch, "
                  "not a scale degree.")
    {
        addAndMakeVisible (panel_);
    }

    void refresh() { panel_.refresh(); }

    [[nodiscard]] int getPreferredHeight() const override { return 360; }

    void resized() override { panel_.setBounds (getLocalBounds()); }

private:
    ui::TuningPanel panel_;

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

    /// Repaints the DICEROLL tab in whatever colour the shared rainbow clock
    /// is at. Called every tick, which is what makes it glow.
    void refreshDiceTab();

    /// The FM ratio line shown beside the SYNC AND PM heading. See the
    /// implementation for why B:A rather than A:B.
    [[nodiscard]] juce::String ratioReadout() const;
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

    /// Held by pointer so it can be *destroyed*, which is the only reliable way
    /// to turn tooltips off: JUCE has no "disabled" state for one, and setting
    /// the delay enormous still shows a tip to anybody who rests on a control.
    ui::TooltipHost tooltips_ { *this };


    /// The brushed metal behind everything, cached at the window's size.
    MetalBackground metal_;

    std::unique_ptr<ui::HeaderBar> header_;

    static constexpr int kNumPages = 7;

    /// The MOD page is the one with the step strip under its grid, and the ENV
    /// page is the bespoke one, so both are named rather than numbered where
    /// the layout needs them.
    static constexpr int kOscPage    = 0;
    static constexpr int kFilterPage = 1;
    static constexpr int kEnvPage    = 2;
    static constexpr int kModPage    = 3;
    static constexpr int kManglePage = 4;
    static constexpr int kTuningPage = 5;
    static constexpr int kDicePage   = 6;

    /// One look and feel per page, so each wears its own accent. JUCE resolves
    /// one by walking *up* the tree, so setting it on a page colours every
    /// control inside it.
    ///
    /// **Sized by kNumPages rather than by a literal**, and it was a literal 6:
    /// adding a seventh page wrote one past the end and segfaulted inside a
    /// LookAndFeel destructor, which points nowhere near the cause. Declared
    /// here rather than beside the tooltip host so the constant is in scope.
    std::array<std::unique_ptr<ui::KnobLookAndFeel>, kNumPages> pageLookAndFeels_;

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
