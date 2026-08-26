#include <tezla/ui/SpectrumDisplay.hpp>

namespace tezla::ui
{

SpectrumDisplay::SpectrumDisplay (Palette palette) : palette_ (palette)
{
    setInterceptsMouseClicks (false, false);
}

void SpectrumDisplay::prepare (double sampleRate, int fftOrder, int numBins)
{
    inputAnalyser_.prepare (sampleRate, fftOrder, numBins, lowHz_, highHz_);
    outputAnalyser_.prepare (sampleRate, fftOrder, numBins, lowHz_, highHz_);

    // The top of the display cannot exceed Nyquist, and at 44.1 kHz a 20 kHz
    // axis does. Take the range the analyser actually settled on rather than the
    // one that was asked for, or the grid labels stop matching the curves.
    lowHz_  = inputAnalyser_.getBinFrequency (0);
    highHz_ = inputAnalyser_.getBinFrequency (inputAnalyser_.getNumBins() - 1);

    // A little under two dB per frame at 30 fps is about 50 dB a second: fast
    // enough to follow a drum, slow enough to read.
    inputAnalyser_.setBallistics (1.8f, 0.4f);
    outputAnalyser_.setBallistics (1.8f, 0.4f);

    ready_ = true;
}

void SpectrumDisplay::update (const dsp::SpectrumCapture& input, const dsp::SpectrumCapture& output)
{
    if (! ready_)
        return;

    const bool a = inputAnalyser_.update (input);
    const bool b = outputAnalyser_.update (output);

    if (a || b)
        repaint();
}

void SpectrumDisplay::setDimmed (bool shouldDim)
{
    if (dimmed_ == shouldDim)
        return;

    dimmed_ = shouldDim;
    repaint();
}

void SpectrumDisplay::setFocusFrequency (double hz, bool aboveMode)
{
    if (std::abs (hz - focusHz_) < 0.5 && aboveMode == aboveMode_)
        return;

    focusHz_ = hz;
    aboveMode_ = aboveMode;
    repaint();
}

float SpectrumDisplay::xFor (juce::Rectangle<float> area, double hz) const
{
    const double clamped = juce::jlimit (lowHz_, highHz_, hz);
    const double position = std::log (clamped / lowHz_) / std::log (highHz_ / lowHz_);

    return area.getX() + area.getWidth() * static_cast<float> (position);
}

float SpectrumDisplay::yFor (juce::Rectangle<float> area, float db) const
{
    const float clamped = juce::jlimit (kBottomDb, kTopDb, db);
    const float position = (clamped - kBottomDb) / (kTopDb - kBottomDb);

    return area.getBottom() - area.getHeight() * position;
}

void SpectrumDisplay::drawGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour (palette_.dimText.withAlpha (0.16f));
    g.setFont (juce::FontOptions (9.0f));

    // Decade lines with the useful thirds between them, which is how anyone who
    // reads spectra expects the axis to be marked.
    for (const double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0,
                             2000.0, 5000.0, 10000.0, 20000.0 })
    {
        if (hz < lowHz_ || hz > highHz_)
            continue;

        const float x = xFor (area, hz);
        g.setColour (palette_.dimText.withAlpha (0.14f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());

        const auto label = hz >= 1000.0 ? juce::String (hz / 1000.0, hz < 10000.0 ? 1 : 0) + "k"
                                        : juce::String (juce::roundToInt (hz));

        g.setColour (palette_.dimText.withAlpha (0.42f));
        g.drawText (label, juce::Rectangle<float> (x - 16.0f, area.getBottom() - 11.0f, 32.0f, 11.0f),
                    juce::Justification::centred);
    }

    for (const float db : { 0.0f, -20.0f, -40.0f, -60.0f })
    {
        const float y = yFor (area, db);
        g.setColour (palette_.dimText.withAlpha (db == 0.0f ? 0.30f : 0.12f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }
}

void SpectrumDisplay::drawCurve (juce::Graphics& g, juce::Rectangle<float> area,
                                 const std::vector<float>& bins, juce::Colour colour,
                                 bool fill) const
{
    if (bins.size() < 2)
        return;

    juce::Path path;
    bool started = false;

    for (std::size_t i = 0; i < bins.size(); ++i)
    {
        const float x = xFor (area, inputAnalyser_.getBinFrequency (i));
        const float y = yFor (area, bins[i]);

        if (! started)
        {
            path.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    if (! started)
        return;

    if (fill)
    {
        juce::Path filled = path;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setColour (colour.withAlpha (0.14f));
        g.fillPath (filled);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (fill ? 1.6f : 1.1f));
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.brighter (0.05f));
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (palette_.panel.brighter (0.22f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    // Room at the bottom for the frequency labels, and a little inset elsewhere
    // so a curve pinned at 0 dB is not drawn on top of the border.
    auto plot = bounds.reduced (4.0f, 3.0f).withTrimmedBottom (11.0f);

    if (! ready_ || plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    drawGrid (g, plot);

    // The Focus marker, and which side of it is being worked on. Without it the
    // control and the picture are two separate things the user has to relate in
    // their head.
    const float focusX = xFor (plot, focusHz_);
    const auto shaded = aboveMode_
        ? juce::Rectangle<float> (focusX, plot.getY(), plot.getRight() - focusX, plot.getHeight())
        : juce::Rectangle<float> (plot.getX(), plot.getY(), focusX - plot.getX(), plot.getHeight());

    g.setColour (palette_.accent.withAlpha (0.055f));
    g.fillRect (shaded);

    g.setColour (palette_.accent.withAlpha (0.45f));
    g.drawVerticalLine (juce::roundToInt (focusX), plot.getY(), plot.getBottom());

    const auto inputColour  = dimmed_ ? palette_.dimText.withAlpha (0.35f)
                                      : palette_.dimText.withAlpha (0.75f);
    const auto outputColour = dimmed_ ? palette_.dimText.withAlpha (0.45f)
                                      : palette_.accentBright;

    // Input underneath and unfilled, output on top and filled: the eye reads the
    // filled one as the subject and the outline as the reference, which is the
    // right way round for "what did this do to my signal".
    drawCurve (g, plot, inputAnalyser_.getMagnitudesDb(), inputColour, false);
    drawCurve (g, plot, outputAnalyser_.getMagnitudesDb(), outputColour, true);

    // A legend in one row at the top right, so the two names cannot overlap each
    // other however the window is sized.
    auto legend = plot.removeFromTop (13.0f).removeFromRight (76.0f);
    g.setFont (juce::FontOptions (9.5f));

    auto outArea = legend.removeFromRight (34.0f);
    g.setColour (outputColour);
    g.drawText ("OUT", outArea, juce::Justification::centredRight);

    g.setColour (inputColour);
    g.drawText ("IN", legend.removeFromRight (24.0f), juce::Justification::centredRight);
}

} // namespace tezla::ui
