// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

namespace tezla::stryda {

namespace {

constexpr int kHeaderHeight = 46;
constexpr int kMargin = 12;
constexpr int kMinStripWidth = 190;
constexpr int kRowHeight = 74;
constexpr int kCaptionHeight = 15;

/// The tab strip under the header.
constexpr int kTabHeight = 30;

/// The bandwidth readout on the matrix page.
constexpr int kReadoutHeight = 74;

/// How many points one cycle of an operator's wave is drawn from. Enough that
/// a sixteen-harmonic saw reads as a saw rather than as steps.
constexpr int kWaveSamples = 256;

/// The wave strip at the top of each operator column.
constexpr int kWaveHeight = 40;

constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

/// The slot number down the left of each modulation row.
constexpr int kSlotNumberWidth = 18;

/// The braid row at the foot of the sequencer page.
constexpr int kBraidHeight = 64;

/// The three F5 plates, split the same way in `paint` and in `resized`.
///
/// One function rather than two copies of the arithmetic, because the plate a
/// control is drawn on and the plate it is laid out on drifting apart is the
/// classic way a panel ends up with a knob half off its own background.
struct BandPlates
{
    juce::Rectangle<int> filter;
    juce::Rectangle<int> sub;
    juce::Rectangle<int> unison;
};

[[nodiscard]] BandPlates splitBand (juce::Rectangle<int> band)
{
    BandPlates plates;

    // The filter has eleven controls, the sub five plus two choices, unison
    // four. The widths follow the counts rather than being equal thirds.
    plates.filter = band.removeFromLeft (band.getWidth() * 47 / 100);
    plates.sub = band.removeFromLeft (band.getWidth() * 55 / 100);
    plates.unison = band;

    return plates;
}

/// The MOD page's three regions, and the slot table's own grid, split once and
/// used by both `paintMod` and `layoutMod`.
///
/// Same reason as `splitBand`: a plate drawn in one place and laid out from
/// another arithmetic is how a knob ends up half off its own background. Here
/// it matters more, because the row numbers and column headings are *painted*
/// while the boxes under them are *laid out*.
struct ModPlates
{
    juce::Rectangle<int> lfo[2];
    juce::Rectangle<int> macros;
    juce::Rectangle<int> matrix;

    /// The slot table: `columns` strips of `perColumn` rows, each `rowHeight`
    /// tall, starting under a heading row.
    juce::Rectangle<int> table;
    int columns { 2 };
    int perColumn { 4 };
    int rowHeight { 34 };
    int headingHeight { 16 };
};

[[nodiscard]] ModPlates splitMod (juce::Rectangle<int> area)
{
    ModPlates plates;

    // **The table is sized first and the rest goes to the LFOs.** Splitting by
    // a percentage left the matrix plate half empty on a tall window while the
    // LFO lamps were squeezed into 24 px -- the rows are capped, so the plate
    // should be too.
    const int perColumn = area.getWidth() >= 840 ? 4 : kNumSlots;
    const int rows = perColumn;
    const int wanted = ui::design::kValueHeight + 6 + 16 + rows * 40 + 16;

    auto top = area.removeFromTop (
        juce::jmax (area.getHeight() * 34 / 100, area.getHeight() - wanted - kMargin));
    area.removeFromTop (kMargin);

    auto lfoArea = top.removeFromLeft (top.getWidth() * 62 / 100);
    top.removeFromLeft (kMargin);

    plates.lfo[0] = lfoArea.removeFromLeft (lfoArea.getWidth() / 2);
    lfoArea.removeFromLeft (6);
    plates.lfo[1] = lfoArea;
    plates.macros = top;
    plates.matrix = area;

    // A combo box below about 150 px cannot show a destination name, so a
    // narrow panel gets one column of eight rather than two of four.
    auto table = plates.matrix.reduced (8);
    table.removeFromTop (ui::design::kValueHeight + 6);

    plates.perColumn = perColumn;
    plates.columns = kNumSlots / plates.perColumn;
    plates.headingHeight = 16;

    // **Capped, not divided.** Dividing the plate by the row count made a
    // 120 px combo box on a tall window: a row is as tall as its controls
    // need, and the spare height stays spare.
    const int available = table.getHeight() - plates.headingHeight;
    plates.rowHeight = juce::jlimit (26, 40, available / plates.perColumn);
    plates.table = table;

    return plates;
}

[[nodiscard]] juce::String hzText (double hz)
{
    if (hz >= 1000.0)
        return juce::String (hz / 1000.0, 1) + " kHz";

    return juce::String (hz, 0) + " Hz";
}

void styleCaption (juce::Label& label, ui::Palette palette)
{
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, palette.dimText);
    label.setFont (juce::FontOptions (11.0f));
    label.setInterceptsMouseClicks (false, false);
}

} // namespace

// ---------------------------------------------------------------------------
// The matrix
// ---------------------------------------------------------------------------

MatrixGrid::MatrixGrid (StrydaProcessor& owner, ui::Palette palette)
    : palette_ (palette)
{
    const auto tint = ui::design::tintFor (palette.accent, 2);

    for (int to = 0; to < kNumOperators; ++to)
    {
        for (int from = 0; from < kNumOperators; ++from)
        {
            auto cell = std::make_unique<Control>();

            const bool diagonal = to == from;
            const juce::String id = diagonal ? ids::op (to, "Feedback") : ids::cell (to, from);

            cell->knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            ui::styleKnob (cell->knob, palette,
                           diagonal ? palette.secondary : tint,
                           ui::design::Emphasis::trim);
            cell->knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

            cell->knob.setTooltip (
                diagonal
                  ? "Operator " + juce::String (to + 1) + " modulating its own phase, in cycles. "
                    "Bounded by construction -- phase modulation cannot make a bigger output, "
                    "only a differently shaped one -- and the feedback signal is the mean of the "
                    "last two samples, which nulls the useless Nyquist mode outright."
                  : "Operator " + juce::String (from + 1) + " modulating operator "
                        + juce::String (to + 1) + ", in cycles of phase deviation.\n\n"
                          "The direction matters: a modulator with a HIGHER number arrives "
                          "instantly, a LOWER one arrives one sample later. So "
                        + juce::String (from + 1) + " \xe2\x86\x92 " + juce::String (to + 1)
                        + " and " + juce::String (to + 1) + " \xe2\x86\x92 "
                        + juce::String (from + 1) + " do not sound alike at the same setting. "
                          "That single-sample delay is what makes any wiring computable.");

            cell->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                owner.getState(), id, cell->knob);

            addAndMakeVisible (cell->knob);
            cells_[static_cast<std::size_t> (to)][static_cast<std::size_t> (from)] = std::move (cell);
        }

        auto noise = std::make_unique<Control>();
        noise->knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        ui::styleKnob (noise->knob, palette, palette.dimText, ui::design::Emphasis::trim);
        noise->knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        noise->knob.setTooltip (
            "Noise modulating operator " + juce::String (to + 1) + "'s phase, in cycles. "
            "The one modulator with no pitch, and the grit no amount of sine-on-sine will "
            "give you. A little goes a long way on a growl.");

        noise->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            owner.getState(), ids::noise (to), noise->knob);

        addAndMakeVisible (noise->knob);
        noise_[static_cast<std::size_t> (to)] = std::move (noise);
    }
}

void MatrixGrid::paint (juce::Graphics& g)
{
    ui::paintPlate (g, getLocalBounds(), palette_.panel, palette_.accent);

    auto area = getLocalBounds().reduced (8);
    ui::paintPlateHeading (g, palette_, area.removeFromTop (ui::design::kValueHeight + 4),
                           "MATRIX", "who modulates whom, in cycles",
                           ui::design::tintFor (palette_.accent, 2));

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.0f));

    const int labelWidth = 26;
    auto grid = area.reduced (0, 4);
    auto headerRow = grid.removeFromTop (14);
    headerRow.removeFromLeft (labelWidth);

    const int columnWidth = headerRow.getWidth() / (kNumOperators + 1);

    for (int from = 0; from < kNumOperators; ++from)
        g.drawText (juce::String (from + 1),
                    headerRow.removeFromLeft (columnWidth),
                    juce::Justification::centred);

    g.drawText ("N", headerRow, juce::Justification::centred);

    const int rowHeight = grid.getHeight() / kNumOperators;

    for (int to = 0; to < kNumOperators; ++to)
    {
        auto row = grid.removeFromTop (rowHeight);
        g.setColour (palette_.dimText);
        g.drawText (juce::String (to + 1), row.removeFromLeft (labelWidth),
                    juce::Justification::centred);
    }
}

void MatrixGrid::resized()
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (ui::design::kValueHeight + 4);
    area = area.reduced (0, 4);
    area.removeFromTop (14);
    area.removeFromLeft (26);

    const int rowHeight = area.getHeight() / kNumOperators;
    const int columnWidth = area.getWidth() / (kNumOperators + 1);

    for (int to = 0; to < kNumOperators; ++to)
    {
        auto row = area.removeFromTop (rowHeight);

        for (int from = 0; from < kNumOperators; ++from)
            cells_[static_cast<std::size_t> (to)][static_cast<std::size_t> (from)]
                ->knob.setBounds (row.removeFromLeft (columnWidth).reduced (2));

        noise_[static_cast<std::size_t> (to)]->knob.setBounds (row.reduced (2));
    }
}


// ---------------------------------------------------------------------------
// The spectrum, with the predicted edge on it
// ---------------------------------------------------------------------------

BandwidthView::BandwidthView (StrydaProcessor& owner, ui::Palette palette)
    : processor_ (owner), palette_ (palette)
{
    setInterceptsMouseClicks (false, false);
    setTooltip (
        "The output spectrum, with three markers that matter more than the curve does.\n\n"
        "**EDGE** is the predicted top sideband, computed from the matrix rather than measured "
        "-- everything the patch produces is below it. The predictor has been measured against "
        "a real spectrum to 0 Hz at a 6 Hz bin width, so if the curve has content past the "
        "line, that is a bug rather than a surprise.\n\n"
        "**NYQUIST** is half the internal rate, where the oversampler is working. Anything past "
        "it folds back and becomes inharmonic mush.\n\n"
        "**CAP** appears only when the index cap is actually doing something, and shows where "
        "it pulled the edge back to. A cap that is not binding draws nothing, because it is "
        "doing nothing -- exactly, to the bit.");
}

void BandwidthView::prepare (double sampleRate)
{
    // **The axis stops at the HOST Nyquist, not the internal one.** The capture
    // is filled after decimation, so there is nothing above it to draw; an axis
    // running to the internal Nyquist would show a curve that stops two thirds
    // of the way across and read as a broken analyser rather than as a correct
    // one. Markers past the end are pinned to the right edge and say so.
    //
    // 4096 points: about 12 Hz at 48 kHz, which separates the sidebands of a
    // bass note rather than smearing them into one another.
    highHz_ = std::max (2000.0, sampleRate * 0.5 * 0.98);
    analyser_.prepare (sampleRate, 12, 200, lowHz_, highHz_);
}

void BandwidthView::refresh (double predictedTopHz, double cappedTopHz, bool capBiting)
{
    predicted_ = predictedTopHz;
    capped_ = cappedTopHz;
    capBiting_ = capBiting;

    if (analyser_.update (processor_.getOutputCapture()))
        repaint();
    else
        repaint();      // the markers move with the patch even in silence
}

float BandwidthView::xFor (double hz) const
{
    // Logarithmic, like every frequency axis in the suite.
    const double clamped = juce::jlimit (lowHz_, highHz_, hz);
    const double span = std::log (highHz_ / lowHz_);

    return static_cast<float> (std::log (clamped / lowHz_) / span)
             * static_cast<float> (getWidth());
}

void BandwidthView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (palette_.background.darker (0.25f));
    g.fillRoundedRectangle (area, 4.0f);

    // Decade lines, so the axis is readable without a full ruler.
    g.setFont (juce::FontOptions (9.0f));

    for (const double hz : { 100.0, 1000.0, 10000.0 })
    {
        if (hz >= highHz_)
            continue;

        const float x = xFor (hz);

        g.setColour (palette_.dimText.withAlpha (0.18f));
        g.drawVerticalLine (static_cast<int> (x), area.getY(), area.getBottom());

        g.setColour (palette_.dimText.withAlpha (0.55f));
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k"
                                 : juce::String (hz, 0),
                    static_cast<int> (x) + 3, static_cast<int> (area.getBottom()) - 13,
                    30, 12, juce::Justification::left);
    }

    // The curve.
    const auto& magnitudes = analyser_.getMagnitudesDb();

    if (magnitudes.size() >= 2)
    {
        constexpr float kTopDb = 0.0f;
        constexpr float kBottomDb = -84.0f;

        juce::Path curve;

        for (std::size_t bin = 0; bin < magnitudes.size(); ++bin)
        {
            const float x = area.getWidth() * static_cast<float> (bin)
                              / static_cast<float> (magnitudes.size() - 1);
            const float level = juce::jlimit (kBottomDb, kTopDb, magnitudes[bin]);
            const float y = area.getBottom()
                          - area.getHeight() * (level - kBottomDb) / (kTopDb - kBottomDb);

            if (bin == 0)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }

        juce::Path filled (curve);
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setColour (palette_.accent.withAlpha (0.18f));
        g.fillPath (filled);

        g.setColour (palette_.accent.withAlpha (0.9f));
        g.strokePath (curve, juce::PathStrokeType (1.3f));
    }

    // The markers, drawn over the curve because they are the point of it.
    int labelRow = 0;

    const auto marker = [&] (double hz, juce::Colour colour, const juce::String& label,
                             bool dashed)
    {
        if (! (hz > lowHz_))
            return;

        // Off the top of the axis is the normal case for the EDGE at a factor
        // of four: it is pinned to the right and carries its own number, which
        // is more useful than a line that silently sits on the border.
        const bool beyond = hz > highHz_;
        const float x = beyond ? area.getRight() - 1.0f : xFor (hz);

        const auto text = beyond
                            ? label + " " + (hz >= 1000.0
                                                ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + "k"
                                                : juce::String (hz, 0))
                            : label;

        g.setColour (colour);

        if (dashed)
        {
            const float dashes[] { 4.0f, 3.0f };
            juce::Path line;
            line.startNewSubPath (x, area.getY() + 2.0f);
            line.lineTo (x, area.getBottom() - 2.0f);

            juce::PathStrokeType (1.4f).createDashedStroke (line, line, dashes, 2);
            g.strokePath (line, juce::PathStrokeType (1.4f));
        }
        else
        {
            g.drawVerticalLine (static_cast<int> (x), area.getY() + 2.0f, area.getBottom() - 2.0f);
        }

        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));

        // Right-aligned when the line is near the right edge, so a label never
        // runs off the plot; and stacked, so two markers that land in the same
        // place -- which they do whenever both are past the axis -- do not
        // print on top of each other.
        const bool nearRight = x > area.getWidth() - 90.0f;
        const int row = labelRow++;

        g.drawText (text,
                    static_cast<int> (nearRight ? x - 86.0f : x + 4.0f),
                    static_cast<int> (area.getY()) + 3 + row * 12, 84, 12,
                    nearRight ? juce::Justification::right : juce::Justification::left);
    };

    const double nyquist = processor_.getInternalNyquistHz();

    marker (nyquist, palette_.dimText.withAlpha (0.8f), "NYQUIST", true);
    marker (predicted_, palette_.secondary, "EDGE", false);

    if (capBiting_)
        marker (capped_, juce::Colour { 0xffe8b23a }, "UNCAPPED", true);
}

// ---------------------------------------------------------------------------
// The NOTES page
// ---------------------------------------------------------------------------

namespace
{
/// The words the panel uses that a player would reasonably not know, and what
/// each one means here specifically. Written for someone who knows synths and
/// has not read a Bessel function in their life.
struct GlossaryEntry { const char* term; const char* meaning; };

const GlossaryEntry kGlossary[] {
    { "Index",
      "How hard one operator modulates another, in **cycles** of phase deviation. It is the "
      "brightness control of FM: a higher index means more sidebands, further out. Every cell "
      "in the matrix is an index." },

    { "Ratio",
      "An operator's frequency as a multiple of the note. In FM the ratio IS the interval -- "
      "sidebands land at the carrier plus and minus whole multiples of the modulator, so a "
      "whole-number ratio fuses into one tone and 4.76 is a bell." },

    { "Character",
      "Classic FM at 0, ModFM at 1, continuously between. At the ModFM end the partials fall "
      "away in order and the index behaves like a filter opening; at the classic end it steps "
      "through Bessel nulls and the timbre flickers. **It only does anything on an operator "
      "that something else modulates** -- it scales with the index arriving, so on a carrier "
      "nobody modulates it is inert at every setting." },

    { "Shape",
      "The operator's waveform. Sine is the default and the safe one: a non-sine operator's "
      "harmonics MULTIPLY the sideband ladder rather than adding to it, so a Saw modulator at "
      "index 4 is a different order of brightness -- and of aliasing -- than a sine." },

    { "Fold",
      "Phase distortion: the ramp the operator reads is bent before anything reads it, which "
      "puts a knee in the wave. Exactly the identity at 0." },

    { "Feedback",
      "An operator modulating itself, drawn on the matrix diagonal because that is the same "
      "idea as a cell. Bounded by construction, and the two-sample average inside it is what "
      "stops it settling into a useless flip at Nyquist." },

    { "Braid",
      "A named topology -- Stack, Twin, Fan, Ring, Pairs, Solo. Pressing one WRITES the matrix "
      "and leaves it editable, so it is a starting point rather than a mode." },

    { "Matrix order",
      "Operators evaluate 6 down to 1, so a cell BELOW the diagonal is instantaneous within "
      "the sample and one ABOVE it is one sample old. That delay is what turns an algebraic "
      "loop into a computable one, and it is why op 4 -> op 2 and op 2 -> op 4 do not sound "
      "alike at the same index." },

    { "Index cap",
      "Scales every index down until the predicted top sideband fits under the internal "
      "Nyquist -- 1980s key scaling derived from the arithmetic rather than dialled in. Off by "
      "default. When it is not binding it is **exactly** inert, to the bit." },

    { "Split",
      "A crossover. The low band goes round the vowel lane and the whole mangle chain "
      "untouched, which is what stops a mangled bass losing its bottom. At 0 Hz it is skipped "
      "outright, because a Linkwitz-Riley crossover summed back together is an allpass rather "
      "than an identity." },

    { "Sub lane",
      "An oscillator with its own envelope that enters neither the matrix nor the mangle "
      "chain. Nothing in the instrument can touch it, so the fundamental stays where you put "
      "it however hard the operators are driven." },

    { "Index spread",
      "Unison detunes the copies in pitch; this spreads the modulation index across them too, "
      "so they differ in TIMBRE as well. It is the thickest control here and it is what a "
      "reese wants." },

    { "Ratio mode",
      "Free, Harmonic (the nearest simple p:q), or Scale -- the ratio snapped to the loaded "
      "tuning's degrees, octave-extended. Scale mode puts a modulator's whole sideband ladder "
      "on your scale rather than on 12-TET's, at every key, and it is the one idea here no "
      "other FM synth has." },

    { "Matrix depth",
      "A modulation destination that scales every live cell together rather than adding to "
      "them. Scaling can only move what is already there; adding would switch on paths the "
      "patch never asked for." }
};
} // namespace

NotesView::NotesView (StrydaProcessor& owner, ui::Palette palette)
    : processor_ (owner), palette_ (palette)
{
    setInterceptsMouseClicks (false, false);
}

void NotesView::setProgram (int index)
{
    if (index == program_)
        return;

    program_ = index;
    builtWidth_ = 0;        // force a rebuild at the next resize or paint
    build (getWidth());
    repaint();
}

void NotesView::build (int width)
{
    if (width <= 0)
        return;

    builtWidth_ = width;
    blocks_.clear();

    const auto& presets = StrydaProcessor::getPresets();
    const auto safe = juce::jlimit (0, static_cast<int> (presets.size()) - 1, program_);

    if (presets.empty())
        return;

    const auto& preset = presets[static_cast<std::size_t> (safe)];

    // The little markup: a blank line separates paragraphs, `**text**` is bold.
    const auto addBlock = [this, width] (Block::Kind kind, const juce::String& text)
    {
        Block block;
        block.kind = kind;

        const float size = kind == Block::Kind::title ? 19.0f
                         : kind == Block::Kind::heading ? 12.0f
                         : 13.0f;

        const auto colour = kind == Block::Kind::title ? palette_.accent
                          : kind == Block::Kind::heading ? palette_.secondary
                          : palette_.text;

        // Split on `**` and alternate weight, so the bold survives wrapping
        // rather than being a marker the reader has to ignore.
        auto remaining = text;
        bool bold = false;

        while (remaining.isNotEmpty())
        {
            const auto marker = remaining.indexOf ("**");
            const auto piece = marker >= 0 ? remaining.substring (0, marker) : remaining;

            if (piece.isNotEmpty())
                block.text.append (piece,
                    juce::FontOptions (size, bold || kind != Block::Kind::body
                                                 ? juce::Font::bold : juce::Font::plain),
                    colour);

            if (marker < 0)
                break;

            remaining = remaining.substring (marker + 2);
            bold = ! bold;
        }

        block.text.setLineSpacing (kind == Block::Kind::body ? 3.0f : 1.0f);
        block.layout.createLayout (block.text, static_cast<float> (width));
        block.height = static_cast<int> (std::ceil (block.layout.getHeight()))
                     + (kind == Block::Kind::body ? 12 : 16);

        blocks_.push_back (std::move (block));
    };

    addBlock (Block::Kind::title, preset.name);

    for (const auto& paragraph : juce::StringArray::fromTokens (
             juce::String (preset.notes), "\n\n", {}))
        if (paragraph.trim().isNotEmpty())
            addBlock (Block::Kind::body, paragraph.trim());

    addBlock (Block::Kind::heading, "GLOSSARY");

    for (const auto& entry : kGlossary)
        addBlock (Block::Kind::body,
                  "**" + juce::String (entry.term) + "** -- " + juce::String (entry.meaning));

    height_ = 0;
    for (const auto& block : blocks_)
        height_ += block.height;

    height_ += 24;
}

void NotesView::resized()
{
    if (getWidth() != builtWidth_)
    {
        build (getWidth());

        // The viewport sizes this component from `getPreferredHeight`, and the
        // height only becomes known once the text has been wrapped -- so a
        // rebuild at a new width has to ask for a new size rather than assume
        // the old one still fits.
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
            setSize (getWidth(), juce::jmax (viewport->getHeight(), height_));
    }
}

void NotesView::paint (juce::Graphics& g)
{
    if (getWidth() != builtWidth_)
        build (getWidth());

    int y = 0;

    for (const auto& block : blocks_)
    {
        block.layout.draw (g, juce::Rectangle<float> (
            0.0f, static_cast<float> (y),
            static_cast<float> (getWidth()), block.layout.getHeight()));

        y += block.height;
    }
}

void StrydaEditor::layoutNotes (juce::Rectangle<int> area)
{
    notesViewport_.setBounds (area.reduced (12, 8));

    if (notesView_ != nullptr)
    {
        // The scrollbar's width, so the text never runs under it.
        const int width = juce::jmax (200, notesViewport_.getWidth() - 18);

        notesView_->setSize (width, juce::jmax (notesViewport_.getHeight(),
                                                notesView_->getPreferredHeight()));
    }
}

// ---------------------------------------------------------------------------
// The operator's own wave
// ---------------------------------------------------------------------------

OperatorWave::OperatorWave (StrydaProcessor& owner, int operatorIndex, ui::Palette palette)
    : processor_ (owner), operator_ (operatorIndex), palette_ (palette)
{
    setInterceptsMouseClicks (false, false);
    setTooltip (
        "One cycle of what this operator actually puts out -- not a drawing of a sine, but a "
        "real operator run for a cycle and fed the same modulation the matrix feeds it. "
        "SHAPE, FOLD, CHAR and the operator's own feedback all show up here exactly as they "
        "sound.\n\n"
        "If CHAR appears to do nothing, that is the mathematics rather than a fault: Character "
        "scales with the index ARRIVING at this operator, so an operator nothing modulates is "
        "its plain waveform at every setting. Give it something to answer to -- a matrix cell, "
        "or its own feedback -- and the knob comes alive.");
}

float OperatorWave::plain (const char* field) const
{
    return plainOf (ids::op (operator_, field));
}

float OperatorWave::plainOf (const juce::String& id) const
{
    if (auto* parameter = processor_.getState().getParameter (id))
        return parameter->convertFrom0to1 (parameter->getValue());

    return 0.0f;
}

void OperatorWave::paint (juce::Graphics& g)
{
    const auto tint = ui::design::tintFor (palette_.accent, operator_);
    auto plot = getLocalBounds().toFloat().reduced (3.0f);

    g.setColour (palette_.background.darker (0.25f));
    g.fillRoundedRectangle (plot, 3.0f);

    g.setColour (palette_.dimText.withAlpha (0.25f));
    g.drawLine (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), 1.0f);

    // The same operator the voice runs, configured from the patch.
    dsp::FmOperator op;
    op.prepare (kWaveSamples);          // one cycle per "second"
    op.setFrequency (1.0);
    op.setShape (static_cast<dsp::FmShape> (juce::jlimit (
        0, static_cast<int> (dsp::FmShape::count) - 1,
        static_cast<int> (std::lround (plain ("Shape"))))));

    const double character = plain ("Character");
    op.setCharacter (character);
    op.setTilt (1.0 - character);
    op.setFold (plain ("Fold"));

    // Feedback is `o<n>Feedback` -- it is *drawn* on the matrix diagonal,
    // because a cell modulating an operator with itself is the same idea as one
    // modulating it with a neighbour, but it is its own parameter rather than a
    // cell: `m11` does not exist.
    const double feedback = plain ("Feedback");
    op.setFeedback (feedback);
    op.setQuadratureNeeded (false);

    // The modulation arriving from this operator's row of the matrix, at each
    // modulator's frequency relative to this one. Exactly the three sums
    // `OperatorMatrix::process` builds, from the patch rather than the audio.
    struct Incoming { double index; double ratio; };
    std::vector<Incoming> incoming;

    const double ownRatio = std::max (1.0e-6, static_cast<double> (plain ("Ratio")));

    for (int from = 0; from < kNumOperators; ++from)
    {
        if (from == operator_)
            continue;   // the diagonal is feedback, which the operator does itself

        const double cell = plainOf (ids::cell (operator_, from));

        if (cell <= 0.0)
            continue;

        const double sourceRatio = std::max (1.0e-6,
            static_cast<double> (plainOf (ids::op (from, "Ratio"))));

        incoming.push_back ({ cell, sourceRatio / ownRatio });
    }

    juce::Path wave;

    for (int i = 0; i < kWaveSamples; ++i)
    {
        const double t = static_cast<double> (i) / kWaveSamples;

        double pm = 0.0;
        double am = 0.0;
        double norm = 0.0;

        for (const auto& source : incoming)
        {
            const double theta = kTwoPi * source.ratio * t;

            pm += source.index * std::sin (theta);
            am += source.index * std::cos (theta);
            norm += source.index;
        }

        const double value = op.advance (pm, am, norm);
        const float x = plot.getX() + plot.getWidth() * static_cast<float> (t);
        const float y = plot.getCentreY()
                      - plot.getHeight() * 0.44f * static_cast<float> (juce::jlimit (-1.0, 1.0, value));

        if (i == 0)
            wave.startNewSubPath (x, y);
        else
            wave.lineTo (x, y);
    }

    g.setColour (tint);
    g.strokePath (wave, juce::PathStrokeType (1.4f));

    // Say when the operator has nothing arriving, because that is when CHAR
    // and the index look broken and are not.
    if (incoming.empty() && feedback <= 0.0)
    {
        g.setColour (palette_.dimText.withAlpha (0.7f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("unmodulated", plot.reduced (4.0f, 2.0f),
                    juce::Justification::topRight);
    }
}

// ---------------------------------------------------------------------------
// The ADV envelope graph
// ---------------------------------------------------------------------------

AdvGraph::AdvGraph (StrydaProcessor& owner, int envelopeIndex, ui::Palette palette)
    : processor_ (owner), envelope_ (envelopeIndex), palette_ (palette)
{
    setTooltip ("The shape this envelope plays, drawn with the DSP's own tension arithmetic -- "
                "what you see is what it does. Click a breakpoint to aim the three knobs "
                "underneath at it: TIME is how long the leg into it takes, LEVEL where it "
                "arrives, CURVE how it bends on the way.\n\n"
                "The ringed point is the sustain: the envelope holds there until the key is "
                "released, then plays the rest. The shaded region is the loop, while LOOP is on.");
}

float AdvGraph::plain (const char* field, int point) const
{
    if (auto* parameter = processor_.getState().getParameter (ids::advPoint (envelope_, point, field)))
        return parameter->convertFrom0to1 (parameter->getValue());

    return 0.0f;
}

int AdvGraph::pointCount() const
{
    if (auto* parameter = processor_.getState().getParameter (ids::adv (envelope_, "Points")))
        return juce::jlimit (2, dsp::MultiEnvelope::kMaxPoints,
                             static_cast<int> (std::lround (
                                 parameter->convertFrom0to1 (parameter->getValue()))));

    return 2;
}

void AdvGraph::paint (juce::Graphics& g)
{
    const auto tint = ui::design::tintFor (palette_.accent, envelope_ == 0 ? 1 : 3);
    auto plot = getLocalBounds().toFloat().reduced (6.0f);

    g.setColour (palette_.background.darker (0.25f));
    g.fillRoundedRectangle (plot, 4.0f);

    const int count = pointCount();

    // The x axis is the envelope's own length, so a short envelope fills the
    // graph exactly as a long one does -- what is being read here is the
    // shape, and a picture whose scale changes with a knob is unreadable.
    double total = 0.0;

    for (int p = 0; p < count; ++p)
        total += static_cast<double> (plain ("Time", p));

    total = std::max (total, 1.0e-4);

    std::array<float, static_cast<std::size_t> (dsp::MultiEnvelope::kMaxPoints) + 1> xs {};
    std::array<float, static_cast<std::size_t> (dsp::MultiEnvelope::kMaxPoints) + 1> ys {};

    // Slot 0 is the gate's own start at level 0; slot p+1 is point p.
    xs[0] = plot.getX();
    ys[0] = plot.getBottom();

    double elapsed = 0.0;

    for (int p = 0; p < count; ++p)
    {
        elapsed += static_cast<double> (plain ("Time", p));

        const auto i = static_cast<std::size_t> (p) + 1;
        xs[i] = plot.getX() + plot.getWidth() * static_cast<float> (elapsed / total);
        ys[i] = plot.getBottom() - plot.getHeight() * plain ("Level", p);
    }

    const int sustainIndex = [this]
    {
        if (auto* parameter = processor_.getState().getParameter (ids::adv (envelope_, "Sustain")))
            return static_cast<int> (std::lround (
                       parameter->convertFrom0to1 (parameter->getValue()))) - 1;

        return 0;
    }();

    const int loopStart = [this]
    {
        if (auto* parameter = processor_.getState().getParameter (ids::adv (envelope_, "LoopStart")))
            return static_cast<int> (std::lround (
                       parameter->convertFrom0to1 (parameter->getValue()))) - 1;

        return 0;
    }();

    const bool looping = [this]
    {
        if (auto* parameter = processor_.getState().getParameter (ids::adv (envelope_, "Loop")))
            return parameter->getValue() > 0.5f;

        return false;
    }();

    // The loop region first, under everything.
    if (looping && loopStart < count - 1)
    {
        const float from = xs[static_cast<std::size_t> (juce::jlimit (0, count, loopStart + 1))];
        const float to = xs[static_cast<std::size_t> (count)];

        g.setColour (tint.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (from, plot.getY(), std::max (1.0f, to - from),
                                            plot.getHeight()));
    }

    // The curve, leg by leg, in the library's own arithmetic: a segment aims
    // past its destination and approaches it (positive tension) or aims past
    // its origin and recedes (negative), which is `dsp::Adsr::overshootFor`.
    juce::Path curve;
    curve.startNewSubPath (xs[0], ys[0]);

    for (int p = 0; p < count; ++p)
    {
        const auto i = static_cast<std::size_t> (p);
        const double from = p == 0 ? 0.0 : static_cast<double> (plain ("Level", p - 1));
        const double to = static_cast<double> (plain ("Level", p));
        const double tension = static_cast<double> (plain ("Tens", p));
        const double distance = to - from;

        const int steps = 24;

        for (int s = 1; s <= steps; ++s)
        {
            const double u = static_cast<double> (s) / steps;
            double level = to;

            if (std::abs (distance) >= 1.0e-12)
            {
                const double overshoot = dsp::Adsr::overshootFor (tension);
                const double ratio = (overshoot - 1.0) / overshoot;
                const double target = tension < 0.0 ? from - distance * (overshoot - 1.0)
                                                    : to + distance * (overshoot - 1.0);

                level = target + (from - target) * std::pow (ratio, tension < 0.0 ? -u : u);
            }

            const float x = xs[i] + (xs[i + 1] - xs[i]) * static_cast<float> (u);
            curve.lineTo (x, plot.getBottom() - plot.getHeight() * static_cast<float> (level));
        }
    }

    g.setColour (tint.withAlpha (0.22f));
    {
        juce::Path filled (curve);
        filled.lineTo (xs[static_cast<std::size_t> (count)], plot.getBottom());
        filled.lineTo (plot.getX(), plot.getBottom());
        filled.closeSubPath();
        g.fillPath (filled);
    }

    g.setColour (tint);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // The points, with the selected one filled and the sustain ringed.
    for (int p = 0; p < count; ++p)
    {
        const auto i = static_cast<std::size_t> (p) + 1;
        const float radius = p == selected_ ? 4.5f : 3.0f;

        g.setColour (p == selected_ ? palette_.text : tint);
        g.fillEllipse (xs[i] - radius, ys[i] - radius, radius * 2.0f, radius * 2.0f);

        if (p == sustainIndex)
        {
            g.setColour (palette_.text.withAlpha (0.85f));
            g.drawEllipse (xs[i] - 7.0f, ys[i] - 7.0f, 14.0f, 14.0f, 1.4f);
        }
    }

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (total, 2) + " s",
                getLocalBounds().reduced (10, 6), juce::Justification::topRight);
}

void AdvGraph::mouseDown (const juce::MouseEvent& event)
{
    auto plot = getLocalBounds().toFloat().reduced (6.0f);
    const int count = pointCount();

    double total = 0.0;

    for (int p = 0; p < count; ++p)
        total += static_cast<double> (plain ("Time", p));

    total = std::max (total, 1.0e-4);

    int nearest = 0;
    float best = std::numeric_limits<float>::max();
    double elapsed = 0.0;

    for (int p = 0; p < count; ++p)
    {
        elapsed += static_cast<double> (plain ("Time", p));

        const float x = plot.getX() + plot.getWidth() * static_cast<float> (elapsed / total);
        const float y = plot.getBottom() - plot.getHeight() * plain ("Level", p);
        const float distance = event.position.getDistanceFrom (juce::Point<float> (x, y));

        if (distance < best)
        {
            best = distance;
            nearest = p;
        }
    }

    if (nearest == selected_)
        return;

    selected_ = nearest;
    repaint();

    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

// ---------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------

StrydaEditor::StrydaEditor (StrydaProcessor& owner)
    : juce::AudioProcessorEditor (&owner),
      processor_ (owner),
      tooltips_ (*this, owner.areTooltipsEnabled()),
      header_ (owner.getState(), "STRYDA", "FM synthesiser", nullptr, palette_),
      matrix_ (owner, palette_)
{
    setLookAndFeel (&lookAndFeel_);

    addAndMakeVisible (header_);
    addAndMakeVisible (matrix_);

    header_.onTooltipsToggled = [this] (bool enabled)
    {
        processor_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_.setTooltipsEnabled (owner.areTooltipsEnabled());

    header_.onSwapRequested = [this] { processor_.getAbCompare().swapSlots(); };
    header_.onCopyRequested = [this] { processor_.getAbCompare().copyToOtherSlot(); };

    // Master trim, oversampling and render quality, in the header where every
    // other plugin in the suite keeps them. They were built in F3 and simply
    // never attached, so the panel had no way to turn oversampling off -- which
    // is the first thing anybody reaches for when an instrument costs too much,
    // and the first thing the user asked for from the rig. There is no dry/wet
    // on an instrument, so the mix id is null.
    header_.attachSuiteControls (owner.getState(), nullptr, ids::master,
                                 ids::oversampling, ids::renderOversampling);

    // Six strips, one per operator, each in its own hue so the eye can find a
    // row in the matrix and the strip it belongs to without reading a number.
    // Short enough to fit the cell at the narrowest window the editor allows.
    // A truncated caption reads as a bug, and "CHARA..." was exactly that.
    //
    // Three rows of four: what the operator IS, where it SITS, and how it
    // MOVES. Key scaling and velocity are on their own plate, because they are
    // set once per patch and then never touched.
    static const char* const captions[] { "RATIO", "FINE", "CHAR", "FOLD",
                                          "LEVEL", "PAN", "FORMANT", "WIDTH",
                                          "ATK", "DECAY", "SUS", "REL" };
    static const char* const names[] { "Ratio", "Fine", "Character", "Fold",
                                       "Level", "Pan", "Formant", "Width",
                                       "Attack", "Decay", "Sustain", "Release" };

    for (int op = 0; op < kNumOperators; ++op)
    {
        const auto tint = ui::design::tintFor (palette_.accent, op);

        for (int i = 0; i < 12; ++i)
        {
            auto& control = addControl (ids::op (op, names[i]), captions[i], tint,
                                        i == 0 || i == 4 ? ui::design::Emphasis::lead
                                                         : ui::design::Emphasis::normal);
            strips_[static_cast<std::size_t> (op)].push_back (&control);
        }

        auto box = std::make_unique<juce::ComboBox>();
        ui::styleChoice (*box, palette_, tint);
        box->addItemList (choices::operatorMode, 1);
        box->setTooltip (
            "Normal is an operator like any other.\n\n"
            "Formant makes it a self-contained resonance at the Formant frequency, whatever "
            "note is played -- two carriers on adjacent harmonics, crossfaded by the fractional "
            "part, so the peak sits between them rather than snapping to one. That is a whole "
            "two-operator ModFM pair collapsed into one slot, which is what makes a three-formant "
            "vowel reachable on six operators.\n\n"
            "It needs room: the formant has to sit about ten harmonics above the note, or the "
            "resonance skirt folds through DC and drags the centre sharp. Measured, at eight "
            "harmonics it is 20 cents out; at eleven, 3.");

        modeAttachments_[static_cast<std::size_t> (op)]
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::op (op, "Mode"), *box);

        addAndMakeVisible (*box);
        modeBoxes_[static_cast<std::size_t> (op)] = std::move (box);

        // ---- F9: the shape choice and the wave it produces ------------------

        auto shape = std::make_unique<juce::ComboBox>();
        ui::styleChoice (*shape, palette_, tint);
        shape->addItemList (choices::fmShape, 1);
        shape->setTooltip (
            "The operator's waveform. **Sine is the default and it is the one to reach for**: "
            "in FM the harmonics of a non-sine operator MULTIPLY the sideband ladder rather "
            "than adding to it, so a Saw modulator at index 4 is a different order of "
            "brightness -- and of aliasing -- than a sine at the same index.\n\n"
            "They are safe to use anyway, because each shape is a fixed sum of harmonics "
            "(2 for Bright, 8 for Triangle and Square, 16 for Saw) and the bandwidth predictor "
            "is told the number, so the index cap protects them by arithmetic rather than by "
            "luck. Expect the cap to bite sooner on Saw, and expect Auto oversampling to be "
            "worth the CPU.\n\n"
            "As a CARRIER a shape is a straightforward tone change. As a MODULATOR it is the "
            "loudest control on the panel.");

        shapeAttachments_[static_cast<std::size_t> (op)]
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::op (op, "Shape"), *shape);

        addAndMakeVisible (*shape);
        shapeBoxes_[static_cast<std::size_t> (op)] = std::move (shape);

        waves_[static_cast<std::size_t> (op)]
            = std::make_unique<OperatorWave> (owner, op, palette_);
        addAndMakeVisible (*waves_[static_cast<std::size_t> (op)]);

        // Key scaling and velocity, on their own plate.
        static const char* const scalingCaptions[] { "BREAK", "BELOW", "ABOVE",
                                                     "VEL LVL", "VEL IDX" };
        static const char* const scalingNames[] { "KeyBreak", "KeyLeft", "KeyRight",
                                                  "VelLevel", "VelIndex" };

        for (int i = 0; i < 5; ++i)
        {
            // No per-control caption: the column heading is drawn once by
            // paint(). Six repeats of the same five words is six times the ink
            // for the same information, and it is what pushed the first version
            // of this grid out of alignment.
            auto& control = addControl (ids::op (op, scalingNames[i]), {},
                                        tint, ui::design::Emphasis::trim);
            control.caption.setVisible (false);
            scaling_[static_cast<std::size_t> (op)].push_back (&control);
        }

        juce::ignoreUnused (scalingCaptions);

        scaling_[static_cast<std::size_t> (op)][1]->knob.setTooltip (
            "How operator " + juce::String (op + 1) + " changes as you play BELOW the break "
            "point, per octave. Positive is louder and brighter, negative is quieter and "
            "duller; 0 is exactly flat.\n\n"
            "On a carrier you hear a volume change; on a modulator you hear a timbral one. "
            "That is what makes a patch behave like an instrument across the keyboard instead "
            "of like the same sound transposed.");

        scaling_[static_cast<std::size_t> (op)][2]->knob.setTooltip (
            "The same, ABOVE the break point. Pulling a modulator down as you go up the "
            "keyboard is the classic use: it stops high notes turning to gravel, and it is "
            "the hand-dialled ancestor of the Index cap.");
    }

    // Tooltips that say what the two unusual controls actually do.
    for (int op = 0; op < kNumOperators; ++op)
    {
        strips_[static_cast<std::size_t> (op)][0]->knob.setTooltip (
            "Operator " + juce::String (op + 1) + "'s frequency, as a multiple of the note.\n\n"
            "In FM the ratio IS the interval: sidebands land at the carrier plus and minus "
            "whole multiples of the modulator, so a simple ratio fuses into one instrument "
            "and an awkward one becomes a bell. 1, 2 and 3 are harmonic; 3.5 is the classic "
            "bell; 11 and above is metal.");

        strips_[static_cast<std::size_t> (op)][3]->knob.setTooltip (
            "Phase distortion: bends the phase ramp so the waveform races through part of its "
            "cycle and crawls through the rest. A sine grows a leading edge and turns saw-like, "
            "which is a filter-sweep gesture from one oscillator and no filter.\n\n"
            "It is phase modulation with a piecewise modulator locked to this operator's own "
            "cycle, which is why it costs a transfer function rather than an operator. Exactly "
            "the identity at 0.");

        strips_[static_cast<std::size_t> (op)][2]->knob.setTooltip (
            "One knob from classic FM to ModFM.\n\n"
            "At 0 this is exactly the phase modulation of the eighties -- bit for bit, not an "
            "approximation. At 1 it is ModFM, whose partials fall away smoothly instead of "
            "each one pumping through the Bessel nulls, so the modulation index behaves like a "
            "filter opening rather than a timbre flickering.\n\n"
            "For a bass, the top end is usually the one that behaves. It costs an exponential "
            "per sample; at 0 that is branched out entirely and costs nothing.");
    }

    globals_.push_back (&addControl (ids::master, "MASTER",
                                     palette_.accentBright, ui::design::Emphasis::lead));

    ui::styleChoice (indexCapBox_, palette_, palette_.secondary);
    indexCapBox_.addItemList (choices::indexCap, 1);
    indexCapBox_.setTooltip (
        "Holds the predicted top of the spectrum under the internal Nyquist by scaling every "
        "index down -- key scaling, derived from the arithmetic instead of dialled in by hand.\n\n"
        "Off leaves everything alone. Soft leans towards the safe setting; Hard goes all the way "
        "there. When it is not binding it is exactly inert: not almost, exactly -- the same "
        "samples to the last bit.\n\n"
        "Measured: a two-operator pair at index 16 and ratio 7 reads -114.6 dB of aliasing at "
        "x4, and +6.2 dB at index 64. Oversampling alone does not cover the top of the range; "
        "this does.");
    addAndMakeVisible (indexCapBox_);

    indexCapAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        owner.getState(), ids::indexCap, indexCapBox_);

    // ---- F5: the filter, the sub lane and the unison stack -----------------

    {
        const auto tint = palette_.secondary;

        static const char* const filterCaptions[] { "CUTOFF", "RESO", "MORPH", "KEY",
                                                    "ENV", "DRIVE", "SING",
                                                    "ATK", "DECAY", "SUS", "REL" };
        const juce::String filterIds[] {
            ids::filterCutoff, ids::filterReso, ids::filterMorph, ids::filterKeyTrack,
            ids::filterEnv, ids::filterDrive, ids::filterSing,
            ids::filterAttack, ids::filterDecay, ids::filterSustain, ids::filterRelease
        };

        for (int i = 0; i < 11; ++i)
            filter_.push_back (&addControl (filterIds[i], filterCaptions[i], tint,
                                            i == 0 ? ui::design::Emphasis::lead
                                                   : ui::design::Emphasis::trim));

        filter_[0]->knob.setTooltip (
            "One filter per voice, after the whole operator matrix and before the sub lane.\n\n"
            "At 20 kHz it is not merely transparent, it is SKIPPED -- bit for bit, the same "
            "samples a build without a filter would produce. So a patch that does not want one "
            "pays nothing for it, and an old project cannot change the day this shipped.");

        filter_[2]->knob.setTooltip (
            "Lowpass at 0, bandpass in the middle, highpass at 1, crossfaded rather than "
            "switched -- so it can be swept and automated without a click.");

        filter_[4]->knob.setTooltip (
            "How far the filter envelope opens the cutoff, in OCTAVES, positive or negative. "
            "This is the classic bass gesture: a fast decay on a couple of octaves is the "
            "pluck, a slow one is the sweep.");

        filter_[6]->knob.setTooltip (
            "Sing: pushes the resonance towards self-oscillation, where the filter becomes a "
            "sine of its own at the cutoff. On a growl it adds a formant that tracks the "
            "filter rather than the note.");

        static const char* const subCaptions[] { "LEVEL", "ATK", "DECAY", "SUS", "REL" };
        const juce::String subIds[] { ids::subLevel, ids::subAttack, ids::subDecay,
                                      ids::subSustain, ids::subRelease };

        for (int i = 0; i < 5; ++i)
            sub_.push_back (&addControl (subIds[i], subCaptions[i], tint,
                                         i == 0 ? ui::design::Emphasis::lead
                                                : ui::design::Emphasis::trim));

        sub_[0]->knob.setTooltip (
            "A sine or triangle under the patch that goes through NOTHING: not the operator "
            "matrix, not the filter, and not the mangle chain when that arrives. It has its "
            "own envelope and its own level.\n\n"
            "That is the difference between a bass that survives a club system and one that "
            "collapses the moment the growl bites -- the low end stops being something the "
            "distortion can eat. At 0 the whole lane is skipped.\n\n"
            "Only ONE copy of a unison stack carries it, so eight voices do not give you "
            "eight detuned subs fighting over the same octave.");

        ui::styleChoice (subOctaveBox_, palette_, tint);
        subOctaveBox_.addItemList (choices::subOctave, 1);
        subOctaveBox_.setTooltip ("Where the sub sits relative to the note. One octave down is "
                                  "the usual place for a DnB bass; two is for when the patch "
                                  "itself is already low.");
        addAndMakeVisible (subOctaveBox_);
        subOctaveAttachment_
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::subOctave, subOctaveBox_);

        ui::styleChoice (subShapeBox_, palette_, tint);
        subShapeBox_.addItemList (choices::subShape, 1);
        subShapeBox_.setTooltip ("Sine is the clean fundamental. Triangle adds quiet odd "
                                 "harmonics, which is what makes a sub audible on a phone "
                                 "speaker that cannot reproduce the fundamental at all.");
        addAndMakeVisible (subShapeBox_);
        subShapeAttachment_
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::subShape, subShapeBox_);

        static const char* const unisonCaptions[] { "VOICES", "DETUNE", "SPREAD", "INDEX" };
        const juce::String unisonIds[] { ids::unison, ids::unisonDetune,
                                         ids::unisonSpread, ids::unisonIndex };

        for (int i = 0; i < 4; ++i)
            unison_.push_back (&addControl (unisonIds[i], unisonCaptions[i], tint,
                                            i == 0 ? ui::design::Emphasis::lead
                                                   : ui::design::Emphasis::trim));

        unison_[0]->knob.setTooltip (
            "How many copies of the patch one note plays.\n\n"
            "They come out of the same voice budget: at 4 voices of unison and 8 of polyphony "
            "you get two notes. That is the cost, and it is worth knowing before a chord "
            "steals its own notes.\n\n"
            "At 1 the whole stack is exactly inert -- the detune, spread and index spread have "
            "nothing to spread across, so the voice is bit-identical to unison off.");

        unison_[3]->knob.setTooltip (
            "**Index spread, and it is the thickest thing here.** Detuning a stack gives you "
            "the same timbre several times a few cents apart. Offsetting each copy's "
            "modulation index instead gives you several DIFFERENT timbres beating against each "
            "other, which is what a reese actually is.\n\n"
            "Only cells that are already doing something are offset, so this cannot switch on "
            "a modulation path the patch never asked for. Watch the bandwidth readout: spread "
            "pushes the loudest copy's index up as well as down.");
    }

    ui::styleChoice (presetBox_, palette_, palette_.accent);
    for (int i = 0; i < owner.getNumPrograms(); ++i)
        presetBox_.addItem (owner.getProgramName (i), i + 1);
    presetBox_.setSelectedId (owner.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox_.setTooltip ("Presets. Each one starts from the defaults, so a control a preset "
                           "does not mention is at its default rather than wherever the last "
                           "patch left it.");
    presetBox_.onChange = [this]
    {
        processor_.setCurrentProgram (presetBox_.getSelectedId() - 1);
    };
    addAndMakeVisible (presetBox_);

    styleCaption (bandwidth_, palette_);
    bandwidth_.setJustificationType (juce::Justification::topLeft);
    bandwidth_.setFont (juce::FontOptions (13.0f));
    bandwidth_.setTooltip (
        "An UPPER BOUND on where the spectrum ends, in closed form from the ratios and "
        "indices, against the internal Nyquist. Assumes A4; a note an octave up doubles it.\n\n"
        "For one modulator on one carrier it is exact -- measured against a rendered spectrum "
        "across 45 combinations, it agrees to the bin. For a STACK -- a modulator that is "
        "itself modulated -- it over-estimates, measured at about 2x at two deep and up to 19x "
        "at three, because each stage widens a signal that has already been widened. So read "
        "it as a ceiling, not a reading: under it you are certainly clean, over it you might "
        "still be fine.\n\n"
        "If it goes far over and you can hear grit, turn an index down, raise the oversampling, "
        "or switch the Index cap on.");
    addAndMakeVisible (bandwidth_);

    styleCaption (voices_, palette_);
    voices_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (voices_);

    // ---- F6: the ratio sequencer and the six braids -------------------------

    {
        const auto tint = palette_.accent;

        for (int i = 0; i < RatioSequencer::kMaxSteps; ++i)
            steps_.push_back (&addControl (ids::step (i), juce::String (i + 1), tint,
                                           ui::design::Emphasis::trim));

        steps_[0]->knob.setTooltip (
            "Each step is a RATIO, not a level.\n\n"
            "In FM the ratio is the interval -- sidebands land at the carrier plus and minus "
            "whole multiples of the modulator -- so stepping a modulator's ratio does not "
            "sweep a timbre, it swaps one harmonic identity for another. That is the neuro "
            "growl as a rhythm rather than as a filter.\n\n"
            "A step change never resets the phase: the accumulator runs on and the spectrum "
            "jumps. Glide interpolates the ratio between steps, which slides the whole "
            "sideband ladder rather than crossfading two of them.\n\n"
            "Every step starts at 1.0, so switching RUN on with an untouched pattern changes "
            "nothing -- move a step and you hear the rhythm appear.");

        static const char* const seqCaptions[] { "STEPS", "GLIDE" };
        const juce::String seqIds[] { ids::seqLength, ids::seqGlide };

        for (int i = 0; i < 2; ++i)
            seqControls_.push_back (&addControl (seqIds[i], seqCaptions[i], tint,
                                                 ui::design::Emphasis::normal));

        seqOnButton_.setClickingTogglesState (true);
        seqOnButton_.setTooltip (
            "Runs the pattern. Locked to the host's transport when it is playing, so the "
            "jumps land on the bar; free-running at the division's rate when it is not, so "
            "you can hear the pattern with the transport stopped.");
        addAndMakeVisible (seqOnButton_);
        seqOnAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            owner.getState(), ids::seqOn, seqOnButton_);

        ui::styleChoice (seqTargetBox_, palette_, tint);
        seqTargetBox_.addItemList (choices::seqTarget, 1);
        seqTargetBox_.setTooltip (
            "Which operator's ratio the pattern drives. Off leaves every operator on the "
            "ratio its own knob says.\n\n"
            "Point it at a MODULATOR for a growl that changes harmonic identity in time; "
            "point it at the carrier for a melodic line made of ratios rather than of notes.");
        addAndMakeVisible (seqTargetBox_);
        seqTargetAttachment_
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::seqTarget, seqTargetBox_);

        ui::styleChoice (seqDivisionBox_, palette_, tint);
        for (int i = 0; i < dsp::numDivisions; ++i)
            seqDivisionBox_.addItem (dsp::divisions[static_cast<std::size_t> (i)].name, i + 1);
        seqDivisionBox_.setTooltip (
            "How long a step lasts, as a note value. 1/16 against the drums is where a neuro "
            "bass usually lives.\n\n"
            "The engine cuts its sample loop at the step edge, so a jump lands exactly on the "
            "beat rather than up to a buffer late -- and the render is identical at 64 and at "
            "512 samples a block.");
        addAndMakeVisible (seqDivisionBox_);
        seqDivisionAttachment_
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::seqDivision, seqDivisionBox_);

        for (int i = 0; i < braids::kCount; ++i)
        {
            const auto& braid = braids::table()[static_cast<std::size_t> (i)];
            auto& button = braidButtons_[static_cast<std::size_t> (i)];

            button.setButtonText (braid.name);
            button.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.18f));
            button.setColour (juce::TextButton::textColourOffId, palette_.accent);
            button.setTooltip (juce::String (braid.description)
                                 + "\n\nPressing this WRITES the matrix and the operator "
                                   "levels and then gets out of the way -- every cell is still "
                                   "a knob, nothing is locked, and your ratios, envelopes and "
                                   "Character are left exactly as they were.");
            button.onClick = [this, i] { processor_.applyBraid (i); };
            addAndMakeVisible (button);
        }
    }

    // ---- F7: Split, the vowel lane and the mangle chain ---------------------

    {
        const auto tint = ui::design::tintFor (palette_.accent, 4);

        static const char* const vowelCaptions[] { "SPLIT", "MIX", "VOWEL",
                                                   "TRACT", "SHARP", "GLIDE", "STEPS" };
        const juce::String vowelIds[] { ids::split, ids::vowelMix, ids::vowelMorph,
                                        ids::vowelTract, ids::vowelSharp,
                                        ids::vowelSeqGlide, ids::vowelSeqLength };

        for (int i = 0; i < 7; ++i)
            vowel_.push_back (&addControl (vowelIds[i], vowelCaptions[i], tint,
                                           i <= 1 ? ui::design::Emphasis::lead
                                                  : ui::design::Emphasis::normal));

        vowel_[0]->knob.setTooltip (
            "SPLIT keeps the bottom of the sound out of everything on this page. Below the "
            "corner the signal goes round the vowel lane and the whole mangle chain untouched, "
            "so a growl can be destroyed above 150 Hz while the fundamental stays exactly "
            "where the sub lane put it.\n\n"
            "At 0 it is not merely flat, it is SKIPPED -- which matters, because a "
            "Linkwitz-Riley crossover summed back together is an allpass rather than an "
            "identity. That is why this control did not ship a phase earlier: with nothing "
            "between the two bands it would have cost phase and bought nothing.");

        vowel_[1]->knob.setTooltip (
            "The vowel lane: three resonances placed where a vocal tract puts them, swept by "
            "VOWEL through ee - eh - ah - oh - oo. This is what makes an FM growl sound like "
            "it is saying something rather than merely buzzing.\n\n"
            "At mix 0 the whole lane is skipped. TRACT is the length of the throat -- short "
            "is a child, long is a cathedral -- and SHARP is how resonant each formant is.");

        for (int i = 0; i < RatioSequencer::kMaxSteps; ++i)
            vowelSteps_.push_back (&addControl (ids::vowelStep (i), juce::String (i + 1),
                                                tint, ui::design::Emphasis::trim));

        vowelSteps_[0]->knob.setTooltip (
            "Sixteen steps of VOWEL position, on their own division, so the bass can talk in "
            "time without the ratio sequencer having to agree with it.\n\n"
            "Glide slides between vowels rather than cutting, which is the difference between "
            "a word and a stutter.");

        vowelSeqButton_.setClickingTogglesState (true);
        vowelSeqButton_.setTooltip ("Runs the vowel pattern, locked to the transport.");
        addAndMakeVisible (vowelSeqButton_);
        vowelSeqAttachment_
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                owner.getState(), ids::vowelSeqOn, vowelSeqButton_);

        ui::styleChoice (vowelDivisionBox_, palette_, tint);
        for (int i = 0; i < dsp::numDivisions; ++i)
            vowelDivisionBox_.addItem (dsp::divisions[static_cast<std::size_t> (i)].name, i + 1);
        vowelDivisionBox_.setTooltip ("How long a vowel step lasts.");
        addAndMakeVisible (vowelDivisionBox_);
        vowelDivisionAttachment_
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::vowelSeqDiv, vowelDivisionBox_);

        static const char* const mangleCaptions[] {
            "FOLD", "BITS", "CRUSH", "RATE",
            "COMB", "COMB Hz", "C FBK",
            "PHASE", "PHS Hz", "P FBK",
            "DRIVE",
            "THRESH", "RATIO", "ATK", "REL", "MAKEUP"
        };
        const juce::String mangleIds[] {
            ids::fold, ids::crushBits, ids::crushAmount, ids::downsample,
            ids::combMix, ids::combHz, ids::combFeedback,
            ids::phaserMix, ids::phaserHz, ids::phaserFeedback,
            ids::mangleDrive,
            ids::compThreshold, ids::compRatio, ids::compAttack,
            ids::compRelease, ids::compMakeup
        };

        for (int i = 0; i < 16; ++i)
            mangle_.push_back (&addControl (mangleIds[i], mangleCaptions[i],
                                            palette_.secondary,
                                            ui::design::Emphasis::trim));

        mangle_[0]->knob.setTooltip (
            "A sine wavefolder: past the first fold the curve turns back on itself and each "
            "extra bit of level adds another crease rather than more loudness. Antialiased "
            "(ADAA), so it does not spray images the way a naive folder does. Exactly the "
            "identity at 0.");

        mangle_[1]->knob.setTooltip (
            "BITS and CRUSH and RATE are the ONE place in this plugin where aliasing is the "
            "instrument rather than a defect, so they run at the host rate with no "
            "oversampling and no antialiasing at all -- the folded-back images ARE the sound "
            "(CLAUDE.md's documented exception).\n\n"
            "16 bits and rate 1 are skipped outright.");

        mangle_[10]->knob.setTooltip (
            "A biased tanh, antialiased, with an auto-trim so the knob changes tone rather "
            "than loudness -- loudness sells distortion and we do not want to be fooled. The "
            "bias arrives with the drive, so the even harmonics come in with it rather than "
            "being there from the start. A 12 Hz first-order blocker after it stops the bias's "
            "DC reaching a sub.");

        mangle_[12]->knob.setTooltip (
            "At 1 : 1 the compressor is the identity function and is SKIPPED -- not run with "
            "a ratio that happens to do nothing. Above it, this is the glue that makes a "
            "mangled growl sit still enough to sit in a mix.");
    }

    // ---- F8: the modulation layer ------------------------------------------
    //
    // Two ADV envelopes with a graph each, two LFOs, four macros and eight
    // slots. **Every slot defaults to Off**, so the whole layer is skipped and
    // a patch that uses none of it is bit-identical to a build without it.

    for (int e = 0; e < 2; ++e)
    {
        const auto slot = static_cast<std::size_t> (e);
        const auto tint = ui::design::tintFor (palette_.accent, e == 0 ? 1 : 3);

        advGraphs_[slot] = std::make_unique<AdvGraph> (owner, e, palette_);
        addAndMakeVisible (*advGraphs_[slot]);
        advGraphs_[slot]->onSelectionChanged = [this, e] { retargetAdvPoint (e); };

        advShape_[slot].push_back (&addControl (ids::adv (e, "Points"), "POINTS", tint,
                                                ui::design::Emphasis::trim));
        advShape_[slot].push_back (&addControl (ids::adv (e, "Sustain"), "SUSTAIN", tint,
                                                ui::design::Emphasis::trim));
        advShape_[slot].push_back (&addControl (ids::adv (e, "LoopStart"), "LOOP FROM", tint,
                                                ui::design::Emphasis::trim));

        advShape_[slot][0]->knob.setTooltip (
            "How many breakpoints this envelope uses. Points past the last one are flat "
            "level-0 legs, so raising this adds time rather than a shape you did not ask for.");

        advShape_[slot][1]->knob.setTooltip (
            "Which breakpoint the envelope holds at while the key is down; the rest plays on "
            "release. **Without one an ADV envelope is a one-shot** -- useful for a pluck, "
            "wrong for a held bass.");

        advShape_[slot][2]->knob.setTooltip (
            "Where the loop returns to, while LOOP is on. The loop runs from here to the last "
            "point and back, which is how a sustained growl gets a rhythm of its own without "
            "spending an LFO on it.");

        advLoopButtons_[slot].setClickingTogglesState (true);
        advLoopButtons_[slot].setTooltip (
            "Loops the region from LOOP FROM to the last point, forwards then backwards, for as "
            "long as the note is held.");
        addAndMakeVisible (advLoopButtons_[slot]);
        advLoopAttachments_[slot]
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                owner.getState(), ids::adv (e, "Loop"), advLoopButtons_[slot]);

        // The three point knobs. They are re-attached as the graph's selection
        // moves rather than duplicated forty-eight times.
        static const char* const pointCaptions[] { "TIME", "LEVEL", "CURVE" };

        for (int k = 0; k < 3; ++k)
        {
            const auto index = static_cast<std::size_t> (k);
            auto& caption = advPoints_[slot].captions[index];
            auto& knob = advPoints_[slot].knobs[index];

            caption.setText (pointCaptions[k], juce::dontSendNotification);
            styleCaption (caption, palette_);
            addAndMakeVisible (caption);

            knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            ui::styleKnob (knob, palette_, tint, ui::design::Emphasis::normal);
            knob.setNumDecimalPlacesToDisplay (3);
            knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64,
                                  ui::design::kValueHeight);
            addAndMakeVisible (knob);
        }

        advPoints_[slot].knobs[0].setTooltip ("How long the leg into the selected breakpoint takes.");
        advPoints_[slot].knobs[1].setTooltip ("Where the selected breakpoint sits, 0 to 1.");
        advPoints_[slot].knobs[2].setTooltip (
            "How the leg bends: positive aims past the destination and eases in, negative aims "
            "past the origin and accelerates away. Zero is a straight line. Same arithmetic as "
            "every other envelope in the suite.");

        ui::styleName (advPoints_[slot].heading, palette_, tint);
        addAndMakeVisible (advPoints_[slot].heading);

        retargetAdvPoint (e);
    }

    for (int l = 0; l < 2; ++l)
    {
        const auto slot = static_cast<std::size_t> (l);
        const auto tint = ui::design::tintFor (palette_.accent, l == 0 ? 0 : 5);

        lfoWaveBoxes_[slot] = std::make_unique<juce::ComboBox>();
        ui::styleChoice (*lfoWaveBoxes_[slot], palette_, tint);
        lfoWaveBoxes_[slot]->addItemList (choices::lfoWave, 1);
        lfoWaveBoxes_[slot]->setTooltip (
            "The shape. The two random waves step at the LFO's rate -- SMOOTH RANDOM "
            "interpolates between steps, RANDOM jumps.");
        addAndMakeVisible (*lfoWaveBoxes_[slot]);
        lfoWaveAttachments_[slot]
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::lfo (l, "Wave"), *lfoWaveBoxes_[slot]);

        lfoControls_[slot].push_back (&addControl (ids::lfo (l, "Rate"), "RATE", tint));
        lfoControls_[slot].push_back (&addControl (ids::lfo (l, "Smooth"), "SMOOTH", tint,
                                                   ui::design::Emphasis::trim));
        lfoControls_[slot].push_back (&addControl (ids::lfo (l, "Phase"), "PHASE", tint,
                                                   ui::design::Emphasis::trim));

        lfoControls_[slot][0]->knob.setTooltip (
            "Free-running rate. Ignored while SYNC is on, where the division sets it from the "
            "session tempo instead.");

        lfoControls_[slot][1]->knob.setTooltip (
            "Rounds the waveform's corners. At the top it will swallow a square whole, which "
            "is a way of getting a slow rise out of a fast shape.");

        lfoControls_[slot][2]->knob.setTooltip (
            "Where in the cycle the LFO starts, while RETRIG is on. Two LFOs on the same rate "
            "at different phases is the cheapest way to make a growl move in two directions "
            "at once.");

        lfoSyncButtons_[slot].setClickingTogglesState (true);
        lfoSyncButtons_[slot].setTooltip (
            "Locks the rate to the session tempo at the division beside it. The rate is "
            "resolved once per block from the host's tempo, so every voice agrees about it.");
        addAndMakeVisible (lfoSyncButtons_[slot]);
        lfoSyncAttachments_[slot]
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                owner.getState(), ids::lfo (l, "Sync"), lfoSyncButtons_[slot]);

        lfoDivisionBoxes_[slot] = std::make_unique<juce::ComboBox>();
        ui::styleChoice (*lfoDivisionBoxes_[slot], palette_, tint);
        for (int i = 0; i < dsp::numDivisions; ++i)
            lfoDivisionBoxes_[slot]->addItem (dsp::divisions[static_cast<std::size_t> (i)].name,
                                              i + 1);
        lfoDivisionBoxes_[slot]->setTooltip ("How long one LFO cycle lasts, while SYNC is on.");
        addAndMakeVisible (*lfoDivisionBoxes_[slot]);
        lfoDivisionAttachments_[slot]
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::lfo (l, "Div"), *lfoDivisionBoxes_[slot]);

        lfoRetrigButtons_[slot].setClickingTogglesState (true);
        lfoRetrigButtons_[slot].setTooltip (
            "Restarts the LFO at PHASE on every note. Off, it free-runs -- so two notes played "
            "a bar apart are at different points in the cycle, which is what makes a pad "
            "breathe and a bass line vary.");
        addAndMakeVisible (lfoRetrigButtons_[slot]);
        lfoRetrigAttachments_[slot]
            = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                owner.getState(), ids::lfo (l, "Retrig"), lfoRetrigButtons_[slot]);
    }

    for (int m = 0; m < kNumMacros; ++m)
        macros_.push_back (&addControl (ids::macro (m), "MACRO " + juce::String (m + 1),
                                        palette_.secondary));

    macros_[0]->knob.setTooltip (
        "A knob with no job until a slot gives it one. Assign the same macro to four "
        "destinations with different amounts -- some negative -- and one hand movement opens "
        "the filter, deepens the matrix and detunes an operator together. This is where a "
        "growl becomes playable rather than programmed.");

    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto slot = static_cast<std::size_t> (s);

        slotSourceBoxes_[slot] = std::make_unique<juce::ComboBox>();
        ui::styleChoice (*slotSourceBoxes_[slot], palette_, palette_.secondary);
        slotSourceBoxes_[slot]->addItemList (choices::modSources, 1);
        slotSourceBoxes_[slot]->setTooltip (
            "What moves this slot. **Off costs nothing**: a slot without a source, a "
            "destination and an amount is not merely zero, it is never read -- and if no slot "
            "has all three the whole layer is skipped and the patch is bit-identical to one "
            "from before the layer existed.");
        addAndMakeVisible (*slotSourceBoxes_[slot]);
        slotSourceAttachments_[slot]
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::modSlot (s, "Src"), *slotSourceBoxes_[slot]);

        slotDestBoxes_[slot] = std::make_unique<juce::ComboBox>();
        ui::styleChoice (*slotDestBoxes_[slot], palette_, palette_.secondary);
        slotDestBoxes_[slot]->addItemList (choices::modDests, 1);
        slotDestBoxes_[slot]->setTooltip (
            "What it moves. MATRIX DEPTH scales every live cell together -- the gesture people "
            "actually want, and safer than thirty separate destinations, because scaling never "
            "switches on a path the patch did not ask for.\n\n"
            "Only continuous controls appear here. A choice or a switch reconfigures rather "
            "than adjusts, so modulating one would mean rebuilding a filter graph per chunk.");
        addAndMakeVisible (*slotDestBoxes_[slot]);
        slotDestAttachments_[slot]
            = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                owner.getState(), ids::modSlot (s, "Dst"), *slotDestBoxes_[slot]);

        auto& amount = addControl (ids::modSlot (s, "Amt"), juce::String (s + 1),
                                   palette_.secondary, ui::design::Emphasis::trim);

        // **A row wants a bar, not a rotary.** A knob shrunk into a 34 px table
        // row is a 20 px circle with three pixels of pointer, and the amount is
        // bipolar: a horizontal slider shows which side of zero it is on at a
        // glance, which is the one thing a rotary that small cannot.
        amount.knob.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.knob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46,
                                     ui::design::kValueHeight);
        slotAmounts_.push_back (&amount);
    }

    slotAmounts_[0]->knob.setTooltip (
        "How far, in the destination's own units, and bipolar so a slot can subtract. A ratio "
        "moves in ratio, a cutoff in octaves, a level in level.\n\n"
        "Exactly zero is inert, and inert here means the destination is not even read.");

    spectrum_ = std::make_unique<BandwidthView> (owner, palette_);
    spectrum_->prepare (owner.getSampleRate() > 0.0 ? owner.getSampleRate() : 48000.0);
    addChildComponent (*spectrum_);

    notesView_ = std::make_unique<NotesView> (owner, palette_);
    notesView_->setProgram (owner.getCurrentProgram());

    notesViewport_.setViewedComponent (notesView_.get(), false);
    notesViewport_.setScrollBarsShown (true, false);
    notesViewport_.setScrollBarThickness (14);
    addChildComponent (notesViewport_);

    tuningPage_ = std::make_unique<ui::TuningPanel> (owner, palette_,
        "Notes play through this scale, as everywhere in the suite. What is different is that "
        "an operator's RATIO can snap to it too -- set an operator's Ratio mode to Scale on the "
        "OPERATORS page and its ratio lands on the nearest degree, octave-extended.\n\n"
        "That matters because in FM the ratio IS the interval: sidebands land at the carrier "
        "plus and minus whole multiples of the modulator, so a snapped modulator puts its "
        "entire sideband ladder on this scale's degrees -- at every key, because a ratio is an "
        "interval and not a pitch. In 19-TET or a Persian dastgah that is the difference "
        "between a growl that belongs to the track and one that fights it.\n\n"
        "Fixed-Hz operators and the formant mode are exempt and always will be: a formant "
        "centre is a vocal-tract resonance, not a musical interval, and snapping it would be "
        "wrong rather than merely useless. The scale travels with the project as .scl text.");
    addChildComponent (*tuningPage_);

    // The tabs. Built last so every page's controls exist to be hidden.
    static const char* const tabNames[] { "OPERATORS", "MATRIX", "VOICE", "SEQ",
                                         "MANGLE", "ADV", "MOD", "NOTES", "TUNING" };

    static_assert (static_cast<int> (std::size (tabNames)) == pageCount,
                   "every page needs a tab, and in the same order");

    for (int i = 0; i < pageCount; ++i)
    {
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        tab.setButtonText (tabNames[i]);

        // So `tezla-render editor hit:tab-mod shot:...` can photograph a page
        // without a window manager -- which is how a layout gets checked here
        // at all, the rig being the only place a panel is otherwise seen.
        tab.setComponentID ("tab-" + juce::String (tabNames[i]).toLowerCase());
        tab.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        tab.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tab);
    }

    setResizable (true, true);

    // **Back down, and deliberately.** One page of everything wanted 980x760,
    // which does not fit a laptop with a DAW's chrome around it -- reported
    // from the rig. Paged, the window only has to fit the largest single page,
    // and every page lays its rows out from the height it is given rather than
    // from a fixed row size, so it degrades by getting denser instead of by
    // clipping.
    setResizeLimits (860, 520, 2200, 1500);
    setSize (1040, 640);

    showPage (pageOperators);

    startTimerHz (12);
}

StrydaEditor::~StrydaEditor()
{
    setLookAndFeel (nullptr);
}

double StrydaEditor::predictedTop() const
{
    auto& state = processor_.getState();

    const auto value = [&state] (const juce::String& id)
    {
        const auto* raw = state.getRawParameterValue (id);
        return raw != nullptr ? static_cast<double> (raw->load()) : 0.0;
    };

    dsp::FmBandwidth bandwidth;
    bandwidth.setOperatorCount (kNumOperators);

    // A4 as the reference note, because the readout has to mean something
    // before anything is played and the panel says which note it assumes.
    constexpr double kReferenceHz = 440.0;

    for (int op = 0; op < kNumOperators; ++op)
    {
        bandwidth.setOperatorFrequency (op, kReferenceHz * value (ids::op (op, "Ratio")));
        bandwidth.setFeedback (op, value (ids::op (op, "Feedback")));

        // **The readout has to know about SHAPE or it disagrees with the cap.**
        // A non-sine operator carries n harmonics of its own frequency and puts
        // its ladder n times further out; the cap multiplies by that number, so
        // a readout that did not would say "clear" about a patch the cap was
        // holding down hard.
        bandwidth.setHarmonics (op, dsp::fmShapeHarmonics (
            static_cast<dsp::FmShape> (juce::jlimit (
                0, static_cast<int> (dsp::FmShape::count) - 1,
                static_cast<int> (std::lround (value (ids::op (op, "Shape"))))))));

        for (int from = 0; from < kNumOperators; ++from)
            if (op != from)
                bandwidth.setIndex (from, op, value (ids::cell (op, from)));
    }

    // **The figure has to be what you will actually hear, not what the patch
    // asked for.** The cap is on by default, so reporting the uncapped
    // prediction makes a patch the cap is holding perfectly safe read as
    // ALIASING -- which is worse than no readout at all, because it teaches the
    // player to ignore it.
    const double nyquist = processor_.getInternalNyquistHz();
    const int cap = static_cast<int> (std::lround (value (ids::indexCap)));
    const double amount = cap == 0 ? 0.0 : (cap == 1 ? 0.6 : 1.0);

    capBiting_ = false;
    uncappedTop_ = bandwidth.topSidebandHz();

    if (amount > 0.0 && nyquist > 0.0)
    {
        const double full = bandwidth.indexScaleFor (0.9 * nyquist);
        const double scale = 1.0 + amount * (full - 1.0);

        capBiting_ = scale < 1.0;
        return bandwidth.topSidebandHz (dsp::fm::kThresholdDb, scale);
    }

    return uncappedTop_;
}

Control& StrydaEditor::addControl (const juce::String& id,
                                   const juce::String& caption,
                                   juce::Colour tint,
                                   ui::design::Emphasis emphasis)
{
    auto control = std::make_unique<Control>();

    control->caption.setText (caption, juce::dontSendNotification);
    styleCaption (control->caption, palette_);
    addAndMakeVisible (control->caption);

    control->knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    ui::styleKnob (control->knob, palette_, tint, emphasis);

    // Two places, and a text box wide enough for them. A skewed range
    // round-trips its default to 0.999999, which the default formatting shows
    // as "0.99..." -- indistinguishable from a bug.
    control->knob.setNumDecimalPlacesToDisplay (2);
    control->knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64,
                                   ui::design::kValueHeight);

    addAndMakeVisible (control->knob);

    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor_.getState(), id, control->knob);

    controls_.push_back (std::move (control));
    return *controls_.back();
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
//
// **The panel is paged because it stopped fitting on the user's screen.**
// F3 put six operator strips, a 6x6 matrix and a scaling plate on one page and
// that already wanted 1120x700; F5 added a band of three more plates under the
// strips and pushed the minimum to 980x760, which is taller than a laptop with
// a DAW's own chrome around it. Reported from the rig on 2026-09-04.
//
// One page of everything is also the wrong shape for what is still coming: F6
// adds a sequencer and a tuning page, F7 a mangle chain, F8 a modulation
// matrix. So the fix is structural rather than a font size -- each page gets
// the whole content area, the window shrinks to fit the largest single page
// rather than the sum of them, and the minimum comes back down to 900x560.
//
// Controls not on the current page are HIDDEN, not moved: a control that is
// merely somewhere else still takes the mouse wheel and the keyboard.

void StrydaEditor::styleTab (juce::TextButton& tab, bool active)
{
    tab.setColour (juce::TextButton::buttonColourId,
                   active ? palette_.accent : palette_.panel.brighter (0.18f));
    tab.setColour (juce::TextButton::textColourOffId,
                   active ? palette_.background : palette_.accent);
}

void StrydaEditor::showPage (int page)
{
    currentPage_ = juce::jlimit (0, static_cast<int> (pageCount) - 1, page);

    for (int i = 0; i < pageCount; ++i)
        styleTab (tabs_[static_cast<std::size_t> (i)], i == currentPage_);

    applyPageVisibility();
    resized();
    repaint();
}

void StrydaEditor::applyPageVisibility()
{
    const bool operators = currentPage_ == pageOperators;
    const bool matrix = currentPage_ == pageMatrix;
    const bool voice = currentPage_ == pageVoice;

    for (auto& strip : strips_)
        for (auto* control : strip)
        {
            control->knob.setVisible (operators);
            control->caption.setVisible (operators);
        }

    for (auto& box : modeBoxes_)
        if (box != nullptr)
            box->setVisible (operators);

    for (auto& box : shapeBoxes_)
        if (box != nullptr)
            box->setVisible (operators);

    for (auto& wave : waves_)
        if (wave != nullptr)
            wave->setVisible (operators);

    matrix_.setVisible (matrix);
    bandwidth_.setVisible (matrix);

    if (spectrum_ != nullptr)
        spectrum_->setVisible (matrix);
    indexCapBox_.setVisible (matrix);

    for (auto& row : scaling_)
        for (auto* control : row)
            control->knob.setVisible (matrix);

    const auto showBand = [voice] (const std::vector<Control*>& controls)
    {
        for (auto* control : controls)
        {
            control->knob.setVisible (voice);
            control->caption.setVisible (voice);
        }
    };

    showBand (filter_);
    showBand (sub_);
    showBand (unison_);

    subOctaveBox_.setVisible (voice);
    subShapeBox_.setVisible (voice);

    const bool sequencer = currentPage_ == pageSequencer;

    for (auto* control : steps_)
    {
        control->knob.setVisible (sequencer);
        control->caption.setVisible (sequencer);
    }

    for (auto* control : seqControls_)
    {
        control->knob.setVisible (sequencer);
        control->caption.setVisible (sequencer);
    }

    seqOnButton_.setVisible (sequencer);
    seqTargetBox_.setVisible (sequencer);
    seqDivisionBox_.setVisible (sequencer);

    for (auto& button : braidButtons_)
        button.setVisible (sequencer);

    const bool mangling = currentPage_ == pageMangle;

    const auto showList = [mangling] (const std::vector<Control*>& controls)
    {
        for (auto* control : controls)
        {
            control->knob.setVisible (mangling);
            control->caption.setVisible (mangling);
        }
    };

    showList (vowel_);
    showList (vowelSteps_);
    showList (mangle_);

    vowelSeqButton_.setVisible (mangling);
    vowelDivisionBox_.setVisible (mangling);

    const bool adv = currentPage_ == pageAdv;

    for (int e = 0; e < 2; ++e)
    {
        const auto slot = static_cast<std::size_t> (e);

        advGraphs_[slot]->setVisible (adv);
        advLoopButtons_[slot].setVisible (adv);
        advPoints_[slot].heading.setVisible (adv);

        for (auto* control : advShape_[slot])
        {
            control->knob.setVisible (adv);
            control->caption.setVisible (adv);
        }

        for (int k = 0; k < 3; ++k)
        {
            const auto index = static_cast<std::size_t> (k);
            advPoints_[slot].knobs[index].setVisible (adv);
            advPoints_[slot].captions[index].setVisible (adv);
        }
    }

    const bool modulating = currentPage_ == pageMod;

    for (int l = 0; l < 2; ++l)
    {
        const auto slot = static_cast<std::size_t> (l);

        lfoWaveBoxes_[slot]->setVisible (modulating);
        lfoDivisionBoxes_[slot]->setVisible (modulating);
        lfoSyncButtons_[slot].setVisible (modulating);
        lfoRetrigButtons_[slot].setVisible (modulating);

        for (auto* control : lfoControls_[slot])
        {
            control->knob.setVisible (modulating);
            control->caption.setVisible (modulating);
        }
    }

    for (auto* control : macros_)
    {
        control->knob.setVisible (modulating);
        control->caption.setVisible (modulating);
    }

    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto slot = static_cast<std::size_t> (s);

        slotSourceBoxes_[slot]->setVisible (modulating);
        slotDestBoxes_[slot]->setVisible (modulating);
        slotAmounts_[slot]->knob.setVisible (modulating);

        // The slot rows label themselves by position, so the caption would only
        // repeat the row number the combo boxes already sit on.
        slotAmounts_[slot]->caption.setVisible (false);
    }

    const bool notes = currentPage_ == pageNotes;
    notesViewport_.setVisible (notes);

    if (notes && notesView_ != nullptr)
    {
        notesView_->setProgram (processor_.getCurrentProgram());
        layoutNotes (getLocalBounds()
                       .withTrimmedTop (kHeaderHeight + kTabHeight)
                       .withTrimmedBottom (34 + kMargin)
                       .reduced (kMargin));
    }

    if (tuningPage_ != nullptr)
    {
        const bool tuning = currentPage_ == pageTuning;

        tuningPage_->setVisible (tuning);

        if (tuning)
            tuningPage_->refresh();
    }
}

void StrydaEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);

    auto area = getLocalBounds();
    area.removeFromTop (kHeaderHeight);
    area.removeFromTop (kTabHeight);
    area = area.reduced (kMargin);
    area.removeFromBottom (34 + kMargin);

    switch (currentPage_)
    {
        case pageOperators: paintOperators (g, area); break;
        case pageMatrix:    paintMatrix (g, area);    break;
        case pageVoice:     paintVoice (g, area);     break;
        case pageSequencer: paintSequencer (g, area); break;
        case pageMangle:    paintMangle (g, area);    break;
        case pageAdv:       paintAdv (g, area);       break;
        case pageMod:       paintMod (g, area);       break;

        case pageNotes:
            ui::paintPlate (g, area, palette_.panel, palette_.accent);
            break;

        default: break;   // the tuning page paints itself
    }
}

void StrydaEditor::paintOperators (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int stripWidth = juce::jmax (kMinStripWidth, area.getWidth() / kNumOperators);

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto strip = area.removeFromLeft (stripWidth);
        const auto tint = ui::design::tintFor (palette_.accent, op);

        ui::paintPlate (g, strip.reduced (3), palette_.panel, tint);
        ui::paintPlateHeading (g, palette_,
                               strip.reduced (3).removeFromTop (ui::design::kValueHeight + 4),
                               "OP " + juce::String (op + 1), {}, tint);
    }
}

void StrydaEditor::paintMatrix (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int matrixWidth = juce::jlimit (360, 760, area.getWidth() * 3 / 5);
    area.removeFromLeft (matrixWidth);
    area.removeFromLeft (kMargin);
    area.removeFromTop (kReadoutHeight + 6);

    ui::paintPlate (g, area, palette_.panel, palette_.secondary);

    auto heading = area.reduced (6);
    ui::paintPlateHeading (g, palette_, heading.removeFromTop (ui::design::kValueHeight + 4),
                           "SCALING", "how each operator answers the key and the touch",
                           palette_.secondary);

    auto headings = heading.removeFromTop (kCaptionHeight);
    headings.removeFromLeft (30);

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.0f));

    static const char* const columns[] { "BREAK", "BELOW", "ABOVE", "VEL LVL", "VEL IDX" };
    const int headingWidth = headings.getWidth() / 5;

    for (const auto* column : columns)
        g.drawText (column, headings.removeFromLeft (headingWidth),
                    juce::Justification::centred);

    const int scaleRow = heading.getHeight() / kNumOperators;

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto row = heading.removeFromTop (scaleRow);
        g.setColour (ui::design::tintFor (palette_.accent, op));
        g.drawText (juce::String (op + 1), row.removeFromLeft (30),
                    juce::Justification::centred);
    }
}

void StrydaEditor::paintVoice (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto band = splitBand (area);

    const auto plate = [&g, this] (juce::Rectangle<int> bounds,
                                   const char* title,
                                   const char* subtitle)
    {
        ui::paintPlate (g, bounds.reduced (3), palette_.panel, palette_.secondary);
        ui::paintPlateHeading (g, palette_,
                               bounds.reduced (3).removeFromTop (ui::design::kValueHeight + 4),
                               title, subtitle, palette_.secondary);
    };

    plate (band.filter, "FILTER", "per voice, after the matrix");
    plate (band.sub, "SUB", "its own lane, through nothing");
    plate (band.unison, "UNISON", "copies, and how they differ");
}

void StrydaEditor::resized()
{
    auto area = getLocalBounds();
    header_.setBounds (area.removeFromTop (kHeaderHeight));

    auto tabRow = area.removeFromTop (kTabHeight).reduced (kMargin, 3);
    const int tabWidth = juce::jmin (150, tabRow.getWidth() / pageCount);

    for (int i = 0; i < pageCount; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (tabWidth).reduced (2, 0));

    area = area.reduced (kMargin);

    auto footer = area.removeFromBottom (34);
    area.removeFromBottom (kMargin);

    presetBox_.setBounds (footer.removeFromLeft (juce::jmin (200, footer.getWidth() / 3))
                              .reduced (2));
    footer.removeFromLeft (kMargin);

    auto masterCell = footer.removeFromRight (100);
    globals_[0]->caption.setBounds (masterCell.removeFromTop (kCaptionHeight));
    globals_[0]->knob.setBounds (masterCell);

    voices_.setBounds (footer.removeFromRight (120));

    switch (currentPage_)
    {
        case pageOperators: layoutOperators (area); break;
        case pageMatrix:    layoutMatrix (area);    break;
        case pageVoice:     layoutVoice (area);     break;
        case pageSequencer: layoutSequencer (area);  break;
        case pageMangle:    layoutMangle (area);     break;
        case pageAdv:       layoutAdv (area);        break;
        case pageMod:       layoutMod (area);        break;
        case pageNotes:     layoutNotes (area);      break;

        case pageTuning:
            if (tuningPage_ != nullptr)
                tuningPage_->setBounds (area);
            break;

        default: break;
    }
}

void StrydaEditor::layoutOperators (juce::Rectangle<int> area)
{
    const int stripWidth = juce::jmax (kMinStripWidth, area.getWidth() / kNumOperators);

    // Three rows share whatever height the page has, rather than each taking a
    // fixed 74: that is what lets the window shrink instead of clipping.
    const int modeRowHeight = 26;
    const int rowHeight = juce::jmax (34, (area.getHeight() - modeRowHeight * 2 - kWaveHeight
                                             - ui::design::kValueHeight - 8
                                             - kCaptionHeight * 3) / 3);

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto strip = area.removeFromLeft (stripWidth).reduced (3);
        strip.removeFromTop (ui::design::kValueHeight + 8);

        auto& controls = strips_[static_cast<std::size_t> (op)];

        // The wave first, at the top of the strip, because it is what the eye
        // goes to and because every knob under it changes it.
        waves_[static_cast<std::size_t> (op)]
            ->setBounds (strip.removeFromTop (kWaveHeight).reduced (2, 0));

        auto shapeRow = strip.removeFromBottom (modeRowHeight);
        shapeBoxes_[static_cast<std::size_t> (op)]->setBounds (shapeRow.reduced (6, 2));

        auto modeRow = strip.removeFromBottom (modeRowHeight);
        modeBoxes_[static_cast<std::size_t> (op)]->setBounds (modeRow.reduced (6, 2));

        for (std::size_t row = 0; row < 3; ++row)
        {
            auto line = strip.removeFromTop (rowHeight + kCaptionHeight);
            const int columnWidth = line.getWidth() / 4;

            for (std::size_t column = 0; column < 4; ++column)
            {
                const std::size_t i = row * 4 + column;
                if (i >= controls.size())
                    continue;

                auto cell = line.removeFromLeft (columnWidth);
                controls[i]->caption.setBounds (cell.removeFromTop (kCaptionHeight));
                controls[i]->knob.setBounds (cell.reduced (2, 0));
            }
        }
    }
}

void StrydaEditor::layoutMatrix (juce::Rectangle<int> area)
{
    const int matrixWidth = juce::jlimit (360, 760, area.getWidth() * 3 / 5);
    matrix_.setBounds (area.removeFromLeft (matrixWidth));

    area.removeFromLeft (kMargin);

    bandwidth_.setBounds (area.removeFromTop (kReadoutHeight).reduced (12, 6));

    // The spectrum sits directly under the readout, because it is the same
    // claim drawn rather than written: the number says where the edge is, the
    // picture says where it is against everything else.
    if (spectrum_ != nullptr)
        spectrum_->setBounds (area.removeFromTop (
            juce::jlimit (70, 150, area.getHeight() * 30 / 100)).reduced (10, 2));

    area.removeFromTop (6);

    auto plate = area.reduced (6);
    plate.removeFromTop (ui::design::kValueHeight + 6);

    indexCapBox_.setBounds (plate.removeFromBottom (26).reduced (4, 1));

    plate.removeFromTop (kCaptionHeight);   // the column headings paint() draws
    plate.removeFromLeft (30);

    const int scaleRow = juce::jmax (18, plate.getHeight() / kNumOperators);
    const int scaleColumn = plate.getWidth() / 5;

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto row = plate.removeFromTop (scaleRow);

        for (std::size_t i = 0; i < scaling_[static_cast<std::size_t> (op)].size(); ++i)
            scaling_[static_cast<std::size_t> (op)][i]
                ->knob.setBounds (row.removeFromLeft (scaleColumn).reduced (4, 1));
    }
}

void StrydaEditor::paintSequencer (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto braidRow = area.removeFromBottom (kBraidHeight);
    area.removeFromBottom (kMargin);

    ui::paintPlate (g, area, palette_.panel, palette_.accent);
    ui::paintPlateHeading (g, palette_,
                           area.reduced (6).removeFromTop (ui::design::kValueHeight + 4),
                           "RATIO SEQUENCER",
                           "sixteen steps whose value is a ratio, cut at the step edge",
                           palette_.accent);

    ui::paintPlate (g, braidRow, palette_.panel, palette_.secondary);
    ui::paintPlateHeading (g, palette_,
                           braidRow.reduced (6).removeFromTop (ui::design::kValueHeight + 4),
                           "BRAIDS", "a topology to start from, still editable afterwards",
                           palette_.secondary);
}

void StrydaEditor::layoutSequencer (juce::Rectangle<int> area)
{
    auto braidRow = area.removeFromBottom (kBraidHeight).reduced (6);
    area.removeFromBottom (kMargin);

    braidRow.removeFromTop (ui::design::kValueHeight + 6);

    const int braidWidth = braidRow.getWidth() / braids::kCount;

    for (auto& button : braidButtons_)
        button.setBounds (braidRow.removeFromLeft (braidWidth).reduced (4, 2));

    auto plate = area.reduced (6);
    plate.removeFromTop (ui::design::kValueHeight + 6);

    // The five controls across the top, then the sixteen steps in two rows of
    // eight -- which is how a pattern is read, a bar at a time.
    auto controlRow = plate.removeFromTop (30);

    seqOnButton_.setBounds (controlRow.removeFromLeft (80).reduced (3, 1));
    controlRow.removeFromLeft (kMargin);
    seqTargetBox_.setBounds (controlRow.removeFromLeft (110).reduced (3, 1));
    controlRow.removeFromLeft (kMargin);
    seqDivisionBox_.setBounds (controlRow.removeFromLeft (110).reduced (3, 1));
    controlRow.removeFromLeft (kMargin);

    const int knobWidth = juce::jmin (90, juce::jmax (40, controlRow.getWidth() / 2));

    for (auto* control : seqControls_)
    {
        auto cell = controlRow.removeFromLeft (knobWidth);
        control->caption.setBounds (cell.removeFromTop (kCaptionHeight));
        control->knob.setBounds (cell);
    }

    plate.removeFromTop (kMargin);

    const int rowHeight = juce::jmax (30, plate.getHeight() / 2 - kCaptionHeight);

    for (int row = 0; row < 2; ++row)
    {
        auto line = plate.removeFromTop (rowHeight + kCaptionHeight);
        const int columnWidth = line.getWidth() / 8;

        for (int column = 0; column < 8; ++column)
        {
            const auto index = static_cast<std::size_t> (row * 8 + column);

            if (index >= steps_.size())
                break;

            auto cell = line.removeFromLeft (columnWidth);
            steps_[index]->caption.setBounds (cell.removeFromTop (kCaptionHeight));
            steps_[index]->knob.setBounds (cell.reduced (2, 0));
        }
    }
}

void StrydaEditor::paintMangle (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto top = area.removeFromTop (area.getHeight() * 52 / 100);
    area.removeFromTop (kMargin);

    ui::paintPlate (g, top, palette_.panel, ui::design::tintFor (palette_.accent, 4));
    ui::paintPlateHeading (g, palette_, top.reduced (6).removeFromTop (ui::design::kValueHeight + 4),
                           "SPLIT + VOWEL",
                           "the low band goes round everything; the top talks",
                           ui::design::tintFor (palette_.accent, 4));

    ui::paintPlate (g, area, palette_.panel, palette_.secondary);
    ui::paintPlateHeading (g, palette_, area.reduced (6).removeFromTop (ui::design::kValueHeight + 4),
                           "MANGLE", "fold, crush, comb, phase, drive, glue -- each skipped at neutral",
                           palette_.secondary);
}

void StrydaEditor::layoutMangle (juce::Rectangle<int> area)
{
    auto top = area.removeFromTop (area.getHeight() * 52 / 100).reduced (6);
    area.removeFromTop (kMargin);

    top.removeFromTop (ui::design::kValueHeight + 6);

    // Split and the four vowel controls, then the pattern's own switch and
    // division, then sixteen steps in two rows of eight.
    const int rowHeight = juce::jmax (30, (top.getHeight() - 30) / 3 - kCaptionHeight);

    auto controlRow = top.removeFromTop (rowHeight + kCaptionHeight);
    const int columnWidth = controlRow.getWidth() / 9;

    for (auto* control : vowel_)
    {
        auto cell = controlRow.removeFromLeft (columnWidth);
        control->caption.setBounds (cell.removeFromTop (kCaptionHeight));
        control->knob.setBounds (cell.reduced (2, 0));
    }

    vowelSeqButton_.setBounds (controlRow.removeFromLeft (columnWidth).reduced (3, 8));
    vowelDivisionBox_.setBounds (controlRow.reduced (3, 12));

    for (int row = 0; row < 2; ++row)
    {
        auto line = top.removeFromTop (rowHeight + kCaptionHeight);
        const int stepWidth = line.getWidth() / 8;

        for (int column = 0; column < 8; ++column)
        {
            const auto index = static_cast<std::size_t> (row * 8 + column);

            if (index >= vowelSteps_.size())
                break;

            auto cell = line.removeFromLeft (stepWidth);
            vowelSteps_[index]->caption.setBounds (cell.removeFromTop (kCaptionHeight));
            vowelSteps_[index]->knob.setBounds (cell.reduced (2, 0));
        }
    }

    auto plate = area.reduced (6);
    plate.removeFromTop (ui::design::kValueHeight + 6);

    // Sixteen mangle controls in two rows of eight, in chain order.
    const int mangleRow = juce::jmax (30, plate.getHeight() / 2 - kCaptionHeight);

    for (int row = 0; row < 2; ++row)
    {
        auto line = plate.removeFromTop (mangleRow + kCaptionHeight);
        const int width = line.getWidth() / 8;

        for (int column = 0; column < 8; ++column)
        {
            const auto index = static_cast<std::size_t> (row * 8 + column);

            if (index >= mangle_.size())
                break;

            auto cell = line.removeFromLeft (width);
            mangle_[index]->caption.setBounds (cell.removeFromTop (kCaptionHeight));
            mangle_[index]->knob.setBounds (cell.reduced (2, 0));
        }
    }
}

void StrydaEditor::retargetAdvPoint (int which)
{
    const auto slot = static_cast<std::size_t> (which);
    const int point = advGraphs_[slot] != nullptr ? advGraphs_[slot]->getSelectedPoint() : 0;

    static const char* const fields[] { "Time", "Level", "Tens" };

    for (int k = 0; k < 3; ++k)
    {
        const auto index = static_cast<std::size_t> (k);

        // **The old attachment goes first.** Two attachments on one slider both
        // write the slider on a parameter change and both write a parameter on
        // a drag, so leaving the previous one alive would edit the breakpoint
        // you just stopped looking at.
        advPoints_[slot].attachments[index].reset();
        advPoints_[slot].attachments[index]
            = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor_.getState(), ids::advPoint (which, point, fields[k]),
                advPoints_[slot].knobs[index]);
    }

    advPoints_[slot].heading.setText ("POINT " + juce::String (point + 1),
                                      juce::dontSendNotification);
}

void StrydaEditor::paintAdv (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int half = area.getHeight() / 2;

    for (int e = 0; e < 2; ++e)
    {
        auto plate = e == 0 ? area.removeFromTop (half) : area;

        if (e == 1)
            plate.removeFromTop (kMargin);

        const auto tint = ui::design::tintFor (palette_.accent, e == 0 ? 1 : 3);

        ui::paintPlate (g, plate, palette_.panel, tint);
        ui::paintPlateHeading (
            g, palette_, plate.reduced (6).removeFromTop (ui::design::kValueHeight + 4),
            "ADV " + juce::String (e + 1),
            e == 0 ? "a breakpoint envelope -- click a point, edit it with the three knobs"
                   : "the second one; an operator picks which envelope it answers to",
            tint);
    }
}

void StrydaEditor::layoutAdv (juce::Rectangle<int> area)
{
    const int half = area.getHeight() / 2;

    for (int e = 0; e < 2; ++e)
    {
        const auto slot = static_cast<std::size_t> (e);
        auto plate = (e == 0 ? area.removeFromTop (half) : area);

        if (e == 1)
            plate.removeFromTop (kMargin);

        plate = plate.reduced (8);
        plate.removeFromTop (ui::design::kValueHeight + 6);

        // The graph takes the left; the controls stack down the right, because
        // a wide graph is what makes a sixteen-point shape readable.
        auto controls = plate.removeFromRight (juce::jlimit (250, 420, plate.getWidth() * 2 / 5));
        plate.removeFromRight (6);

        advGraphs_[slot]->setBounds (plate);

        // Top row: the three structural controls plus LOOP.
        auto top = controls.removeFromTop (juce::jmax (52, controls.getHeight() / 2 - 6));
        const int columnWidth = top.getWidth() / 4;

        for (auto* control : advShape_[slot])
        {
            auto cell = top.removeFromLeft (columnWidth);
            control->caption.setBounds (cell.removeFromTop (kCaptionHeight));
            control->knob.setBounds (cell.reduced (2, 0));
        }

        advLoopButtons_[slot].setBounds (top.reduced (4, 12));

        controls.removeFromTop (4);

        // Bottom row: which point, then its three knobs.
        advPoints_[slot].heading.setBounds (controls.removeFromTop (kCaptionHeight));

        const int pointWidth = controls.getWidth() / 3;

        for (int k = 0; k < 3; ++k)
        {
            const auto index = static_cast<std::size_t> (k);
            auto cell = controls.removeFromLeft (pointWidth);

            advPoints_[slot].captions[index].setBounds (cell.removeFromTop (kCaptionHeight));
            advPoints_[slot].knobs[index].setBounds (cell.reduced (2, 0));
        }
    }
}

void StrydaEditor::paintMod (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto plates = splitMod (area);

    for (int l = 0; l < 2; ++l)
    {
        const auto tint = ui::design::tintFor (palette_.accent, l == 0 ? 0 : 5);

        ui::paintPlate (g, plates.lfo[l], palette_.panel, tint);
        ui::paintPlateHeading (
            g, palette_,
            plates.lfo[l].reduced (6).removeFromTop (ui::design::kValueHeight + 4),
            "LFO " + juce::String (l + 1),
            l == 0 ? juce::String ("free, or locked to the bar") : juce::String(), tint);
    }

    ui::paintPlate (g, plates.macros, palette_.panel, palette_.secondary);
    ui::paintPlateHeading (
        g, palette_, plates.macros.reduced (6).removeFromTop (ui::design::kValueHeight + 4),
        "MACROS", "one hand, several controls", palette_.secondary);

    ui::paintPlate (g, plates.matrix, palette_.panel, palette_.accent);
    ui::paintPlateHeading (
        g, palette_, plates.matrix.reduced (6).removeFromTop (ui::design::kValueHeight + 4),
        "MATRIX", "eight slots: what moves what, and by how much", palette_.accent);

    // The row numbers and column headings, on the same grid the boxes are laid
    // out on. Without them eight identical Off/Off/knob rows say nothing about
    // which slot is which -- which is exactly how the first screenshot read.
    auto table = plates.table;
    const int columnWidth = table.getWidth() / plates.columns;

    g.setFont (juce::FontOptions (10.0f));

    for (int column = 0; column < plates.columns; ++column)
    {
        auto strip = table.removeFromLeft (columnWidth).reduced (2, 0);
        auto heading = strip.removeFromTop (plates.headingHeight);

        const int amountWidth = juce::jlimit (96, 190, heading.getWidth() * 3 / 10);
        auto amount = heading.removeFromRight (amountWidth);
        heading.removeFromLeft (kSlotNumberWidth);

        const int boxWidth = heading.getWidth() / 2;

        g.setColour (palette_.dimText);
        g.drawText ("SOURCE", heading.removeFromLeft (boxWidth), juce::Justification::centredLeft);
        g.drawText ("DESTINATION", heading, juce::Justification::centredLeft);
        g.drawText ("AMOUNT", amount, juce::Justification::centred);

        for (int row = 0; row < plates.perColumn; ++row)
        {
            const int index = column * plates.perColumn + row;

            if (index >= kNumSlots)
                break;

            auto line = strip.removeFromTop (plates.rowHeight);

            // Lit when the slot is actually doing something, so a page of
            // eight rows says at a glance which ones are live.
            const bool live = slotSourceBoxes_[static_cast<std::size_t> (index)]
                                    ->getSelectedItemIndex() > 0
                              && slotDestBoxes_[static_cast<std::size_t> (index)]
                                       ->getSelectedItemIndex() > 0;

            g.setColour (live ? palette_.accent : palette_.dimText);
            g.drawText (juce::String (index + 1), line.removeFromLeft (kSlotNumberWidth),
                        juce::Justification::centred);
        }
    }
}

void StrydaEditor::layoutMod (juce::Rectangle<int> area)
{
    const auto plates = splitMod (area);

    for (int l = 0; l < 2; ++l)
    {
        const auto slot = static_cast<std::size_t> (l);
        auto plate = plates.lfo[l].reduced (8);
        plate.removeFromTop (ui::design::kValueHeight + 6);

        auto boxes = plate.removeFromTop (26);
        lfoWaveBoxes_[slot]->setBounds (boxes.removeFromLeft (boxes.getWidth() * 55 / 100)
                                            .reduced (2, 1));
        lfoDivisionBoxes_[slot]->setBounds (boxes.reduced (2, 1));

        plate.removeFromTop (6);

        // **The lamps need 40 px of bounds, not whatever is left.** A LampButton
        // reserves `kGlowMargin` (7 px) on every side for its halo and another
        // 3 for the bezel, so a 26 px box leaves a 6 px cap and the legend --
        // sized `capHeight * 0.55` -- comes out at three pixels. They drew as
        // two coloured bars with no writing on them, which is exactly what the
        // first screenshot showed.
        auto lamps = plate.removeFromBottom (juce::jmax (34, plate.getHeight() / 3));
        lfoSyncButtons_[slot].setBounds (lamps.removeFromLeft (lamps.getWidth() / 2).reduced (4, 0));
        lfoRetrigButtons_[slot].setBounds (lamps.reduced (4, 0));

        const int columnWidth = plate.getWidth() / 3;

        for (auto* control : lfoControls_[slot])
        {
            auto cell = plate.removeFromLeft (columnWidth);
            control->caption.setBounds (cell.removeFromTop (kCaptionHeight));
            control->knob.setBounds (cell.reduced (2, 0));
        }
    }

    // Four macros, two by two, so the plate stays square-ish beside the LFOs.
    auto macroPlate = plates.macros.reduced (8);
    macroPlate.removeFromTop (ui::design::kValueHeight + 6);

    for (int row = 0; row < 2; ++row)
    {
        auto line = macroPlate.removeFromTop (macroPlate.getHeight() / (2 - row));
        const int width = line.getWidth() / 2;

        for (int column = 0; column < 2; ++column)
        {
            const auto index = static_cast<std::size_t> (row * 2 + column);
            auto cell = line.removeFromLeft (width);

            macros_[index]->caption.setBounds (cell.removeFromTop (kCaptionHeight));
            macros_[index]->knob.setBounds (cell.reduced (2, 0));
        }
    }

    auto table = plates.table;
    const int columnWidth = table.getWidth() / plates.columns;

    for (int column = 0; column < plates.columns; ++column)
    {
        auto strip = table.removeFromLeft (columnWidth).reduced (2, 0);
        strip.removeFromTop (plates.headingHeight);

        for (int row = 0; row < plates.perColumn; ++row)
        {
            const auto index = static_cast<std::size_t> (column * plates.perColumn + row);

            if (index >= static_cast<std::size_t> (kNumSlots))
                break;

            auto line = strip.removeFromTop (plates.rowHeight);
            const int amountWidth = juce::jlimit (96, 190, line.getWidth() * 3 / 10);

            auto amount = line.removeFromRight (amountWidth);
            slotAmounts_[index]->knob.setBounds (amount.reduced (4, 4));

            line.removeFromLeft (kSlotNumberWidth);

            const int boxWidth = line.getWidth() / 2;
            slotSourceBoxes_[index]->setBounds (line.removeFromLeft (boxWidth).reduced (2, 3));
            slotDestBoxes_[index]->setBounds (line.reduced (2, 3));
        }
    }
}

void StrydaEditor::layoutVoice (juce::Rectangle<int> area)
{
    const auto band = splitBand (area);

    const auto layoutPlate = [] (juce::Rectangle<int> plate,
                                 const std::vector<Control*>& controls,
                                 int perRow,
                                 int rows,
                                 juce::Rectangle<int>* choicesOut)
    {
        plate = plate.reduced (3);
        plate.removeFromTop (ui::design::kValueHeight + 6);

        const int reserved = choicesOut != nullptr ? 30 : 0;
        const int rowHeight = juce::jmax (34, (plate.getHeight() - reserved) / rows
                                                - kCaptionHeight);

        for (std::size_t i = 0; i < controls.size(); i += static_cast<std::size_t> (perRow))
        {
            auto line = plate.removeFromTop (rowHeight + kCaptionHeight);
            const int columnWidth = line.getWidth() / perRow;

            for (int column = 0; column < perRow; ++column)
            {
                const std::size_t index = i + static_cast<std::size_t> (column);
                if (index >= controls.size())
                    break;

                auto cell = line.removeFromLeft (columnWidth);
                controls[index]->caption.setBounds (cell.removeFromTop (kCaptionHeight));
                controls[index]->knob.setBounds (cell.reduced (2, 0));
            }
        }

        if (choicesOut != nullptr)
            *choicesOut = plate;
    };

    layoutPlate (band.filter, filter_, 6, 2, nullptr);

    juce::Rectangle<int> subChoices;
    layoutPlate (band.sub, sub_, 5, 1, &subChoices);

    if (! subChoices.isEmpty())
    {
        auto row = subChoices.removeFromTop (26);
        subOctaveBox_.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4, 1));
        subShapeBox_.setBounds (row.reduced (4, 1));
    }

    layoutPlate (band.unison, unison_, 4, 1, nullptr);
}

void StrydaEditor::timerCallback()
{
    // Computed here rather than read from the audio thread, so the readout is
    // live with the transport stopped. It is a closed form over 36 cells; at
    // twelve frames a second that is nothing, and a panel whose most important
    // number only appears once you press play is a panel nobody trusts.
    const double top = predictedTop();
    const double nyquist = processor_.getInternalNyquistHz();

    juce::String text = "BANDWIDTH (upper bound, at A4)\n" + hzText (top)
                          + " of " + hzText (nyquist) + " internal";

    if (top > nyquist)
        text += "   OVER";
    else if (capBiting_)
        text += "   capped";
    else if (top > 0.8 * nyquist)
        text += "   close";
    else
        text += "   clear";

    bandwidth_.setText (text, juce::dontSendNotification);
    bandwidth_.setColour (juce::Label::textColourId,
                          top > nyquist ? palette_.bypassGlow
                                        : (capBiting_ ? palette_.secondary : palette_.dimText));

    voices_.setText (juce::String (processor_.getActiveVoices()) + " voices",
                     juce::dontSendNotification);

    // Only while the page it lives on is showing: pulling an FFT frame for a
    // hidden component is a redraw nobody sees at thirty frames a second.
    if (spectrum_ != nullptr && currentPage_ == pageMatrix)
        spectrum_->refresh (top, uncappedTop_, capBiting_);

    if (presetBox_.getSelectedId() != processor_.getCurrentProgram() + 1)
        presetBox_.setSelectedId (processor_.getCurrentProgram() + 1, juce::dontSendNotification);

    // Live, from the timer, because what Auto is *doing* depends on the host's
    // rate and only the processor knows it (CLAUDE.md section 6: the tooltip
    // reads the actual current rate rather than making the user work it out).
    header_.setOversamplingTooltip (processor_.describeOversampling());
    header_.setRenderTooltip (processor_.describeRenderQuality());
}

} // namespace tezla::stryda
