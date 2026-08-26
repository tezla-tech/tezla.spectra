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

void SpectrumDisplay::setHarmonicLimits (bool floorOn, double floorHz,
                                         bool ceilingOn, double ceilingHz)
{
    if (floorOn == floorOn_ && ceilingOn == ceilingOn_
        && std::abs (floorHz - floorHz_) < 0.5
        && std::abs (ceilingHz - ceilingHz_) < 0.5)
        return;

    floorOn_   = floorOn;
    floorHz_   = floorHz;
    ceilingOn_ = ceilingOn;
    ceilingHz_ = ceilingHz;
    repaint();
}

void SpectrumDisplay::drawMarker (juce::Graphics& g, juce::Rectangle<float> area, double hz,
                                  const juce::String& label, juce::Colour colour, bool dashed) const
{
    const float x = xFor (area, hz);

    if (dashed)
    {
        // Dashed, so the harmonic limits cannot be mistaken for Focus. Focus
        // decides what gets excited; these two decide where the result is
        // allowed to land, and they are different kinds of boundary.
        const float dashes[] { 3.0f, 3.0f };
        g.setColour (colour);
        g.drawDashedLine ({ x, area.getY() + 15.0f, x, area.getBottom() }, dashes, 2, 1.0f);
    }
    else
    {
        g.setColour (colour);
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    // The label goes on whichever side has room, so a marker near an edge does
    // not print half off the graph.
    // Below the legend row, and flipped to the inside near an edge. Drawn level
    // with the legend, CEIL at 16 kHz printed straight through the word OUT.
    const bool toTheLeft = x > area.getRight() - 46.0f;
    const juce::Rectangle<float> labelArea { toTheLeft ? x - 44.0f : x + 3.0f,
                                             area.getY() + 15.0f, 42.0f, 11.0f };

    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (label, labelArea,
                toTheLeft ? juce::Justification::centredRight : juce::Justification::centredLeft);
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

void SpectrumDisplay::drawGrid (juce::Graphics& g, juce::Rectangle<float> area,
                                juce::Rectangle<float> axis) const
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

        g.setColour (palette_.dimText.withAlpha (0.5f));
        g.drawText (label, juce::Rectangle<float> (x - 16.0f, axis.getY(), 32.0f, axis.getHeight()),
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

void SpectrumDisplay::drawAddedRegion (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& input = inputAnalyser_.getMagnitudesDb();
    const auto& output = outputAnalyser_.getMagnitudesDb();

    if (input.size() != output.size() || input.size() < 2)
        return;

    // Built as a run of closed quadrilaterals rather than one path, because the
    // output only stands above the input in places and the gaps between those
    // places are not part of the answer.
    juce::Path added;
    bool inRun = false;
    std::size_t runStart = 0;

    const auto closeRun = [&] (std::size_t runEnd)
    {
        if (runEnd <= runStart)
            return;

        added.startNewSubPath (xFor (area, outputAnalyser_.getBinFrequency (runStart)),
                               yFor (area, output[runStart]));

        for (std::size_t i = runStart + 1; i <= runEnd; ++i)
            added.lineTo (xFor (area, outputAnalyser_.getBinFrequency (i)), yFor (area, output[i]));

        for (std::size_t i = runEnd + 1; i-- > runStart;)
            added.lineTo (xFor (area, inputAnalyser_.getBinFrequency (i)), yFor (area, input[i]));

        added.closeSubPath();
    };

    for (std::size_t i = 0; i < input.size(); ++i)
    {
        // A tenth of a dB of headroom, so bins where nothing is happening do not
        // shimmer with the last digit of the analysis.
        const bool above = output[i] > input[i] + 0.1f;

        if (above && ! inRun)
        {
            inRun = true;
            runStart = i;
        }
        else if (! above && inRun)
        {
            closeRun (i - 1);
            inRun = false;
        }
    }

    if (inRun)
        closeRun (input.size() - 1);

    g.setColour (palette_.secondary.withAlpha (0.28f));
    g.fillPath (added);
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.background.brighter (0.05f));
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (palette_.panel.brighter (0.22f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    // The frequency labels get a strip of their own below the plot. Drawn
    // inside it they sat on top of the trace, which reaches the bottom of the
    // graph wherever there is no signal -- which is most of it on sparse
    // material.
    auto inner = bounds.reduced (4.0f, 3.0f);
    auto axis = inner.removeFromBottom (12.0f);
    auto plot = inner;

    if (! ready_ || plot.getWidth() < 8.0f || plot.getHeight() < 8.0f)
        return;

    drawGrid (g, plot, axis);

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

    // What is shaded is the gap between the two curves, not the area under the
    // output. The gap *is* the plugin: everything Halo does shows up as output
    // standing above input, so filling it draws the effect itself rather than
    // leaving it to be inferred from the distance between two lines.
    if (! dimmed_)
        drawAddedRegion (g, plot);

    // Output first, input over the top. The other order hides the input
    // entirely: the two agree everywhere the plugin is not doing something, so
    // whichever is drawn second is the only one visible -- and the input is the
    // reference the eye needs to see the output depart from.
    drawCurve (g, plot, outputAnalyser_.getMagnitudesDb(), outputColour, false);
    drawCurve (g, plot, inputAnalyser_.getMagnitudesDb(), inputColour, false);

    // Harmonic limits over the top of the curves, so they stay readable.
    const auto limitColour = palette_.secondary.withAlpha (dimmed_ ? 0.25f : 0.6f);

    if (floorOn_)
        drawMarker (g, plot, floorHz_, "FLOOR", limitColour, true);

    if (ceilingOn_)
        drawMarker (g, plot, ceilingHz_, "CEIL", limitColour, true);

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
