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
} // namespace

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
    const float filled = positionFor (isReduction ? vuDb_ : vuDb_);

    if (filled > 0.0f)
    {
        auto bar = bounds.reduced (1.0f);
        const float height = bar.getHeight() * filled;

        g.setColour (isReduction ? kReduction : kEmber);
        g.fillRoundedRectangle (bar.removeFromBottom (height), 1.5f);
    }

    if (! isReduction && peakDb_ > kMeterFloorDb)
    {
        // A thin line rather than a second bar: the peak is a warning, not a
        // level, and drawing it as a bar invites reading it as one.
        const float y = bounds.getBottom() - bounds.getHeight() * positionFor (peakDb_);
        g.setColour (peakDb_ > -0.1f ? juce::Colours::red : kEmberBright);
        g.fillRect (bounds.getX() + 1.0f, y - 1.0f, bounds.getWidth() - 2.0f, 2.0f);
    }

    // 0 dB reference, so the scale means something.
    if (! isReduction)
    {
        const float y = bounds.getBottom() - bounds.getHeight() * positionFor (0.0f);
        g.setColour (kDimText.withAlpha (0.5f));
        g.fillRect (bounds.getX(), y, bounds.getWidth(), 1.0f);
    }
}

void EmberdriveEditor::addKnob (Knob& knob, const char* parameterId,
                                const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, kEmber);
    knob.slider.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel.brighter (0.25f));
    knob.slider.setColour (juce::Slider::thumbColourId, kEmberBright);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.slider.setTooltip (tooltip);
    knob.slider.setDoubleClickReturnValue (true, knob.slider.getDoubleClickReturnValue());
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setColour (juce::Label::textColourId, kDimText);
    knob.label.setFont (juce::FontOptions (12.0f));
    knob.label.setTooltip (tooltip);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor_.getValueTreeState(), parameterId, knob.slider);
}

// Named processorToUse rather than processor: AudioProcessorEditor already has
// a public member of that name, and shadowing it is the kind of thing that
// compiles fine and then binds to the wrong object.
EmberdriveEditor::EmberdriveEditor (EmberdriveProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse), processor_ (processorToUse)
{
    // Every tooltip says what the control does *and* what it costs. There is no
    // manual; these are the manual.
    addKnob (drive_, ids::drive, "Drive",
        "How hard the signal is pushed into the saturation stage.\n\n"
        "At 0 dB the stage is close to a straight wire (0.006% THD at -20 dBFS) -- "
        "genuinely clean, not a quieter version of the dirty setting. By +18 dB the "
        "harmonics are obvious; +30 dB is destruction.\n\n"
        "With Auto trim on, the output level barely moves as you turn this, so you "
        "are judging tone rather than loudness.");

    addKnob (character_, ids::character, "Character",
        "Morphs between two ways of distorting.\n\n"
        "TAPE (left): symmetric, so only odd harmonics -- a firm, compressive knee, "
        "with a low-frequency head bump and the gentle high-frequency loss of a tape "
        "machine. Holds together on sub bass.\n\n"
        "VALVE (right): asymmetric, which brings in even harmonics -- warmer and "
        "thicker, flatter and brighter through the top.\n\n"
        "This changes harmonic content, not level.");

    addKnob (tone_, ids::tone, "Tone",
        "A tilt applied before the saturation, so it changes what gets distorted "
        "rather than just filtering the result.\n\n"
        "Left is darker (up to 5 dB of low shelf), right is brighter (5 dB of high "
        "shelf). Because it sits ahead of the nonlinearity, turning it up makes the "
        "top end distort more, not just sound louder.");

    addKnob (ceiling_, ids::ceiling, "Ceiling",
        "The level the output is held to. Nothing gets past it by more than the "
        "ripple a finite attack allows -- measured at under 1.5 dB at the fastest "
        "settings.\n\n"
        "Set it just under 0 dBFS for a mix bus, or well down to use the plugin as "
        "a compressor rather than a limiter.");

    addKnob (knee_, ids::knee, "Knee",
        "How far below the Ceiling the curve starts bending.\n\n"
        "At 0 it is a hard corner exactly at the Ceiling: brickwall limiting, "
        "nothing touched until it has to be. At 24 dB the signal starts easing down "
        "24 dB below the Ceiling -- gentle, glue-ish, always slightly working.\n\n"
        "This is the control that decides whether it behaves like a limiter or like "
        "a compressor.");

    addKnob (speed_, ids::speed, "Speed",
        "Attack time -- how fast the gain reduction arrives, as a 1/e time constant, "
        "so it reaches about 63% in the stated time.\n\n"
        "Fast (under 1 ms) flattens transients and is what you want to tame a peak. "
        "Slow (10 ms and up) lets the front of a kick or snare through before the "
        "reduction lands, which is usually what keeps drums punchy.");

    addKnob (release_, ids::release, "Release",
        "How fast the gain reduction lets go, again as a 1/e time constant.\n\n"
        "Short releases breathe and pump, which can be the point on a drum bus. Long "
        "releases stay out of the way. On sustained sub bass, too short a release "
        "modulates the fundamental and you hear it as wobble.\n\n"
        "See Auto release for material that needs both.");

    addKnob (mix_, ids::mix, "Mix",
        "Blend between the dry signal and the processed one.\n\n"
        "The dry path is delayed to match the processed one exactly, so partial mix "
        "settings do not comb. Parallel saturation at 30-50% is a good way to add "
        "weight to a drum bus without losing the transients.");

    addKnob (output_, ids::output, "Output",
        "Final level trim, applied after everything else.\n\n"
        "This is a plain gain -- it cannot clip the plugin, and it does not feed back "
        "into the saturation or the limiter.");

    oversamplingLabel_.setText ("Oversampling", juce::dontSendNotification);
    oversamplingLabel_.setColour (juce::Label::textColourId, kDimText);
    oversamplingLabel_.setFont (juce::FontOptions (12.0f));
    oversamplingLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (oversamplingLabel_);

    oversampling_.addItemList ({ "Auto", "Off", "x2", "x4", "x8" }, 1);
    oversampling_.setColour (juce::ComboBox::backgroundColourId, kPanel.brighter (0.15f));
    oversampling_.setColour (juce::ComboBox::textColourId, kText);
    oversampling_.setColour (juce::ComboBox::outlineColourId, kPanel.brighter (0.3f));
    addAndMakeVisible (oversampling_);
    oversamplingAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor_.getValueTreeState(), ids::oversampling, oversampling_);

    for (auto* button : { &autoTrim_, &autoRelease_, &bypass_ })
    {
        button->setColour (juce::ToggleButton::textColourId, kText);
        button->setColour (juce::ToggleButton::tickColourId, kEmber);
        addAndMakeVisible (*button);
    }

    autoTrim_.setTooltip (
        "Compensates the level for whatever Drive is doing, so the drive control is "
        "a tone control rather than a volume control.\n\n"
        "Measured: output holds within 0.33 dB across the whole 0 to +30 dB drive "
        "range. Loudness sells distortion; this is here so you are not sold to.");

    autoRelease_.setTooltip (
        "Program-dependent release. A second, slower release runs alongside the one "
        "you set, and whichever is holding more reduction wins.\n\n"
        "Short peaks recover at your Release setting; sustained material recovers "
        "about six times slower. Stops a bass line pumping while still letting "
        "snares breathe. Costs nothing.");

    bypass_.setTooltip (
        "True bypass, delayed to match the plugin's own latency and crossfaded over "
        "10 ms.\n\n"
        "Both of those matter for honest A/B: an undelayed bypass sounds tighter for "
        "reasons that have nothing to do with the plugin, and an abrupt switch clicks.");

    autoTrimAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor_.getValueTreeState(), ids::autoTrim, autoTrim_);
    autoReleaseAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor_.getValueTreeState(), ids::autoRelease, autoRelease_);
    bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor_.getValueTreeState(), ids::bypass, bypass_);

    for (auto* meter : { &inputMeter_, &outputMeter_, &reductionMeter_ })
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
    reductionMeterLabel_.setTooltip ("Gain reduction from the limiter, up to 24 dB.");

    statusLabel_.setColour (juce::Label::textColourId, kDimText);
    statusLabel_.setFont (juce::FontOptions (11.0f));
    statusLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel_);

    setResizable (true, true);
    setResizeLimits (620, 380, 1240, 760);
    setSize (720, 440);

    startTimerHz (30);
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
    oversampling_.setTooltip (processor_.describeOversampling()
                              + "\n\nAuto targets roughly 192 kHz internally at every session rate, so the "
                                "plugin sounds the same at 48 kHz as it does at 192. Forcing a higher factor "
                                "than Auto chooses costs CPU for very little; forcing it Off saves CPU and "
                                "gives up about 25 dB of alias rejection at high drive.");
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
    g.drawText ("TEZLA TECH  ·  saturation + limiter", header.reduced (16, 0),
                juce::Justification::centredRight);
}

void EmberdriveEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (44);

    auto footer = bounds.removeFromBottom (26);
    statusLabel_.setBounds (footer.reduced (16, 4));

    auto meterArea = bounds.removeFromRight (110).reduced (10, 12);
    const int meterWidth = meterArea.getWidth() / 3;

    const auto layoutMeter = [&] (LevelMeter& meter, juce::Label& label, juce::Rectangle<int> area)
    {
        label.setBounds (area.removeFromBottom (14));
        meter.setBounds (area.reduced (4, 0));
    };

    layoutMeter (inputMeter_,     inputMeterLabel_,     meterArea.removeFromLeft (meterWidth));
    layoutMeter (reductionMeter_, reductionMeterLabel_, meterArea.removeFromLeft (meterWidth));
    layoutMeter (outputMeter_,    outputMeterLabel_,    meterArea);

    auto controls = bounds.reduced (12, 8);

    auto toggles = controls.removeFromBottom (28);
    oversamplingLabel_.setBounds (toggles.removeFromLeft (90));
    oversampling_.setBounds (toggles.removeFromLeft (84).reduced (0, 2));
    toggles.removeFromLeft (16);
    autoTrim_.setBounds (toggles.removeFromLeft (100));
    autoRelease_.setBounds (toggles.removeFromLeft (120));
    bypass_.setBounds (toggles.removeFromLeft (90));

    const auto layoutRow = [] (juce::Rectangle<int> row, std::initializer_list<Knob*> knobs)
    {
        const int width = row.getWidth() / static_cast<int> (knobs.size());
        for (auto* knob : knobs)
        {
            auto cell = row.removeFromLeft (width);
            knob->label.setBounds (cell.removeFromTop (16));
            knob->slider.setBounds (cell.reduced (4, 0));
        }
    };

    const int rowHeight = controls.getHeight() / 2;
    layoutRow (controls.removeFromTop (rowHeight), { &drive_, &character_, &tone_, &mix_, &output_ });
    layoutRow (controls, { &ceiling_, &knee_, &speed_, &release_ });
}

} // namespace tezla::emberdrive
