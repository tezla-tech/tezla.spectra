#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tezla::anvil
{

namespace
{
// Anvil's own accent: hot iron, against Emberdrive's ember, Halo's gold,
// Capstone's steel and Transpectus's green. The secondary is cooler on purpose
// -- it carries the transformer's flux, which is the one reading here that is
// not about level, and the two must not be confusable at a glance.
const ui::Palette kPalette {
    juce::Colour { 0xff141210 },   // background
    juce::Colour { 0xff1f1c19 },   // panel
    juce::Colour { 0xffe0d9cd },   // text
    juce::Colour { 0xff8b8279 },   // dim text
    juce::Colour { 0xffc9552b },   // accent: hot iron
    juce::Colour { 0xffe8875c },   // accent bright
    juce::Colour { 0xff4f9fb8 },   // secondary: flux
    juce::Colour { 0xffff7a18 }    // bypass glow, the same in every plugin
};

constexpr int kLabelHeight = 16;
constexpr int kValueHeight = 18;
constexpr int kMaxCellHeight = 130;
constexpr int kNoteHeight = 58;

constexpr int kMinWidth  = 760;
constexpr int kMinHeight = 580;
constexpr int kMaxWidth  = 1520;
constexpr int kMaxHeight = 1160;

constexpr int kMeterWidth = ui::LevelMeter::kMinimumWidth;
constexpr int kTabHeight  = 28;
constexpr int kStatusHeight = 24;
constexpr int kWorkingHeight = 74;

/// How far up the scale a full core reads. The bar is marked at 1.0, which is
/// where the transformer starts to give way; past that it is genuinely
/// saturating, and there is another octave of it worth seeing.
constexpr float kFluxRange = 2.0f;
} // namespace

// ---------------------------------------------------------------------------
// WorkingMeter
// ---------------------------------------------------------------------------

void WorkingMeter::setValues (float sag, float flux, float bias) noexcept
{
    // Held with a slow fall, so a transient stays readable long enough to see.
    // These already move on the amplifier's own time constants -- tens of
    // milliseconds -- so the hold is gentler than a peak meter's.
    sag_  = std::max (sag,  sag_  * 0.90f);
    flux_ = std::max (flux, flux_ * 0.90f);
    bias_ = std::max (bias, bias_ * 0.90f);
}

void WorkingMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (palette_.panel.brighter (0.10f));
    g.fillRoundedRectangle (bounds, 3.0f);

    // A gutter for the row labels, taken before anything else, so a label is
    // never drawn over the reading it is labelling.
    auto gutter = bounds.removeFromLeft (46.0f);
    auto bars   = bounds.reduced (4.0f, 3.0f);

    const float rowHeight = bars.getHeight() / 3.0f;

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.0f));

    static const char* names[3] { "SAG", "FLUX", "BIAS" };

    for (int row = 0; row < 3; ++row)
        g.drawText (names[row],
                    gutter.withHeight (rowHeight).withY (bars.getY() + rowHeight * static_cast<float> (row)),
                    juce::Justification::centredLeft);

    // The flux row carries a mark at 1.0 -- where the core is full -- because a
    // bar with no landmark says only "more" and this one has a threshold that
    // means something.
    const float fullCoreX = bars.getX() + bars.getWidth() / kFluxRange;

    g.setColour (palette_.dimText.withAlpha (0.35f));
    g.drawVerticalLine (juce::roundToInt (fullCoreX),
                        bars.getY() + rowHeight, bars.getY() + 2.0f * rowHeight);

    const auto drawBar = [&] (float amount, juce::Colour colour, int row)
    {
        const float fraction = juce::jlimit (0.0f, 1.0f, amount);

        auto rowBounds = bars.withHeight (rowHeight)
                             .withY (bars.getY() + rowHeight * static_cast<float> (row))
                             .reduced (0.0f, 2.5f);

        g.setColour (palette_.panel.darker (0.35f));
        g.fillRoundedRectangle (rowBounds, 2.0f);

        if (fraction <= 0.002f)
            return;

        g.setColour (colour);
        g.fillRoundedRectangle (rowBounds.withWidth (rowBounds.getWidth() * fraction), 2.0f);
    };

    drawBar (sag_, palette_.accent, 0);

    // Past a full core the bar goes bright, because that is the point at which
    // the reading stops meaning "working" and starts meaning "saturating".
    drawBar (flux_ / kFluxRange,
             flux_ >= 1.0f ? palette_.accentBright : palette_.secondary, 1);

    // Bias is in knees, and a knee of drift is a great deal of drift.
    drawBar (bias_, palette_.secondary.darker (0.2f), 2);
}

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
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId, palette_.panel.brighter (0.25f));
    knob->slider.setColour (juce::Slider::thumbColourId, palette_.accentBright);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
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

    // Populated from the parameter itself. A ComboBoxAttachment selects an item
    // by index and does not create one, so a box left empty here stays empty on
    // screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state_.getParameter (parameterId)))
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
}

void ControlPage::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    if (note_.isEmpty() || noteArea_.isEmpty())
        return;

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (12.0f));

    // Four lines, and the box is sized for four. These notes carry the measured
    // numbers behind each control -- a note silently cut in half is worse than
    // no note, because the half that survives still reads as a whole sentence.
    g.drawFittedText (note_, noteArea_, juce::Justification::centredTop, 4, 1.0f);
}

void ControlPage::resized()
{
    if (cells_.empty())
        return;

    auto bounds = getLocalBounds().reduced (4, 2);

    // The note gets its space reserved before the grid takes any, rather than
    // living on whatever is left over.
    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (6, 0);
    else
        noteArea_ = {};

    const int rows = (static_cast<int> (cells_.size()) + columns_ - 1) / columns_;
    const int cellWidth  = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (bounds.getHeight() / juce::jmax (1, rows), kMaxCellHeight);

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
                knob.slider.setBounds (cell.reduced (4, 0));
                break;
            }
            case Cell::Kind::choice:
            {
                auto& choice = *choices_[static_cast<std::size_t> (cells_[i].index)];
                choice.label.setBounds (cell.removeFromTop (kLabelHeight));
                choice.box.setBounds (cell.withSizeKeepingCentre (
                    juce::jmin (cell.getWidth() - 12, 148), 26));
                break;
            }
            case Cell::Kind::gap:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// AnvilEditor
// ---------------------------------------------------------------------------

AnvilEditor::AnvilEditor (AnvilProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      anvil_ (processorToUse),
      palette_ (kPalette),
      inputMeter_ (std::make_unique<ui::LevelMeter> (kPalette)),
      outputMeter_ (std::make_unique<ui::LevelMeter> (kPalette)),
      workingMeter_ (std::make_unique<WorkingMeter> (kPalette))
{
    header_ = std::make_unique<ui::HeaderBar> (
        anvil_.getState(), "ANVIL",
        "Valve amplifier, speaker load and cabinet", ids::bypass, palette_);

    header_->onSwapRequested = [this]
    {
        anvil_.getAbCompare().swapSlots();
        header_->setActiveSlot (anvil_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (anvil_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        anvil_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (anvil_.getAbCompare().otherSlotFilled());
    };

    // The TIPS button. The flag lives on the processor so it survives the
    // window being closed, and the header is told the current value rather
    // than assuming its own default -- reopening a panel whose tips were off
    // must not turn them back on.
    header_->onTooltipsToggled = [this] (bool enabled)
    {
        anvil_.setTooltipsEnabled (enabled);
        tooltips_.setEnabled (enabled);
    };

    header_->setTooltipsEnabled (anvil_.getTooltipsEnabled());
    tooltips_.setEnabled (anvil_.getTooltipsEnabled());

    header_->setActiveSlot (anvil_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (anvil_.getAbCompare().otherSlotFilled());

    // The suite-wide pair, in the header rather than at the bottom of a page.
    header_->attachSuiteControls (anvil_.getState(), ids::mix, ids::output, ids::oversampling);
    addAndMakeVisible (*header_);

    buildPages();

    static const char* tabNames[kNumPages] { "AMP", "CAB", "CHARACTER" };

    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (tabNames[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    inputMeter_->setReferenceDb (0.0f);
    outputMeter_->setReferenceDb (0.0f);
    outputMeter_->setScaleVisible (true);

    addAndMakeVisible (*inputMeter_);
    addAndMakeVisible (*outputMeter_);
    addAndMakeVisible (*workingMeter_);

    for (auto* meter : { inputMeter_.get(), outputMeter_.get() })
        meter->setTooltip ("Peak level. The number is the worst peak since it was last "
                           "cleared -- click the meter to clear it. Hover to read the live "
                           "level instead of the held one.");

    workingMeter_->setTooltip (
        "What the amplifier is doing to itself, which is not a level.\n\n"
        "SAG -- how far the supply rail has fallen under load. This is the compression "
        "people hear as bloom and attribute to the speaker.\n\n"
        "FLUX -- how full the output transformer's core is. The mark is 1.0, where it "
        "starts to give way. Play a low note and a high one at the same level: the low "
        "one fills it twice as far, because flux is the integral of voltage and nothing "
        "in the code tests the frequency.\n\n"
        "BIAS -- how far the first valve's operating point has drifted, in knees. This is "
        "what makes the hundredth chord sound unlike the first.");

    for (auto* label : { &inputMeterLabel_, &outputMeterLabel_, &workingLabel_ })
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
    setSize (900, 660);

    startTimerHz (30);
}

void AnvilEditor::buildPages()
{
    // ---- AMP -----------------------------------------------------------------

    auto& amp = pages_[0];
    amp = std::make_unique<ControlPage> (anvil_.getState(), palette_, 4);

    amp->addChoice (ids::voicing, "Voicing",
        "Which amplifier this is. The three differ in what real amplifiers differ in: how "
        "many valves the signal passes before the tone stack, where the stack sits among "
        "them, how much global feedback the output stage has, and how stiff the supply and "
        "the transformer are.");

    amp->addKnob (ids::gain, "Gain",
        "How hard the preamp valves are driven. Scaled per voicing so the number means the "
        "same thing on each: at +12 dB the clean lane measures 38 dB of THD and the "
        "high-gain lane 6.");

    amp->addKnob (ids::master, "Master",
        "How hard the output stage is driven. Attenuation only, because that is what a "
        "master volume is -- a potentiometer in front of the phase inverter. Turning it up "
        "does not add gain, it stops removing it, and what then distorts is the inverter "
        "and the output valves. Measured: from -36 to -6 dB the second and third harmonics "
        "close from 16.7 dB apart to 12.2, which is a valve running out of swing rather "
        "than anything getting louder.");

    amp->addKnob (ids::output, "Output",
        "Trim, after everything. Each voicing is already calibrated so that Master at 0 dB "
        "with Gain at maximum lands within a decibel of -1 dBFS.");

    amp->addKnob (ids::bass, "Bass",
        "The passive tone stack, solved as the circuit it is rather than as three "
        "independent filters. Every control here changes what the others do, there is an "
        "insertion loss that varies with the settings, and that interaction is most of why "
        "a real amplifier's tone controls behave as they do.");

    amp->addKnob (ids::middle, "Middle",
        "The one that matters most. Scooping the mids *before* the distortion is the entire "
        "modern high-gain sound -- and on the Modern voicing this stack sits after only two "
        "of the three valves, so it decides what gets distorted rather than only what you "
        "hear of it.");

    amp->addKnob (ids::treble, "Treble",
        "Interacts with the slope resistor and with Bass, as it does in the circuit. "
        "Measured scoop depth at the neutral setting: 9.6 dB on the American stack, 6.6 on "
        "the British, 8.9 on the Modern.");

    amp->addKnob (ids::mix, "Mix",
        "Dry against wet, with the dry path delayed by exactly the reported latency. At 0 "
        "the output is the input to the bit -- the oversampler's round trip is a whole "
        "number of samples by design, so this is an integer delay and not a filter.");

    // ---- CAB -----------------------------------------------------------------

    auto& cab = pages_[1];
    cab = std::make_unique<ControlPage> (anvil_.getState(), palette_, 4);

    cab->addChoice (ids::cabinet, "Cabinet",
        "Synthesised from the mechanisms, never a captured impulse response. The enclosure's "
        "alignment, the rear radiation for an open back, cone breakup, and the driver's own "
        "top from cone mass and voice-coil inductance. None -- take the amplifier straight "
        "out, for re-amping into a real cabinet or somebody else's IR.");

    cab->addKnob (ids::micPosition, "Mic Position",
        "Across the cone, from the dust cap to the surround. The outer cone radiates almost "
        "nothing above a couple of kilohertz, so this is a lowpass whose corner sweeps two "
        "octaves -- 6 kHz to 1.5 kHz. Measured: 10 dB off the top at 3.5 kHz, and the low "
        "end does not move at all.");

    cab->addKnob (ids::micDistance, "Mic Distance",
        "How far back. A directional microphone's proximity rise is 6 dB per octave below "
        "c/(2*pi*d), so closing in lifts the bass and nothing else. Measured: 9 dB at 100 Hz "
        "between 2 cm and 50, and everything above 4 kHz unchanged to three decimal places.");

    cab->addKnob (ids::damping, "Damping",
        "Nominal load over the amplifier's output impedance -- and the part almost every amp "
        "simulation leaves out. A speaker is 93 ohms at its resonance and 6.6 at 400 Hz; a "
        "valve amplifier's output impedance is a large fraction of that, so the two form a "
        "divider that the speaker's impedance curve shapes. At 0.5 that is worth +7.7 dB at "
        "resonance and +6.7 at 8 kHz; at 20 it is a third of a decibel.");

    // ---- CHARACTER -----------------------------------------------------------

    auto& character = pages_[2];
    character = std::make_unique<ControlPage> (anvil_.getState(), palette_, 3);

    character->addKnob (ids::core, "Core",
        "The frequency at which a full swing just fills the output transformer's core, and "
        "the control this plugin exists for. Flux is the integral of voltage, so the same "
        "voltage puts twice as much of it in an octave down -- the flux falls 6 dB per "
        "octave and the distortion it makes falls about 18. Push it up and everything under "
        "a couple of hundred hertz goes thick and harmonically rich while the midrange stays "
        "exactly where it was. The range goes far past any transformer ever wound.");

    character->addKnob (ids::presence, "Presence",
        "Shunts the high end out of the *feedback* signal, which is what the control on the "
        "back of an amplifier actually is -- a capacitor from the feedback tap to ground. It "
        "boosts nothing. It removes the loop's correction up top, so the output stage's own "
        "gain and its own distortion show through, which is why it sounds nothing like a "
        "treble control even when the curves look similar.");

    character->addKnob (ids::resonance, "Resonance",
        "The same trick at the other end. Removing the low frequencies from the feedback "
        "lets the output stage and the transformer do as they like down there -- which, with "
        "a core that saturates on flux, is a great deal. This is the control that makes a "
        "low note bloom.");

    character->addKnob (ids::sag, "Sag",
        "How far the supply rail falls under load, scaling the voicing's own figure. A valve "
        "rectifier sags more and slower than a solid-state one: the vintage lane starts at "
        "32% over 55 ms, the modern at 16% over 20. Past 100% is deliberately available and "
        "is not a real amplifier.");

    character->addKnob (ids::stages, "Stages",
        "Extra preamp valves, cascaded after the tone stack so turning this up adds "
        "distortion behind the tone controls rather than silently moving them. Stock is what "
        "the voicing asks for: one valve, two or three.");

    character->addChoice (ids::oversampling, "Oversampling",
        "Anvil's Auto targets about 384 kHz internally, where the rest of the suite targets "
        "192. A cascade compounds -- each valve distorts the harmonics the last one made -- "
        "so it takes more here. See the note below for the measured figures.");
}

void AnvilEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        auto& page = pages_[static_cast<std::size_t> (i)];
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        const bool active = i == currentPage_;

        if (page != nullptr)
        {
            if (active)
                addAndMakeVisible (*page);
            else
                page->setVisible (false);
        }

        tab.setColour (juce::TextButton::buttonColourId,
                       active ? palette_.accent.withAlpha (0.55f) : palette_.panel.brighter (0.08f));
        tab.setColour (juce::TextButton::textColourOffId, active ? palette_.text : palette_.dimText);
    }

    resized();
}

void AnvilEditor::updateForSwitches()
{
    auto& state = anvil_.getState();

    const int voicing    = static_cast<int> (state.getRawParameterValue (ids::voicing)->load());
    const int cabinet    = static_cast<int> (state.getRawParameterValue (ids::cabinet)->load());
    const int oversample = static_cast<int> (state.getRawParameterValue (ids::oversampling)->load());
    const int latency    = anvil_.getLatencySamples();
    const int core       = juce::roundToInt (state.getRawParameterValue (ids::core)->load());

    if (voicing == shownVoicing_ && cabinet == shownCabinet_ && oversample == shownOversample_
        && latency == shownLatency_ && core == shownCore_)
        return;

    shownVoicing_ = voicing;
    shownCabinet_ = cabinet;
    shownOversample_ = oversample;
    shownLatency_ = latency;
    shownCore_ = core;

    pages_[0]->setNote (anvil_.describeVoicing());

    // With no cabinet there is nothing for the microphone to stand in front of.
    // Greyed rather than hidden: a knob that moves and does nothing reads as a
    // broken plugin. Damping stays live -- the speaker's impedance is what the
    // amplifier sees whether or not anything is listening to it.
    const bool hasCabinet = cabinet != static_cast<int> (CabinetChoice::none);

    for (const char* id : { ids::micPosition, ids::micDistance })
        pages_[1]->setControlEnabled (id, hasCabinet);

    pages_[1]->setNote (hasCabinet
        ? juce::String ("The microphone's two controls do not overlap. Position sweeps the top "
                        "by ten decibels and leaves 100 Hz where it was; Distance moves 100 Hz "
                        "by nine and leaves everything above 4 kHz alone. Damping is the "
                        "amplifier's grip on the speaker and is live either way.")
        : juce::String ("Cabinet is off -- the amplifier and the speaker load, with no acoustic "
                        "model after them. Damping still applies, because the speaker's "
                        "impedance is what the amplifier sees whether or not anything is "
                        "listening to it. Expect it to be bright: nothing is rolling off the "
                        "top the way a 12 inch cone does."));

    pages_[2]->setNote (anvil_.describeCore());
}

void AnvilEditor::timerCallback()
{
    auto& meters = anvil_.getMeterValues();

    inputMeter_->setValues (meters.inputVuDb.load (std::memory_order_relaxed),
                            meters.inputPeakDb.load (std::memory_order_relaxed));
    outputMeter_->setValues (meters.outputVuDb.load (std::memory_order_relaxed),
                             meters.outputPeakDb.load (std::memory_order_relaxed));

    const float sag  = meters.sag.load (std::memory_order_relaxed);
    const float flux = meters.flux.load (std::memory_order_relaxed);
    const float bias = meters.bias.load (std::memory_order_relaxed);

    workingMeter_->setValues (sag, flux, bias);

    inputMeter_->repaint();
    outputMeter_->repaint();
    workingMeter_->repaint();

    const double rate = anvil_.getSampleRate() > 0.0 ? anvil_.getSampleRate() : 48000.0;
    const int latency = anvil_.getLatencySamples();

    juce::String status;
    status << "SAG " << juce::String (juce::roundToInt (sag * 100.0f)) << " %"
           << "   \xe2\x80\xa2   CORE " << juce::String (flux, 2) << " full"
           << "   \xe2\x80\xa2   OUT " << juce::String (
                  meters.outputPeakDb.load (std::memory_order_relaxed), 1) << " dB peak"
           << "   \xe2\x80\xa2   LATENCY ";

    // Until the host has started audio there is no latency figure, only an
    // uninitialised one -- and printing "0 sm" for it says the plugin has none,
    // which is a different claim entirely.
    if (anvil_.isPrepared())
        status << latency << " sm (" << juce::String (1000.0 * latency / rate, 2) << " ms)";
    else
        status << "--";

    statusLabel_.setText (status, juce::dontSendNotification);

    header_->setActiveSlot (anvil_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (anvil_.getAbCompare().otherSlotFilled());

    updateForSwitches();
}

void AnvilEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void AnvilEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 4));

    // The meters flank the panel, so a reading and the control that changes it
    // are never on different pages.
    auto left = bounds.removeFromLeft (kMeterWidth + 8).reduced (4, 6);
    inputMeterLabel_.setBounds (left.removeFromBottom (12));
    inputMeter_->setBounds (left);

    // Wider on the right: the output meter carries the labelled scale.
    auto right = bounds.removeFromRight (kMeterWidth + ui::LevelMeter::kScaleWidth + 8)
                     .reduced (4, 6);
    outputMeterLabel_.setBounds (right.removeFromBottom (12));
    outputMeter_->setBounds (right);

    auto working = bounds.removeFromTop (kWorkingHeight + 12).reduced (4, 4);
    workingLabel_.setBounds (working.removeFromTop (12));
    workingMeter_->setBounds (working);

    auto tabRow = bounds.removeFromTop (kTabHeight).reduced (4, 2);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth).reduced (2, 0));

    for (auto& page : pages_)
        if (page != nullptr)
            page->setBounds (bounds.reduced (4, 2));
}

} // namespace tezla::anvil
