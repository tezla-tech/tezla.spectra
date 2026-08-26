#include <tezla/ui/HeaderBar.hpp>

namespace tezla::ui
{

namespace
{
constexpr int kButtonHeight = 24;
constexpr int kBypassWidth  = 92;
constexpr int kSwapWidth    = 56;
constexpr int kCopyWidth    = 52;
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
    addAndMakeVisible (bypassButton_);

    bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, bypassParameterId, bypassButton_);

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

    for (auto* button : { &swapButton_, &copyButton_ })
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
                textArea.withTrimmedRight (kBypassWidth + kSwapWidth + kCopyWidth + 4 * kGap),
                juce::Justification::centredLeft);
}

void HeaderBar::resized()
{
    auto bounds = getLocalBounds().reduced (12, 0);
    const int y = (bounds.getHeight() - kButtonHeight) / 2;

    auto strip = bounds.withHeight (kButtonHeight).withY (y);

    bypassButton_.setBounds (strip.removeFromRight (kBypassWidth));
    strip.removeFromRight (kGap * 2);
    copyButton_.setBounds (strip.removeFromRight (kCopyWidth));
    strip.removeFromRight (kGap);
    swapButton_.setBounds (strip.removeFromRight (kSwapWidth));
}

} // namespace tezla::ui
