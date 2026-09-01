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
constexpr int kKeypadHeight = 250;
constexpr int kDialRowHeight = 30;
/// The control this plugin is *about* -- Band is the telephone line, and
/// narrowing it is the whole sound.
///
/// Drawn larger than its neighbours so the eye lands on it first; see
/// PanelDesign.hpp for why a size is the hierarchy cue that survives being
/// glanced at.
[[nodiscard]] ui::design::Emphasis emphasisOf (const juce::String& id) noexcept
{
    return id == ids::band ? ui::design::Emphasis::lead
                        : ui::design::Emphasis::normal;
}

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
// KeypadView
// ---------------------------------------------------------------------------

KeypadView::KeypadView (ui::Palette palette, Region region)
    : palette_ (palette), region_ (region)
{
    setInterceptsMouseClicks (true, false);
}

void KeypadView::setSounding (std::uint64_t mask)
{
    if (mask == sounding_)
        return;

    sounding_ = mask;
    repaint();
}

void KeypadView::setRegion (Region region)
{
    if (region == region_)
        return;

    region_ = region;
    resized();
    repaint();
}

void KeypadView::setMapRoot (int root)
{
    mapRoot_ = root;
}

juce::String KeypadView::describe (Tone tone) const
{
    const int note = noteForTone (tone, mapRoot_);
    const auto suffix = "  --  MIDI note " + juce::String (note) + " ("
                          + juce::MidiMessage::getMidiNoteName (note, true, true, 4) + ").";

    if (isDtmf (tone))
    {
        int row = 0, column = 0;
        dtmfIndices (tone, row, column);

        return "DTMF " + juce::String (nameFor (tone)) + ": "
                 + juce::String (kDtmfRowHz[row], 0) + " Hz and "
                 + juce::String (kDtmfColHz[column], 0) + " Hz together, to ITU-T Q.23."
                 + suffix;
    }

    // The cadence is the thing worth saying about a call-progress tone -- the
    // frequencies are on the key, the timing is what tells busy from reorder.
    const ToneProgram program = programFor (tone, region_);
    juce::String cadence;

    if (program.stepCount == 1 && program.steps[0].seconds < 0.0)
    {
        cadence = "continuous";
    }
    else
    {
        for (int i = 0; i < program.stepCount; ++i)
            cadence << (i > 0 ? " / " : "")
                    << juce::String (program.steps[i].seconds, 3).trimCharactersAtEnd ("0")
                    << (program.steps[i].partials > 0 || program.steps[i].click ? " on" : " off");

        if (! program.loops)
            cadence << ", once";
    }

    juce::String frequencies;

    for (int i = 0; i < program.steps[0].partials; ++i)
        frequencies << (i > 0 ? " + " : "") << juce::String (program.steps[0].frequency[i], 1)
                    << " Hz";

    if (frequencies.isEmpty())
        frequencies = "loop-break clicks";

    return juce::String (nameFor (tone)) + ": " + frequencies + ", " + cadence + "." + suffix;
}

juce::String KeypadView::getTooltip()
{
    const int index = padAt (getMouseXYRelative());

    if (index < 0)
        return "The keypad is the encoding: row frequencies down the left, column "
               "frequencies along the top, and a key is where one of each crosses.";

    return describe (pads_[static_cast<std::size_t> (index)].tone);
}

bool KeypadView::isSounding (Tone tone) const
{
    const auto index = static_cast<int> (tone);

    return index >= 0 && index < kToneCount
             && (sounding_ & (std::uint64_t { 1 } << index)) != 0;
}

int KeypadView::padAt (juce::Point<int> position) const
{
    for (std::size_t i = 0; i < pads_.size(); ++i)
        if (pads_[i].bounds.contains (position))
            return static_cast<int> (i);

    return -1;
}

void KeypadView::press (int padIndex)
{
    if (padIndex == held_)
        return;

    releaseHeld();

    if (padIndex < 0)
        return;

    held_ = padIndex;

    if (onKey != nullptr)
        onKey (static_cast<int> (pads_[static_cast<std::size_t> (padIndex)].tone), true);

    repaint();
}

void KeypadView::releaseHeld()
{
    if (held_ < 0)
        return;

    if (onKey != nullptr)
        onKey (static_cast<int> (pads_[static_cast<std::size_t> (held_)].tone), false);

    held_ = -1;
    repaint();
}

void KeypadView::mouseDown (const juce::MouseEvent& event)
{
    press (padAt (event.getPosition()));
}

void KeypadView::mouseDrag (const juce::MouseEvent& event)
{
    // Dragging across the pad slides from key to key, as a finger does on a
    // touchscreen keypad and as nothing does on a real telephone. It is what
    // makes a DTMF run playable by hand.
    press (padAt (event.getPosition()));
}

void KeypadView::mouseUp (const juce::MouseEvent&)
{
    releaseHeld();
}

void KeypadView::resized()
{
    pads_.clear();

    auto bounds = getLocalBounds().reduced (6, 4);

    // The keypad and its two frequency rulers take the left; the call-progress
    // tones take the rest.
    //
    // 54 rather than 40 for the ruler: "941 Hz" at ten point needs about 40,
    // and at exactly 40 it touched both the panel edge and the keys. The
    // screenshot showed the leading digit clipped.
    constexpr int kRuler = 54;

    auto left = bounds.removeFromLeft (juce::jmin (bounds.getWidth() * 3 / 5, 420));
    bounds.removeFromLeft (10);

    columnLabelArea_ = left.removeFromTop (18).withTrimmedLeft (kRuler);
    rowLabelArea_ = left.removeFromLeft (kRuler);
    keypadArea_ = left;

    const int keyWidth = keypadArea_.getWidth() / 4;
    const int keyHeight = keypadArea_.getHeight() / 4;

    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
        {
            const Tone tone = toneForDtmf (row, column);

            Pad pad;
            pad.tone = tone;
            pad.caption = nameFor (tone);
            pad.detail = juce::String (kDtmfRowHz[row], 0) + " / "
                           + juce::String (kDtmfColHz[column], 0);
            pad.large = true;
            pad.bounds = { keypadArea_.getX() + column * keyWidth,
                           keypadArea_.getY() + row * keyHeight,
                           keyWidth - 4, keyHeight - 4 };

            pads_.push_back (pad);
        }

    // The call-progress tones, on the same grid. The captions follow the
    // region: a UK line has an engaged tone and a number-unobtainable tone
    // where a Bell one has busy and an intercept.
    const bool uk = region_ == Region::unitedKingdom;

    struct Entry { Tone tone; const char* bell; const char* bt; };

    static const Entry entries[] {
        { Tone::dialTone,        "DIAL",    "DIAL"     },
        { Tone::busy,            "BUSY",    "ENGAGED"  },
        { Tone::ringback,        "RING",    "RING"     },
        { Tone::congestion,      "REORDER", "CONGEST"  },
        { Tone::unobtainable,    "SIT LONG", "N.U."    },
        { Tone::howler,          "HOWLER",  "HOWLER"   },
        { Tone::callWaiting,     "WAITING", "WAITING"  },
        { Tone::sit,             "SIT",     "SIT"      },
        { Tone::faxCalling,      "FAX",     "FAX"      },
        { Tone::modemAnswer,     "MODEM",   "MODEM"    },
        { Tone::singleFrequency, "2600",    "2600"     },
        { Tone::rotaryPulse,     "ROTARY",  "ROTARY"   },
    };

    constexpr int kProgressColumns = 4;
    const int progressWidth = bounds.getWidth() / kProgressColumns;
    const int progressHeight = bounds.getHeight() / 3;

    for (int i = 0; i < 12; ++i)
    {
        Pad pad;
        pad.tone = entries[i].tone;
        pad.caption = uk ? entries[i].bt : entries[i].bell;
        // Inset vertically as well as horizontally: a call-progress pad has
        // one short word on it and does not need the whole row's height, and
        // at full height they dwarfed the keypad they sit beside.
        pad.bounds = juce::Rectangle<int> {
            bounds.getX() + (i % kProgressColumns) * progressWidth,
            bounds.getY() + (i / kProgressColumns) * progressHeight,
            progressWidth - 4, progressHeight - 4 }.reduced (0, 8);

        pads_.push_back (pad);
    }
}

void KeypadView::drawPad (juce::Graphics& g, const Pad& pad) const
{
    const bool lit = isSounding (pad.tone);
    const auto area = pad.bounds.toFloat();

    g.setColour (lit ? palette_.accent.withAlpha (0.45f) : palette_.panel.brighter (0.10f));
    g.fillRoundedRectangle (area, 4.0f);

    g.setColour (lit ? palette_.accentBright : palette_.panel.brighter (0.30f));
    g.drawRoundedRectangle (area, 4.0f, lit ? 1.6f : 1.0f);

    g.setColour (lit ? palette_.accentBright : palette_.text);

    if (pad.large)
    {
        auto text = pad.bounds;
        auto detail = text.removeFromBottom (12);

        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawFittedText (pad.caption, text, juce::Justification::centred, 1);

        g.setColour (lit ? palette_.accentBright.withAlpha (0.8f) : palette_.dimText);
        g.setFont (juce::FontOptions (9.0f));
        g.drawFittedText (pad.detail, detail, juce::Justification::centred, 1);
    }
    else
    {
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (pad.caption, pad.bounds, juce::Justification::centred, 1);
    }
}

void KeypadView::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // The rulers. A row or a column lights when any key on it is sounding,
    // which is the crossbar drawn: the key is where the two meet.
    bool rowLit[4] {};
    bool columnLit[4] {};

    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            if (isSounding (toneForDtmf (row, column)))
            {
                rowLit[row] = true;
                columnLit[column] = true;
            }

    // The eight constituent frequencies are keys of their own further up the
    // map, and they light their own ruler entry too -- which is how somebody
    // discovers that those keys exist.
    for (int i = 0; i < 4; ++i)
    {
        rowLit[i] = rowLit[i]
                      || isSounding (static_cast<Tone> (static_cast<int> (Tone::row697) + i));
        columnLit[i] = columnLit[i]
                         || isSounding (static_cast<Tone> (static_cast<int> (Tone::col1209) + i));
    }

    g.setFont (juce::FontOptions (10.0f));

    const int keyHeight = keypadArea_.getHeight() / 4;
    const int keyWidth = keypadArea_.getWidth() / 4;

    for (int row = 0; row < 4; ++row)
    {
        g.setColour (rowLit[row] ? palette_.accentBright : palette_.dimText);
        g.drawFittedText (juce::String (kDtmfRowHz[row], 0) + " Hz",
                          { rowLabelArea_.getX(), keypadArea_.getY() + row * keyHeight,
                            rowLabelArea_.getWidth() - 6, keyHeight - 4 },
                          juce::Justification::centredRight, 1);
    }

    for (int column = 0; column < 4; ++column)
    {
        g.setColour (columnLit[column] ? palette_.accentBright : palette_.dimText);
        g.drawFittedText (juce::String (kDtmfColHz[column], 0),
                          { keypadArea_.getX() + column * keyWidth, columnLabelArea_.getY(),
                            keyWidth - 4, columnLabelArea_.getHeight() },
                          juce::Justification::centred, 1);
    }

    for (const auto& pad : pads_)
        drawPad (g, pad);
}

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

    ui::styleChoice (choice->box, palette_, palette_.accent);
    choice->box.setTooltip (tooltip);
    addAndMakeVisible (choice->box);

    choice->label.setText (name, juce::dontSendNotification);
    ui::styleName (choice->label, palette_, palette_.accent);
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

/// The colour `ui::styleName` gave every name on this page.
///
/// Held as a function rather than repeated at the two disable sites: a name
/// greyed back to the plain dim text would lose its accent warmth the first
/// time a control was switched off and never get it back, which reads as one
/// label in the wrong colour and is exactly the kind of thing nobody finds.
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

    for (auto& choice : choices_)
        if (choice->id == id)
        {
            choice->box.setEnabled (enabled);
            choice->label.setColour (juce::Label::textColourId,
                                     enabled ? nameColour() : nameColour().withAlpha (0.35f));
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
                // **Emphasis is a size.** The cell keeps its footprint -- the
                // grid is a grid -- and only the control inside it moves.
                knob.slider.setBounds (ui::emphasised (cell.reduced (4, 0),
                                                       emphasisOf (knob.id)));
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
    // The house look and feel, installed on the editor so every page inherits
    // it: JUCE walks up the parent chain to find one.
    setLookAndFeel (&knobLook_);

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

    keypad_ = std::make_unique<KeypadView> (palette_, Region::northAmerica);
    keypad_->setComponentID ("keypad");
    keypad_->onKey = [this] (int toneIndex, bool held)
    {
        crossbar_.setPanelKeyHeld (toneIndex, held);
    };
    addAndMakeVisible (*keypad_);

    dialCaption_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    dialCaption_.setColour (juce::Label::textColourId, palette_.dimText);
    dialCaption_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (dialCaption_);

    dialField_.setText (crossbar_.getDialNumber(), juce::dontSendNotification);
    dialField_.setColour (juce::TextEditor::backgroundColourId, palette_.panel.brighter (0.15f));
    dialField_.setColour (juce::TextEditor::textColourId, palette_.text);
    dialField_.setColour (juce::TextEditor::outlineColourId, palette_.panel.brighter (0.3f));
    dialField_.setColour (juce::TextEditor::focusedOutlineColourId, palette_.accent);
    dialField_.setFont (juce::FontOptions (14.0f));
    dialField_.setTooltip (
        "The number the DIAL key plays. Write it however you like -- spaces, "
        "dashes and brackets are skipped rather than dialled, so "
        "'+1 (555) 010-4477' is seven... thirteen keys and no punctuation. It "
        "is stored with the project but is NOT a parameter: a phone number is "
        "text, and there is no honest way to automate one.");
    dialField_.onTextChange = [this] { crossbar_.setDialNumber (dialField_.getText()); };
    addAndMakeVisible (dialField_);

    dialButton_.setColour (juce::TextButton::buttonColourId, palette_.accent.withAlpha (0.30f));
    dialButton_.setColour (juce::TextButton::textColourOffId, palette_.text);
    dialButton_.setTooltip (
        "Dials the number now. The same thing the last key of the map does, so "
        "it can be played from a clip as well as pressed here.");
    dialButton_.setComponentID ("dial-button");
    dialButton_.onClick = [this] { crossbar_.triggerDial(); };
    addAndMakeVisible (dialButton_);

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
    setResizeLimits (820, 700, 1500, 1200);
    setSize (940, 800);

    startTimerHz (15);
}

CrossbarEditor::~CrossbarEditor()
{
    // A look and feel must be detached from every component using it *before*
    // it is destroyed, and the editor itself is one of them. JUCE asserts on a
    // dangling one, and only in a debug build -- a release plugin would just
    // read freed memory.
    setLookAndFeel (nullptr);
}

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

    const auto sounding = crossbar_.getSoundingToneMask();

    if (sounding != shownSounding_)
    {
        shownSounding_ = sounding;
        keypad_->setSounding (sounding);
    }

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
    const int region = indexOf (ids::region);
    const double effective = crossbar_.getEffectiveRateHz();

    if (codec == shownCodec_ && rate == shownRate_ && band == shownBand_
          && dialMode == shownDialMode_ && cadence == shownCadence_
          && region == shownRegion_
          && std::abs (effective - shownEffectiveRate_) < 1.0)
        return;

    shownCodec_ = codec;
    shownRate_ = rate;
    shownBand_ = band;
    shownDialMode_ = dialMode;
    shownCadence_ = cadence;
    shownRegion_ = region;
    shownEffectiveRate_ = effective;

    keypad_->setRegion (region == 1 ? Region::unitedKingdom : Region::northAmerica);
    keypad_->setMapRoot (indexOf (ids::mapRoot));

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

    keypad_->setBounds (bounds.removeFromTop (kKeypadHeight));

    auto dialRow = bounds.removeFromTop (kDialRowHeight + 6).reduced (2, 3);
    dialCaption_.setBounds (dialRow.removeFromLeft (70));
    dialRow.removeFromLeft (6);
    dialButton_.setBounds (dialRow.removeFromRight (80));
    dialRow.removeFromRight (8);
    dialField_.setBounds (dialRow);

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
