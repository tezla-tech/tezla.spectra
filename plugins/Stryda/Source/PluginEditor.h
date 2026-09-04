// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The F3 panel: six operator strips, the matrix, and the bandwidth readout.
//
// One page deliberately. The point of F3 is to get the instrument onto the rig
// and into the user's hands before anything else is designed around it, so the
// layout says what the engine does and nothing more.

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include <tezla/ui/HeaderBar.hpp>
#include <tezla/ui/HouseControls.hpp>
#include <tezla/ui/KnobLookAndFeel.hpp>
#include <tezla/ui/Palette.hpp>
#include <tezla/ui/PanelDesign.hpp>
#include <tezla/ui/LampButton.hpp>
#include <tezla/ui/Plate.hpp>
#include <tezla/ui/TuningPanel.hpp>
#include <tezla/ui/TooltipHost.hpp>

#include "PluginProcessor.h"

namespace tezla::stryda {

/// A knob plus its caption plus its attachment, which is the unit the layout
/// actually deals in.
struct Control
{
    juce::Label caption;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/// The 6x6 grid. Every cell is an index in cycles; the diagonal is the
/// operator's own feedback, which is the same idea and belongs in the same
/// square rather than hidden on a strip.
class MatrixGrid final : public juce::Component
{
public:
    MatrixGrid (StrydaProcessor& owner, ui::Palette palette);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    ui::Palette palette_;
    std::array<std::array<std::unique_ptr<Control>, kNumOperators>, kNumOperators> cells_ {};
    std::array<std::unique_ptr<Control>, kNumOperators> noise_ {};
};

/// The output spectrum, with the predicted top sideband drawn on it.
///
/// ---------------------------------------------------------------------------
/// **The prediction is the point, not the curve**
/// ---------------------------------------------------------------------------
///
/// A spectrum analyser is a commodity. What is worth showing here is the thing
/// this instrument knows that an analyser cannot: where the sidebands are going
/// to stop, computed from the matrix rather than measured after the fact.
/// `dsp::FmBandwidth` has been measured against a real spectrum since F1 and is
/// exact to the bin (0 Hz error, 6.0 Hz bins), and this is where that
/// measurement becomes something a player can see rather than a number in a
/// test log.
///
/// Three markers, and each says something different:
///
///  - **EDGE** -- the predicted top sideband. Everything the patch produces is
///    below it. If the curve has content past it, the prediction is wrong and
///    that is a bug worth reporting.
///  - **NYQUIST** -- half the internal rate, which is where the oversampler is
///    working. Sidebands past this fold back and become the inharmonic mush
///    CLAUDE.md section 7 calls a defect.
///  - **UNCAPPED** -- where the edge WOULD have been without the index cap,
///    drawn only when the cap is actually doing something. A cap that is not
///    binding is exactly inert and draws nothing, which is the honest picture
///    of it, and the gap between the two lines is the work it did.
class BandwidthView final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    BandwidthView (StrydaProcessor& owner, ui::Palette palette);

    void prepare (double sampleRate);

    /// Pulls a frame and redraws. Called from the editor's timer.
    void refresh (double predictedTopHz, double cappedTopHz, bool capBiting);

    void paint (juce::Graphics& g) override;

private:
    [[nodiscard]] float xFor (double hz) const;

    StrydaProcessor& processor_;
    ui::Palette palette_;
    dsp::SpectrumAnalyser analyser_;

    double lowHz_ { 20.0 };
    double highHz_ { 20000.0 };
    double predicted_ { 0.0 };
    double capped_ { 0.0 };
    bool capBiting_ { false };
};

/// The current preset's notes, and a glossary of the words the panel uses.
///
/// A tooltip answers "what does this knob do". A preset needs something else:
/// what the patch is FOR, what to hold down, what is worth automating, and
/// where the sound came from -- which is a paragraph and sometimes a century of
/// history. `Preset::notes` sits beside the settings it describes so the two
/// cannot drift, and the field is not defaultable, so a preset added without
/// notes fails to compile rather than showing a blank page.
///
/// The markup is deliberately tiny: a blank line separates paragraphs and
/// `**text**` is bold. Anything more would be a document format, and the notes
/// are prose.
class NotesView final : public juce::Component
{
public:
    NotesView (StrydaProcessor& owner, ui::Palette palette);

    /// Rebuild for a new preset. Cheap enough to call on a change and far too
    /// expensive to call every frame, which is why the editor compares the
    /// index first.
    void setProgram (int index);

    /// How tall the laid-out text is, so the viewport can scroll it.
    [[nodiscard]] int getPreferredHeight() const noexcept { return height_; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Block
    {
        enum class Kind { title, heading, body };

        Kind kind { Kind::body };
        juce::AttributedString text;
        juce::TextLayout layout;
        int height { 0 };
    };

    void build (int width);

    StrydaProcessor& processor_;
    ui::Palette palette_;
    int program_ { -1 };
    int builtWidth_ { 0 };
    int height_ { 0 };
    std::vector<Block> blocks_;
};

/// One operator's actual output wave, for one cycle.
///
/// ---------------------------------------------------------------------------
/// **It runs the real operator. Nothing here draws a sine and bends it**
/// ---------------------------------------------------------------------------
///
/// A picture that merely resembles the wave is worse than no picture, because
/// it is believed. So this steps a live `dsp::FmOperator` -- the same class the
/// voice runs -- through one cycle, fed the same three numbers `OperatorMatrix`
/// feeds it every sample:
///
///     pm   = sum over modulators of  index * sin(theta_from)
///     am   = sum over modulators of  index * cos(theta_from)
///     norm = sum over modulators of  index
///
/// built from this operator's column of the patch's matrix. Feedback, phase
/// distortion (Fold), the waveform choice and Character all therefore appear
/// exactly as they sound.
///
/// **And it explains the thing that otherwise looks like a bug.** Character is
/// the ModFM exponential `exp(r k cos - r k)`, and `k` is the index *arriving*
/// at this operator: with nothing modulating it, k = 0, the exponential is 1,
/// and the wave stays whatever the shape says at every Character setting. A
/// player who turns CHAR on a carrier nobody modulates hears nothing and
/// reasonably concludes the knob is broken. Here they see why instead.
class OperatorWave final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    OperatorWave (StrydaProcessor& owner, int operatorIndex, ui::Palette palette);

    void paint (juce::Graphics& g) override;

private:
    [[nodiscard]] float plain (const char* field) const;
    [[nodiscard]] float plainOf (const juce::String& id) const;

    StrydaProcessor& processor_;
    int operator_ { 0 };
    ui::Palette palette_;
};

/// One ADV envelope drawn as the polyline it is, with the point you clicked
/// last picked out.
///
/// **The curve is the DSP's curve**, not a spline that resembles it: each leg
/// is `dsp::Adsr::overshootFor`'s own arithmetic, the same function the
/// envelope runs, so a shape that looks wrong here is wrong in the sound too.
/// Editing is the three knobs beside it -- click a point to aim them at it.
/// Dragging the graph itself is F9's job, along with the rest of the displays.
class AdvGraph final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    AdvGraph (StrydaProcessor& owner, int envelopeIndex, ui::Palette palette);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;

    /// Which point the three edit knobs are aimed at.
    [[nodiscard]] int getSelectedPoint() const noexcept { return selected_; }

    /// Called when the selection changes, so the page can re-attach the knobs.
    std::function<void()> onSelectionChanged;

private:
    [[nodiscard]] float plain (const char* field, int point) const;
    [[nodiscard]] int pointCount() const;

    StrydaProcessor& processor_;
    int envelope_ { 0 };
    ui::Palette palette_;
    int selected_ { 0 };
};

class StrydaEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit StrydaEditor (StrydaProcessor& owner);
    ~StrydaEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /// The pages, in tab order. **Append-only in spirit rather than by rule:**
    /// nothing stores a page index, but a player learns where things are.
    enum Page
    {
        pageOperators = 0,
        pageMatrix,
        pageVoice,
        pageSequencer,
        pageMangle,
        pageAdv,
        pageMod,
        pageNotes,
        pageTuning,
        pageCount
    };

private:
    void timerCallback() override;

    void showPage (int page);
    void styleTab (juce::TextButton& tab, bool active);

    void layoutOperators (juce::Rectangle<int> area);
    void layoutMatrix (juce::Rectangle<int> area);
    void layoutVoice (juce::Rectangle<int> area);
    void layoutSequencer (juce::Rectangle<int> area);
    void layoutMangle (juce::Rectangle<int> area);
    void layoutAdv (juce::Rectangle<int> area);
    void layoutMod (juce::Rectangle<int> area);
    void layoutNotes (juce::Rectangle<int> area);

    void paintOperators (juce::Graphics& g, juce::Rectangle<int> area);
    void paintMatrix (juce::Graphics& g, juce::Rectangle<int> area);
    void paintVoice (juce::Graphics& g, juce::Rectangle<int> area);
    void paintSequencer (juce::Graphics& g, juce::Rectangle<int> area);
    void paintMangle (juce::Graphics& g, juce::Rectangle<int> area);
    void paintAdv (juce::Graphics& g, juce::Rectangle<int> area);
    void paintMod (juce::Graphics& g, juce::Rectangle<int> area);

    /// Aim envelope `which`'s three edit knobs at whichever point its graph
    /// has selected. The attachment is rebuilt rather than the value copied:
    /// an attachment *is* the link to the parameter, so pointing three knobs
    /// at a different breakpoint means three new ones.
    void retargetAdvPoint (int which);

    /// Everything the current page does not own is hidden rather than laid out
    /// off-screen: a control that is merely somewhere else still takes the
    /// mouse, and a hidden one cannot.
    void applyPageVisibility();

    int currentPage_ { pageOperators };
    std::array<juce::TextButton, pageCount> tabs_ {};

    /// The predicted top of the spectrum, from the parameters rather than from
    /// the audio thread, so the readout is live with the transport stopped.
    [[nodiscard]] double predictedTop() const;

    /// Set by predictedTop(): the top the patch would produce with no cap, so
    /// the spectrum can show what the cap pulled it back from.
    mutable double uncappedTop_ { 0.0 };

    /// Set by predictedTop(): whether the index cap is doing work at the moment.
    mutable bool capBiting_ { false };

    [[nodiscard]] Control& addControl (const juce::String& id,
                                       const juce::String& caption,
                                       juce::Colour tint,
                                       ui::design::Emphasis emphasis = ui::design::Emphasis::normal);

    StrydaProcessor& processor_;
    ui::Palette palette_ {};
    ui::KnobLookAndFeel lookAndFeel_ { palette_ };
    ui::TooltipHost tooltips_;

    ui::HeaderBar header_;
    MatrixGrid matrix_;

    std::vector<std::unique_ptr<Control>> controls_;

    /// Per operator, in the order they are laid out down a strip.
    std::array<std::vector<Control*>, kNumOperators> strips_ {};

    /// Key scaling and velocity, five per operator, on their own plate.
    std::array<std::vector<Control*>, kNumOperators> scaling_ {};

    /// One per operator: Normal or Formant.
    std::array<std::unique_ptr<juce::ComboBox>, kNumOperators> modeBoxes_ {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>,
               kNumOperators> modeAttachments_ {};

    /// F9: the waveform choice and the wave it actually produces, one per strip.
    std::array<std::unique_ptr<juce::ComboBox>, kNumOperators> shapeBoxes_ {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>,
               kNumOperators> shapeAttachments_ {};
    std::array<std::unique_ptr<OperatorWave>, kNumOperators> waves_ {};

    std::vector<Control*> globals_;

    /// F5: the per-voice filter, the protected sub lane and the unison stack,
    /// each on its own plate in the band under the operator strips.
    std::vector<Control*> filter_;
    std::vector<Control*> sub_;
    std::vector<Control*> unison_;

    juce::ComboBox subOctaveBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> subOctaveAttachment_;

    juce::ComboBox subShapeBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> subShapeAttachment_;

    /// F6: the ratio sequencer -- sixteen step knobs and five controls -- and
    /// the six named braids, which are buttons rather than a choice because
    /// pressing one *writes* the matrix and leaves it editable.
    std::vector<Control*> steps_;
    std::vector<Control*> seqControls_;

    ui::LampButton seqOnButton_ { "RUN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqOnAttachment_;

    juce::ComboBox seqTargetBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> seqTargetAttachment_;

    juce::ComboBox seqDivisionBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> seqDivisionAttachment_;

    std::array<juce::TextButton, braids::kCount> braidButtons_ {};

    /// The shared microtuning panel. The processor is its host, exactly as it
    /// is for Malleus, Sonitus, Svarayantra and Ictus.
    std::unique_ptr<ui::TuningPanel> tuningPage_;

    /// F7: Split, the vowel lane with its own pattern, and the mangle chain.
    std::vector<Control*> vowel_;
    std::vector<Control*> vowelSteps_;
    std::vector<Control*> mangle_;

    ui::LampButton vowelSeqButton_ { "TALK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> vowelSeqAttachment_;

    juce::ComboBox vowelDivisionBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vowelDivisionAttachment_;

    /// F8: the modulation layer. Two ADV envelopes with their graphs, two
    /// LFOs, four macros and eight slots.
    std::array<std::unique_ptr<AdvGraph>, 2> advGraphs_ {};
    std::array<std::vector<Control*>, 2> advShape_ {};
    std::array<ui::LampButton, 2> advLoopButtons_ { ui::LampButton { "LOOP" },
                                                    ui::LampButton { "LOOP" } };
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 2>
        advLoopAttachments_ {};

    /// Three knobs per envelope, re-attached as the graph's selection moves.
    struct PointEditor
    {
        std::array<juce::Label, 3> captions {};
        std::array<juce::Slider, 3> knobs {};
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3>
            attachments {};
        juce::Label heading;
    };

    std::array<PointEditor, 2> advPoints_ {};

    std::array<std::vector<Control*>, 2> lfoControls_ {};
    std::array<std::unique_ptr<juce::ComboBox>, 2> lfoWaveBoxes_ {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, 2>
        lfoWaveAttachments_ {};
    std::array<std::unique_ptr<juce::ComboBox>, 2> lfoDivisionBoxes_ {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, 2>
        lfoDivisionAttachments_ {};
    std::array<ui::LampButton, 2> lfoSyncButtons_ { ui::LampButton { "SYNC" },
                                                    ui::LampButton { "SYNC" } };
    std::array<ui::LampButton, 2> lfoRetrigButtons_ { ui::LampButton { "RETRIG" },
                                                      ui::LampButton { "RETRIG" } };
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 2>
        lfoSyncAttachments_ {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 2>
        lfoRetrigAttachments_ {};

    std::vector<Control*> macros_;

    /// Eight rows of source, destination and amount.
    std::array<std::unique_ptr<juce::ComboBox>, kNumSlots> slotSourceBoxes_ {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>,
               kNumSlots> slotSourceAttachments_ {};
    std::array<std::unique_ptr<juce::ComboBox>, kNumSlots> slotDestBoxes_ {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>,
               kNumSlots> slotDestAttachments_ {};
    std::vector<Control*> slotAmounts_;

    /// F9b: the spectrum on the MATRIX page, under the readout.
    std::unique_ptr<BandwidthView> spectrum_;

    juce::Label bandwidth_;
    juce::Label voices_;

    juce::ComboBox indexCapBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> indexCapAttachment_;

    /// F9b: the NOTES page. A viewport because a good preset note is longer
    /// than a panel is tall, and clipping the interesting half would be worse
    /// than not writing it.
    juce::Viewport notesViewport_;
    std::unique_ptr<NotesView> notesView_;

    juce::ComboBox presetBox_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrydaEditor)
};

} // namespace tezla::stryda
