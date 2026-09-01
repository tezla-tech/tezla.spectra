// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <tezla/dsp/Oversampler.hpp>

#include <algorithm>
#include <cmath>

namespace tezla::ferrite
{

namespace
{
// Ferrite's own accent: oxide rust, against Emberdrive's ember and Capstone's
// steel -- the colour of the tape itself. The bypass glow is the same in
// every plugin.
const ui::Palette kPalette {
    juce::Colour { 0xff151312 },   // background
    juce::Colour { 0xff1e1a17 },   // panel
    juce::Colour { 0xffd9d2c8 },   // text
    juce::Colour { 0xff8a8178 },   // dim text
    juce::Colour { 0xffc0623d },   // accent: oxide
    juce::Colour { 0xffe98e63 },   // accent bright
    juce::Colour { 0xffbfa06a },   // secondary: worn brass
    juce::Colour { 0xffff7a18 }    // bypass glow, the same in every plugin
};

constexpr int kLabelHeight = 16;
constexpr int kValueHeight = 18;
constexpr int kMaxCellHeight = 130;
constexpr int kNoteHeight = 58;

constexpr int kMinWidth  = 720;
constexpr int kMinHeight = 540;
constexpr int kMaxWidth  = 1440;
constexpr int kMaxHeight = 1080;

constexpr int kMeterWidth = ui::LevelMeter::kMinimumWidth;
constexpr int kTabHeight  = 28;
constexpr int kStatusHeight = 24;
/// The control this plugin is *about* -- drive is what a tape machine is set by.
///
/// Drawn larger than its neighbours so the eye lands on it first; see
/// PanelDesign.hpp for why a size is the hierarchy cue that survives being
/// glanced at.
[[nodiscard]] ui::design::Emphasis emphasisOf (const juce::String& id) noexcept
{
    return id == ids::drive ? ui::design::Emphasis::lead
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

    // Populated from the parameter itself. A ComboBoxAttachment selects an
    // item by index and does not create one, so a box left empty here stays
    // empty on screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state_.getParameter (parameterId)))
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

void ControlPage::addToggle (const char* parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    auto toggle = std::make_unique<Toggle> (name);

    toggle->button.setClickingTogglesState (true);
    toggle->button.setTooltip (tooltip);
    addAndMakeVisible (toggle->button);

    toggle->id = parameterId;
    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, toggle->button);

    cells_.push_back ({ Cell::Kind::toggle, static_cast<int> (toggles_.size()) });
    toggles_.push_back (std::move (toggle));
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
                                   enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
            knob->label.repaint();
        }

    for (auto& choice : choices_)
        if (choice->id == id)
        {
            choice->box.setEnabled (enabled);
            choice->label.setColour (juce::Label::textColourId,
                                     enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
            choice->label.repaint();
        }

    for (auto& toggle : toggles_)
        if (toggle->id == id)
            toggle->button.setEnabled (enabled);
}

void ControlPage::paint (juce::Graphics& g)
{
    g.setColour (kPalette.panel);
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

    // The note gets its space reserved before the grid takes any, rather
    // than living on whatever is left over.
    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (6, 0);
    else
        noteArea_ = {};

    const int rows = (static_cast<int> (cells_.size()) + columns_ - 1) / columns_;
    const int cellWidth  = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (bounds.getHeight() / juce::jmax (1, rows), kMaxCellHeight);

    // Centred vertically rather than pinned to the top, so a short page does
    // not huddle under the tabs with half the panel empty below.
    const int top = bounds.getY()
                  + juce::jmax (0, (bounds.getHeight() - rows * cellHeight) / 2);

    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row    = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    top + row * cellHeight,
                                    cellWidth, cellHeight };

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
                    juce::jmin (cell.getWidth() - 12, 132), 26));
                break;
            }
            case Cell::Kind::toggle:
            {
                // `sized` adds the glow margin: the button is larger than the
                // switch drawn in it so the lit halo is not clipped away. JUCE
                // throws away anything painted past a component's own bounds,
                // silently.
                auto& toggle = *toggles_[static_cast<std::size_t> (cells_[i].index)];

                const int width = juce::jlimit (54, 120,
                    cell.getWidth() - 8 - 2 * ui::LampButton::kGlowMargin);

                toggle.button.setBounds (
                    ui::LampButton::sized (width, 34).withCentre (cell.getCentre()));
                break;
            }
            case Cell::Kind::gap:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// FerriteEditor
// ---------------------------------------------------------------------------

FerriteEditor::FerriteEditor (FerriteProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      ferrite_ (processorToUse),
      palette_ (kPalette),
      inputMeter_ (std::make_unique<ui::LevelMeter> (kPalette)),
      outputMeter_ (std::make_unique<ui::LevelMeter> (kPalette))
{
    // The house look and feel, installed on the editor so every page inherits
    // it: JUCE walks up the parent chain to find one.
    setLookAndFeel (&knobLook_);

    header_ = std::make_unique<ui::HeaderBar> (
        ferrite_.getState(), "FERRITE",
        "Tape machine: hysteresis, losses, wow and flutter", ids::bypass, palette_);

    header_->onSwapRequested = [this]
    {
        ferrite_.getAbCompare().swapSlots();
        header_->setActiveSlot (ferrite_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (ferrite_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        ferrite_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (ferrite_.getAbCompare().otherSlotFilled());
    };

    header_->onTooltipsToggled = [this] (bool enabled)
    {
        ferrite_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setTooltipsEnabled (ferrite_.getTooltipsEnabled());
    tooltips_.setEnabled (ferrite_.getTooltipsEnabled());

    header_->setActiveSlot (ferrite_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (ferrite_.getAbCompare().otherSlotFilled());

    // The suite-wide trio -- mix, output, oversampling -- in the header,
    // where they sit on every plugin in the suite.
    header_->attachSuiteControls (ferrite_.getState(), ids::mix, ids::output,
                                  ids::oversampling);
    addAndMakeVisible (*header_);

    buildPages();

    static const char* tabNames[kNumPages] { "TAPE", "MOTION", "MACHINE" };

    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (tabNames[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    inputMeter_->setReferenceDb (0.0f);
    outputMeter_->setScaleVisible (true);

    addAndMakeVisible (*inputMeter_);
    addAndMakeVisible (*outputMeter_);

    inputMeter_->setTooltip ("The level going onto the tape -- after the Input control, "
                             "which is what a machine's needle shows. VU ballistics "
                             "(300 ms), with the worst peak held; click to clear it. "
                             "Working tape hard is the point, but the needle is how you "
                             "know how hard.");

    outputMeter_->setTooltip ("What leaves the plugin, bypass and mix included. VU "
                              "ballistics with a held peak; click to clear. With Auto "
                              "Trim on, drive changes should barely move this -- that "
                              "is the trim doing its job.");

    for (auto* label : { &inputMeterLabel_, &outputMeterLabel_ })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, palette_.dimText);
        label->setFont (juce::FontOptions (10.0f));
        addAndMakeVisible (label);
    }

    statusLabel_.setJustificationType (juce::Justification::centred);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel_);

    showPage (0);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (860, 600);

    startTimerHz (30);
}

void FerriteEditor::buildPages()
{
    // ---- TAPE ----------------------------------------------------------------

    auto& tape = pages_[0];
    tape = std::make_unique<ControlPage> (ferrite_.getState(), palette_, 4);

    tape->addKnob (ids::input, "Input",
        "The level going onto the tape. This is the drive control that behaves like the "
        "real one: hotter in means a harder-worked loop and more compression. With Auto "
        "Trim on the loudness change is measured and undone, so what you hear changing "
        "is the tone.");

    tape->addKnob (ids::drive, "Drive",
        "How hard the signal works the magnetic loop -- the steepness of the tape's "
        "response. Low leaves the tape barely magnetised (the trim brings the level "
        "back, so it stays comparable); high pushes the loop's whole S-curve into the "
        "signal. Odd harmonics first, compression always.");

    tape->addKnob (ids::saturation, "Saturation",
        "Where the tape runs out -- the ceiling the magnetisation cannot pass. Lower "
        "ceiling means the loop flattens sooner: more squash at the same drive. The "
        "makeup for the lost level is built in and measured, not guessed.");

    tape->addKnob (ids::bias, "Bias",
        "The machine's bias setting, as reversibility of the loop. High is a modern, "
        "well-biased machine: a narrow loop hugging the clean curve. Low is under-biased "
        "and nasty: a wide loop, hysteresis memory, and the thick even-order warmth that "
        "comes with it. The auto-trim follows it either way.");

    tape->addChoice (ids::speed, "Speed",
        "Tape speed sets where the wavelength losses land, and they move an octave per "
        "doubling: what 30 ips loses above 20 kHz, 3.75 ips loses above 2.5 k. It also "
        "moves the head bump: 21 Hz at 7.5 ips, 42 at 15, 85 at 30. Slower is darker, "
        "thicker, rougher.");

    tape->addKnob (ids::bump, "Head Bump",
        "The low shelf where the playback head's contact wavelength resonates -- the "
        "weight tape adds that EQ does not quite copy. 100% is the measured 2.5 dB; "
        "0% removes it entirely; 200% is twice the decibels. It sits at the frequency "
        "the Speed control says.");

    tape->addToggle (ids::autoTrim, "Auto Trim",
        "Measures the small-signal gain of the exact loop the audio is about to meet -- "
        "a short probe at the oversampled rate -- and compensates, so drive and bias "
        "change tone rather than loudness. Held within 1.5 dB across the whole panel, "
        "measured. Off gives you the raw level, compression and all.");

    tape->addGap();

    // ---- MOTION --------------------------------------------------------------

    auto& motion = pages_[1];
    motion = std::make_unique<ControlPage> (ferrite_.getState(), palette_, 4);

    motion->addKnob (ids::wow, "Wow",
        "Slow speed drift -- the pitch leaning seasick over about a second. The rate "
        "itself wanders and the depth breathes (a mean-reverting random walk), because a "
        "motor that wobbled perfectly evenly would sound like an LFO, and this must not. "
        "Both channels ride the same tape, so stereo stays coherent.");

    motion->addKnob (ids::flutter, "Flutter",
        "Fast roughness from the motor: three partials at the flutter rate with fixed "
        "phases and amplitudes, taken from a fit of an actual machine's transport (a "
        "TC-260 -- attributed in the source). It reads as texture and grain rather than "
        "as vibrato.");

    motion->addKnob (ids::hiss, "Hiss",
        "Tape noise, calibrated: the number IS the noise floor in dBFS at the output, "
        "measured within a decibel. Per-channel generators -- real hiss is per-track "
        "noise, so the channels decorrelate. All the way down is OFF, and off is "
        "bit-exact absence, not merely quiet.");

    motion->addGap();

    motion->addKnob (ids::wowRate, "Wow Rate",
        "The centre of the wow cycle. 0.5-1.2 Hz is machine territory; the actual rate "
        "drifts around whatever this says, on purpose.");

    motion->addKnob (ids::flutterRate, "Flutter Rate",
        "The fundamental of the three-partial flutter stack. Mechanically this is the "
        "capstan's rotation; 10-15 Hz reads as roughness, 25+ as a buzz.");

    // ---- MACHINE -------------------------------------------------------------

    auto& machine = pages_[2];
    machine = std::make_unique<ControlPage> (ferrite_.getState(), palette_, 3);

    machine->addKnob (ids::spacing, "Spacing",
        "Head-to-tape spacing in microns. The dominant loss: e to the minus kd, so a "
        "few microns of dirt cost real treble at short wavelengths. A clean pro machine "
        "sits near 2-5; a worn cassette deck is 10+. Applied per Speed, like the "
        "physics says.");

    machine->addKnob (ids::thickness, "Thickness",
        "Coating thickness in microns. Thicker coating means the deep layers sit "
        "further from the head and their high frequencies never make it out -- a "
        "gentler, broader rolloff than the spacing loss.");

    machine->addKnob (ids::gap, "Head Gap",
        "The playback head's gap in microns. Sets the gap null -- the frequency where "
        "the head averages a whole wavelength to nothing. On a real machine this is "
        "the ultimate treble limit; at 30 ips it is far above the audio band, at 3.75 "
        "it is not.");

    for (auto& page : pages_)
        addChildComponent (*page);
}

void FerriteEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        const bool active = i == currentPage_;

        pages_[static_cast<std::size_t> (i)]->setVisible (active);

        auto& tab = tabs_[static_cast<std::size_t> (i)];
        tab.setColour (juce::TextButton::buttonColourId,
                       active ? palette_.panel.brighter (0.25f) : palette_.background.brighter (0.05f));
        tab.setColour (juce::TextButton::textColourOffId,
                       active ? palette_.text : palette_.dimText);
    }

    resized();
}

void FerriteEditor::updateForSwitches()
{
    auto& state = ferrite_.getState();

    const auto raw = [&state] (const char* id)
    {
        return state.getRawParameterValue (id)->load();
    };

    const int wowOn      = raw (ids::wow) > 0.001f ? 1 : 0;
    const int flutterOn  = raw (ids::flutter) > 0.001f ? 1 : 0;
    const int autoTrim   = raw (ids::autoTrim) > 0.5f ? 1 : 0;
    const int oversample = static_cast<int> (raw (ids::oversampling));
    const int latency    = ferrite_.getLatencySamples();

    if (wowOn == shownWowOn_ && flutterOn == shownFlutterOn_ && autoTrim == shownAutoTrim_
        && oversample == shownOversample_ && latency == shownLatency_)
        return;

    shownWowOn_ = wowOn;
    shownFlutterOn_ = flutterOn;
    shownAutoTrim_ = autoTrim;
    shownOversample_ = oversample;
    shownLatency_ = latency;

    // A rate control with its depth at zero is inert; greyed rather than
    // hidden, so the page keeps its shape.
    auto& motion = *pages_[1];
    motion.setControlEnabled (ids::wowRate, wowOn != 0);
    motion.setControlEnabled (ids::flutterRate, flutterOn != 0);

    pages_[0]->setNote (autoTrim != 0
        ? juce::String ("Auto Trim is on: drive, saturation, bias and input changes are "
                        "loudness-compensated (measured, within 1.5 dB), so judge the tone. ")
            + ferrite_.describeLatency()
        : juce::String ("Auto Trim is off: the raw tape level, so drive changes are loud. "
                        "Loudness sells distortion -- match levels before judging. ")
            + ferrite_.describeLatency());

    motion.setNote ("Wow and flutter are one modulated read head: depths set how far the "
                    "speed strays, rates set how fast. Zero depth is bit-exact -- the "
                    "delay collapses to its integer centre, which is inside the reported "
                    "latency, so the neutral setting costs nothing but that delay.");

    pages_[2]->setNote (ferrite_.describeOversampling());
}

void FerriteEditor::timerCallback()
{
    auto& meters = ferrite_.getMeterValues();

    inputMeter_->setValues (meters.inputVuDb.load (std::memory_order_relaxed),
                            meters.inputPeakDb.load (std::memory_order_relaxed));
    outputMeter_->setValues (meters.outputVuDb.load (std::memory_order_relaxed),
                             meters.outputPeakDb.load (std::memory_order_relaxed));

    inputMeter_->repaint();
    outputMeter_->repaint();

    auto& state = ferrite_.getState();

    const int speedIndex = juce::jlimit (0, choices::speed.size() - 1,
        static_cast<int> (state.getRawParameterValue (ids::speed)->load()));

    const double rate = ferrite_.getSampleRate() > 0.0 ? ferrite_.getSampleRate() : 48000.0;
    const int latency = ferrite_.getLatencySamples();

    const auto mode = static_cast<dsp::OversamplingMode> (juce::jlimit (0, 4,
        static_cast<int> (state.getRawParameterValue (ids::oversampling)->load())));
    const int factor = dsp::oversamplingFactor (mode, rate);

    juce::String status;
    status << choices::speed[speedIndex]
           << "   \xe2\x80\xa2   TAPE VU "
           << juce::String (meters.inputVuDb.load (std::memory_order_relaxed), 1) << " dB"
           << "   \xe2\x80\xa2   x" << factor
           << "   \xe2\x80\xa2   LATENCY ";

    // Until the host has started audio there is no latency figure, only an
    // uninitialised one.
    if (ferrite_.isPrepared())
        status << latency << " sm (" << juce::String (1000.0 * latency / rate, 2) << " ms)";
    else
        status << "--";

    statusLabel_.setText (status, juce::dontSendNotification);

    header_->setActiveSlot (ferrite_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (ferrite_.getAbCompare().otherSlotFilled());

    updateForSwitches();
}

FerriteEditor::~FerriteEditor()
{
    setLookAndFeel (nullptr);
}

void FerriteEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void FerriteEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 4));

    // The meters flank the panel: the tape needle on the left, the output on
    // the right, so the reading and the control that changes it are never on
    // different pages.
    auto left = bounds.removeFromLeft (kMeterWidth + 8).reduced (4, 6);
    inputMeterLabel_.setBounds (left.removeFromBottom (12));
    inputMeter_->setBounds (left);

    auto right = bounds.removeFromRight (kMeterWidth + ui::LevelMeter::kScaleWidth + 8)
                     .reduced (4, 6);
    outputMeterLabel_.setBounds (right.removeFromBottom (12));
    outputMeter_->setBounds (right);

    auto tabRow = bounds.removeFromTop (kTabHeight).reduced (4, 2);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth).reduced (2, 0));

    for (auto& page : pages_)
        page->setBounds (bounds.reduced (4, 2));
}

} // namespace tezla::ferrite
