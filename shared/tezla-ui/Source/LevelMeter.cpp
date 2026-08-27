#include <tezla/ui/LevelMeter.hpp>

#include <cmath>

namespace tezla::ui
{

namespace
{
/// Where the bar changes colour. Below the first it is the plugin's accent,
/// between them amber, above the second red -- so the level reads at a glance
/// without reading the number.
constexpr float kCautionDb = -6.0f;
constexpr float kOverDb    = 0.0f;

/// Ticks worth labelling. Dense at the top where decisions are made, sparse at
/// the bottom where the exact number stops mattering.
constexpr float kTicks[] { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f };

/// Below this the readout says "-inf" rather than a large negative number that
/// looks like a reading.
constexpr float kSilenceDb = -99.0f;

juce::String formatDb (float db)
{
    if (db <= kSilenceDb)
        return juce::String::fromUTF8 ("-\xe2\x88\x9e");   // -infinity

    // One decimal up to 10 dB either side, none beyond: "-23.4" is precision
    // nobody uses and it costs the digit that matters at a glance.
    return std::abs (db) < 10.0f ? juce::String (db, 1) : juce::String (juce::roundToInt (db));
}
} // namespace

LevelMeter::LevelMeter (Palette palette)
    : palette_ (palette)
{
    // The whole component is the reset button, so say so.
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

float LevelMeter::positionFor (float db) noexcept
{
    const float clamped = juce::jlimit (kFloorDb, kTopDb, db);
    return (clamped - kFloorDb) / (kTopDb - kFloorDb);
}

void LevelMeter::setValues (float vuDb, float peakDb) noexcept
{
    vuDb_ = vuDb;
    peakDb_ = peakDb;

    // The hold only ever rises. That is the point of it: it is still true ten
    // minutes later, which is what makes it worth clearing by hand.
    if (peakDb > heldDb_)
        heldDb_ = peakDb;
}

void LevelMeter::setReferenceDb (float referenceDb) noexcept
{
    referenceDb_ = referenceDb;
}

void LevelMeter::setScaleVisible (bool shouldBeVisible)
{
    if (scaleVisible_ == shouldBeVisible)
        return;

    scaleVisible_ = shouldBeVisible;
    repaint();
}

void LevelMeter::resetHold() noexcept
{
    heldDb_ = -100.0f;
    repaint();
}

void LevelMeter::mouseEnter (const juce::MouseEvent&)
{
    hovering_ = true;
    repaint();
}

void LevelMeter::mouseExit (const juce::MouseEvent&)
{
    hovering_ = false;
    repaint();
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    resetHold();
}

juce::Rectangle<float> LevelMeter::barBounds() const
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop (static_cast<float> (kReadoutHeight));

    if (scaleVisible_)
        bounds.removeFromRight (static_cast<float> (kScaleWidth));

    // The bar is a fixed width, centred, rather than filling whatever it is
    // given. The component has to be wide enough for the readout -- "-12.3" at
    // 14 point needs about 46 pixels -- and stretching the bar to match would
    // make a level meter that looked like a progress bar.
    const float width = juce::jmin (static_cast<float> (kBarWidth), bounds.getWidth());

    return bounds.withWidth (width)
                 .withX (bounds.getX() + (bounds.getWidth() - width) * 0.5f)
                 .reduced (0.0f, 1.0f);
}

void LevelMeter::paintScale (juce::Graphics& g, juce::Rectangle<float> bar) const
{
    auto scale = getLocalBounds().toFloat()
                     .withTop (bar.getY())
                     .withBottom (bar.getBottom())
                     .withLeft (bar.getRight());

    g.setFont (juce::FontOptions (9.5f));

    for (const float db : kTicks)
    {
        const float y = bar.getBottom() - positionFor (db) * bar.getHeight();

        // 0 dBFS is the wall, so it is drawn like one.
        const bool wall = std::abs (db - kOverDb) < 0.01f;

        g.setColour (wall ? palette_.text.withAlpha (0.75f)
                          : palette_.dimText.withAlpha (0.65f));

        // Short, and well clear of the number. At four pixels with one pixel of
        // gap the tick read as the number's minus sign: "-12" became "--12"
        // and "+6" became "-+6".
        g.drawHorizontalLine (juce::roundToInt (y), scale.getX() + 1.0f, scale.getX() + 4.0f);

        g.drawText (db > 0.0f ? "+" + juce::String (juce::roundToInt (db))
                              : juce::String (juce::roundToInt (db)),
                    juce::Rectangle<float> { scale.getX() + 9.0f, y - 6.0f,
                                             scale.getWidth() - 9.0f, 12.0f },
                    juce::Justification::centredLeft);
    }
}

void LevelMeter::paintReadout (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // Hovering shows the live peak; otherwise the held one. Reading a level
    // while your hands are on the knobs is the common case, and waiting for a
    // hold to update is not reading.
    const float shown = hovering_ ? peakDb_ : heldDb_;
    const bool over = heldDb_ > referenceDb_ && heldDb_ > kSilenceDb;

    if (over && ! hovering_)
    {
        g.setColour (palette_.over.withAlpha (0.25f));
        g.fillRoundedRectangle (area.reduced (1.0f), 3.0f);
    }

    // Full contrast, not the dim grey a caption gets: this is the number the
    // meter exists to show.
    // Red for over, and deliberately not the bypass orange: those are different
    // statements and a user has to tell them apart at a glance.
    g.setColour (over && ! hovering_ ? palette_.over
               : hovering_           ? palette_.accentBright
                                     : palette_.text);

    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (formatDb (shown), area, juce::Justification::centred);
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto bar = barBounds();

    auto readout = getLocalBounds().toFloat().withHeight (static_cast<float> (kReadoutHeight));
    paintReadout (g, readout);

    g.setColour (palette_.panel.brighter (0.10f));
    g.fillRoundedRectangle (bar, 3.0f);

    // The bar, in three zones. Drawn as one fill clipped to the level rather
    // than three, so the boundaries land on the dB values rather than on
    // whatever the rounding did.
    const float level = positionFor (vuDb_) * bar.getHeight();

    if (level > 1.0f)
    {
        auto filled = bar.withTop (bar.getBottom() - level);

        juce::ColourGradient gradient (palette_.accent, bar.getCentreX(), bar.getBottom(),
                                       palette_.over, bar.getCentreX(), bar.getY(), false);

        gradient.addColour (static_cast<double> (positionFor (kCautionDb)), palette_.accent);
        gradient.addColour (static_cast<double> (positionFor (kOverDb)), juce::Colour { 0xffe0a33c });

        g.setGradientFill (gradient);
        g.fillRoundedRectangle (filled, 3.0f);
    }

    // 0 dBFS, over the bar so it stays visible when the level is past it.
    const float wall = bar.getBottom() - positionFor (kOverDb) * bar.getHeight();
    g.setColour (palette_.text.withAlpha (0.35f));
    g.drawHorizontalLine (juce::roundToInt (wall), bar.getX(), bar.getRight());

    // The reference, when it is somewhere other than 0 -- an output meter is
    // given its plugin's Ceiling, and that is the line that matters there.
    if (std::abs (referenceDb_ - kOverDb) > 0.05f)
    {
        const float y = bar.getBottom() - positionFor (referenceDb_) * bar.getHeight();
        g.setColour (palette_.accentBright.withAlpha (0.65f));
        g.drawHorizontalLine (juce::roundToInt (y), bar.getX(), bar.getRight());
    }

    // The instantaneous peak, as a line rather than a fill.
    if (peakDb_ > kFloorDb)
    {
        const float y = bar.getBottom() - positionFor (peakDb_) * bar.getHeight();
        g.setColour (peakDb_ > referenceDb_ ? palette_.over : palette_.accentBright);
        g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
    }

    // And the hold, as a brighter one that stays put.
    if (heldDb_ > kFloorDb)
    {
        const float y = bar.getBottom() - positionFor (heldDb_) * bar.getHeight();
        g.setColour (heldDb_ > referenceDb_ ? palette_.over : palette_.text.withAlpha (0.8f));
        g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 1.5f);
    }

    if (scaleVisible_)
        paintScale (g, bar);
}

} // namespace tezla::ui
