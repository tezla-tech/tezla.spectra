#include "PluginEditor.h"

#include <tezla/dsp/Oversampler.hpp>

namespace tezla::halo
{

namespace
{
const juce::Colour kBackground  { 0xff141416 };
const juce::Colour kPanel       { 0xff1d1d20 };
const juce::Colour kText        { 0xffd8d5cf };
const juce::Colour kDimText     { 0xff86837e };
const juce::Colour kGlow        { 0xffd9b24a };
const juce::Colour kGlowBright  { 0xfff2d888 };
const juce::Colour kHarmonics   { 0xff54c7c0 };

constexpr int kLabelHeight = 16;
constexpr int kValueHeight = 18;

/// A single row of controls stretched to the full page height puts a giant knob
/// a long way below its own label. Capping the cell height keeps the proportions
/// right however tall the window is.
constexpr int kMaxCellHeight = 132;

constexpr float kMeterFloorDb = -60.0f;
constexpr float kMeterTopDb   = 6.0f;

/// The harmonics meter is a ratio, not a level: how much harmonic energy is
/// being added relative to the source. Anything above about -6 dB is a lot.
constexpr float kHarmonicsFloorDb = -60.0f;
constexpr float kHarmonicsTopDb   = 0.0f;
} // namespace

float LevelMeter::positionFor (float db) const noexcept
{
    if (style_ == Style::harmonics)
        return juce::jlimit (0.0f, 1.0f,
                             (db - kHarmonicsFloorDb) / (kHarmonicsTopDb - kHarmonicsFloorDb));

    return juce::jlimit (0.0f, 1.0f, (db - kMeterFloorDb) / (kMeterTopDb - kMeterFloorDb));
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (kBackground.brighter (0.08f));
    g.fillRoundedRectangle (bounds, 2.0f);

    const bool isHarmonics = style_ == Style::harmonics;
    const float filled = positionFor (vuDb_);

    if (filled > 0.0f)
    {
        auto bar = bounds.reduced (1.0f);
        g.setColour (isHarmonics ? kHarmonics : kGlow);
        g.fillRoundedRectangle (bar.removeFromBottom (bar.getHeight() * filled), 1.5f);
    }

    if (! isHarmonics && peakDb_ > kMeterFloorDb)
    {
        // A thin line rather than a second bar: the peak is a warning, not a
        // level, and drawing it as a bar invites reading it as one.
        const float y = bounds.getBottom() - bounds.getHeight() * positionFor (peakDb_);
        g.setColour (peakDb_ > -0.1f ? juce::Colours::red : kGlowBright);
        g.fillRect (bounds.getX() + 1.0f, y - 1.0f, bounds.getWidth() - 2.0f, 2.0f);
    }

    if (! isHarmonics)
    {
        const float y = bounds.getBottom() - bounds.getHeight() * positionFor (0.0f);
        g.setColour (kDimText.withAlpha (0.5f));
        g.fillRect (bounds.getX(), y, bounds.getWidth(), 1.0f);
    }
}

void WrappingLabel::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (getFont());
    g.drawFittedText (getText(), getLocalBounds(), juce::Justification::topLeft, 2, 1.0f);
}

// ============================================================================

void ControlPage::addKnob (const char* parameterId, const juce::String& name, const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, kValueHeight);
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, kGlow);
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel.brighter (0.25f));
    knob->slider.setColour (juce::Slider::thumbColourId, kGlowBright);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, kText);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, kDimText);
    knob->label.setFont (juce::FontOptions (12.0f));
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    cells_.push_back ({ Cell::Kind::knob, static_cast<int> (knobs_.size()) });
    knobs_.push_back (std::move (knob));
}

void ControlPage::addChoice (const char* parameterId, const juce::String& name, const juce::String& tooltip)
{
    auto choice = std::make_unique<Choice>();

    // Populate from the parameter itself. A ComboBoxAttachment selects an item
    // by index and does not create one, so a box left empty here stays empty on
    // screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state_.getParameter (parameterId)))
        choice->box.addItemList (parameter->choices, 1);
    else
        jassertfalse;   // addChoice used on something that is not a choice parameter

    choice->box.setColour (juce::ComboBox::backgroundColourId, kPanel.brighter (0.15f));
    choice->box.setColour (juce::ComboBox::textColourId, kText);
    choice->box.setColour (juce::ComboBox::outlineColourId, kPanel.brighter (0.3f));
    choice->box.setTooltip (tooltip);
    addAndMakeVisible (choice->box);

    choice->label.setText (name, juce::dontSendNotification);
    choice->label.setJustificationType (juce::Justification::centred);
    choice->label.setColour (juce::Label::textColourId, kDimText);
    choice->label.setFont (juce::FontOptions (12.0f));
    choice->label.setTooltip (tooltip);
    addAndMakeVisible (choice->label);

    choice->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state_, parameterId, choice->box);

    cells_.push_back ({ Cell::Kind::choice, static_cast<int> (choices_.size()) });
    choices_.push_back (std::move (choice));
}

void ControlPage::addToggle (const char* parameterId, const juce::String& name, const juce::String& tooltip)
{
    auto toggle = std::make_unique<Toggle>();

    toggle->button.setButtonText (name);
    toggle->button.setColour (juce::ToggleButton::textColourId, kText);
    toggle->button.setColour (juce::ToggleButton::tickColourId, kGlow);
    toggle->button.setTooltip (tooltip);
    addAndMakeVisible (toggle->button);

    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, toggle->button);

    cells_.push_back ({ Cell::Kind::toggle, static_cast<int> (toggles_.size()) });
    toggles_.push_back (std::move (toggle));
}

void ControlPage::addBreak()
{
    // Pads to the end of the current row, so a group starts on a fresh line.
    while (cells_.size() % static_cast<std::size_t> (columns_) != 0)
        cells_.push_back ({ Cell::Kind::gap, 0 });
}

void ControlPage::paint (juce::Graphics& g)
{
    if (note_.isEmpty())
        return;

    auto area = getLocalBounds().reduced (10, 4).withTop (gridBottom_ + 10);
    if (area.getHeight() < 20)
        return;

    g.setColour (kDimText);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (note_, area, juce::Justification::topLeft, 6, 1.0f);
}

void ControlPage::resized()
{
    if (cells_.empty())
        return;

    auto bounds = getLocalBounds().reduced (4, 2);

    const int rows = (static_cast<int> (cells_.size()) + columns_ - 1) / columns_;
    const int cellWidth  = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (bounds.getHeight() / juce::jmax (1, rows), kMaxCellHeight);

    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row    = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    bounds.getY() + row * cellHeight,
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
                    juce::jmin (cell.getWidth() - 12, 132), 26));
                break;
            }
            case Cell::Kind::toggle:
            {
                auto& toggle = *toggles_[static_cast<std::size_t> (cells_[i].index)];
                toggle.button.setBounds (cell.withSizeKeepingCentre (
                    juce::jmin (cell.getWidth() - 8, 130), 26));
                break;
            }
            case Cell::Kind::gap:
                break;
        }
    }

    gridBottom_ = bounds.getY() + rows * cellHeight;
}

// ============================================================================

HaloEditor::HaloEditor (HaloProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse), halo_ (processorToUse)
{
    buildPages();

    static const char* pageNames[kNumPages] { "MAIN", "SHAPE" };
    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (pageNames[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].setColour (juce::TextButton::buttonColourId, kPanel);
        tabs_[static_cast<std::size_t> (i)].setColour (juce::TextButton::textColourOffId, kDimText);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    for (auto* meter : { &inputMeter_, &harmonicsMeter_, &outputMeter_ })
        addAndMakeVisible (*meter);

    for (auto* label : { &inputMeterLabel_, &harmonicsMeterLabel_, &outputMeterLabel_ })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, kDimText);
        label->setFont (juce::FontOptions (10.0f));
        addAndMakeVisible (*label);
    }

    inputMeterLabel_.setTooltip ("Input level. Solid bar is VU (300 ms averaging); the line is peak.");
    outputMeterLabel_.setTooltip ("Output level. Solid bar is VU (300 ms averaging); the line is peak. "
                                  "The line turns red at 0 dBFS.");
    harmonicsMeterLabel_.setTooltip (
        "How much harmonic energy is being added, relative to the source, from -60 dB to 0 dB.\n\n"
        "This is the reading an exciter actually needs and the one a level meter cannot give you: "
        "with Auto trim on, the output meter barely moves however hard you drive it, so it tells "
        "you nothing about how much you are adding. Around -20 dB is a polish; above -6 dB is an "
        "effect.");

    statusLabel_.setColour (juce::Label::textColourId, kDimText);
    statusLabel_.setFont (juce::FontOptions (11.0f));
    statusLabel_.setJustificationType (juce::Justification::topLeft);
    statusLabel_.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (statusLabel_);

    showPage (0);

    setResizable (true, true);
    setResizeLimits (720, 470, 1440, 940);
    setSize (820, 530);

    startTimerHz (30);
}

void HaloEditor::buildPages()
{
    auto& state = halo_.getValueTreeState();

    // ---- MAIN ---------------------------------------------------------------
    //
    // Five columns on both pages, not four. Both pages then fit in two rows,
    // which leaves room under the grid for the page note -- at four columns the
    // SHAPE page needed three rows, Bypass sat alone on the last one, and the
    // note was silently dropped because paint() had under 20 px to draw it in.
    auto main = std::make_unique<ControlPage> (state, 5);

    main->addChoice (ids::bandMode, "Mode",
        "Which side of Focus gets excited.\n\n"
        "ABOVE is the classic exciter: harmonics of the presence region land in the "
        "air band, and the result reads as detail and openness rather than as EQ.\n\n"
        "BELOW is a bass enhancer, and it is the more interesting half on this music. "
        "A 40 Hz sub is simply not there on a phone or a laptop, but its harmonics at "
        "80 and 120 Hz are -- and the ear supplies the fundamental it cannot hear. Your "
        "sub survives the journey to a small speaker without you touching the sub.");

    main->addKnob (ids::focus, "Focus",
        "Where the band starts, or ends, depending on Mode.\n\n"
        "A 24 dB/octave Linkwitz-Riley split, so it is decisive rather than a gentle "
        "tilt. In ABOVE mode, 2-4 kHz adds presence and bite; 5-8 kHz adds air. In "
        "BELOW mode, set it just above the fundamental you want to reinforce -- around "
        "110 Hz for a 40-55 Hz sub.\n\n"
        "Costs nothing: this filter runs whatever you set it to.");

    main->addKnob (ids::drive, "Drive",
        "How hard the band is pushed into the harmonic generator.\n\n"
        "This sets the recipe, not the level -- how far up the harmonic series the "
        "energy goes. Low settings are almost purely second and third; high settings "
        "reach further and get grittier. Amount decides how much of it you hear.\n\n"
        "At 0 the generator is exactly the zero function, so the plugin is bit-exact "
        "bypassed however everything else is set.");

    main->addKnob (ids::colour, "Colour",
        "Which harmonics get made.\n\n"
        "ODD (left) is third, fifth, seventh: edge, bite, presence. It is the half that "
        "makes a reese cut through.\n\n"
        "EVEN (right) is second, fourth: octave-up shimmer and warmth. It is also the "
        "cleaner half -- an even function of the input cannot produce a fundamental at "
        "all, so at full EVEN what gets added is provably harmonics and nothing else.\n\n"
        "The two are level-matched to within about 1.4 dB across the whole Drive range, "
        "so this changes the sound rather than the volume.");

    main->addKnob (ids::amount, "Amount",
        "How much of the generated harmonics get added to the source.\n\n"
        "The dry signal is never filtered and never oversampled -- it passes through a "
        "delay line and nothing else. At the bottom of the travel this reads Off and "
        "the output is the input, bit for bit, so you can leave the plugin in a chain "
        "with no effect at all.\n\n"
        "Watch the HARM meter rather than the output meter: with Auto trim on, the "
        "output level barely moves however much you add.");

    main->addKnob (ids::track, "Track",
        "How much the harmonics follow the source level.\n\n"
        "At 0 they behave like a real nonlinearity: quiet passages get proportionally "
        "less excitement, loud ones bloom. Measured, a 30 dB drop in source takes 52 dB "
        "off the harmonics.\n\n"
        "At 100% the band is normalised before the generator, so the harmonic-to-source "
        "ratio holds constant at every level -- measured at 0.0 dB across the same "
        "30 dB range. That is what makes the effect predictable on a whole mix instead "
        "of only working at one fader position.\n\n"
        "Costs nothing; it is a gain, not a filter.");

    main->addKnob (ids::punch, "Punch",
        "Makes the harmonics arrive on transients and leave the sustain alone.\n\n"
        "Two envelope followers on the band, one fast and one slow; how far the fast "
        "one sits above the slow one is what a transient looks like. At 100% steady "
        "material gets nothing and hits get everything.\n\n"
        "This is what stops an exciter turning a jungle break into a wash of cymbals. "
        "At 0 the stage is bit-exact, so it costs nothing when you are not using it.");

    main->addKnob (ids::output, "Output",
        "Final level trim, applied after everything else.\n\n"
        "A plain gain: it cannot clip the plugin and it does not feed back into the "
        "generator.");

    main->setNote (
        "Amount at Off is a bit-exact bypass -- the dry path goes through a delay line and nothing else. "
        "Latency is reported to the host and matched on bypass, so A/B is honest.");

    pages_[0] = std::move (main);

    // ---- SHAPE --------------------------------------------------------------
    auto shape = std::make_unique<ControlPage> (state, 5);

    shape->addToggle (ids::floorOn, "Floor",
        "Removes generated harmonics below the Floor frequency.\n\n"
        "Mostly for BELOW mode: the second harmonic of a 40 Hz sub is 80 Hz, which is "
        "still sub, and adding energy there makes the low end muddier rather than more "
        "audible. Set Floor above your sub and you add only the part a small speaker "
        "can actually reproduce.\n\n"
        "Off by default, and switching it in clears its filter state so it cannot click.");

    shape->addKnob (ids::floorHz, "Floor Hz",
        "Where the Floor filter sits. 24 dB/octave, fourth-order Butterworth, so the "
        "passband above it is left flat.\n\n"
        "Has no effect unless Floor is switched on.");

    shape->addToggle (ids::ceilingOn, "Ceiling",
        "Removes generated harmonics above the Ceiling frequency.\n\n"
        "On by default and worth leaving on. High-order harmonics up near Nyquist are "
        "inaudible, eat headroom, and are the ones most likely to fold back as aliasing "
        "-- measured, the default Ceiling buys 2 to 3 dB of alias rejection for free.");

    shape->addKnob (ids::ceilingHz, "Ceiling Hz",
        "Where the Ceiling filter sits. 24 dB/octave, fourth-order Butterworth.\n\n"
        "16 kHz by default. Lower it if the top is getting harsh; the harmonics below "
        "it are left alone -- measured 0.2 dB of change an octave below the corner "
        "against 24.5 dB an octave above it.");

    shape->addToggle (ids::listen, "Listen",
        "Solos the harmonics: you hear what is being added and nothing else.\n\n"
        "Worth doing on every setting you dial in. Because the wet path carries almost "
        "no fundamental by design, this really is the added material rather than a "
        "filtered copy of your source -- which is exactly what it would be on a "
        "conventional exciter.\n\n"
        "Auto trim is suspended while this is on, so what you hear is not being "
        "level-corrected behind your back.");

    shape->addToggle (ids::autoTrim, "Auto Trim",
        "Holds the output level steady as you add harmonics, so you judge brightness "
        "rather than loudness.\n\n"
        "Harmonics and source share no partials, so they add as power; undoing that is "
        "one square root rather than a loudness model. It follows the programme over "
        "about a quarter of a second, so it reads as a level match and not as a "
        "compressor.\n\n"
        "Exactly 1.0 when nothing is being added, so it cannot disturb the bypass.");

    shape->addKnob (ids::input, "Input",
        "Level trim before everything.\n\n"
        "Both paths see it, so it is not a drive control -- it does not change the "
        "harmonic recipe on its own. Use it if the plugin is being fed something far "
        "from a sensible level.");

    shape->addChoice (ids::oversampling, "Oversampling",
        "How much headroom the generator gets to work in.\n\n"
        "Generating harmonics creates energy above the audio band. Without room for it, "
        "it folds back as inharmonic rubbish. Auto picks the factor from your session "
        "rate to land near 192 kHz internally, so the plugin sounds identical at 44.1, "
        "48, 96 and 192 kHz.\n\n"
        "Measured at maximum drive: -61 to -93 dB of audible-band aliasing with Auto, "
        "against -35 dB with it off. Off is cheaper and is what a conventional "
        "host-rate exciter effectively does.\n\n"
        "The status line below says what Auto is doing right now.");

    shape->addToggle (ids::bypass, "Bypass",
        "Latency-matched and crossfaded over 10 ms, so switching it neither clicks nor "
        "shifts the timing.\n\n"
        "The bypassed path is delayed by exactly the latency the host is told about. "
        "Without that the bypassed signal would arrive earlier and sound tighter for "
        "reasons that have nothing to do with the plugin, and every A/B would be a lie.");

    shape->setNote (
        "Floor and Ceiling shape the harmonics only -- the dry signal never passes through either of them, "
        "so nothing here can thin your sub.");

    pages_[1] = std::move (shape);

    for (auto& page : pages_)
        addChildComponent (*page);
}

void HaloEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        pages_[static_cast<std::size_t> (i)]->setVisible (i == currentPage_);
        tabs_[static_cast<std::size_t> (i)].setColour (
            juce::TextButton::buttonColourId, i == currentPage_ ? kPanel.brighter (0.25f) : kPanel);
        tabs_[static_cast<std::size_t> (i)].setColour (
            juce::TextButton::textColourOffId, i == currentPage_ ? kGlowBright : kDimText);
    }

    resized();
    repaint();
}

void HaloEditor::timerCallback()
{
    auto& meters = halo_.getMeterValues();

    inputMeter_.setValues (meters.inputVuDb.load (std::memory_order_relaxed),
                           meters.inputPeakDb.load (std::memory_order_relaxed));
    outputMeter_.setValues (meters.outputVuDb.load (std::memory_order_relaxed),
                            meters.outputPeakDb.load (std::memory_order_relaxed));
    harmonicsMeter_.setValues (meters.harmonicsDb.load (std::memory_order_relaxed), -100.0f);

    inputMeter_.repaint();
    outputMeter_.repaint();
    harmonicsMeter_.repaint();

    statusLabel_.setText (halo_.describeOversampling(), juce::dontSendNotification);
}

void HaloEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (44);

    g.setColour (kPanel);
    g.fillRect (header);

    g.setColour (kGlow);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("HALO", header.reduced (16, 0), juce::Justification::centredLeft);

    g.setColour (kDimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("TEZLA TECH  \xc2\xb7  harmonic exciter"),
                header.reduced (16, 0), juce::Justification::centredRight);
}

void HaloEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (44);

    auto tabStrip = bounds.removeFromTop (30);
    const int tabWidth = juce::jmin (110, tabStrip.getWidth() / kNumPages);
    tabStrip.removeFromLeft (12);
    for (auto& tab : tabs_)
        tab.setBounds (tabStrip.removeFromLeft (tabWidth).reduced (2, 3));

    auto footer = bounds.removeFromBottom (40);
    statusLabel_.setBounds (footer.reduced (16, 4));

    auto meterArea = bounds.removeFromRight (110).reduced (10, 12);
    const int meterWidth = meterArea.getWidth() / 3;

    const auto layoutMeter = [] (LevelMeter& meter, juce::Label& label, juce::Rectangle<int> area)
    {
        label.setBounds (area.removeFromBottom (14));
        meter.setBounds (area.reduced (4, 0));
    };

    layoutMeter (inputMeter_,     inputMeterLabel_,     meterArea.removeFromLeft (meterWidth));
    layoutMeter (harmonicsMeter_, harmonicsMeterLabel_, meterArea.removeFromLeft (meterWidth));
    layoutMeter (outputMeter_,    outputMeterLabel_,    meterArea);

    for (auto& page : pages_)
        page->setBounds (bounds.reduced (8, 4));
}

} // namespace tezla::halo
