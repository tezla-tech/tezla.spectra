#include <tezla/ui/HeaderBar.hpp>

namespace tezla::ui
{

namespace
{
constexpr int kButtonHeight = 24;
constexpr int kBypassWidth  = 92;
constexpr int kSwapWidth    = 56;
constexpr int kCopyWidth    = 52;

/// The two suite-wide controls in the header. The output is a small rotary with
/// its value beside it rather than under it -- the bar is 46 pixels tall and a
/// stacked label and value would leave nothing for the knob.
constexpr int kOutputWidth  = 104;
constexpr int kOutputKnobHeight = 36;
constexpr int kOversamplingWidth = 82;
constexpr int kMixWidth     = 92;
constexpr int kGap          = 6;
} // namespace

GlowButton::GlowButton (const juce::String& name, Palette palette)
    : juce::Button (name), palette_ (palette), glow_ (palette.bypassGlow)
{
    setClickingTogglesState (true);
}

void GlowButton::setGlowColour (juce::Colour colour)
{
    glow_ = colour;
    repaint();
}

void GlowButton::paintButton (juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = getToggleState();
    constexpr float corner = 4.0f;

    if (lit)
    {
        // A halo outside the button, drawn as a few expanding rounded rectangles
        // of falling alpha. Cheap, and it is what makes the state readable
        // without looking directly at it -- a filled rectangle alone reads as
        // "a button", where this reads as "something is on".
        for (int ring = 4; ring >= 1; --ring)
        {
            const auto spread = static_cast<float> (ring) * 1.6f;
            g.setColour (glow_.withAlpha (0.10f - 0.018f * static_cast<float> (ring)));
            g.fillRoundedRectangle (bounds.expanded (spread), corner + spread);
        }

        g.setGradientFill (juce::ColourGradient (glow_.brighter (0.35f), bounds.getCentreX(), bounds.getY(),
                                                 glow_, bounds.getCentreX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (glow_.brighter (0.7f));
        g.drawRoundedRectangle (bounds, corner, 1.2f);
    }
    else
    {
        g.setColour (palette_.panel.brighter (shouldDrawHighlighted ? 0.28f : 0.16f));
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (palette_.panel.brighter (0.42f));
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }

    if (shouldDrawDown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds, corner);
    }

    // Near-black on the lit button rather than white: it is the higher contrast
    // of the two against a saturated orange, and it reads as an indicator lamp.
    g.setColour (lit ? juce::Colour (0xff20120a) : palette_.dimText);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

HeaderBar::HeaderBar (juce::AudioProcessorValueTreeState& state,
                      const juce::String& title,
                      const juce::String& subtitle,
                      const char* bypassParameterId,
                      Palette palette)
    : palette_ (palette),
      title_ (title),
      subtitle_ (subtitle),
      bypassButton_ ("BYPASS", palette)
{
    bypassButton_.setButtonText ("BYPASS");
    bypassButton_.setTooltip (
        "Takes the plugin out of circuit, from anywhere -- it is in the header rather than on a "
        "page so it is reachable whichever tab you are on.\n\n"
        "Latency-matched: the bypassed signal is delayed by exactly the latency the host has been "
        "told about, so switching does not shift the timing. Without that the bypassed side "
        "arrives early and sounds tighter for reasons that have nothing to do with the plugin, "
        "and every comparison you make is wrong.\n\n"
        "Crossfaded over 10 ms, so it does not click.");

    // **An instrument has nothing to bypass.** Passing no parameter id leaves
    // the button out entirely rather than showing a dead one: bypassing a synth
    // means silence, which is what muting the track already does, and a header
    // control that does nothing is worse than a missing one. Everything else in
    // the bar -- the name, A/B, Copy -- applies to an instrument unchanged.
    hasBypass_ = bypassParameterId != nullptr && *bypassParameterId != '\0';

    if (hasBypass_)
    {
        addAndMakeVisible (bypassButton_);

        bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            state, bypassParameterId, bypassButton_);
    }

    swapButton_.setTooltip (
        "Two complete settings, A and B, and this swaps between them.\n\n"
        "Everything moves: every knob, switch and menu. Turn something while B is live and it is "
        "B that changes, so you can build two versions of a sound and flip between them without "
        "losing either. Both are saved with the project.");
    swapButton_.onClick = [this] { if (onSwapRequested) onSwapRequested(); };
    addAndMakeVisible (swapButton_);

    copyButton_.setTooltip (
        "Copies the settings you are on now into the other slot, so you have somewhere to start "
        "from rather than an empty B.\n\n"
        "The usual way round: dial in a sound, copy it across, swap, then change one thing.");
    copyButton_.onClick = [this] { if (onCopyRequested) onCopyRequested(); };
    addAndMakeVisible (copyButton_);

    // **The tooltips in this workshop are whole paragraphs** -- they are how the
    // plugins document themselves, and that is exactly why they need a switch.
    // What teaches you the instrument on the first day is in the way on the
    // fiftieth, and hovering over a knob mid-take to have a hundred words
    // appear over the control next to it is its own kind of unusable.
    tipsButton_.setClickingTogglesState (true);
    tipsButton_.setToggleState (true, juce::dontSendNotification);
    tipsButton_.setTooltip ("Turns the hover tooltips off. They are long on purpose -- every "
                            "control here explains what it does and what it costs -- which is "
                            "worth a lot until you know the instrument and is in the way after.");
    tipsButton_.onClick = [this]
    {
        if (onTooltipsToggled)
            onTooltipsToggled (tipsButton_.getToggleState());
    };
    addAndMakeVisible (tipsButton_);

    for (auto* button : { &swapButton_, &copyButton_, &tipsButton_ })
    {
        button->setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.16f));
        button->setColour (juce::TextButton::textColourOffId, palette_.dimText);
        button->setColour (juce::TextButton::textColourOnId, palette_.accentBright);
    }

    setActiveSlot (false);
}

void HeaderBar::setActiveSlot (bool isSlotB)
{
    slotB_ = isSlotB;

    // The live slot is named on the button, so which one you are editing is
    // never a guess. "A / B" alone would leave you working out which half is
    // current from the sound, which is exactly what A/B is meant to stop.
    swapButton_.setButtonText (slotB_ ? "B" : "A");
    swapButton_.setColour (juce::TextButton::textColourOffId,
                           slotB_ ? palette_.accentBright : palette_.dimText);
    repaint();
}

void HeaderBar::attachSuiteControls (juce::AudioProcessorValueTreeState& state,
                                     const char* mixParameterId,
                                     const char* outputParameterId,
                                     const char* oversamplingParameterId)
{
    if (mixParameterId != nullptr && *mixParameterId != 0)
    {
        hasMix_ = true;

        mixSlider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        mixSlider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
        mixSlider_.setColour (juce::Slider::textBoxTextColourId, palette_.text);
        mixSlider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        mixSlider_.setTooltip (
            "Dry against processed. Up here rather than on a page because it is the control you "
            "reach for while listening to something else -- parallel saturation is set by ear "
            "against the mix, not by looking at the tab it lives on.");

        if (auto* parameter = state.getParameter (mixParameterId))
            mixSlider_.setDoubleClickReturnValue (
                true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

        addAndMakeVisible (mixSlider_);

        mixAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, mixParameterId, mixSlider_);

        mixLabel_.setJustificationType (juce::Justification::centredRight);
        mixLabel_.setColour (juce::Label::textColourId, palette_.dimText);
        mixLabel_.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        addAndMakeVisible (mixLabel_);
    }

    if (outputParameterId != nullptr && *outputParameterId != 0)
    {
        hasOutput_ = true;

        outputSlider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        outputSlider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 18);
        outputSlider_.setColour (juce::Slider::textBoxTextColourId, palette_.text);
        outputSlider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        outputSlider_.setTooltip (
            "The last gain in the plugin, after everything. **It defaults to 0 dB across the whole "
            "suite** -- unity, so nothing is lost on the way into the mixer for the sake of a "
            "safety margin nobody asked for. Pull it down if the meter goes red, not before.");

        if (auto* parameter = state.getParameter (outputParameterId))
            outputSlider_.setDoubleClickReturnValue (
                true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

        addAndMakeVisible (outputSlider_);

        outputAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, outputParameterId, outputSlider_);

        outputLabel_.setJustificationType (juce::Justification::centredRight);
        outputLabel_.setColour (juce::Label::textColourId, palette_.dimText);
        outputLabel_.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        addAndMakeVisible (outputLabel_);
    }

    if (oversamplingParameterId != nullptr && *oversamplingParameterId != 0)
    {
        hasOversampling_ = true;

        if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (
                                  state.getParameter (oversamplingParameterId)))
            oversamplingBox_.addItemList (parameter->choices, 1);

        oversamplingBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.12f));
        oversamplingBox_.setColour (juce::ComboBox::textColourId, palette_.text);
        oversamplingBox_.setTooltip (
            "How much headroom the nonlinear stages get. Auto reads the session's own rate and "
            "picks a factor that lands near 192 kHz internally, so the harmonics come out the same "
            "at 48 kHz as at 192.");
        addAndMakeVisible (oversamplingBox_);

        oversamplingAttachment_
          = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                state, oversamplingParameterId, oversamplingBox_);

        oversamplingLabel_.setJustificationType (juce::Justification::centredRight);
        oversamplingLabel_.setColour (juce::Label::textColourId, palette_.dimText);
        oversamplingLabel_.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        addAndMakeVisible (oversamplingLabel_);
    }

    resized();
}

void HeaderBar::setTooltipsEnabled (bool enabled)
{
    // `dontSendNotification`, because this is how a saved state is restored and
    // a restore that fired the callback would be indistinguishable from a click.
    tipsButton_.setToggleState (enabled, juce::dontSendNotification);
    tipsButton_.setColour (juce::TextButton::textColourOnId, palette_.text);
    tipsButton_.setColour (juce::TextButton::textColourOffId, palette_.dimText.withAlpha (0.55f));
    tipsButton_.repaint();
}

void HeaderBar::setOtherSlotFilled (bool filled)
{
    otherFilled_ = filled;
    // The label stays "COPY" whether or not the other slot holds anything. An
    // arrow was tried and does not fit the button at this size -- it wrapped on
    // top of the word. Whether B is empty is shown by the slot letter, not here.
    copyButton_.setButtonText ("COPY");
}

void HeaderBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (palette_.panel);
    g.fillRect (bounds);

    // A hairline of the plugin's accent along the bottom, which is most of what
    // distinguishes one of these from another at a glance.
    g.setColour (palette_.accent.withAlpha (0.55f));
    g.fillRect (bounds.removeFromBottom (1));

    auto textArea = bounds.reduced (16, 0);

    g.setColour (palette_.accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    const int titleWidth = juce::GlyphArrangement::getStringWidthInt (
        juce::Font (juce::FontOptions (20.0f, juce::Font::bold)), title_);
    g.drawText (title_, textArea.removeFromLeft (titleWidth + 12), juce::Justification::centredLeft);

    // The subtitle sits next to the name rather than at the far right, so the
    // right-hand end belongs entirely to the controls.
    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("TEZLA TECH  \xc2\xb7  ") + subtitle_,
                textArea.withTrimmedRight ((hasBypass_ ? kBypassWidth + 2 * kGap : 0)
                                             + kSwapWidth + 2 * kCopyWidth + 3 * kGap
                                             + (hasOutput_ ? kOutputWidth + 26 + 2 * kGap : 0)
                                             + (hasOversampling_ ? kOversamplingWidth + 26 + 2 * kGap : 0)
                                             + (hasMix_ ? kMixWidth + 26 + 2 * kGap : 0)),
                juce::Justification::centredLeft);
}

void HeaderBar::resized()
{
    auto bounds = getLocalBounds().reduced (12, 0);
    const int y = (bounds.getHeight() - kButtonHeight) / 2;

    auto strip = bounds.withHeight (kButtonHeight).withY (y);

    if (hasBypass_)
    {
        bypassButton_.setBounds (strip.removeFromRight (kBypassWidth));
        strip.removeFromRight (kGap * 2);
    }
    tipsButton_.setBounds (strip.removeFromRight (kCopyWidth));
    strip.removeFromRight (6);
    copyButton_.setBounds (strip.removeFromRight (kCopyWidth));
    strip.removeFromRight (kGap);
    swapButton_.setBounds (strip.removeFromRight (kSwapWidth));

    // The two suite-wide controls, to the left of the A/B pair.
    if (hasOversampling_)
    {
        strip.removeFromRight (kGap * 2);
        oversamplingBox_.setBounds (strip.removeFromRight (kOversamplingWidth));
        strip.removeFromRight (4);
        oversamplingLabel_.setBounds (strip.removeFromRight (26));
    }

    if (hasOutput_)
    {
        strip.removeFromRight (kGap * 2);

        auto area = strip.removeFromRight (kOutputWidth);

        // The knob keeps its own height rather than the button strip's, because
        // a rotary squashed to eighteen pixels is a smear.
        outputSlider_.setBounds (area.withHeight (kOutputKnobHeight)
                                     .withY (getHeight() / 2 - kOutputKnobHeight / 2));

        strip.removeFromRight (2);
        outputLabel_.setBounds (strip.removeFromRight (26));
    }

    if (hasMix_)
    {
        strip.removeFromRight (kGap * 2);

        auto area = strip.removeFromRight (kMixWidth);

        mixSlider_.setBounds (area.withHeight (kOutputKnobHeight)
                                  .withY (getHeight() / 2 - kOutputKnobHeight / 2));

        strip.removeFromRight (2);
        mixLabel_.setBounds (strip.removeFromRight (26));
    }
}

} // namespace tezla::ui
