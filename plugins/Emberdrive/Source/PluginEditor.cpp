#include "PluginEditor.h"

namespace tezla::emberdrive
{

namespace
{
const juce::Colour kBackground   { 0xff141416 };
const juce::Colour kPanel        { 0xff1d1d20 };
const juce::Colour kText         { 0xffd8d5cf };
const juce::Colour kDimText      { 0xff86837e };
const juce::Colour kEmber        { 0xffd8722c };
const juce::Colour kEmberBright  { 0xfff2a03d };
const juce::Colour kReduction    { 0xff4f9fd8 };

constexpr float kMeterFloorDb   = -60.0f;
constexpr float kMeterTopDb     = 6.0f;
constexpr float kReductionMaxDb = 24.0f;

constexpr int kLabelHeight = 16;
constexpr int kValueHeight = 18;

/// Cap on how tall one grid cell may grow. Without it a page with a single row
/// -- MANGLE has two controls -- stretches that row to the full page height,
/// which centres a giant knob a hundred pixels below its own label.
constexpr int kMaxCellHeight = 132;
} // namespace

// ============================================================================

float LevelMeter::positionFor (float db) const noexcept
{
    if (style_ == Style::gainReduction)
        return juce::jlimit (0.0f, 1.0f, -db / kReductionMaxDb);

    return juce::jlimit (0.0f, 1.0f, (db - kMeterFloorDb) / (kMeterTopDb - kMeterFloorDb));
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (kBackground.brighter (0.08f));
    g.fillRoundedRectangle (bounds, 2.0f);

    const bool isReduction = style_ == Style::gainReduction;
    const float filled = positionFor (vuDb_);

    if (filled > 0.0f)
    {
        auto bar = bounds.reduced (1.0f);
        g.setColour (isReduction ? kReduction : kEmber);
        g.fillRoundedRectangle (bar.removeFromBottom (bar.getHeight() * filled), 1.5f);
    }

    if (! isReduction && peakDb_ > kMeterFloorDb)
    {
        // A thin line rather than a second bar: the peak is a warning, not a
        // level, and drawing it as a bar invites reading it as one.
        const float y = bounds.getBottom() - bounds.getHeight() * positionFor (peakDb_);
        g.setColour (peakDb_ > -0.1f ? juce::Colours::red : kEmberBright);
        g.fillRect (bounds.getX() + 1.0f, y - 1.0f, bounds.getWidth() - 2.0f, 2.0f);
    }

    if (! isReduction)
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
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, kEmber);
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel.brighter (0.25f));
    knob->slider.setColour (juce::Slider::thumbColourId, kEmberBright);
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
    // screen and cannot be operated at all -- which is exactly how it shipped
    // the first time this page was drawn.
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
    toggle->button.setColour (juce::ToggleButton::tickColourId, kEmber);
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
                    juce::jmin (cell.getWidth() - 12, 96), 26));
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

EmberdriveEditor::EmberdriveEditor (EmberdriveProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse), processor_ (processorToUse)
{
    buildPages();

    static const char* pageNames[kNumPages] { "MAIN", "BANDS", "MANGLE", "EXPERT" };
    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (pageNames[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].setColour (juce::TextButton::buttonColourId, kPanel);
        tabs_[static_cast<std::size_t> (i)].setColour (juce::TextButton::textColourOffId, kDimText);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    for (auto* meter : { &inputMeter_, &reductionMeter_, &outputMeter_ })
        addAndMakeVisible (*meter);

    for (auto* label : { &inputMeterLabel_, &reductionMeterLabel_, &outputMeterLabel_ })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, kDimText);
        label->setFont (juce::FontOptions (10.0f));
        addAndMakeVisible (*label);
    }

    inputMeterLabel_.setTooltip ("Input level. Solid bar is VU (300 ms averaging); the line is peak.");
    outputMeterLabel_.setTooltip ("Output level. Solid bar is VU (300 ms averaging); the line is peak. "
                                  "The line turns red at 0 dBFS.");
    reductionMeterLabel_.setTooltip ("Gain reduction, up to 24 dB. In multiband mode this is the "
                                     "master limiter on the summed bands.");

    statusLabel_.setColour (juce::Label::textColourId, kDimText);
    statusLabel_.setFont (juce::FontOptions (11.0f));
    statusLabel_.setJustificationType (juce::Justification::topLeft);
    statusLabel_.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (statusLabel_);

    showPage (0);

    setResizable (true, true);
    setResizeLimits (740, 500, 1480, 1000);
    setSize (840, 560);

    startTimerHz (30);
}

void EmberdriveEditor::buildPages()
{
    auto& state = processor_.getValueTreeState();

    // ---- MAIN --------------------------------------------------------------
    auto main = std::make_unique<ControlPage> (state, 5);

    main->addKnob (ids::drive, "Drive",
        "How hard the signal is pushed into the saturation stage.\n\n"
        "At 0 dB the stage is close to a straight wire (0.006% THD at -20 dBFS) -- "
        "genuinely clean, not a quieter version of the dirty setting. By +18 dB the "
        "harmonics are obvious; +30 dB is destruction.\n\n"
        "With Auto trim on, the output level barely moves as you turn this, so you "
        "are judging tone rather than loudness.");

    main->addKnob (ids::character, "Character",
        "Morphs between two ways of distorting.\n\n"
        "TAPE (left): symmetric, so only odd harmonics -- a firm, compressive knee, "
        "with a low-frequency head bump and the gentle high-frequency loss of a tape "
        "machine. Holds together on sub bass.\n\n"
        "VALVE (right): asymmetric, which brings in even harmonics -- warmer and "
        "thicker, flatter and brighter through the top.\n\n"
        "This changes harmonic content, not level. The EXPERT page can override "
        "every constant it drives.");

    main->addKnob (ids::tone, "Tone",
        "A tilt applied before the saturation, so it changes what gets distorted "
        "rather than just filtering the result.\n\n"
        "Left is darker (up to 5 dB of low shelf), right is brighter (5 dB of high "
        "shelf). Because it sits ahead of the nonlinearity, turning it up makes the "
        "top end distort more, not just sound louder.");

    main->addKnob (ids::mix, "Mix",
        "Blend between the dry signal and the processed one.\n\n"
        "The dry path is delayed to match the processed one exactly, so partial mix "
        "settings do not comb. Parallel saturation at 30-50% is a good way to add "
        "weight to a drum bus without losing the transients.");

    main->addKnob (ids::output, "Output",
        "Final level trim, applied after everything else.\n\n"
        "This is a plain gain -- it cannot clip the plugin, and it does not feed back "
        "into the saturation or the limiter.");

    main->addKnob (ids::ceiling, "Ceiling",
        "The level the output is held to. Nothing gets past it by more than the "
        "ripple a finite attack allows -- measured at under 1.5 dB at the fastest "
        "settings.\n\n"
        "Set it just under 0 dBFS for a mix bus, or well down to use the plugin as "
        "a compressor rather than a limiter.");

    main->addKnob (ids::knee, "Knee",
        "How far below the Ceiling the curve starts bending.\n\n"
        "At 0 it is a hard corner exactly at the Ceiling: brickwall limiting, "
        "nothing touched until it has to be. At 24 dB the signal starts easing down "
        "24 dB below the Ceiling -- gentle, glue-ish, always slightly working.\n\n"
        "This is the control that decides whether it behaves like a limiter or like "
        "a compressor.");

    main->addKnob (ids::speed, "Speed",
        "Attack time -- how fast the gain reduction arrives, as a 1/e time constant, "
        "so it reaches about 63% in the stated time.\n\n"
        "Fast (under 1 ms) flattens transients and is what you want to tame a peak. "
        "Slow (10 ms and up) lets the front of a kick or snare through before the "
        "reduction lands, which is usually what keeps drums punchy.");

    main->addKnob (ids::release, "Release",
        "How fast the gain reduction lets go, again as a 1/e time constant.\n\n"
        "Short releases breathe and pump, which can be the point on a drum bus. Long "
        "releases stay out of the way. On sustained sub bass, too short a release "
        "modulates the fundamental and you hear it as wobble.\n\n"
        "See Auto release for material that needs both.");

    main->addChoice (ids::oversampling, "Oversampling",
        "Auto targets roughly 192 kHz internally at every session rate, so the plugin "
        "sounds the same at 48 kHz as it does at 192.");

    main->addToggle (ids::autoTrim, "Auto trim",
        "Compensates the level for whatever Drive is doing, so the drive control is "
        "a tone control rather than a volume control.\n\n"
        "Measured: output holds within 0.33 dB across the whole 0 to +30 dB drive "
        "range. In multiband mode each band is compensated for its own drive trim. "
        "Loudness sells distortion; this is here so you are not sold to.");

    main->addToggle (ids::autoRelease, "Auto release",
        "Program-dependent release. A second, slower release runs alongside the one "
        "you set, and whichever is holding more reduction wins.\n\n"
        "Short peaks recover at your Release setting; sustained material recovers "
        "about six times slower. Stops a bass line pumping while still letting "
        "snares breathe. Costs nothing.");

    main->addToggle (ids::bypass, "Bypass",
        "True bypass, delayed to match the plugin's own latency and crossfaded over "
        "10 ms.\n\n"
        "Both of those matter for honest A/B: an undelayed bypass sounds tighter for "
        "reasons that have nothing to do with the plugin, and an abrupt switch clicks.");

    pages_[0] = std::move (main);

    // ---- BANDS -------------------------------------------------------------
    auto bands = std::make_unique<ControlPage> (state, 5);

    bands->addToggle (ids::multiband, "Multiband",
        "Splits the signal into three bands and saturates each one separately, then "
        "limits the sum.\n\n"
        "The reason to use it here: a clean sub under destroyed mids. Pull the low "
        "band's drive down and push the mid band's up and the bottom stays solid "
        "while everything above it falls apart.\n\n"
        "The crossover is Linkwitz-Riley 4th order, so the bands sum flat. That "
        "costs phase rotation -- multiband mode is not phase-transparent against "
        "bypass. Every crossover-based processor has this; none of them mention it.");

    bands->addKnob (ids::crossoverLow, "Low / Mid",
        "Where the low band ends. For dubstep, somewhere around 100-150 Hz keeps the "
        "sub fundamental and its first harmonic in the low band, which is what lets "
        "you leave it clean.");

    bands->addKnob (ids::crossoverHigh, "Mid / High",
        "Where the high band begins. Around 2-3 kHz puts the bite of a reese in the "
        "mid band and the air in the high one.");

    bands->addBreak();

    bands->addKnob (ids::bandLowDrive, "Low Drive",
        "Drive trim for the low band, relative to the main Drive control.\n\n"
        "Pull this well down to keep the sub clean while the rest of the spectrum "
        "gets hammered. Auto trim compensates each band's level separately, so this "
        "changes how dirty the band is, not how loud.");

    bands->addKnob (ids::bandMidDrive, "Mid Drive",
        "Drive trim for the mid band. This is where a reese lives, and where pushing "
        "hard pays off most.");

    bands->addKnob (ids::bandHighDrive, "High Drive",
        "Drive trim for the high band. Small amounts add air and bite; large amounts "
        "get harsh quickly, because there is less room above the high band before "
        "harmonics run out of spectrum.");

    bands->addBreak();

    bands->addChoice (ids::bandLowState, "Low Band",
        "On, Mute or Solo. Solo on any band silences the others; Mute always wins. "
        "Soloing is how you find the right crossover point by ear.");

    bands->addChoice (ids::bandMidState, "Mid Band",
        "On, Mute or Solo. Solo on any band silences the others; Mute always wins.");

    bands->addChoice (ids::bandHighState, "High Band",
        "On, Mute or Solo. Solo on any band silences the others; Mute always wins.");

    // Kept to what fits: drawFittedText drops whole lines rather than scrolling,
    // and a note that visibly runs out of room reads worse than a shorter one.
    bands->setNote (
        "A clean sub under destroyed mids: pull Low Drive down, push Mid Drive up. "
        "Linkwitz-Riley 4th order, so the bands sum flat -- at the cost of phase rotation, "
        "which means multiband is not phase-transparent against bypass.");

    pages_[1] = std::move (bands);

    // ---- MANGLE ------------------------------------------------------------
    auto mangle = std::make_unique<ControlPage> (state, 5);

    mangle->addKnob (ids::foldAmount, "Fold",
        "A wavefolder, sitting between Drive and the saturation stage.\n\n"
        "A clipper flattens a peak and stops; a folder turns it back on itself, so "
        "the harmonics keep changing as you push instead of converging on a square "
        "wave. That is what makes this worth having as well as Drive.\n\n"
        "At 0 it is exactly a straight wire -- not almost, exactly. Turn it up and "
        "the sound goes metallic, then bell-like, then completely synthetic.");

    mangle->addKnob (ids::rectify, "Rectify",
        "Blends toward full-wave rectification: every negative half cycle flipped up.\n\n"
        "That doubles the fundamental, so you get an octave-up ghost that tracks whatever "
        "note is playing with no pitch tracking involved. Partway up, the two halves of the "
        "waveform stop matching, which is a very direct route to even harmonics.\n\n"
        "It sits first in the chain, so Fold and the saturation work on the octave rather "
        "than on the original note. Stack it with Fold for textures that stop resembling "
        "the input entirely.");

    mangle->addChoice (ids::foldRange, "Range",
        "The multiplier on Fold. This is the destruction switch.\n\n"
        "x1 is a musical wavefolder. x10 is aggressive -- harmonics louder than the "
        "note that made them. x100 folds a full-scale signal about 32 times per half "
        "cycle and produces something with no obvious relationship to the input.\n\n"
        "x100 is a BASS tool. Measured: aliasing stays below -175 dB on 40-160 Hz "
        "fundamentals, but reaches -46 dB by 2.6 kHz, because a folder's harmonics "
        "extend to roughly the fold gain times the fundamental. On a sub or a reese "
        "it is clean; on a lead it will alias, and that may be what you want.");

    // One paragraph, because the grid is three rows deep here and drawFittedText
    // drops whole lines rather than scrolling. The Range x100 bass caveat lives
    // in that control's own tooltip.
    mangle->setNote (
        "Chain order: Rectify, then Fold, then the saturation, with Feedback wrapping all of "
        "it. Crush and Downsample come last at the host's rate, deliberately un-antialiased -- "
        "there the artefacts are the instrument, not a defect. Both are wet-only.");

    mangle->addBreak();

    mangle->addKnob (ids::feedback, "Feedback",
        "Routes the output back into the drive stage through a short delay.\n\n"
        "Turn it up and the plugin starts sustaining and screaming: energy keeps circulating "
        "after the note has stopped, and the whole nonlinear chain is inside the loop, so it "
        "does not sound like a delay -- it sounds like the plugin has come alive.\n\n"
        "It cannot run away. There is a soft clip inside the loop that bounds whatever comes "
        "back regardless of level, it is not defeatable, and the amount is capped below unity "
        "on top of that. Every combination of feedback, delay, drive and fold has been swept "
        "and stays bounded.");

    mangle->addKnob (ids::feedbackTime, "FB Time",
        "How long the loop takes. The circulating signal repeats at this period, so the "
        "resonance it settles on is 1/time -- shown in the readout.\n\n"
        "Short (under 2 ms) is a metallic, pitched ring. Long (20 ms and up) is a stuttering "
        "repeat. Sweeping it while feedback is high is the trick worth knowing: the pitch "
        "slides with it.\n\n"
        "Measured by autocorrelation: the output repeats at the set delay to within a sample, "
        "with a correlation of 0.99.");

    mangle->addBreak();

    mangle->addKnob (ids::crush, "Crush",
        "Bit-depth reduction, from 16 bits down to 1. Off at zero, exactly.\n\n"
        "This is the one stage in the plugin that is deliberately NOT antialiased and does "
        "not run oversampled. Everywhere else, aliasing is a defect to suppress; in a bit "
        "crusher it is the instrument. An antialiased quantiser just sounds like a slightly "
        "noisy version of the input -- the harsh ringing character you actually want comes "
        "from the folded-back images.\n\n"
        "Measured: takes a chain sitting at -236 dB of aliasing up to -26 dB. On purpose.");

    mangle->addKnob (ids::downsample, "Downsample",
        "Sample-rate reduction by holding each sample for longer. Off at 1x, exactly.\n\n"
        "At 8x on a 48 kHz session the signal behaves as though it were running at 6 kHz, "
        "with all the gritty ringing that implies. Fractional ratios work, so it sweeps "
        "smoothly rather than jumping between whole divisions.\n\n"
        "Like Crush, it runs at the host's rate with no antialiasing, for the same reason. "
        "Both are wet-only, so the Mix control blends against a clean dry path.");

    pages_[2] = std::move (mangle);

    // ---- EXPERT ------------------------------------------------------------
    auto expert = std::make_unique<ControlPage> (state, 5);

    expert->addToggle (ids::expertEnabled, "Expert",
        "Takes the analogue constants off Character and puts them under direct "
        "control.\n\n"
        "While this is off, Character drives bias, the head bump and the gap loss "
        "exactly as it always has, and everything on this page is ignored. Turning "
        "it on hands you the same knobs the model was using.");

    expert->addKnob (ids::expBias, "Bias",
        "Raw asymmetry of the saturation curve, replacing what Character was setting.\n\n"
        "0 is symmetric: odd harmonics only. Away from 0 in either direction brings "
        "in even harmonics -- the octave-flavoured warmth people mean by valve. Large "
        "values are lopsided in a way no real circuit would be, which is the point of "
        "exposing it.\n\n"
        "Asymmetry makes DC as well as even harmonics; the DC blocker below handles it.");

    expert->addKnob (ids::expHeadroom, "Headroom",
        "How much the signal is backed off before it hits the curve -- the constant "
        "that decides how soon saturation starts.\n\n"
        "The default of 4 is what makes Drive at 0 genuinely transparent. Pull it to "
        "1 and the same Drive setting distorts far harder.\n\n"
        "It also decides how much the antialiasing is worth: measured, ADAA buys "
        "2 dB at headroom 4 and 25 dB at headroom 1, because at low headroom the "
        "curve is generating far more high harmonics to fold back.");

    expert->addKnob (ids::expDcHz, "DC Block",
        "Corner of the high-pass that removes the DC an asymmetric curve produces.\n\n"
        "10 Hz by default: low enough that a 40 Hz sub passes within 0.35 dB. Raising "
        "it tightens the bottom end; go far and you will hear the sub thin out, which "
        "in this music is usually the wrong trade.");

    expert->addToggle (ids::expAdaa, "Antialiasing",
        "Antiderivative antialiasing on the fold and saturation stages.\n\n"
        "On is correct. Off is here so you can hear what it is doing, and because "
        "the aliasing is sometimes the sound you want -- it is what a cheap digital "
        "distortion actually sounds like.\n\n"
        "Measured at +30 dB drive with oversampling off: -69 dB of aliasing with it "
        "on, -59 dB with it off. With the folder running the difference is 30 dB "
        "or more.");

    expert->addBreak();

    expert->addKnob (ids::expHeadBumpHz, "Bump Freq",
        "Frequency of the tape head bump -- the low-frequency resonance a real tape "
        "machine has because of the geometry of the head and the tape path.\n\n"
        "Lower puts the lift under the sub; higher puts it in the punch region.");

    expert->addKnob (ids::expHeadBumpDb, "Bump Gain",
        "How much head bump. Positive is the tape behaviour; negative scoops instead, "
        "which is not analogue but is useful for making room.");

    expert->addKnob (ids::expGapLossHz, "Gap Freq",
        "Where the tape's high-frequency loss begins. On a real machine this is set "
        "by the record head's gap width and the tape speed -- faster tape, higher "
        "corner.");

    expert->addKnob (ids::expGapLossDb, "Gap Gain",
        "How much high-frequency loss. Negative is the tape behaviour. Positive turns "
        "it into a lift instead, which brings the top end into the saturation harder.");

    expert->addBreak();

    expert->addKnob (ids::expStereoLink, "Stereo Link",
        "How much the two channels share their gain reduction.\n\n"
        "100% is fully linked: both channels always get the same gain, so the centre "
        "image cannot move. 0% lets each channel act alone, which is wider and looser "
        "but will pull a hard-panned hit towards the other side.\n\n"
        "Linked is the right default. Unlinked is a creative choice, not a better one.");

    expert->addKnob (ids::expDetectorRms, "Detector",
        "Blends the level detector between peak and RMS.\n\n"
        "Peak catches transients and is what you want for limiting. RMS follows "
        "average loudness over about 10 ms and is what you want for glue -- it "
        "ignores a snare's first millisecond and responds to the body instead.");

    expert->setNote (
        "While Expert is off, Character drives all of these and nothing on this page has any "
        "effect. Turning it on hands you the constants the analogue model was using.");

    pages_[3] = std::move (expert);

    for (auto& page : pages_)
        addChildComponent (*page);
}

void EmberdriveEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        const bool selected = i == currentPage_;
        pages_[static_cast<std::size_t> (i)]->setVisible (selected);
        tabs_[static_cast<std::size_t> (i)].setColour (juce::TextButton::buttonColourId,
                                                       selected ? kEmber.withAlpha (0.28f) : kPanel);
        tabs_[static_cast<std::size_t> (i)].setColour (juce::TextButton::textColourOffId,
                                                       selected ? kText : kDimText);
        tabs_[static_cast<std::size_t> (i)].repaint();
    }

    resized();
}

void EmberdriveEditor::timerCallback()
{
    auto& meters = processor_.getMeterValues();

    inputMeter_.setValues (meters.inputVuDb.load (std::memory_order_relaxed),
                           meters.inputPeakDb.load (std::memory_order_relaxed));
    outputMeter_.setValues (meters.outputVuDb.load (std::memory_order_relaxed),
                            meters.outputPeakDb.load (std::memory_order_relaxed));
    reductionMeter_.setValues (meters.gainReductionDb.load (std::memory_order_relaxed), -100.0f);

    inputMeter_.repaint();
    outputMeter_.repaint();
    reductionMeter_.repaint();

    statusLabel_.setText (processor_.describeOversampling(), juce::dontSendNotification);
}

void EmberdriveEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (44);

    g.setColour (kPanel);
    g.fillRect (header);

    g.setColour (kEmber);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("EMBERDRIVE", header.reduced (16, 0), juce::Justification::centredLeft);

    g.setColour (kDimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("TEZLA TECH  -  saturation + limiter", header.reduced (16, 0),
                juce::Justification::centredRight);
}

void EmberdriveEditor::resized()
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
    layoutMeter (reductionMeter_, reductionMeterLabel_, meterArea.removeFromLeft (meterWidth));
    layoutMeter (outputMeter_,    outputMeterLabel_,    meterArea);

    for (auto& page : pages_)
        page->setBounds (bounds.reduced (8, 4));
}

} // namespace tezla::emberdrive
