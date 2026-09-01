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

namespace tezla::phonoss {

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

/// The two displays, at the **top**. They were a 96 px strip below the fold,
/// which is where a plugin puts the thing nobody is expected to look at -- and
/// the sibilance measure is the one reading in this plugin that no other tool
/// in the suite can give.
constexpr int kDisplayHeight = 148;
constexpr int kDisplayLabelHeight = 15;
constexpr int kStatusHeight = 22;

/// The gap between one stage box and the next, which is where the arrow goes.
///
/// **The order is the point of this plugin.** De-ess before compression is
/// most of the argument for it being one strip rather than five plugins -- a
/// compressor that ducks on an /s/ makes the sibilance *louder* relative to
/// the word after it -- and six boxes in a row say nothing about direction.
/// Two triangles in a gap do.
constexpr int kChainGap = 13;

/// **A hue per stage, rotated along the chain.**
///
/// The house step, so this reads as the same panel as everything else -- see
/// PanelDesign.hpp. What is specific here is the *axis*: index 0 is the input
/// and index 5 is the tone stage, so the colour is a position in the signal
/// path rather than an arbitrary label. Six stages span 90 degrees of hue,
/// which is enough that IN and EQ are plainly different and adjacent stages
/// are plainly related -- which is the truth about them.
[[nodiscard]] juce::Colour stageTint (const ui::Palette& palette, int stage)
{
    return ui::design::tintFor (palette.accent, stage);
}
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

int SibilanceDisplay::slotAt (float x) const
{
    const int count = filled_ ? kHistory : writeIndex_;

    if (count < 2 || getWidth() <= 0)
        return -1;

    const float step = static_cast<float> (getWidth()) / static_cast<float> (kHistory - 1);
    const int position = juce::jlimit (0, count - 1, juce::roundToInt (x / step));

    return filled_ ? (writeIndex_ + position) % kHistory : position;
}

void SibilanceDisplay::mouseMove (const juce::MouseEvent& event)
{
    const int slot = slotAt (event.position.x);

    if (slot == hoverSlot_)
        return;   // the pointer moved a pixel and landed in the same sample

    hoverSlot_ = slot;
    hoverX_ = event.position.x;
    repaint();
}

void SibilanceDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hoverSlot_ < 0)
        return;

    hoverSlot_ = -1;
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

    // -- the pointer's own reading ------------------------------------------
    if (hoverSlot_ < 0)
        return;

    const auto slot = static_cast<std::size_t> (hoverSlot_);
    const float value = sibilance_[slot];
    const float cut = reduction_[slot];

    g.setColour (palette_.text.withAlpha (0.35f));
    g.fillRect (hoverX_ - 0.5f, 0.0f, 1.0f, bounds.getHeight());

    g.setColour (palette_.accentBright);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ hoverX_, yFor (value) }));

    // The number, and **how far past the line it is**, which is the thing the
    // graph cannot say: an /s/ 1 dB over the threshold and one 9 dB over need
    // completely different settings and look nearly identical as curves.
    const auto over = static_cast<double> (value) - thresholdDb_;

    const auto text = juce::String (value, 1) + " dB   "
                    + (over >= 0.0 ? "+" : "") + juce::String (over, 1) + " past   "
                    + juce::String (cut, 1) + " dB cut";

    auto box = juce::Rectangle<float> (hoverX_ + 8.0f, 3.0f, 190.0f, 14.0f);

    if (box.getRight() > bounds.getRight() - 3.0f)
        box = box.withX (hoverX_ - 198.0f);

    g.setColour (palette_.background.withAlpha (0.88f));
    g.fillRoundedRectangle (box.expanded (3.0f, 1.0f), 3.0f);

    g.setColour (palette_.text);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (text, box, juce::Justification::centredLeft, false);
}

// ---------------------------------------------------------------------------
// ChainReductionDisplay
// ---------------------------------------------------------------------------

void ChainReductionDisplay::push (const std::array<float, kLanes>& reductionDb)
{
    for (int lane = 0; lane < kLanes; ++lane)
        history_[static_cast<std::size_t> (lane)][static_cast<std::size_t> (writeIndex_)]
            = reductionDb[static_cast<std::size_t> (lane)];

    if (++writeIndex_ >= kHistory)
    {
        writeIndex_ = 0;
        filled_ = true;
    }

    repaint();
}

void ChainReductionDisplay::setLaneEnabled (int lane, bool enabled)
{
    const auto index = static_cast<std::size_t> (juce::jlimit (0, kLanes - 1, lane));

    if (enabled_[index] == enabled)
        return;

    enabled_[index] = enabled;
    repaint();
}

float ChainReductionDisplay::laneFloorDb (int lane) const
{
    // **The gate's lane is scaled to its own job.** A gate legitimately removes
    // 40 dB and a compressor legitimately removes 3; one scale for both draws
    // the compressors as a flat line, which is exactly the stage whose work
    // this display exists to show.
    return lane == 0 ? -48.0f : -18.0f;
}

juce::Rectangle<float> ChainReductionDisplay::laneBounds (int lane) const
{
    const auto full = getLocalBounds().toFloat().reduced (1.0f);
    const float height = full.getHeight() / static_cast<float> (kLanes);

    return { full.getX(), full.getY() + height * static_cast<float> (lane),
             full.getWidth(), height };
}

int ChainReductionDisplay::slotAt (float x) const
{
    const int count = filled_ ? kHistory : writeIndex_;

    if (count < 2 || getWidth() <= 0)
        return -1;

    const float step = static_cast<float> (getWidth()) / static_cast<float> (kHistory - 1);
    const int position = juce::jlimit (0, count - 1, juce::roundToInt (x / step));

    return filled_ ? (writeIndex_ + position) % kHistory : position;
}

void ChainReductionDisplay::mouseMove (const juce::MouseEvent& event)
{
    const int slot = slotAt (event.position.x);

    if (slot == hoverSlot_)
        return;

    hoverSlot_ = slot;
    hoverX_ = event.position.x;
    repaint();
}

void ChainReductionDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hoverSlot_ < 0)
        return;

    hoverSlot_ = -1;
    repaint();
}

void ChainReductionDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.brighter (0.05f));
    g.fillRoundedRectangle (bounds, 3.0f);

    const int count = filled_ ? kHistory : writeIndex_;
    const float step = bounds.getWidth() / static_cast<float> (kHistory - 1);

    for (int lane = 0; lane < kLanes; ++lane)
    {
        const auto index = static_cast<std::size_t> (lane);
        const auto area = laneBounds (lane);
        const auto tint = tints_[index];
        const bool on = enabled_[index];

        // A hairline between lanes, so four bands read as four rather than as
        // one graph with steps in it.
        if (lane > 0)
        {
            g.setColour (palette_.dimText.withAlpha (0.12f));
            g.fillRect (area.getX() + 2.0f, area.getY(), area.getWidth() - 4.0f, 1.0f);
        }

        if (count >= 2 && on)
        {
            const float floorDb = laneFloorDb (lane);

            juce::Path band;
            band.startNewSubPath (0.0f, area.getY());

            for (int i = 0; i < count; ++i)
            {
                const int slot = filled_ ? (writeIndex_ + i) % kHistory : i;
                const float amount = juce::jlimit (
                    0.0f, 1.0f, history_[index][static_cast<std::size_t> (slot)] / floorDb);

                band.lineTo (static_cast<float> (i) * step,
                             area.getY() + amount * (area.getHeight() - 2.0f));
            }

            band.lineTo (static_cast<float> (count - 1) * step, area.getY());
            band.closeSubPath();

            g.setColour (tint.withAlpha (0.45f));
            g.fillPath (band);

            g.setColour (tint.withAlpha (0.85f));
            g.strokePath (band, juce::PathStrokeType (1.0f));
        }

        // The name, and the live figure, at the two ends of the lane.
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.setColour (on ? tint.withAlpha (0.85f) : palette_.dimText.withAlpha (0.4f));
        g.drawText (names_[index], area.withTrimmedLeft (5.0f).withHeight (11.0f),
                    juce::Justification::centredLeft, false);

        const int newest = filled_ ? (writeIndex_ + kHistory - 1) % kHistory
                                   : juce::jmax (0, writeIndex_ - 1);

        const auto shown = hoverSlot_ >= 0 ? static_cast<std::size_t> (hoverSlot_)
                                           : static_cast<std::size_t> (newest);

        // `-0.0 dB` is not a reading anybody wants to see, and a float that
        // has been through a decibel conversion arrives there constantly.
        const auto value = count < 1 ? 0.0f : history_[index][shown];

        const auto figure = ! on ? juce::String ("off")
                                 : (count < 1
                                      ? juce::String ("--")
                                      : juce::String (value > -0.05f ? 0.0f : value, 1) + " dB");

        g.setColour (on ? palette_.text.withAlpha (0.85f) : palette_.dimText.withAlpha (0.4f));
        g.drawText (figure, area.withTrimmedRight (5.0f).withHeight (11.0f),
                    juce::Justification::centredRight, false);
    }

    if (count < 2)
    {
        g.setColour (palette_.dimText.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("waiting for signal", bounds, juce::Justification::centred);
        return;
    }

    if (hoverSlot_ < 0)
        return;

    // One cursor across all four lanes: the whole point is reading them at the
    // *same instant*, so the line spans the display rather than one lane.
    g.setColour (palette_.text.withAlpha (0.35f));
    g.fillRect (hoverX_ - 0.5f, 0.0f, 1.0f, bounds.getHeight());
}

// ---------------------------------------------------------------------------
// StagePanel
// ---------------------------------------------------------------------------

StagePanel::StagePanel (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                        juce::Colour tint, juce::String title,
                        const char* enableParameterId, int columns)
    : state_ (state), palette_ (palette), tint_ (tint), title_ (std::move (title)),
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
    ui::styleKnob (knob->slider, palette_, tint_, emphasisOf (parameterId));
    ui::resetsToDefault (knob->slider, state_, parameterId);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    ui::styleName (knob->label, palette_, tint_);
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->id = parameterId;
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    knobs_.push_back (std::move (knob));
}

void StagePanel::setEmphasis (const char* lead, std::vector<juce::String> trims)
{
    leadId_ = lead != nullptr ? juce::String (lead) : juce::String{};
    trimIds_ = std::move (trims);
}

ui::design::Emphasis StagePanel::emphasisOf (const juce::String& id) const
{
    if (id == leadId_)
        return ui::design::Emphasis::lead;

    for (const auto& trim : trimIds_)
        if (id == trim)
            return ui::design::Emphasis::trim;

    return ui::design::Emphasis::normal;
}

void StagePanel::setReductionDb (double db)
{
    // A tenth of a decibel is the resolution the title prints, so anything
    // finer than that is a repaint of the same picture -- and this arrives
    // thirty times a second, per stage.
    if (std::abs (db - reductionDb_) < 0.05)
        return;

    reductionDb_ = db;
    repaint (getLocalBounds().removeFromTop (kStageTitleHeight + 2));
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

        // **Back to the stage's own colour, not to the plain grey.** Every
        // name here is warmed toward its stage's hue by `ui::styleName`;
        // writing `palette_.dimText` back on the way to enabled would leave
        // one box's labels cold beside five warm ones, and it would only
        // happen after a switch had been off once. The same bug Malleus and
        // Crossbar had, and it is invisible until somebody toggles something.
        const auto colour = palette_.dimText.interpolatedWith (tint_,
                                                               ui::design::kLabelTint);

        knob->label.setColour (juce::Label::textColourId,
                               on ? colour : colour.withAlpha (0.4f));
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

    // **The spine**, in the stage's own hue: the cue that survives being
    // glanced at, in the place a scanning eye already goes.
    const bool on = isStageEnabled();

    {
        juce::Path spine;
        spine.addRoundedRectangle (bounds.getX() + 1.0f, bounds.getY() + 4.0f,
                                   3.0f, bounds.getHeight() - 8.0f,
                                   1.5f, 1.5f, true, false, true, false);

        g.setColour (tint_.withAlpha (on ? 0.85f : 0.25f));
        g.fillPath (spine);
    }

    // The title bar carries the stage's hue when it is in circuit and goes
    // flat when it is not, so the chain reads at a glance from across the room.
    auto titleArea = bounds.removeFromTop (static_cast<float> (kStageTitleHeight));

    // **How hard it is working, as light.** A stage pulling 6 dB is a
    // different object from one pulling 0.2, and the difference matters more
    // than any single knob on it -- so the bar brightens with the reduction
    // rather than only saying "in circuit".
    const auto working = on ? juce::jlimit (0.0f, 1.0f,
                                            static_cast<float> (-reductionDb_) / 12.0f)
                            : 0.0f;

    g.setColour (on ? tint_.withAlpha (0.16f + 0.34f * working)
                    : palette_.background.withAlpha (0.35f));
    g.fillRoundedRectangle (titleArea.reduced (2.0f, 2.0f), 3.0f);

    g.setColour (on ? palette_.text : palette_.dimText);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (title_, titleArea.reduced (enableButton_ != nullptr ? 52.0f : 6.0f, 0.0f),
                juce::Justification::centred);

    // The figure, right-aligned in the same bar. Only where there is a
    // reduction to report -- IN and EQ do not reduce, and printing "0.0 dB"
    // for them would say they were idle rather than that they are not that
    // kind of stage.
    if (bar_ == nullptr)
        return;

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (on ? palette_.text.withAlpha (0.45f + 0.5f * working)
                    : palette_.dimText.withAlpha (0.35f));
    g.drawText (on ? juce::String (reductionDb_ > -0.05 ? 0.0 : reductionDb_, 1) + " dB"
                   : juce::String ("off"),
                titleArea.withTrimmedRight (7.0f).withHeight (titleArea.getHeight()),
                juce::Justification::centredRight, false);
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

        // **Emphasis is a size**, and a size is the one hierarchy cue that
        // survives being glanced at. Threshold is what a gate or a compressor
        // is *set by*; Knee and the sidechain filter are set once and then
        // ignored. The cell keeps its footprint either way.
        knob.slider.setBounds (ui::emphasised (cell.reduced (2, 0), emphasisOf (knob.id)));
    }

    // Readouts go under the knob grid, in whatever the cap left over.
    if (readouts_.empty())
        return;

    auto readoutArea = bounds.withTop (bounds.getY() + rows * cellHeight);

    for (auto& readout : readouts_)
        readout->setBounds (readoutArea.removeFromTop (kReadoutHeight).reduced (6, 0));
}

// ---------------------------------------------------------------------------
// PhonossEditor
// ---------------------------------------------------------------------------

PhonossEditor::PhonossEditor (PhonossProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse),
      phonoss_ (processorToUse),
      palette_ (kPalette),
      knobLook_ (kPalette)
{
    setLookAndFeel (&knobLook_);

    header_ = std::make_unique<ui::HeaderBar> (
        phonoss_.getState(), "PHONOSS",
        "Vocal channel strip -- gate, de-esser, two compressors, tone",
        ids::bypass, palette_);

    header_->attachSuiteControls (phonoss_.getState(), nullptr, ids::outputTrim, nullptr);

    header_->onSwapRequested = [this]
    {
        phonoss_.getAbCompare().swapSlots();
        header_->setActiveSlot (phonoss_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (phonoss_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        phonoss_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (phonoss_.getAbCompare().otherSlotFilled());
    };

    header_->onTooltipsToggled = [this] (bool enabled)
    {
        phonoss_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setTooltipsEnabled (phonoss_.getTooltipsEnabled());
    tooltips_.setEnabled (phonoss_.getTooltipsEnabled());

    header_->setActiveSlot (phonoss_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (phonoss_.getAbCompare().otherSlotFilled());

    addAndMakeVisible (*header_);

    buildStages();

    sibilance_ = std::make_unique<SibilanceDisplay> (palette_);
    addAndMakeVisible (*sibilance_);

    sibilanceLabel_.setText ("SIBILANCE  vs THRESHOLD", juce::dontSendNotification);
    sibilanceLabel_.setColour (juce::Label::textColourId,
                               palette_.dimText.interpolatedWith (stageTint (palette_, deEss),
                                                                  ui::design::kLabelTint));
    sibilanceLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    sibilanceLabel_.setTooltip (phonoss_.describeSibilance());
    addAndMakeVisible (sibilanceLabel_);

    // **Listen belongs beside the display it is used with.** It was at the
    // bottom of the de-ess column, three boxes away from the only picture that
    // tells you whether it is lisping.
    listenButton_ = std::make_unique<ui::LampButton> ("LISTEN");
    listenButton_->setClickingTogglesState (true);
    listenButton_->setTooltip (
        "Monitors what is being REMOVED rather than what is kept. The fastest "
        "way to tell over-essing from under-essing: you should hear esses and "
        "very little else. Vowels in here means it is lisping.");
    addAndMakeVisible (*listenButton_);

    listenAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        phonoss_.getState(), ids::deEssListen, *listenButton_);

    reduction_ = std::make_unique<ChainReductionDisplay> (
        palette_,
        std::array<juce::Colour, 4> { stageTint (palette_, gate),
                                      stageTint (palette_, deEss),
                                      stageTint (palette_, leveller),
                                      stageTint (palette_, peak) },
        std::array<juce::String, 4> { "GATE", "DE-ESS", "LEVELLER", "PEAK" });
    addAndMakeVisible (*reduction_);

    reductionLabel_.setText ("WHERE THE WORK IS DONE", juce::dontSendNotification);
    reductionLabel_.setColour (juce::Label::textColourId,
                               palette_.dimText.interpolatedWith (stageTint (palette_, leveller),
                                                                  ui::design::kLabelTint));
    reductionLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    reductionLabel_.setTooltip (
        "Every stage's gain reduction on **one time axis**, which is the only "
        "way to see them working against each other.\n\n"
        "Four separate meters share no clock, so a de-esser ducking 40 ms "
        "before the leveller does looks exactly like the two ducking together. "
        "Here it does not: the classic vocal fault is a compressor chewing on "
        "an /s/ the de-esser was about to remove, and it is visible as two "
        "lanes dipping in sequence.\n\n"
        "The gate's lane is scaled to 48 dB and the other three to 18, because "
        "a gate legitimately removes 40 dB and a compressor legitimately "
        "removes 3. Hover for the figures at any instant.");
    addAndMakeVisible (reductionLabel_);

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

PhonossEditor::~PhonossEditor()
{
    setLookAndFeel (nullptr);
}

void PhonossEditor::buildStages()
{
    auto& state = phonoss_.getState();

    // ---- IN --------------------------------------------------------------

    stages_[input] = std::make_unique<StagePanel> (state, palette_, stageTint (palette_, input), "IN", nullptr, 1);
    stages_[input]->setEmphasis (ids::inputTrim, {});
    stages_[input]->addKnob (ids::inputTrim, "TRIM",
        "Level into the strip, before anything measures it. Every threshold "
        "below is relative to what arrives here, so moving this moves all of "
        "them -- set it first and leave it.");
    stages_[input]->addKnob (ids::highpass, "HPF", phonoss_.describeHighpass());

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

    stages_[gate] = std::make_unique<StagePanel> (state, palette_, stageTint (palette_, gate), "GATE", ids::gateOn, 2);
    stages_[gate]->setEmphasis (ids::gateThreshold, { ids::gateHysteresis, ids::gateSidechain });
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

    stages_[deEss] = std::make_unique<StagePanel> (state, palette_, stageTint (palette_, deEss), "DE-ESS", ids::deEssOn, 2);
    stages_[deEss]->setEmphasis (ids::deEssThreshold, { ids::deEssKnee, ids::deEssCorner });
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
    // Listen lives beside the sibilance display rather than here -- see the
    // editor's constructor.
    stages_[deEss]->addReductionBar();

    // ---- LEVELLER --------------------------------------------------------

    stages_[leveller] = std::make_unique<StagePanel> (state, palette_, stageTint (palette_, leveller), "LEVELLER", ids::comp1On, 2);
    stages_[leveller]->setEmphasis (ids::comp1Threshold, { ids::comp1Knee, ids::comp1Sidechain });
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

    stages_[peak] = std::make_unique<StagePanel> (state, palette_, stageTint (palette_, peak), "PEAK", ids::comp2On, 2);
    stages_[peak]->setEmphasis (ids::comp2Threshold, { ids::comp2Knee, ids::comp2Sidechain });
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
    stages_[eq] = std::make_unique<StagePanel> (state, palette_, stageTint (palette_, eq), "EQ", ids::eqOn, 2);
    stages_[eq]->setEmphasis (ids::eqMidDb, { ids::eqMidQ });
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

void PhonossEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);

    // **The chain, drawn as one.** An arrow in each gap, at the height of the
    // title bars, in the colour of the stage it points *into* -- so the
    // gradient reads as a direction rather than as six colours, and the fixed
    // order looks decided rather than arbitrary.
    if (chainRow_.isEmpty())
        return;

    const float y = static_cast<float> (chainRow_.getY() + kStageTitleHeight / 2 + 3);

    for (int i = 1; i < kNumStages; ++i)
    {
        const auto& previous = *stages_[static_cast<std::size_t> (i - 1)];
        const auto& next = *stages_[static_cast<std::size_t> (i)];

        const float centre = 0.5f * static_cast<float> (previous.getRight() + next.getX());

        juce::Path arrow;
        arrow.startNewSubPath (centre - 2.5f, y - 3.5f);
        arrow.lineTo (centre + 2.5f, y);
        arrow.lineTo (centre - 2.5f, y + 3.5f);

        g.setColour (stageTint (palette_, i).withAlpha (0.75f));
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
}

void PhonossEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (kHeaderHeight));

    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (8, 0));

    // **The displays, at the top, side by side.**
    //
    // Left asks "is it triggering on the right thing"; right asks "which stage
    // is doing the work". Two questions, two panes -- rather than one pane
    // doing both jobs badly, which is what a single strip along the bottom
    // was.
    auto displays = bounds.removeFromTop (kDisplayHeight).reduced (6, 3);

    auto left = displays.removeFromLeft (displays.getWidth() / 2);
    left.removeFromRight (4);
    auto right = displays;
    right.removeFromLeft (4);

    // The de-ess Listen sits in the left display's caption row, at its right
    // end -- the one control that belongs to that picture rather than to a
    // stage box.
    auto leftCaption = left.removeFromTop (kDisplayLabelHeight);

    const int listenWidth = 66 + 2 * ui::LampButton::kGlowMargin;

    listenButton_->setBounds (
        ui::LampButton::sized (66, 17)
          .withCentre (leftCaption.removeFromRight (listenWidth).getCentre()));

    sibilanceLabel_.setBounds (leftCaption.reduced (2, 0));
    sibilance_->setBounds (left);

    reductionLabel_.setBounds (right.removeFromTop (kDisplayLabelHeight).reduced (2, 0));
    reduction_->setBounds (right);

    // The chain, left to right. IN and EQ are narrower than the dynamics
    // stages because they hold fewer controls; giving every box the same width
    // would leave the interesting ones cramped and the trivial ones half empty.
    bounds.reduce (5, 3);

    const std::array<int, kNumStages> weights { 9, 16, 16, 18, 16, 15 };
    const int total = std::accumulate (weights.begin(), weights.end(), 0);

    // The gaps come out of the row before the weights divide what is left, so
    // a narrow window loses box width rather than losing the arrows.
    const int usable = bounds.getWidth() - (kNumStages - 1) * kChainGap;

    int x = bounds.getX();

    for (int i = 0; i < kNumStages; ++i)
    {
        const int width = i == kNumStages - 1
                        ? bounds.getRight() - x
                        : usable * weights[static_cast<std::size_t> (i)] / total;

        stages_[static_cast<std::size_t> (i)]->setBounds (x, bounds.getY(), width,
                                                          bounds.getHeight());
        x += width + kChainGap;
    }

    chainRow_ = bounds;
}

void PhonossEditor::timerCallback()
{
    const auto& meters = phonoss_.getMeterValues();

    const auto update = [] (StagePanel& stagePanel, float db)
    {
        if (auto* bar = stagePanel.getReductionBar())
            bar->setReductionDb (db);

        // And the title bar, which is where it is readable while your eyes are
        // on a knob.
        stagePanel.setReductionDb (db);
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

    reduction_->push ({ meters.gateDb.load (std::memory_order_relaxed),
                        meters.deEssDb.load (std::memory_order_relaxed),
                        meters.comp1Db.load (std::memory_order_relaxed),
                        meters.comp2Db.load (std::memory_order_relaxed) });

    if (auto* raw = phonoss_.getState().getRawParameterValue (ids::deEssThreshold))
        sibilance_->setThresholdDb (raw->load());

    updateForSwitches();
}

void PhonossEditor::updateForSwitches()
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

        // The reduction lanes run gate, de-ess, leveller, peak -- the four
        // stages that reduce -- so the stage index maps onto a lane by
        // subtracting the input.
        if (i >= gate && i <= peak)
            reduction_->setLaneEnabled (i - gate, on != 0);
    }

    const int identity = phonoss_.isIdentity() ? 1 : 0;

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

} // namespace tezla::phonoss
