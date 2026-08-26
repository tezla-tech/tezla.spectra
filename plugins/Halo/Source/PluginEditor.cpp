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

/// Reserved for the page note, whether or not the grid would have left room.
constexpr int kNoteHeight = 34;

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

    knob->id = parameterId;
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

    choice->id = parameterId;
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

void ControlPage::setControlEnabled (const char* parameterId, bool enabled)
{
    for (auto& knob : knobs_)
    {
        if (knob->id != parameterId)
            continue;

        knob->slider.setEnabled (enabled);
        knob->label.setColour (juce::Label::textColourId, enabled ? kDimText : kDimText.withAlpha (0.35f));
        knob->slider.setColour (juce::Slider::rotarySliderFillColourId,
                                enabled ? kGlow : kGlow.withAlpha (0.2f));
        knob->slider.setColour (juce::Slider::thumbColourId,
                                enabled ? kGlowBright : kGlowBright.withAlpha (0.25f));
        knob->slider.setColour (juce::Slider::textBoxTextColourId,
                                enabled ? kText : kText.withAlpha (0.3f));
        knob->label.repaint();
        knob->slider.repaint();
        return;
    }

    for (auto& choice : choices_)
    {
        if (choice->id != parameterId)
            continue;

        choice->box.setEnabled (enabled);
        choice->label.setColour (juce::Label::textColourId, enabled ? kDimText : kDimText.withAlpha (0.35f));
        choice->label.repaint();
        return;
    }
}

void ControlPage::addBreak()
{
    // Pads to the end of the current row, so a group starts on a fresh line.
    while (cells_.size() % static_cast<std::size_t> (columns_) != 0)
        cells_.push_back ({ Cell::Kind::gap, 0 });
}

void ControlPage::paint (juce::Graphics& g)
{
    if (note_.isEmpty() || noteArea_.getHeight() <= 0)
        return;

    g.setColour (kDimText);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (note_, noteArea_, juce::Justification::topLeft, 6, 1.0f);
}

void ControlPage::resized()
{
    if (cells_.empty())
        return;

    auto bounds = getLocalBounds().reduced (4, 2);

    // The note gets its space reserved before the grid takes any, rather than
    // living on whatever is left over. Twice now it has silently disappeared
    // because the grid grew and the remainder fell below the threshold paint()
    // needed -- and a note that vanishes when the window is a little short is
    // worse than no note, because it is there when you write it and gone when
    // someone uses it.
    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (6, 0);
    else
        noteArea_ = {};

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

}

// ============================================================================

HaloEditor::HaloEditor (HaloProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse), halo_ (processorToUse)
{
    // Halo's own accent, over the house dark panel. Bypass keeps the shared
    // warning orange whatever the accent is, so its state reads the same in
    // every plugin.
    palette_.accent       = kGlow;
    palette_.accentBright = kGlowBright;
    palette_.secondary    = kHarmonics;

    header_ = std::make_unique<ui::HeaderBar> (halo_.getValueTreeState(), "HALO",
                                               "harmonic exciter", ids::bypass, palette_);

    auto& ab = halo_.getAbCompare();
    header_->onSwapRequested = [&ab] { ab.swapSlots(); };
    header_->onCopyRequested = [&ab] { ab.copyToOtherSlot(); };
    ab.onChanged = [this]
    {
        auto& compare = halo_.getAbCompare();
        header_->setActiveSlot (compare.isSlotB());
        header_->setOtherSlotFilled (compare.otherSlotFilled());
    };
    header_->setActiveSlot (ab.isSlotB());
    header_->setOtherSlotFilled (ab.otherSlotFilled());
    addAndMakeVisible (*header_);

    spectrum_ = std::make_unique<ui::SpectrumDisplay> (palette_);

    // Dragging the graph drives the real parameter, gestures and all, so a host
    // records the move as one automation write and the knob follows along.
    // Clamping is the parameter's job: convertTo0to1 does it, and the display
    // has no business knowing that Focus stops at 12 kHz.
    if (auto* focus = halo_.getValueTreeState().getParameter (ids::focus))
    {
        spectrum_->onFocusDragged = [focus] (double hz, ui::SpectrumDisplay::DragPhase phase)
        {
            using Phase = ui::SpectrumDisplay::DragPhase;

            if (phase == Phase::began)
                focus->beginChangeGesture();
            else if (phase == Phase::ended)
                focus->endChangeGesture();
            else
                focus->setValueNotifyingHost (focus->convertTo0to1 (static_cast<float> (hz)));
        };
    }
    spectrum_->prepare (halo_.getSampleRate() > 0.0 ? halo_.getSampleRate() : 48000.0);
    addAndMakeVisible (*spectrum_);

    buildPages();

    static const char* pageNames[kNumPages] { "MAIN", "SHAPE", "CHEBYSHEV" };
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

    updateForGenerator();
    showPage (0);

    setResizable (true, true);
    setResizeLimits (760, 620, 1520, 1240);
    setSize (860, 690);

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

    main->addChoice (ids::generator, "Generator",
        "Which engine makes the harmonics. These are two different instruments behind "
        "one panel, not two flavours of the same one.\n\n"
        "CURVE is what you have been listening to: two smooth shapes blended by Colour. "
        "You pick a shape and take the series that falls out of it, which is how every "
        "exciter works -- and why none of them can be asked for one particular harmonic.\n\n"
        "CHEBYSHEV picks the series and derives the curve. Because T(n) of a cosine is "
        "exactly the nth harmonic, asking for the 5th gives you the 5th and nothing else: "
        "measured, every other harmonic sits 120 dB below it. Drive, Colour and Track grey "
        "out because the CHEBYSHEV page replaces all three.\n\n"
        "Switching crossfades over 15 ms, so you can A/B it while playing.");

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

    main->addKnob (ids::width, "Width",
        "Stereo width of the generated harmonics -- and of nothing else.\n\n"
        "This is the control most exciters cannot offer. Theirs mix back a filtered copy of your "
        "source, so widening the effect widens the source with it and the low end stops being "
        "mono. Halo's wet path carries almost no fundamental and the dry path goes through a "
        "delay line and nothing else, so wide air over a dead-centre sub is simply what happens.\n\n"
        "Normal is a true identity, not a round trip through mid and side -- it multiplies the "
        "side by exactly zero. Mono folds the harmonics to the centre, which is worth trying on a "
        "reese where the added edge is smearing the image.\n\n"
        "Does nothing on a mono track, and nothing to a mono source on a stereo track.");

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

    shape->setNote (
        "Floor and Ceiling shape the harmonics only -- the dry signal never passes through either of them, "
        "so nothing here can thin your sub.");

    pages_[1] = std::move (shape);

    // ---- CHEBYSHEV ----------------------------------------------------------
    //
    // Seven levels in a row reads as a harmonic spectrum, which is the picture
    // the mode is about; Index and Tilt go underneath on the second row.
    auto cheb = std::make_unique<ControlPage> (state, 7);

    static const char* harmonicNames[] {
        "2nd -- octave", "3rd -- fifth", "4th -- 2 oct", "5th -- major 3rd",
        "6th -- 12th", "7th -- flat 7th", "8th -- 3 oct"
    };

    for (int n = dsp::ChebyshevGenerator::kFirstHarmonic;
         n <= dsp::ChebyshevGenerator::kLastHarmonic; ++n)
    {
        const auto index = static_cast<std::size_t> (n - dsp::ChebyshevGenerator::kFirstHarmonic);

        cheb->addKnob (ids::harmonics[index], "H" + juce::String (n),
            juce::String ("Level of the ") + harmonicNames[index] + " harmonic.\n\n"
            "Off through +20 dB. At Index = Exact this really is that harmonic alone: "
            "the others are 120 dB down, not merely quieter. Above unity the harmonic "
            "comes out louder than the source that made it, which is bounded and safe "
            "-- the polynomials never exceed 1, so the level is the number you set.\n\n"
            "With every level at Off the generator is exactly the zero function and the "
            "plugin is bit-exact bypass.");
    }

    cheb->addKnob (ids::chebIndex, "Index",
        "How hard the normalised band is pushed into the polynomials. Le Brun's "
        "waveshaping index -- the same idea as an FM modulation index.\n\n"
        "EXACT (1.0) is the point the whole mode is built around: your recipe arrives "
        "as written.\n\n"
        "Below it the harmonics blend into one another, so the recipe breathes with the "
        "material instead of standing still. Off is a true zero.\n\n"
        "Above it the input clamps and this stops being harmonic synthesis: what comes "
        "out is the wreckage of a chosen series, square-ish and harsh. It aliases like "
        "a distortion because that is what it now is -- around -60 dB on a bass band. "
        "That is the crazy end, and it is deliberate.");

    cheb->addKnob (ids::chebTilt, "Tilt",
        "One knob across all seven levels, pivoting on the 5th. 4 dB per harmonic step "
        "at full deflection, so 24 dB end to end.\n\n"
        "Left is darker -- weight on the low harmonics, which reads as body. Right is "
        "brighter and more metallic. FLAT multiplies your levels by exactly one, so the "
        "macro can never colour a recipe you set by hand.");

    cheb->setNote (
        "Chebyshev harmonic synthesis -- Le Brun, JAES 27(4), 1979. T(n) of a cosine is cos(n t), so harmonic n "
        "arrives at the level you set and nothing else does. Track is pinned at 100% here: unit amplitude is what "
        "makes that true. Best on one note at a time -- on a chord the harmonics of each note intermodulate.");

    pages_[2] = std::move (cheb);

    for (auto& page : pages_)
        addChildComponent (*page);
}

void HaloEditor::updateForGenerator()
{
    const bool chebyshev =
        halo_.getValueTreeState().getRawParameterValue (ids::generator)->load() > 0.5f;

    if (shownGenerator_ == static_cast<int> (chebyshev))
        return;

    shownGenerator_ = static_cast<int> (chebyshev);

    // Drive, Colour and Track are not merely unused in Chebyshev mode -- they
    // are replaced. Leaving them live but inert reads as a broken plugin; greyed
    // out, it reads as a mode, which is what it is.
    for (const char* id : { ids::drive, ids::colour, ids::track })
        pages_[0]->setControlEnabled (id, ! chebyshev);

    pages_[0]->setNote (chebyshev
        ? "Chebyshev generator: Drive, Colour and Track are replaced by the CHEBYSHEV page. Everything else on this "
          "page still applies -- Focus, Punch, Width, Amount and Output work the same either way."
        : "Amount at Off is a bit-exact bypass -- the dry path goes through a delay line and nothing else. "
          "Latency is reported to the host and matched on bypass, so A/B is honest.");

    pages_[0]->repaint();
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
    updateForGenerator();

    if (spectrum_ != nullptr)
    {
        auto& state = halo_.getValueTreeState();

        spectrum_->setFocusFrequency (state.getRawParameterValue (ids::focus)->load(),
                                      state.getRawParameterValue (ids::bandMode)->load() < 0.5f);
        spectrum_->setHarmonicLimits (state.getRawParameterValue (ids::floorOn)->load() > 0.5f,
                                      state.getRawParameterValue (ids::floorHz)->load(),
                                      state.getRawParameterValue (ids::ceilingOn)->load() > 0.5f,
                                      state.getRawParameterValue (ids::ceilingHz)->load());
        spectrum_->setDimmed (state.getRawParameterValue (ids::bypass)->load() > 0.5f);
        spectrum_->update (halo_.getInputCapture(), halo_.getOutputCapture());
    }
}

void HaloEditor::paint (juce::Graphics& g)
{
    // The header draws itself now -- it is a component rather than a painted
    // strip, because it holds the two controls a user reaches for while
    // listening.
    g.fillAll (kBackground);
}

void HaloEditor::resized()
{
    auto bounds = getLocalBounds();

    if (header_ != nullptr)
        header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    auto tabStrip = bounds.removeFromTop (30);
    const int tabWidth = juce::jmin (110, tabStrip.getWidth() / kNumPages);
    tabStrip.removeFromLeft (12);
    for (auto& tab : tabs_)
        tab.setBounds (tabStrip.removeFromLeft (tabWidth).reduced (2, 3));

    auto footer = bounds.removeFromBottom (38);
    statusLabel_.setBounds (footer.reduced (16, 2));

    // The controls take what they need and no more -- two rows of capped cells
    // plus the page note. Everything left over goes to the spectrum, which is
    // the part that benefits from height. Giving the pages the remainder instead
    // left a band of empty panel between the note and the status line.
    auto controls = bounds.removeFromTop (juce::jmin (300, juce::jmax (200, bounds.getHeight() - 170)));

    auto meterArea = controls.removeFromRight (110).reduced (10, 12);
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
        page->setBounds (controls.reduced (8, 4));

    if (spectrum_ != nullptr)
        spectrum_->setBounds (bounds.reduced (12, 4));
}

} // namespace tezla::halo
