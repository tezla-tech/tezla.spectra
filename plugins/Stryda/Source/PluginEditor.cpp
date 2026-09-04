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

    setResizable (true, true);
    setResizeLimits (900, 560, 2200, 1400);
    setSize (1120, 700);

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

    if (amount > 0.0 && nyquist > 0.0)
    {
        const double full = bandwidth.indexScaleFor (0.9 * nyquist);
        const double scale = 1.0 + amount * (full - 1.0);

        capBiting_ = scale < 1.0;
        return bandwidth.topSidebandHz (dsp::fm::kThresholdDb, scale);
    }

    return bandwidth.topSidebandHz();
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

void StrydaEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);

    auto area = getLocalBounds();
    area.removeFromTop (kHeaderHeight);
    area = area.reduced (kMargin);

    auto strips = area.removeFromTop (kRowHeight * 3 + kCaptionHeight * 3 + 56);
    const int stripWidth = juce::jmax (kMinStripWidth, strips.getWidth() / kNumOperators);

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto strip = strips.removeFromLeft (stripWidth);
        const auto tint = ui::design::tintFor (palette_.accent, op);

        ui::paintPlate (g, strip.reduced (3), palette_.panel, tint);
        ui::paintPlateHeading (g, palette_, strip.reduced (3).removeFromTop (ui::design::kValueHeight + 4),
                               "OP " + juce::String (op + 1), {}, tint);
    }

    area.removeFromTop (kMargin);
    area.removeFromBottom (34 + kMargin);

    const int matrixWidth = juce::jlimit (420, 760, area.getWidth() * 3 / 5);
    area.removeFromLeft (matrixWidth);
    area.removeFromLeft (kMargin);
    area.removeFromTop (74 + 6);

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

void StrydaEditor::resized()
{
    auto area = getLocalBounds();
    header_.setBounds (area.removeFromTop (kHeaderHeight));
    area = area.reduced (kMargin);

    const int stripHeight = kRowHeight * 3 + kCaptionHeight * 3 + 56;
    auto strips = area.removeFromTop (stripHeight);
    const int stripWidth = juce::jmax (kMinStripWidth, strips.getWidth() / kNumOperators);

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto strip = strips.removeFromLeft (stripWidth).reduced (3);
        strip.removeFromTop (ui::design::kValueHeight + 8);

        auto& controls = strips_[static_cast<std::size_t> (op)];

        auto modeRow = strip.removeFromBottom (26);
        modeBoxes_[static_cast<std::size_t> (op)]->setBounds (modeRow.reduced (6, 2));

        for (std::size_t row = 0; row < 3; ++row)
        {
            auto line = strip.removeFromTop (kRowHeight + kCaptionHeight);
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

    area.removeFromTop (kMargin);

    auto footer = area.removeFromBottom (34);
    presetBox_.setBounds (footer.removeFromLeft (200).reduced (2));
    footer.removeFromLeft (kMargin);
    indexCapBox_.setBounds (footer.removeFromLeft (150).reduced (2));
    footer.removeFromLeft (kMargin);

    auto masterCell = footer.removeFromRight (110);
    globals_[0]->caption.setBounds (masterCell.removeFromTop (kCaptionHeight));
    globals_[0]->knob.setBounds (masterCell);

    voices_.setBounds (footer.removeFromRight (140));
    footer.removeFromLeft (kMargin);

    area.removeFromBottom (kMargin);

    // The matrix is square-ish however wide the window gets, and the bandwidth
    // readout lives beside it rather than squeezed into the footer -- it is the
    // one number on this panel that says whether the patch is about to alias,
    // and it should not be the smallest thing on it.
    const int matrixWidth = juce::jlimit (420, 760, area.getWidth() * 3 / 5);
    matrix_.setBounds (area.removeFromLeft (matrixWidth));

    area.removeFromLeft (kMargin);
    bandwidth_.setBounds (area.removeFromTop (74).reduced (12, 8));

    area.removeFromTop (6);
    auto plate = area.reduced (6);
    plate.removeFromTop (ui::design::kValueHeight + 6);
    plate.removeFromTop (kCaptionHeight);   // the column headings paint() draws
    plate.removeFromLeft (30);

    const int scaleRow = plate.getHeight() / kNumOperators;
    const int scaleColumn = plate.getWidth() / 5;

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto row = plate.removeFromTop (scaleRow);

        for (std::size_t i = 0; i < scaling_[static_cast<std::size_t> (op)].size(); ++i)
            scaling_[static_cast<std::size_t> (op)][i]
                ->knob.setBounds (row.removeFromLeft (scaleColumn).reduced (4, 1));
    }
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

    if (presetBox_.getSelectedId() != processor_.getCurrentProgram() + 1)
        presetBox_.setSelectedId (processor_.getCurrentProgram() + 1, juce::dontSendNotification);
}

} // namespace tezla::stryda
