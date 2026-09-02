// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <cmath>

#include <tezla/ui/PanelDesign.hpp>

namespace tezla::ictus {

namespace
{
// Ictus's own accent: a hot vermilion -- the beat, the stroke -- against
// Malleus's bone and Anvil's steel. The bypass glow is the same in every
// plugin, though an instrument has nothing to bypass and never shows it.
const ui::Palette kPalette {
    juce::Colour { 0xff141212 },   // background
    juce::Colour { 0xff1c1918 },   // panel
    juce::Colour { 0xffdcd6d0 },   // text
    juce::Colour { 0xff8a827c },   // dim text
    juce::Colour { 0xffe0533c },   // accent: vermilion
    juce::Colour { 0xffff7a5c },   // accent bright
    juce::Colour { 0xff56b8d6 },   // secondary: a cool blue for readings that are not a level
    juce::Colour { 0xffff7a18 }    // bypass glow, the same in every plugin
};

constexpr int kLabelHeight = 16;
constexpr int kMaxCellHeight = 118;
constexpr int kNoteHeight = 56;
constexpr int kStripHeight = 36;

/// The control this plugin is *about*: the pitch a kick lands on is the
/// first thing anybody reaches for, so it is drawn larger.
[[nodiscard]] ui::design::Emphasis emphasisOf (const juce::String& id) noexcept
{
    return id == ids::k1Tune ? ui::design::Emphasis::lead
                             : ui::design::Emphasis::normal;
}

juce::String noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")";
}
} // namespace

// ---------------------------------------------------------------------------
// ControlPage
// ---------------------------------------------------------------------------

void ControlPage::addKnob (const char* parameterId, const juce::String& name,
                           const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // What a house knob is lives in ui/HouseControls.hpp: relief, a machined
    // skirt, a tinted track, the value font, and the wheel turned off so it
    // scrolls the panel instead of editing.
    ui::styleKnob (knob->slider, palette_, palette_.accent, emphasisOf (parameterId));
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

    cells_.push_back ({ Cell::Kind::knob, static_cast<int> (knobs_.size()) });
    knobs_.push_back (std::move (knob));
}

void ControlPage::addSwitch (const char* parameterId, const juce::String& name,
                             const juce::String& legend, const juce::String& tooltip)
{
    auto item = std::make_unique<Switch>();

    item->button = std::make_unique<ui::LampButton> (legend);
    item->button->setClickingTogglesState (true);
    item->button->setTooltip (tooltip);
    addAndMakeVisible (*item->button);

    item->label.setText (name, juce::dontSendNotification);
    ui::styleName (item->label, palette_, palette_.accent);
    item->label.setTooltip (tooltip);
    addAndMakeVisible (item->label);

    item->id = parameterId;
    item->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, *item->button);

    cells_.push_back ({ Cell::Kind::lamp, static_cast<int> (switches_.size()) });
    switches_.push_back (std::move (item));
}

void ControlPage::addGap()
{
    cells_.push_back ({ Cell::Kind::gap, 0 });
}

void ControlPage::setNote (const juce::String& note)
{
    if (note_ == note)
        return;

    note_ = note;
    resized();
    repaint();
}

[[nodiscard]] juce::Colour ControlPage::nameColour() const
{
    return palette_.dimText.interpolatedWith (palette_.accent, ui::design::kLabelTint);
}

void ControlPage::setControlEnabled (const char* parameterId, bool enabled)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
        {
            knob->slider.setEnabled (enabled);
            knob->label.setColour (juce::Label::textColourId,
                                   enabled ? nameColour() : nameColour().withAlpha (0.35f));
            knob->label.repaint();
        }
}

void ControlPage::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    if (note_.isEmpty() || noteArea_.isEmpty())
        return;

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (note_, noteArea_, juce::Justification::centredLeft, 4, 1.0f);
}

void ControlPage::resized()
{
    if (cells_.empty())
        return;

    auto bounds = getLocalBounds().reduced (4, 2);

    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (6, 0);
    else
        noteArea_ = {};

    const int rows = (static_cast<int> (cells_.size()) + columns_ - 1) / columns_;
    const int cellWidth = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (bounds.getHeight() / juce::jmax (1, rows), kMaxCellHeight);

    const int top = bounds.getY() + juce::jmax (0, (bounds.getHeight() - rows * cellHeight) / 2);

    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    top + row * cellHeight,
                                    cellWidth, cellHeight };

        switch (cells_[i].kind)
        {
            case Cell::Kind::knob:
            {
                auto& knob = *knobs_[static_cast<std::size_t> (cells_[i].index)];
                knob.label.setBounds (cell.removeFromTop (kLabelHeight));
                knob.slider.setBounds (ui::emphasised (cell.reduced (4, 0), emphasisOf (knob.id)));
                break;
            }

            case Cell::Kind::lamp:
            {
                auto& item = *switches_[static_cast<std::size_t> (cells_[i].index)];
                item.label.setBounds (cell.removeFromTop (kLabelHeight));
                item.button->setBounds (ui::LampButton::sized (64, 26).withCentre (cell.getCentre()));
                break;
            }

            case Cell::Kind::gap:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// IctusEditor
// ---------------------------------------------------------------------------

IctusEditor::IctusEditor (IctusProcessor& owner)
    : juce::AudioProcessorEditor (&owner),
      ictus_ (owner),
      palette_ (kPalette)
{
    setLookAndFeel (&knobLook_);

    // No bypass parameter: an instrument that is bypassed is an instrument
    // that is silent, which is what muting the track already does.
    header_ = std::make_unique<ui::HeaderBar> (
        ictus_.getState(), "ICTUS", "Drum synthesiser for drum and bass", nullptr, palette_);

    header_->onSwapRequested = [this]
    {
        ictus_.getAbCompare().swapSlots();
        header_->setActiveSlot (ictus_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (ictus_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        ictus_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (ictus_.getAbCompare().otherSlotFilled());
    };

    header_->onTooltipsToggled = [this] (bool enabled)
    {
        ictus_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setActiveSlot (ictus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (ictus_.getAbCompare().otherSlotFilled());
    header_->setTooltipsEnabled (ictus_.getTooltipsEnabled());
    header_->attachSuiteControls (ictus_.getState(), nullptr, ids::output, ids::oversampling,
                                  ids::renderOversampling);
    addAndMakeVisible (*header_);

    // ---- the pad strip ----
    padLabel_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    padLabel_.setColour (juce::Label::textColourId, palette_.accentBright);
    padLabel_.setText ("KICK 1  --  " + noteName (ictus_.getPadNote (PadIndex::kick1)),
                       juce::dontSendNotification);
    padLabel_.setTooltip ("The pad this page edits, and the MIDI note that strikes it: General "
                          "MIDI's kick, so any drum pattern plays it without setup. The other pads "
                          "arrive with their engines; each keeps its GM note.");
    addAndMakeVisible (padLabel_);

    hitButton_.setComponentID ("hit-kick1");
    hitButton_.setColour (juce::TextButton::buttonColourId, palette_.accent.withAlpha (0.35f));
    hitButton_.setColour (juce::TextButton::textColourOffId, palette_.text);
    hitButton_.setTooltip ("Strikes the kick at full velocity, at the top of the next audio block, "
                           "so a patch can be auditioned without a keyboard. A hit while one is "
                           "still sounding crossfades over 1 ms rather than cutting it.");
    hitButton_.onClick = [this] { ictus_.triggerHit (PadIndex::kick1); };
    addAndMakeVisible (hitButton_);

    hitsLabel_.setFont (juce::FontOptions (11.0f));
    hitsLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    hitsLabel_.setJustificationType (juce::Justification::centredRight);
    hitsLabel_.setTooltip ("Hits sounding right now, counted rather than heard: a hit retires the "
                           "sample its envelope lands, and an idle instrument costs nothing "
                           "(measured 0.001% of a core).");
    addAndMakeVisible (hitsLabel_);

    buildPage();

    tooltips_.setEnabled (ictus_.getTooltipsEnabled());
    refreshHeaderTooltips();
    updateGreying();

    setResizable (true, true);
    setResizeLimits (780, 520, 1500, 1000);
    setSize (940, 620);

    startTimerHz (15);
}

IctusEditor::~IctusEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void IctusEditor::buildPage()
{
    auto& state = ictus_.getState();

    page_ = std::make_unique<ControlPage> (state, palette_, 7);

    // ---- pitch ----
    page_->addKnob (ids::k1Tune, "Tune",
        "The pitch the kick LANDS on, 20 to 400 Hz. Everything about the sweep is "
        "relative to this: Start sits above it, Sigh drifts around it. 50 Hz is a "
        "sub kick; 60 to 70 sits up where a break lives. With Follow key lit the "
        "MIDI note sets this instead.");

    page_->addSwitch (ids::k1FollowKey, "Follow key", "Key",
        "Lit: the landed pitch comes from the MIDI note (C1 = 32.7 Hz, E1 = 41.2, "
        "G1 = 49.0) so the kick can sit in the track's key. Dark: Tune sets it and "
        "every note on the pad plays the same kick.");

    page_->addKnob (ids::k1Start, "Start",
        "How far above the landed pitch the hit starts, 0 to 60 semitones. This is "
        "the punch: the body begins high and drops. 0 is a pure sine with no "
        "sweep at all, bit-exactly. Velocity scales it by the amount on Vel > Drop.");

    page_->addKnob (ids::k1Drop, "Drop",
        "How long the drop takes to land, 2 to 200 ms -- the stated time is where "
        "less than 1% of the start height remains. 10 to 30 ms is a tight, "
        "clicky kick; 60 ms and up is a laser. The pitch is exact to 0.016 cents "
        "against the ideal curve at every session rate.");

    page_->addKnob (ids::k1Sigh, "Sigh",
        "A second, slower drop: signed semitones the pitch drifts through the "
        "decay, -12 to +12. Positive starts sharp and settles, the way a real kick "
        "shell relaxes (about 1.5 st for a drum, more for an 808); negative rises. "
        "0 is exact.");

    page_->addKnob (ids::k1SighTime, "Sigh time",
        "How long the sigh takes to land, 100 ms to 2 s. Long times against a "
        "short Decay are inaudible as pitch and heard as weight; short times "
        "against a long tail are the classic bend.");

    page_->addKnob (ids::k1Phase, "Phase",
        "Where the body starts its cycle, 0 to 90 degrees. 0 starts from silence "
        "and rises -- the softest onset. 90 starts at the peak: a full-scale step "
        "on the first sample, which is a click by construction and the punchiest "
        "attack there is. Costs nothing.");

    // ---- colour ----
    page_->addKnob (ids::k1Harmonics, "Harmonics",
        "Adds harmonics to the body in PARALLEL -- the small-speaker control. "
        "Both curves have no linear term, so a quiet body gains nothing but "
        "overtones and the fundamental is never turned up in the process. 0 is "
        "bit-exact off. Runs inside the oversampled section; measured -155 dB of "
        "aliasing at full.");

    page_->addKnob (ids::k1Even, "Even",
        "Which harmonics: 0 is the odd curve only (3rd, 5th -- hollow, hard), 100 "
        "the even curve only (2nd, 4th -- warm, thick), and in between a blend. "
        "The even curve leaves a DC pedestal, so a 10 Hz blocker follows it "
        "whenever this is above 0: -20 dB of bump re peak, and 0.17 dB off a "
        "50 Hz body.");

    page_->addSwitch (ids::k1ToneOn, "Tone on", "Tone",
        "Lit: a low-pass filter tracks the body's pitch so the attack is bright "
        "and the landed tail pure. Dark: no filter in the path at all -- exact, "
        "and decided per hit so switching it never clicks.");

    page_->addKnob (ids::k1Tone, "Tone",
        "The tracking low-pass's cutoff as a multiple of the CURRENT pitch, 1 to "
        "64. At 8, a body dropping from 400 Hz to 50 Hz opens to 3.2 kHz and "
        "settles at 400 Hz. Low values dull the harmonics as the drop lands; high "
        "ones leave them. Retuned every 32 internal samples.");

    // ---- click ----
    page_->addKnob (ids::k1Click, "Click",
        "The beater: one resonant mode struck with the hit, ringing 3 ms. This is "
        "what velocity is most about (Vel > Click), and what makes a kick read on "
        "a small speaker before the body arrives. 0 is exact off.");

    page_->addKnob (ids::k1ClickTone, "Click tone",
        "The click's pitch, 200 Hz to 8 kHz, and the corner of the noise burst's "
        "high-pass. 2 to 4 kHz is a beater on a head; above 6 kHz it is a tick.");

    page_->addKnob (ids::k1Noise, "Noise",
        "A burst of high-passed noise with the hit -- the 909-style attack knob. "
        "Every hit uses its own random stream, seeded from the hit count, so a "
        "roll never repeats a burst and a render is still reproducible.");

    page_->addKnob (ids::k1NoiseTime, "Noise time",
        "How long the noise burst takes to fall 60 dB, 0.5 to 8 ms. Below 2 ms it "
        "is part of the click; above 5 ms it starts to sound like a snare's wires.");

    // ---- amplitude ----
    page_->addKnob (ids::k1Attack, "Attack",
        "The body's rise, 0 to 20 ms. 0 is instant -- the drum this is meant to "
        "be. A few milliseconds softens the click of a Phase setting above 0.");

    page_->addKnob (ids::k1Hold, "Hold",
        "Full level for this long before the decay begins, 0 to 50 ms. A little "
        "hold keeps the drop at full level while it lands.");

    page_->addKnob (ids::k1Decay, "Decay",
        "How long the body takes to fall to silence, 20 ms to 2 s. The hit "
        "retires EXACTLY when it lands -- no tail below hearing, no CPU after. "
        "Velocity can shorten it (Vel > Decay).");

    page_->addKnob (ids::k1Shape, "Shape",
        "The decay's curve: 0 is exponential, the natural ring-down, fast then "
        "slow; 100 is a straight line, which holds the body up longer and then "
        "stops. In between bends between them.");

    page_->addKnob (ids::k1Tail, "Tail",
        "Mixes in a second, longer envelope on the SAME body, so the landed pitch "
        "keeps ringing after the punch has gone. Its attack is the Drop time, so "
        "it carries only the landed pitch and never the sweep. 0 is exact off.");

    page_->addKnob (ids::k1TailTime, "Tail time",
        "How long the tail rings, 100 ms to 4 s. Long tails on a 40 Hz body are "
        "the sub that fills a bar; keep an eye on how it meets the next hit -- a "
        "retrigger crossfades the old one out over 1 ms.");

    page_->addKnob (ids::k1Level, "Level",
        "The kick's own level before the output trim, 0 to 100%. At 100 a Phase "
        "90 hit peaks at full scale; the defaults sit at 80 so the click and "
        "harmonics have room.");

    // ---- velocity ----
    page_->addKnob (ids::k1VelLevel, "Vel > Level",
        "How much velocity moves the level, 0 to 100%. At 100 the level IS the "
        "velocity; at 0 every hit is as loud as the last. Programmed DnB usually "
        "wants this low and Vel > Click high.");

    page_->addKnob (ids::k1VelClick, "Vel > Click",
        "How much velocity moves the click and the noise, 0 to 100%: harder hits "
        "get more beater. The body is untouched, which is how a real drum works.");

    page_->addKnob (ids::k1VelDrop, "Vel > Drop",
        "How much velocity moves the Start height, 0 to 100%: harder hits start "
        "higher and drop further -- more punch at the top of the pattern, less "
        "on the ghosts.");

    page_->addKnob (ids::k1VelDecay, "Vel > Decay",
        "How much velocity moves the decay, 0 to 100%: at 100 a half-velocity "
        "hit decays in half the time. 0 keeps every hit the same length.");

    page_->setNote ("Every knob is read when the hit starts and held for its whole length: a knob "
                    "moved mid-hit changes the next one, never the one sounding. Measured: the "
                    "pitch within 0.016 cents of the ideal curve at 44.1, 48, 96 and 192 kHz; "
                    "two kicks with everything on at 4 to 7% of a core; the plain body bit-exact.");

    addAndMakeVisible (*page_);
}

void IctusEditor::refreshHeaderTooltips()
{
    header_->setOversamplingTooltip (ictus_.describeOversampling());
    header_->setRenderTooltip (ictus_.describeRenderQuality());
}

void IctusEditor::updateGreying()
{
    const auto read = [this] (const char* id)
    {
        if (auto* raw = ictus_.getState().getRawParameterValue (id))
            return raw->load();

        return 0.0f;
    };

    const bool toneOn = read (ids::k1ToneOn) > 0.5f;
    const bool harmonics = read (ids::k1Harmonics) > 0.0f;
    const bool tail = read (ids::k1Tail) > 0.0f;

    if (toneOn == shownToneOn_ && harmonics == shownHarmonics_ && tail == shownTail_)
        return;

    shownToneOn_ = toneOn;
    shownHarmonics_ = harmonics;
    shownTail_ = tail;

    page_->setControlEnabled (ids::k1Tone, toneOn);
    page_->setControlEnabled (ids::k1Even, harmonics);
    page_->setControlEnabled (ids::k1TailTime, tail);
}

void IctusEditor::timerCallback()
{
    const int hits = ictus_.getActiveHitCount();
    hitsLabel_.setText (juce::String (hits) + (hits == 1 ? " hit" : " hits") + " sounding",
                        juce::dontSendNotification);

    // The header's live tooltips are rebuilt only when what they describe
    // has moved, not fifteen times a second.
    const auto read = [this] (const char* id)
    {
        if (auto* raw = ictus_.getState().getRawParameterValue (id))
            return static_cast<int> (std::lround (raw->load()));

        return 0;
    };

    const int factor = ictus_.getOversamplingFactor();
    const int render = read (ids::renderOversampling);
    const int oversampling = read (ids::oversampling);
    const int rateHz = static_cast<int> (std::lround (ictus_.getPreparedRate()));
    const bool offline = ictus_.isNonRealtime();

    if (factor != shownFactor_ || render != shownRender_ || oversampling != shownOversampling_
        || rateHz != shownRateHz_ || offline != shownOffline_)
    {
        shownFactor_ = factor;
        shownRender_ = render;
        shownOversampling_ = oversampling;
        shownRateHz_ = rateHz;
        shownOffline_ = offline;
        refreshHeaderTooltips();
    }

    updateGreying();
}

void IctusEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void IctusEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    auto strip = bounds.removeFromTop (kStripHeight).reduced (10, 4);
    hitButton_.setBounds (strip.removeFromLeft (64));
    strip.removeFromLeft (10);
    hitsLabel_.setBounds (strip.removeFromRight (140));
    padLabel_.setBounds (strip);

    bounds.reduce (8, 6);
    page_->setBounds (bounds);
}

} // namespace tezla::ictus
