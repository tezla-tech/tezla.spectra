// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <tezla/ui/StateIds.hpp>

namespace tezla::syrinx {

namespace
{
/// Violet, and the last accent unclaimed in the suite -- Anvil has hot iron,
/// Capstone blue, Crossbar keypad green, Ferrite oxide, Malleus bone,
/// Transpectus sea green, Sonitus hot pink, Svara-yantra gold, and the two
/// drive plugins share the house orange.
///
/// The **secondary** is deliberately the same amber Capstone uses for its
/// gain reduction. Reduction means the same thing in every box in the suite,
/// so it should look the same; the accent is what says which plugin you are
/// looking at.
const ui::Palette kPalette {
    juce::Colour { 0xff141418 },   // background
    juce::Colour { 0xff1d1d23 },   // panel
    juce::Colour { 0xffd8d5cf },   // text
    juce::Colour { 0xff86837e },   // dim text
    juce::Colour { 0xff9b7ee8 },   // accent: violet
    juce::Colour { 0xffc0abf7 },   // accent bright
    juce::Colour { 0xffe0a33c },   // secondary: gain reduction, as Capstone
    juce::Colour { 0xffff7a18 },   // bypass glow, the same in every plugin
    juce::Colour { 0xffe2483d }    // over
};

constexpr int kHeaderHeight = 58;
constexpr int kSibilanceHeight = 96;
constexpr int kStatusHeight = 22;
constexpr int kStageTitleHeight = 26;
constexpr int kBarHeight = 22;
constexpr int kLabelHeight = 15;
constexpr int kValueHeight = 15;
constexpr int kReadoutHeight = 17;

/// The gap under each knob's value readout.
///
/// Not cosmetic. A rotary slider's value box sits directly under the knob, and
/// the next row's caption sits directly under that -- so with the rows packed
/// tight, "-45.0" reads as a heading for RANGE rather than as the value of
/// THRESH. Crossbar's keypad hit the same thing, and the fix is the same: the
/// spare height in a column goes **entirely to the bottom** rather than being
/// shared out into the cells.
constexpr int kCellGap = 10;

/// What one knob wants: caption, dial, value, and the gap above the next
/// caption. Cells are capped at this rather than stretched to fill, so a box
/// with two controls draws the same size knob as a box with eight.
constexpr int kIdealCellHeight = kLabelHeight + 70 + kValueHeight + kCellGap;

/// How far down a reduction bar reads at full scale. Chosen against what the
/// stages actually do rather than a round number: the gate's Range reaches
/// 80 dB but a vocal gate is set to 10-20, and a compressor pulling more than
/// 24 dB is a setting to reconsider rather than to meter accurately.
constexpr double kBarRangeDb = 24.0;
} // namespace

// ---------------------------------------------------------------------------
// GainReductionBar
// ---------------------------------------------------------------------------

void GainReductionBar::setReductionDb (double db)
{
    const double clamped = std::min (0.0, db);

    // A peak hold, because gain reduction on a vocal is mostly transient: a
    // bar that only showed the instantaneous value would flicker at the frame
    // rate and read as noise. Two seconds at 30 Hz.
    if (clamped < peakDb_)
    {
        peakDb_ = clamped;
        holdTicks_ = 60;
    }
    else if (holdTicks_ > 0)
    {
        --holdTicks_;
    }
    else
    {
        peakDb_ = std::min (0.0, peakDb_ + 0.5);
    }

    if (! juce::approximatelyEqual (clamped, reductionDb_) || holdTicks_ == 60)
    {
        reductionDb_ = clamped;
        repaint();
    }
}

void GainReductionBar::setStageEnabled (bool enabled)
{
    if (enabled == enabled_)
        return;

    enabled_ = enabled;
    repaint();
}

void GainReductionBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (palette_.background.brighter (0.06f));
    g.fillRoundedRectangle (bounds, 2.0f);

    if (! enabled_)
    {
        // Switched out, and saying so: an empty bar and a dead one look the
        // same, and the difference is the whole question the panel answers.
        g.setColour (palette_.dimText.withAlpha (0.45f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("OUT", bounds, juce::Justification::centred);
        return;
    }

    const auto fraction = [] (double db)
    {
        return static_cast<float> (juce::jlimit (0.0, 1.0, -db / kBarRangeDb));
    };

    // Grows leftward from the right edge, which is the direction reduction
    // goes: full signal at the right, pulled down toward the left.
    const float width = bounds.getWidth();
    const float now = fraction (reductionDb_) * width;

    if (now > 0.5f)
    {
        g.setColour (palette_.secondary.withAlpha (0.85f));
        g.fillRoundedRectangle (bounds.getRight() - now, bounds.getY(),
                                now, bounds.getHeight(), 2.0f);
    }

    const float held = fraction (peakDb_) * width;

    if (held > 1.0f)
    {
        g.setColour (palette_.secondary.brighter (0.5f));
        g.fillRect (bounds.getRight() - held, bounds.getY(), 1.5f, bounds.getHeight());
    }

    g.setColour (palette_.text.withAlpha (reductionDb_ < -0.1 ? 0.9f : 0.35f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (reductionDb_ < -0.05 ? juce::String (reductionDb_, 1) + " dB" : "0.0 dB",
                bounds.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
// SibilanceDisplay
// ---------------------------------------------------------------------------

void SibilanceDisplay::push (double sibilanceDb, double reductionDb)
{
    sibilance_[static_cast<std::size_t> (writeIndex_)] = static_cast<float> (sibilanceDb);
    reduction_[static_cast<std::size_t> (writeIndex_)] = static_cast<float> (reductionDb);

    if (++writeIndex_ >= kHistory)
    {
        writeIndex_ = 0;
        filled_ = true;
    }

    repaint();
}

void SibilanceDisplay::setThresholdDb (double db)
{
    if (juce::approximatelyEqual (db, thresholdDb_))
        return;

    thresholdDb_ = db;
    repaint();
}

void SibilanceDisplay::setStageEnabled (bool enabled)
{
    if (enabled == enabled_)
        return;

    enabled_ = enabled;
    repaint();
}

float SibilanceDisplay::yFor (double db) const
{
    const auto clamped = juce::jlimit (kFloorDb, kCeilingDb, db);
    const auto fraction = (clamped - kFloorDb) / (kCeilingDb - kFloorDb);

    return static_cast<float> (getHeight() - fraction * getHeight());
}

void SibilanceDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.brighter (0.05f));
    g.fillRoundedRectangle (bounds, 3.0f);

    // The threshold, drawn across everything: the whole reason this display
    // exists is to show the detector's reading *against* it.
    const float thresholdY = yFor (thresholdDb_);

    g.setColour (palette_.accent.withAlpha (0.55f));

    for (float x = 2.0f; x < bounds.getWidth() - 2.0f; x += 6.0f)
        g.fillRect (x, thresholdY, 3.0f, 1.0f);

    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (thresholdDb_, 1) + " dB",
                juce::Rectangle<float> (bounds.getRight() - 62.0f, thresholdY - 12.0f, 58.0f, 12.0f),
                juce::Justification::centredRight);

    const int count = filled_ ? kHistory : writeIndex_;

    if (count < 2)
    {
        // An empty graph and a broken one look identical, so say which.
        g.setColour (palette_.dimText.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("waiting for signal", bounds, juce::Justification::centred);
        return;
    }

    const float step = bounds.getWidth() / static_cast<float> (kHistory - 1);

    // The reading itself, as a filled trace. Above the line is an /s/; below
    // it is the body of the voice, which is where a vowel lives however loud
    // the singer gets -- that independence is the point of measuring a ratio.
    juce::Path trace;
    bool started = false;

    for (int i = 0; i < count; ++i)
    {
        const int index = filled_ ? (writeIndex_ + i) % kHistory : i;
        const float x = static_cast<float> (i) * step;
        const float y = yFor (sibilance_[static_cast<std::size_t> (index)]);

        if (! started)
        {
            trace.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            trace.lineTo (x, y);
        }
    }

    g.setColour (enabled_ ? palette_.accentBright : palette_.dimText.withAlpha (0.5f));
    g.strokePath (trace, juce::PathStrokeType (1.4f));

    // What the stage did about it, in the reduction amber, hanging from the
    // top so the two are readable at once rather than overlapping.
    juce::Path reduction;
    started = false;

    for (int i = 0; i < count; ++i)
    {
        const int index = filled_ ? (writeIndex_ + i) % kHistory : i;
        const float x = static_cast<float> (i) * step;
        const auto amount = juce::jlimit (
            0.0f, 1.0f, -reduction_[static_cast<std::size_t> (index)] / 18.0f);
        const float y = amount * bounds.getHeight() * 0.5f;

        if (! started)
        {
            reduction.startNewSubPath (x, 0.0f);
            reduction.lineTo (x, y);
            started = true;
        }
        else
        {
            reduction.lineTo (x, y);
        }
    }

    if (started && enabled_)
    {
        reduction.lineTo (static_cast<float> (count - 1) * step, 0.0f);
        reduction.closeSubPath();

        g.setColour (palette_.secondary.withAlpha (0.30f));
        g.fillPath (reduction);
    }
}

// ---------------------------------------------------------------------------
// StagePanel
// ---------------------------------------------------------------------------

StagePanel::StagePanel (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                        juce::String title, const char* enableParameterId, int columns)
    : state_ (state), palette_ (palette), title_ (std::move (title)),
      enableId_ (enableParameterId), columns_ (columns)
{
    if (enableId_ == nullptr)
        return;

    enableButton_ = std::make_unique<ui::LampButton> ("ON");
    enableButton_->setClickingTogglesState (true);
    enableButton_->setTooltip (
        "Switches this stage out of the chain. Off is the stage's own neutral "
        "setting, which is a bit-exact identity -- the samples pass through "
        "untouched rather than nearly untouched.");
    addAndMakeVisible (*enableButton_);

    enableAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, enableId_, *enableButton_);
}

void StagePanel::addKnob (const char* parameterId, const juce::String& name,
                          const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // What a house knob is lives in ui/HouseControls.hpp: relief, a machined
    // skirt, a tinted track, the value font, and the wheel turned off so it
    // scrolls the panel instead of editing.
    ui::styleKnob (knob->slider, palette_, palette_.accent);
    ui::resetsToDefault (knob->slider, state_, parameterId);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    ui::styleName (knob->label, palette_, palette_.accent);
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->id = parameterId;
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    knobs_.push_back (std::move (knob));
}

void StagePanel::addToggle (const char* parameterId, const juce::String& name,
                            const juce::String& tooltip)
{
    auto toggle = std::make_unique<Toggle> (name);

    toggle->button.setClickingTogglesState (true);
    toggle->button.setTooltip (tooltip);
    addAndMakeVisible (toggle->button);

    toggle->id = parameterId;
    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, toggle->button);

    toggles_.push_back (std::move (toggle));
}

void StagePanel::addReductionBar()
{
    bar_ = std::make_unique<GainReductionBar> (palette_);
    addAndMakeVisible (*bar_);
}

void StagePanel::addReadout (const juce::String& tooltip)
{
    auto readout = std::make_unique<juce::Label>();

    readout->setJustificationType (juce::Justification::centred);
    readout->setColour (juce::Label::textColourId, palette_.dimText);
    readout->setFont (juce::FontOptions (11.5f));
    readout->setTooltip (tooltip);
    addAndMakeVisible (*readout);

    readouts_.push_back (std::move (readout));
}

void StagePanel::setReadout (int index, const juce::String& text)
{
    if (index < 0 || index >= static_cast<int> (readouts_.size()))
        return;

    readouts_[static_cast<std::size_t> (index)]->setText (text, juce::dontSendNotification);
}

bool StagePanel::isStageEnabled() const
{
    if (enableId_ == nullptr)
        return true;

    if (auto* raw = state_.getRawParameterValue (enableId_))
        return raw->load() > 0.5f;

    return true;
}

void StagePanel::refreshEnablement()
{
    const bool on = isStageEnabled();

    for (auto& knob : knobs_)
    {
        knob->slider.setEnabled (on);
        knob->label.setColour (juce::Label::textColourId,
                               on ? palette_.dimText : palette_.dimText.withAlpha (0.4f));
        knob->label.repaint();
    }

    for (auto& toggle : toggles_)
        toggle->button.setEnabled (on);

    if (bar_ != nullptr)
        bar_->setStageEnabled (on);
}

void StagePanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (palette_.background.brighter (0.10f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    // The title bar carries the accent when the stage is in circuit and goes
    // flat when it is not, so the chain reads at a glance from across the room.
    const bool on = isStageEnabled();
    auto titleArea = bounds.removeFromTop (static_cast<float> (kStageTitleHeight));

    g.setColour (on ? palette_.accent.withAlpha (0.16f) : palette_.background.withAlpha (0.35f));
    g.fillRoundedRectangle (titleArea.reduced (2.0f, 2.0f), 3.0f);

    g.setColour (on ? palette_.text : palette_.dimText);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (title_, titleArea.reduced (enableButton_ != nullptr ? 52.0f : 6.0f, 0.0f),
                juce::Justification::centred);
}

void StagePanel::resized()
{
    auto bounds = getLocalBounds().reduced (4, 3);
    auto titleArea = bounds.removeFromTop (kStageTitleHeight);

    if (enableButton_ != nullptr)
        // `sized` adds the glow margin -- the button is larger than the switch
        // drawn in it, so the lit halo has somewhere to land rather than being
        // clipped away by the component's own bounds.
        enableButton_->setBounds (ui::LampButton::sized (34, 18)
                                    .withCentre (titleArea.removeFromLeft (50).getCentre()));

    if (bar_ != nullptr)
        bar_->setBounds (bounds.removeFromBottom (kBarHeight).reduced (2, 2));

    // Toggles sit at the foot of the box, above the bar: they are switches
    // rather than settings and grouping them keeps the knob grid regular.
    if (! toggles_.empty())
    {
        // Each row is the switch plus its glow margin on both sides -- the
        // button is larger than the switch drawn in it so the lit halo is not
        // clipped away by the component's own bounds.
        const int rowHeight = 22 + 2 * ui::LampButton::kGlowMargin;

        auto toggleArea = bounds.removeFromBottom (
            static_cast<int> (toggles_.size()) * rowHeight + 4);

        for (auto& toggle : toggles_)
            toggle->button.setBounds (
                ui::LampButton::sized (juce::jmax (54, toggleArea.getWidth() - 8
                                                        - 2 * ui::LampButton::kGlowMargin), 22)
                  .withCentre (toggleArea.removeFromTop (rowHeight).getCentre()));
    }

    if (knobs_.empty())
        return;

    const int rows = (static_cast<int> (knobs_.size()) + columns_ - 1) / columns_;
    const int cellWidth = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (kIdealCellHeight,
                                       bounds.getHeight() / juce::jmax (1, rows));

    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    bounds.getY() + row * cellHeight,
                                    cellWidth, cellHeight };

        auto& knob = *knobs_[i];
        knob.label.setBounds (cell.removeFromTop (kLabelHeight));

        // The gap comes off the bottom, so the value never abuts the caption
        // below it. See kCellGap.
        cell.removeFromBottom (kCellGap);
        knob.slider.setBounds (cell.reduced (2, 0));
    }

    // Readouts go under the knob grid, in whatever the cap left over.
    if (readouts_.empty())
        return;

    auto readoutArea = bounds.withTop (bounds.getY() + rows * cellHeight);

    for (auto& readout : readouts_)
        readout->setBounds (readoutArea.removeFromTop (kReadoutHeight).reduced (6, 0));
}

// ---------------------------------------------------------------------------
// SyrinxEditor
// ---------------------------------------------------------------------------

SyrinxEditor::SyrinxEditor (SyrinxProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse),
      syrinx_ (processorToUse),
      palette_ (kPalette),
      knobLook_ (kPalette)
{
    setLookAndFeel (&knobLook_);

    header_ = std::make_unique<ui::HeaderBar> (
        syrinx_.getState(), "SYRINX",
        "Vocal channel strip -- gate, de-esser, two compressors, tone",
        ids::bypass, palette_);

    header_->attachSuiteControls (syrinx_.getState(), nullptr, ids::outputTrim, nullptr);

    header_->onSwapRequested = [this]
    {
        syrinx_.getAbCompare().swapSlots();
        header_->setActiveSlot (syrinx_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (syrinx_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        syrinx_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (syrinx_.getAbCompare().otherSlotFilled());
    };

    header_->onTooltipsToggled = [this] (bool enabled)
    {
        syrinx_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setTooltipsEnabled (syrinx_.getTooltipsEnabled());
    tooltips_.setEnabled (syrinx_.getTooltipsEnabled());

    header_->setActiveSlot (syrinx_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (syrinx_.getAbCompare().otherSlotFilled());

    addAndMakeVisible (*header_);

    buildStages();

    sibilance_ = std::make_unique<SibilanceDisplay> (palette_);
    addAndMakeVisible (*sibilance_);

    sibilanceLabel_.setText ("SIBILANCE", juce::dontSendNotification);
    sibilanceLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    sibilanceLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    sibilanceLabel_.setTooltip (syrinx_.describeSibilance());
    addAndMakeVisible (sibilanceLabel_);

    statusLabel_.setJustificationType (juce::Justification::centredRight);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statusLabel_);

    setResizable (true, true);
    setResizeLimits (980, 560, 2200, 1100);
    setSize (1300, 700);

    // Before the first paint, not on the first timer tick. Otherwise a strip
    // opened with the gate switched out draws its gate knobs live for a
    // thirtieth of a second -- which is exactly long enough to be the frame a
    // screenshot catches, and it was.
    updateForSwitches();

    startTimerHz (30);
}

SyrinxEditor::~SyrinxEditor()
{
    setLookAndFeel (nullptr);
}

void SyrinxEditor::buildStages()
{
    auto& state = syrinx_.getState();

    // ---- IN --------------------------------------------------------------

    stages_[input] = std::make_unique<StagePanel> (state, palette_, "IN", nullptr, 1);
    stages_[input]->addKnob (ids::inputTrim, "TRIM",
        "Level into the strip, before anything measures it. Every threshold "
        "below is relative to what arrives here, so moving this moves all of "
        "them -- set it first and leave it.");
    stages_[input]->addKnob (ids::highpass, "HPF", syrinx_.describeHighpass());

    // Peak in and peak out, which is the pair of numbers you actually want
    // while setting a strip: everything between them is the strip's doing.
    stages_[input]->addReadout ("Peak level arriving, after the input trim.");
    stages_[input]->addReadout ("Peak level leaving. If this is near 0 dBFS, "
                                "trim the output rather than letting Capstone "
                                "clean it up.");

    // Written before the first timer tick, so the panel opens showing what
    // these are rather than two blank lines.
    stages_[input]->setReadout (0, "IN  --");
    stages_[input]->setReadout (1, "OUT  --");

    // ---- GATE ------------------------------------------------------------

    stages_[gate] = std::make_unique<StagePanel> (state, palette_, "GATE", ids::gateOn, 2);
    stages_[gate]->addKnob (ids::gateThreshold, "THRESH",
        "Where the gate opens. Set it under the quietest thing you want to "
        "keep, not under the noise: the two are usually much closer together "
        "than they sound.");
    stages_[gate]->addKnob (ids::gateHysteresis, "HYST",
        "How far the signal has to fall BELOW the threshold before the gate "
        "shuts. This is the control that stops chattering, and measurement "
        "says it is the only one that does: a tone sitting on the threshold "
        "flips the gate 1600 times in two seconds with none of it, 20 with a "
        "40 ms hold and no hysteresis, and once with 3 dB of hysteresis.");
    stages_[gate]->addKnob (ids::gateRange, "RANGE",
        "How far down 'closed' is. 10 to 20 dB is the useful setting on a "
        "vocal -- a gate that closes to silence removes the room as well as "
        "the noise and the result breathes in a vacuum. 0 is a bit-exact "
        "bypass of the stage.");
    stages_[gate]->addKnob (ids::gateAttack, "ATTACK",
        "How fast it opens. Fast, or the front of every word is clipped off; "
        "this is not a control that wants to be musical.");
    stages_[gate]->addKnob (ids::gateHold, "HOLD",
        "How long it stays open after the level drops. This is what carries "
        "the gate across the gap inside a word -- hysteresis cannot, because "
        "the signal there really is gone. Measured: 100 ms crosses a 60 ms "
        "gap and still shuts across a 300 ms one.");
    stages_[gate]->addKnob (ids::gateRelease, "RELEASE",
        "How fast it closes once the hold runs out. Slow enough that a tail "
        "fades rather than stops.");
    stages_[gate]->addKnob (ids::gateSidechain, "SC HPF",
        "A high-pass on the gate's DETECTOR only, so a rumbling room does not "
        "hold it open through every pause. The signal itself is untouched. "
        "0 switches it off.");
    stages_[gate]->addReductionBar();

    // ---- DE-ESS ----------------------------------------------------------

    stages_[deEss] = std::make_unique<StagePanel> (state, palette_, "DE-ESS", ids::deEssOn, 2);
    stages_[deEss]->addKnob (ids::deEssCorner, "CORNER",
        "Where the sibilant band starts. 5 to 8 kHz for most voices; lower "
        "for a deep one, higher if it is dulling the words rather than the "
        "esses.");
    stages_[deEss]->addKnob (ids::deEssThreshold, "THRESH",
        "The RATIO of high-band energy to the body of the voice -- not a "
        "level. That is what stops it re-triggering when the singer pushes, "
        "and what stops a bright vowel reading as an /s/. A vowel sits 20 to "
        "40 dB under its own body; an /s/ comes up to around zero.");
    stages_[deEss]->addKnob (ids::deEssRatio, "RATIO",
        "How hard it works once over the threshold.");
    stages_[deEss]->addKnob (ids::deEssKnee, "KNEE",
        "How gradually it comes in around the threshold. A soft knee is what "
        "keeps it from sounding like it is switching on and off.");
    stages_[deEss]->addKnob (ids::deEssRange, "RANGE",
        "The most it may duck. Caps the reduction so a hard /s/ cannot take "
        "the whole top of the voice with it. 0 is a bit-exact bypass.");
    stages_[deEss]->addKnob (ids::deEssAttack, "ATTACK",
        "Fast, because an /s/ is short. Too slow and the front of it is "
        "through before the stage reacts.");
    stages_[deEss]->addKnob (ids::deEssRelease, "RELEASE",
        "How fast it lets go. Too long and the word after the /s/ is dull.");
    stages_[deEss]->addToggle (ids::deEssListen, "LISTEN",
        "Monitors what is being REMOVED rather than what is kept. The fastest "
        "way to tell over-essing from under-essing: you should hear esses and "
        "very little else. Vowels in here means it is lisping.");
    stages_[deEss]->addReductionBar();

    // ---- LEVELLER --------------------------------------------------------

    stages_[leveller] = std::make_unique<StagePanel> (state, palette_, "LEVELLER", ids::comp1On, 2);
    stages_[leveller]->addKnob (ids::comp1Threshold, "THRESH",
        "Where the slow compressor starts working. This one is for the shape "
        "of the performance -- the difference between a loud line and a quiet "
        "one -- so it should be working most of the time, gently.");
    stages_[leveller]->addKnob (ids::comp1Ratio, "RATIO",
        "2:1 to 4:1 here. 1:1 is a bit-exact bypass of the stage.");
    stages_[leveller]->addKnob (ids::comp1Knee, "KNEE",
        "Wide, on this one. A soft knee is most of what makes a leveller "
        "inaudible.");
    stages_[leveller]->addKnob (ids::comp1Attack, "ATTACK",
        "Slow: 20 to 50 ms. Fast attack here would flatten the transients "
        "that make consonants intelligible -- that job belongs to PEAK.");
    stages_[leveller]->addKnob (ids::comp1Release, "RELEASE",
        "Slow: 200 ms and up, so it rides phrases rather than syllables.");
    stages_[leveller]->addKnob (ids::comp1Makeup, "MAKEUP",
        "Puts back what the reduction took, so you judge tone rather than "
        "loudness. 0 dB is exact.");
    stages_[leveller]->addKnob (ids::comp1Mix, "MIX",
        "Parallel compression: blends the compressed signal with the dry one. "
        "1.0 is fully compressed and 0.0 is a bit-exact dry path.");
    stages_[leveller]->addKnob (ids::comp1Sidechain, "SC HPF",
        "A high-pass on the DETECTOR only, so low-frequency weight does not "
        "duck the whole voice. 0 switches it off.");
    stages_[leveller]->addToggle (ids::comp1Auto, "AUTO REL",
        "Program-dependent release: fast for a transient, slow for a "
        "sustained passage. On a voice this is usually the right answer, "
        "which is why it is on by default here and off on PEAK.");
    stages_[leveller]->addReductionBar();

    // ---- PEAK ------------------------------------------------------------

    stages_[peak] = std::make_unique<StagePanel> (state, palette_, "PEAK", ids::comp2On, 2);
    stages_[peak]->addKnob (ids::comp2Threshold, "THRESH",
        "Higher than the leveller's: this one only catches what got past it. "
        "If it is working all the time, the leveller is not doing its job.");
    stages_[peak]->addKnob (ids::comp2Ratio, "RATIO",
        "High -- 6:1 and up. This is a catcher, not a shaper. 1:1 is a "
        "bit-exact bypass.");
    stages_[peak]->addKnob (ids::comp2Knee, "KNEE",
        "Tighter than the leveller's, so it does not start working early.");
    stages_[peak]->addKnob (ids::comp2Attack, "ATTACK",
        "Fast: 1 to 5 ms. Too fast dulls consonants; too slow lets the peak "
        "through, which is the one thing this stage exists to stop.");
    stages_[peak]->addKnob (ids::comp2Release, "RELEASE",
        "Fast: 50 to 150 ms, so it is out of the way before the next word.");
    stages_[peak]->addKnob (ids::comp2Makeup, "MAKEUP",
        "Usually 0 here -- the leveller has already set the level, and this "
        "stage is only clipping the corners off. 0 dB is exact.");
    stages_[peak]->addKnob (ids::comp2Mix, "MIX",
        "Parallel blend. 0.0 is a bit-exact dry path.");
    stages_[peak]->addKnob (ids::comp2Sidechain, "SC HPF",
        "A high-pass on the DETECTOR only. 0 switches it off.");
    stages_[peak]->addToggle (ids::comp2Auto, "AUTO REL",
        "Off by default here: a peak catcher wants a release you chose, not "
        "one that changes with the material.");
    stages_[peak]->addReductionBar();

    // ---- EQ --------------------------------------------------------------

    // Frequency then gain, one band per row, with Q last. Ordering them by
    // parameter declaration instead put MID's gain beside HIGH's frequency,
    // which reads as one band with two corners.
    stages_[eq] = std::make_unique<StagePanel> (state, palette_, "EQ", ids::eqOn, 2);
    stages_[eq]->addKnob (ids::eqLowHz, "LOW Hz", "Corner of the low shelf.");
    stages_[eq]->addKnob (ids::eqLowDb, "LOW",
        "Weight. Cut here rather than boosting if the voice is muddy -- there "
        "is usually too much rather than too little. 0 dB is exact.");
    stages_[eq]->addKnob (ids::eqMidHz, "MID Hz",
        "Where the mid bell sits. 2 to 4 kHz is presence and intelligibility; "
        "400 Hz to 800 Hz is where boxiness lives.");
    stages_[eq]->addKnob (ids::eqMidDb, "MID", "0 dB is exact.");
    stages_[eq]->addKnob (ids::eqHighHz, "HIGH Hz", "Corner of the high shelf.");
    stages_[eq]->addKnob (ids::eqHighDb, "HIGH",
        "Air. It sits AFTER the de-esser, so lifting it here does not feed "
        "the sibilance detector -- which is why the EQ is last. For real air "
        "above 8 kHz, Halo does it better than a shelf can. 0 dB is exact.");
    stages_[eq]->addKnob (ids::eqMidQ, "MID Q",
        "Narrow to fix a problem, wide to shape a tone.");

    for (auto& stagePanel : stages_)
        addAndMakeVisible (*stagePanel);
}

void SyrinxEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void SyrinxEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (kHeaderHeight));

    auto footer = bounds.removeFromBottom (kSibilanceHeight + kStatusHeight);
    auto status = footer.removeFromBottom (kStatusHeight);

    statusLabel_.setBounds (status.reduced (8, 0));

    auto sibilanceArea = footer.reduced (6, 3);
    sibilanceLabel_.setBounds (sibilanceArea.removeFromTop (14).reduced (2, 0));
    sibilance_->setBounds (sibilanceArea);

    // The chain, left to right. IN and EQ are narrower than the dynamics
    // stages because they hold fewer controls; giving every box the same width
    // would leave the interesting ones cramped and the trivial ones half empty.
    bounds.reduce (5, 3);

    const std::array<int, kNumStages> weights { 9, 16, 16, 18, 16, 15 };
    const int total = std::accumulate (weights.begin(), weights.end(), 0);

    int x = bounds.getX();

    for (int i = 0; i < kNumStages; ++i)
    {
        const int width = i == kNumStages - 1
                        ? bounds.getRight() - x
                        : bounds.getWidth() * weights[static_cast<std::size_t> (i)] / total;

        stages_[static_cast<std::size_t> (i)]->setBounds (x, bounds.getY(), width,
                                                          bounds.getHeight());
        x += width;
    }
}

void SyrinxEditor::timerCallback()
{
    const auto& meters = syrinx_.getMeterValues();

    const auto update = [] (StagePanel& stagePanel, float db)
    {
        if (auto* bar = stagePanel.getReductionBar())
            bar->setReductionDb (db);
    };

    update (*stages_[gate], meters.gateDb.load (std::memory_order_relaxed));
    update (*stages_[deEss], meters.deEssDb.load (std::memory_order_relaxed));
    update (*stages_[leveller], meters.comp1Db.load (std::memory_order_relaxed));
    update (*stages_[peak], meters.comp2Db.load (std::memory_order_relaxed));

    const auto levelText = [] (const char* caption, float db)
    {
        return db <= -99.0f ? juce::String (caption) + "  --"
                            : juce::String (caption) + "  " + juce::String (db, 1);
    };

    stages_[input]->setReadout (0, levelText ("IN", meters.inputDb.load (std::memory_order_relaxed)));
    stages_[input]->setReadout (1, levelText ("OUT", meters.outputDb.load (std::memory_order_relaxed)));

    sibilance_->push (meters.sibilanceDb.load (std::memory_order_relaxed),
                      meters.deEssDb.load (std::memory_order_relaxed));

    if (auto* raw = syrinx_.getState().getRawParameterValue (ids::deEssThreshold))
        sibilance_->setThresholdDb (raw->load());

    updateForSwitches();
}

void SyrinxEditor::updateForSwitches()
{
    for (int i = 0; i < kNumStages; ++i)
    {
        auto& stagePanel = *stages_[static_cast<std::size_t> (i)];
        const int on = stagePanel.isStageEnabled() ? 1 : 0;

        if (on == shownEnabled_[static_cast<std::size_t> (i)])
            continue;

        shownEnabled_[static_cast<std::size_t> (i)] = on;
        stagePanel.refreshEnablement();
        stagePanel.repaint();

        if (i == deEss)
            sibilance_->setStageEnabled (on != 0);
    }

    const int identity = syrinx_.isIdentity() ? 1 : 0;

    if (identity == shownIdentity_)
        return;

    shownIdentity_ = identity;

    // The claim is worth making on the panel rather than only in the manual,
    // because "transparent" and "the identity function" are different things
    // and only one of them survives twenty instances in a project.
    statusLabel_.setText (identity != 0
                              ? "Every stage neutral -- output is bit-identical to input"
                              : juce::String{},
                          juce::dontSendNotification);
    statusLabel_.setColour (juce::Label::textColourId,
                            identity != 0 ? palette_.accent : palette_.dimText);
}

} // namespace tezla::syrinx
