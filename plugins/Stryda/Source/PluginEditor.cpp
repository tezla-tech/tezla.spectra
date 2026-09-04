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
    static const char* const tabNames[] { "OPERATORS", "MATRIX", "VOICE", "SEQ", "TUNING" };

    for (int i = 0; i < pageCount; ++i)
    {
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        tab.setButtonText (tabNames[i]);
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

    matrix_.setVisible (matrix);
    bandwidth_.setVisible (matrix);
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
    const int rowHeight = juce::jmax (34, (area.getHeight() - modeRowHeight
                                             - ui::design::kValueHeight - 8
                                             - kCaptionHeight * 3) / 3);

    for (int op = 0; op < kNumOperators; ++op)
    {
        auto strip = area.removeFromLeft (stripWidth).reduced (3);
        strip.removeFromTop (ui::design::kValueHeight + 8);

        auto& controls = strips_[static_cast<std::size_t> (op)];

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

    if (presetBox_.getSelectedId() != processor_.getCurrentProgram() + 1)
        presetBox_.setSelectedId (processor_.getCurrentProgram() + 1, juce::dontSendNotification);

    // Live, from the timer, because what Auto is *doing* depends on the host's
    // rate and only the processor knows it (CLAUDE.md section 6: the tooltip
    // reads the actual current rate rather than making the user work it out).
    header_.setOversamplingTooltip (processor_.describeOversampling());
    header_.setRenderTooltip (processor_.describeRenderQuality());
}

} // namespace tezla::stryda
