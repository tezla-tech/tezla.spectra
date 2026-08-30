// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tezla::crossbar {

namespace
{
// Crossbar's own accent: the green of a lit keypad, against Emberdrive's
// ember, Ferrite's oxide and Malleus's bone. The secondary is bakelite amber
// -- the colour of the handset rather than of the keys.
const ui::Palette kPalette {
    juce::Colour { 0xff121412 },   // background
    juce::Colour { 0xff191d19 },   // panel
    juce::Colour { 0xffd6dcd4 },   // text
    juce::Colour { 0xff7f887e },   // dim text
    juce::Colour { 0xff58b672 },   // accent: keypad green
    juce::Colour { 0xff7fe39b },   // accent bright
    juce::Colour { 0xffc9a227 },   // secondary: bakelite amber
    juce::Colour { 0xffff7a18 }    // bypass glow, the same in every plugin
};

constexpr int kLabelHeight = 16;
constexpr int kValueHeight = 18;
constexpr int kMaxCellHeight = 130;
constexpr int kNoteHeight = 58;

constexpr int kHeaderHeight = 40;
constexpr int kTabHeight = 28;
} // namespace

// ---------------------------------------------------------------------------
// WrappingLabel
// ---------------------------------------------------------------------------

void WrappingLabel::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (getFont());
    g.drawFittedText (getText(), getLocalBounds(), getJustificationType(), 3, 1.0f);
}

// ---------------------------------------------------------------------------
// ControlPage
// ---------------------------------------------------------------------------

void ControlPage::addKnob (const char* parameterId, const juce::String& name,
                           const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, kValueHeight);
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, palette_.accent);
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId,
                            palette_.panel.brighter (0.25f));
    knob->slider.setColour (juce::Slider::thumbColourId, palette_.accentBright);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId,
                            juce::Colours::transparentBlack);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, palette_.dimText);
    knob->label.setFont (juce::FontOptions (12.0f));
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->id = parameterId;
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    cells_.push_back ({ Cell::Kind::knob, static_cast<int> (knobs_.size()) });
    knobs_.push_back (std::move (knob));
}

void ControlPage::addChoice (const char* parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    auto choice = std::make_unique<Choice>();

    // Populated from the parameter itself, so the box can never disagree with
    // the frozen choice list.
    if (auto* parameter =
            dynamic_cast<juce::AudioParameterChoice*> (state_.getParameter (parameterId)))
        choice->box.addItemList (parameter->choices, 1);
    else
        jassertfalse;   // addChoice used on something that is not a choice parameter

    choice->box.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    choice->box.setColour (juce::ComboBox::textColourId, palette_.text);
    choice->box.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    choice->box.setTooltip (tooltip);
    addAndMakeVisible (choice->box);

    choice->label.setText (name, juce::dontSendNotification);
    choice->label.setJustificationType (juce::Justification::centred);
    choice->label.setColour (juce::Label::textColourId, palette_.dimText);
    choice->label.setFont (juce::FontOptions (12.0f));
    choice->label.setTooltip (tooltip);
    addAndMakeVisible (choice->label);

    choice->id = parameterId;
    choice->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state_, parameterId, choice->box);

    cells_.push_back ({ Cell::Kind::choice, static_cast<int> (choices_.size()) });
    choices_.push_back (std::move (choice));
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

void ControlPage::setControlEnabled (const char* parameterId, bool enabled)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
        {
            knob->slider.setEnabled (enabled);
            knob->label.setColour (juce::Label::textColourId,
                                   enabled ? palette_.dimText
                                           : palette_.dimText.withAlpha (0.35f));
            knob->label.repaint();
        }

    for (auto& choice : choices_)
        if (choice->id == id)
        {
            choice->box.setEnabled (enabled);
            choice->label.setColour (juce::Label::textColourId,
                                     enabled ? palette_.dimText
                                             : palette_.dimText.withAlpha (0.35f));
            choice->label.repaint();
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

    // Four lines, and the box is sized for four. These notes carry the
    // measured numbers behind each page -- a note silently cut in half is
    // worse than no note.
    g.drawFittedText (note_, noteArea_, juce::Justification::centredTop, 4, 1.0f);
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

    const int top = bounds.getY()
                  + juce::jmax (0, (bounds.getHeight() - rows * cellHeight) / 2);

    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    top + row * cellHeight,
                                    cellWidth, cellHeight };

        // The air goes at the BOTTOM of the cell rather than being split
        // between top and bottom, and that asymmetry is the point: a knob's
        // value readout has to sit closer to its own knob than to the label
        // of the row underneath, or the number reads as though it belongs to
        // the control below it. Caught by screenshotting the panel -- an
        // even split left the two gaps within ten pixels of each other and
        // "2.00" looked like a heading for Sustain.
        cell.removeFromBottom (16);

        switch (cells_[i].kind)
        {
            case Cell::Kind::knob:
            {
                auto& knob = *knobs_[static_cast<std::size_t> (cells_[i].index)];
                knob.label.setBounds (cell.removeFromTop (kLabelHeight));
                knob.slider.setBounds (cell.reduced (4, 0));
                break;
            }
            case Cell::Kind::choice:
            {
                auto& choice = *choices_[static_cast<std::size_t> (cells_[i].index)];
                choice.label.setBounds (cell.removeFromTop (kLabelHeight));
                choice.box.setBounds (cell.withSizeKeepingCentre (
                    juce::jmin (cell.getWidth() - 12, 168), 26));
                break;
            }
            case Cell::Kind::gap:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// CrossbarEditor
// ---------------------------------------------------------------------------

CrossbarEditor::CrossbarEditor (CrossbarProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      crossbar_ (processorToUse),
      palette_ (kPalette)
{
    titleLabel_.setText ("CROSSBAR", juce::dontSendNotification);
    titleLabel_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel_.setColour (juce::Label::textColourId, palette_.text);
    addAndMakeVisible (titleLabel_);

    subtitleLabel_.setText ("Telephone tones, and the line they came down",
                            juce::dontSendNotification);
    subtitleLabel_.setFont (juce::FontOptions (12.0f));
    subtitleLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    subtitleLabel_.setTooltip (
        "A crossbar switch is the matrix that connected calls from the 1930s on: "
        "horizontal and vertical bars, and a call is where one of each crosses. "
        "DTMF is the same idea in the audio band -- four row tones, four column "
        "tones, and a key is the pair that crosses.");
    addAndMakeVisible (subtitleLabel_);

    vendorLabel_.setText ("Tezla Tech", juce::dontSendNotification);
    vendorLabel_.setFont (juce::FontOptions (12.0f));
    vendorLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    vendorLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (vendorLabel_);

    voicesLabel_.setFont (juce::FontOptions (11.0f));
    voicesLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    voicesLabel_.setJustificationType (juce::Justification::centredRight);
    voicesLabel_.setTooltip (
        "Sounding tones of the 16 available. A voice is counted while its key is "
        "held and through its release; once the envelope reaches zero the voice is "
        "gone and costs one branch.");
    addAndMakeVisible (voicesLabel_);

    tooltipsButton_.setClickingTogglesState (true);
    tooltipsButton_.setToggleState (crossbar_.getTooltipsEnabled(),
                                    juce::dontSendNotification);
    tooltipsButton_.setTooltip ("Tooltips on or off. This one is always on.");
    tooltipsButton_.onClick = [this]
    {
        crossbar_.setTooltipsEnabled (tooltipsButton_.getToggleState());
        tooltips_.setEnabled (tooltipsButton_.getToggleState());
    };
    addAndMakeVisible (tooltipsButton_);

    buildPages();

    const char* tabNames[kNumPages] { "TONE", "LINE", "DIAL" };
    const char* tabIds[kNumPages] { "tab-tone", "tab-line", "tab-dial" };

    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (tabNames[i]);
        tabs_[static_cast<std::size_t> (i)].setComponentID (tabIds[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    tooltips_.setEnabled (crossbar_.getTooltipsEnabled());

    showPage (0);

    setResizable (true, true);
    setResizeLimits (720, 500, 1400, 1000);
    setSize (860, 600);

    startTimerHz (15);
}

CrossbarEditor::~CrossbarEditor() = default;

void CrossbarEditor::buildPages()
{
    auto& state = crossbar_.getState();

    // ---- TONE ------------------------------------------------------------

    auto tone = std::make_unique<ControlPage> (state, palette_, 4);

    tone->addChoice (ids::region, "Region",
                     "Which national call-progress set the tones come from. DTMF is "
                     "international and does not change; everything else does. North "
                     "America is the Bell Precise Tone Plan -- dial 350+440, busy "
                     "480+620 at half a second on and off. The United Kingdom is BT's "
                     "-- dial 350+450, whose 100 Hz beat is the sound people mean by "
                     "'British dial tone', and the 0.4/0.2/0.4/2.0 double ring.");

    tone->addChoice (ids::cadence, "Cadence",
                     "From key: the cadence starts when you press. Free running: as an "
                     "exchange does it, the tone was already going and you hear "
                     "whatever part of it is passing. Steady: no cadence at all, so a "
                     "busy tone becomes a drone and the call-progress row becomes a "
                     "chord.");

    tone->addKnob (ids::twist, "Twist",
                   "The level of a DTMF pair's high tone relative to its low one. Real "
                   "transmitters send +2 dB to survive line loss, which is the default; "
                   "ITU-T Q.24 requires a receiver to accept 8 dB of it and 4 dB the "
                   "other way, which is the range. It changes the balance, not the "
                   "loudness -- the pair always peaks at full scale.");

    tone->addKnob (ids::mapRoot, "Map root",
                   "Where the map starts, as a MIDI note. 36 is C1, where a drum map "
                   "starts. The first octave from here is the keypad -- 1 2 3 4 5 6 7 "
                   "8 9 * 0 # -- then A B C D, then the twelve call-progress tones, "
                   "then the eight DTMF frequencies on their own, then the dial key. "
                   "Thirty-seven keys in all; anything outside is silent.");

    tone->addKnob (ids::attack, "Attack",
                   "How long a tone takes to reach full level. 2 ms is a telephone; "
                   "hundreds of milliseconds turns the keypad into a pad.");

    tone->addKnob (ids::decay, "Decay",
                   "How long it takes to fall from full level to the sustain. Inert "
                   "while Sustain is at 1, which is where a telephone tone lives.");

    tone->addKnob (ids::sustain, "Sustain",
                   "The level a held key settles at. 1 is a telephone -- a dial tone "
                   "does not decay. Below 1 the keypad becomes percussive.");

    tone->addKnob (ids::release, "Release",
                   "How long a tone takes to fade after the key is up. The voice is "
                   "retired the moment it reaches zero, and costs nothing after that.");

    pages_[0] = std::move (tone);
    addChildComponent (*pages_[0]);

    // ---- LINE ------------------------------------------------------------

    auto line = std::make_unique<ControlPage> (state, palette_, 3);

    line->addChoice (ids::band, "Band",
                     "The channel's bandwidth, fourth-order at both edges. Toll "
                     "300-3400 is ITU-T G.712 and is the sound everybody means by "
                     "'telephone'. Wideband 50-7000 is G.722, what HD voice widened it "
                     "to. Handset and Speaker are narrower still. It sits BEFORE the "
                     "rate reduction, which is what makes it the anti-alias filter -- "
                     "turn it off for the crunch.");

    line->addChoice (ids::rate, "Rate",
                     "Sample-and-hold rate reduction. 8 kHz is the network's own rate, "
                     "the one G.711 is defined at, and the default. 16 kHz is G.722. "
                     "This runs at the host rate with no antialiasing on purpose: the "
                     "folded images ARE the sound, which is the one place in this suite "
                     "where aliasing is wanted.");

    line->addChoice (ids::codec, "Codec",
                     "How the samples are quantised. Mu-law is North America and Japan, "
                     "A-law is Europe -- both are G.711's eight LOGARITHMIC bits, so "
                     "the quantisation noise rides up and down with the signal and the "
                     "signal-to-noise ratio stays put across 40 dB. Linear is a plain "
                     "bit crusher for comparison, and its noise floor does not move: "
                     "that is the difference between a telephone and a sampler.");

    line->addKnob (ids::bits, "Bits",
                   "How many bits of the code word survive. In a companding mode 1 to "
                   "8, and taking one away is what a T1 span did when it stole the "
                   "low bit for signalling -- 7-bit mu-law is a sound the network "
                   "really made. Each bit removed costs about 7 dB. In Linear mode 1 "
                   "to 16, where 16 is bit-exact bypass.");

    line->addKnob (ids::noise, "Line noise",
                   "Hiss on the loop, band-limited with everything else because that "
                   "is where it comes from. Exactly zero at zero -- not merely quiet. "
                   "The taper is squared, so the useful settings are in the bottom half "
                   "of the travel: -35 dBFS at the top, -47 at halfway.");

    line->addKnob (ids::level, "Level",
                   "Output trim. Sixteen keys at once sum, as on any polyphonic "
                   "instrument, and with a codec switched on the sum clips at the "
                   "codec's own ceiling -- which is what a telephone channel does, and "
                   "why this control has 60 dB of downward range.");

    pages_[1] = std::move (line);
    addChildComponent (*pages_[1]);

    // ---- DIAL ------------------------------------------------------------

    auto dial = std::make_unique<ControlPage> (state, palette_, 3);

    dial->addChoice (ids::dialMode, "Mode",
                     "Tone sends DTMF pairs, as a push-button phone does. Pulse breaks "
                     "the loop ten times a second, as a rotary dial does -- so a digit "
                     "lasts as long as it is worth, and a '0' takes a full second where "
                     "a '1' takes a tenth. That is why short emergency numbers were "
                     "chosen.");

    dial->addKnob (ids::dialDigit, "Digit",
                   "How long each digit sounds in Tone mode. Inert in Pulse mode, "
                   "where a digit's length is its own value.");

    dial->addKnob (ids::dialGap, "Gap",
                   "The silence between digits. 100 ms is a push-button phone dialling "
                   "briskly; a rotary dial needs about 700 ms, because the dial has to "
                   "return before the next digit.");

    pages_[2] = std::move (dial);
    addChildComponent (*pages_[2]);
}

void CrossbarEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        const bool active = i == currentPage_;

        if (pages_[static_cast<std::size_t> (i)] != nullptr)
            pages_[static_cast<std::size_t> (i)]->setVisible (active);

        tabs_[static_cast<std::size_t> (i)].setColour (
            juce::TextButton::buttonColourId,
            active ? palette_.accent.withAlpha (0.35f) : palette_.panel);
        tabs_[static_cast<std::size_t> (i)].setColour (
            juce::TextButton::textColourOffId, active ? palette_.text : palette_.dimText);
    }

    resized();
}

void CrossbarEditor::timerCallback()
{
    voicesLabel_.setText (juce::String (crossbar_.getActiveVoiceCount()) + " / 16",
                          juce::dontSendNotification);

    updateForSwitches();
}

void CrossbarEditor::updateForSwitches()
{
    const auto& state = crossbar_.getState();

    const auto indexOf = [&state] (const char* id)
    {
        if (auto* raw = state.getRawParameterValue (id))
            return static_cast<int> (raw->load());

        return 0;
    };

    const int codec = indexOf (ids::codec);
    const int rate = indexOf (ids::rate);
    const int band = indexOf (ids::band);
    const int dialMode = indexOf (ids::dialMode);
    const int cadence = indexOf (ids::cadence);
    const double effective = crossbar_.getEffectiveRateHz();

    if (codec == shownCodec_ && rate == shownRate_ && band == shownBand_
          && dialMode == shownDialMode_ && cadence == shownCadence_
          && std::abs (effective - shownEffectiveRate_) < 1.0)
        return;

    shownCodec_ = codec;
    shownRate_ = rate;
    shownBand_ = band;
    shownDialMode_ = dialMode;
    shownCadence_ = cadence;
    shownEffectiveRate_ = effective;

    // Bits does nothing with the codec switched off, and a knob that moves and
    // does nothing reads as a broken plugin.
    pages_[1]->setControlEnabled (ids::bits, codec != 0);

    // A pulsed digit's length is its own value, so the Digit control is inert.
    pages_[2]->setControlEnabled (ids::dialDigit, dialMode == 0);

    // The LINE note says what the chain is actually doing right now, in the
    // order it does it -- including the rate the host is running at, since
    // "8 kHz" means a different ratio in a 44.1 and a 192 kHz session.
    juce::String lineNote;

    lineNote << "Chain: " << (band == 0 ? "no band limit" : juce::String (choices::bands[band]))
             << "  ->  ";

    if (rate == 0)
        lineNote << "no rate reduction";
    else
        lineNote << juce::String (effective, 0) << " Hz effective (host "
                 << juce::String (crossbar_.getPreparedRate(), 0) << " Hz)";

    lineNote << "  ->  " << juce::String (choices::codecs[codec]);

    if (codec != 0 && codec != 3)
        lineNote << ", " << juce::String (indexOf (ids::bits)) << " bits";

    lineNote << "\n";

    if (band == 0 && rate != 0)
        lineNote << "No band limit in front of the rate reduction, so the images fold "
                    "back: this is the crunch setting.";
    else if (band != 0 && rate != 0)
        lineNote << "The band limit is the anti-alias filter, which is why this sounds "
                    "grubby rather than crunchy. Turn Band off for the crunch.";
    else
        lineNote << "Codec off, rate off and band off is bit-exact identity -- the "
                    "signal is not touched at all.";

    pages_[1]->setNote (lineNote);

    // The TONE note explains the map, which is the thing nobody would guess.
    juce::String toneNote;
    toneNote << "The first octave from the map root is the keypad: 1 2 3 4 5 6 7 8 9 * 0 #.\n"
             << "Then A B C D; then dial, busy, ringback, congestion, unobtainable, howler,\n"
             << "call waiting, SIT, fax, modem, 2600, rotary; then the eight DTMF\n"
             << "frequencies alone; then the dial key.";
    pages_[0]->setNote (toneNote);

    juce::String dialNote;
    dialNote << "Number: " << crossbar_.getDialNumber() << "\n"
             << (dialMode == 0 ? "Tone: every digit the same length."
                               : "Pulse: a digit lasts its own value at ten breaks a second.");
    pages_[2]->setNote (dialNote);
}

void CrossbarEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);

    g.setColour (palette_.accent.withAlpha (0.35f));
    g.fillRect (8, kHeaderHeight - 2, getWidth() - 16, 1);
}

void CrossbarEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (kHeaderHeight).reduced (10, 4);

    titleLabel_.setBounds (header.removeFromLeft (120));
    tooltipsButton_.setBounds (header.removeFromRight (28).withSizeKeepingCentre (24, 22));
    vendorLabel_.setBounds (header.removeFromRight (80));
    voicesLabel_.setBounds (header.removeFromRight (70));
    subtitleLabel_.setBounds (header);

    bounds.reduce (8, 6);

    auto tabRow = bounds.removeFromTop (kTabHeight + 4).reduced (0, 2);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth)
                  .reduced (2, 0));

    bounds.removeFromTop (4);

    for (auto& page : pages_)
        if (page != nullptr)
            page->setBounds (bounds);
}

} // namespace tezla::crossbar
