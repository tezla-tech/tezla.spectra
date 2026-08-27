#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tezla::transpectus
{

namespace
{
// Transpectus reads rather than colours, so its accent is a cool instrument
// green against Emberdrive's ember, Halo's gold and Capstone's steel.
const ui::Palette kPalette {
    juce::Colour { 0xff141416 },   // background
    juce::Colour { 0xff1d1d20 },   // panel
    juce::Colour { 0xffd8d5cf },   // text
    juce::Colour { 0xff86837e },   // dim text
    juce::Colour { 0xff5bb98c },   // accent
    juce::Colour { 0xff8fe0b4 },   // accent bright
    juce::Colour { 0xff54c7c0 },   // secondary
    juce::Colour { 0xffff7a18 },   // bypass glow, the same in every plugin
    juce::Colour { 0xffe2483d }    // over
};

constexpr int kMinWidth  = 760;
constexpr int kMinHeight = 520;
constexpr int kMaxWidth  = 1520;
constexpr int kMaxHeight = 1040;

constexpr int kStatusHeight = 42;
constexpr int kControlHeight = 52;

/// Below this a loudness reading is silence rather than a number.
constexpr double kSilenceThreshold = -150.0;

/// Where PLR stops meaning "punchy" and starts meaning "squashed". Guidance,
/// sourced, and labelled as such -- not a pass/fail.
constexpr double kPlrSquashed = 5.0;
constexpr double kPlrHealthy  = 8.0;
} // namespace

// ---------------------------------------------------------------------------
// Readout
// ---------------------------------------------------------------------------

Readout::Readout (ui::Palette palette, juce::String caption, juce::String unit)
    : palette_ (palette), caption_ (std::move (caption)), unit_ (std::move (unit))
{
}

void Readout::setValue (juce::String text, juce::String note)
{
    if (value_ == text && note_ == note)
        return;

    value_ = std::move (text);
    note_ = std::move (note);
    repaint();
}

void Readout::setWarning (bool shouldWarn)
{
    if (warning_ == shouldWarn)
        return;

    warning_ = shouldWarn;
    repaint();
}

void Readout::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    auto area = bounds.reduced (10.0f, 7.0f);

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (caption_, area.removeFromTop (13.0f), juce::Justification::centredLeft);

    auto noteArea = note_.isEmpty() ? juce::Rectangle<float>{} : area.removeFromBottom (18.0f);

    // Sized to the cell rather than fixed. These panels get resized a long way
    // -- the window goes from 760 to 1520 wide -- and a number that stays 26
    // point floats in the middle of a tall cell looking like a placeholder.
    const float size = juce::jlimit (20.0f, 46.0f, area.getHeight() * 0.62f);
    const auto valueFont = juce::Font (juce::FontOptions (size, juce::Font::bold));

    g.setColour (warning_ ? palette_.over : palette_.text);
    g.setFont (valueFont);
    g.drawText (value_, area, juce::Justification::centredLeft);

    // The unit sits immediately after the number rather than in the far corner:
    // "-14.2 LUFS" reads as one thing, "-14.2" with "LUFS" across the panel
    // reads as two.
    if (! unit_.isEmpty())
    {
        const float valueWidth = juce::GlyphArrangement::getStringWidth (valueFont, value_);

        auto unitArea = area.withLeft (area.getX() + valueWidth + 6.0f);

        if (unitArea.getWidth() > 8.0f)
        {
            g.setColour (palette_.dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (unit_, unitArea, juce::Justification::centredLeft);
        }
    }

    if (! note_.isEmpty())
    {
        g.setColour (warning_ ? palette_.over.withAlpha (0.9f) : palette_.accent);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (note_, noteArea.toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// CorrelationBar
// ---------------------------------------------------------------------------

void CorrelationBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    auto area = bounds.reduced (8.0f, 6.0f);

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (caption_, area.removeFromTop (13.0f), juce::Justification::centredLeft);

    auto value = area.removeFromRight (52.0f);
    g.setColour (warning_ ? palette_.over : palette_.text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (juce::String (correlation_, 2), value, juce::Justification::centredRight);

    auto track = area.reduced (2.0f, 0.0f).withSizeKeepingCentre (area.getWidth() - 4.0f, 10.0f);

    g.setColour (palette_.background);
    g.fillRoundedRectangle (track, 3.0f);

    // The mono-safe half, marked rather than left to be remembered. Below zero
    // a fold to mono starts costing real level; at -1 it costs all of it.
    auto safe = track.withLeft (track.getCentreX());
    g.setColour (palette_.accent.withAlpha (0.16f));
    g.fillRoundedRectangle (safe, 3.0f);

    g.setColour (palette_.dimText.withAlpha (0.5f));
    g.drawVerticalLine (juce::roundToInt (track.getCentreX()), track.getY(), track.getBottom());

    // The needle.
    const float position = track.getX()
                         + track.getWidth() * (juce::jlimit (-1.0f, 1.0f, correlation_) + 1.0f) * 0.5f;

    g.setColour (warning_ ? palette_.over : palette_.accentBright);
    g.fillRoundedRectangle (juce::Rectangle<float> { position - 2.0f, track.getY() - 2.0f,
                                                     4.0f, track.getHeight() + 4.0f }, 2.0f);

    g.setColour (palette_.dimText.withAlpha (0.7f));
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("-1", track.withWidth (16.0f).translated (0.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("0",  track.withSizeKeepingCentre (16.0f, 10.0f).translated (0.0f, 12.0f),
                juce::Justification::centred);
    g.drawText ("+1", track.withLeft (track.getRight() - 18.0f).translated (0.0f, 12.0f),
                juce::Justification::centredRight);
}

// ---------------------------------------------------------------------------
// SpectrumView
// ---------------------------------------------------------------------------

namespace
{
constexpr double kLowHz  = 20.0;
constexpr double kHighHz = 20000.0;

/// Where the octave gridlines go.
constexpr double kGridHz[] { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 };

/// The display's vertical range, in dB.
constexpr float kTopDb   = 0.0f;
constexpr float kFloorDb = -84.0f;
} // namespace

SpectrumView::SpectrumView (ui::Palette palette, dsp::ReferenceCurve& reference)
    : palette_ (palette), reference_ (reference)
{
    // 2048 points: about 23 Hz of resolution at 48 kHz, which separates the
    // harmonics of anything above a bass note and still redraws comfortably.
    analyser_.prepare (48000.0, 11, TranspectusProcessor::kSpectrumBins, kLowHz, kHighHz);

    // Falls slowly enough to read, holds peaks long enough to see them.
    analyser_.setBallistics (1.6f, 0.28f);

    setTooltip ("The live spectrum with a peak hold behind it. The dotted line is a pink-noise "
                "slope -- physics, not a genre target. Capture Reference measures a track you "
                "play through the plugin and overlays its balance; Difference then shows yours "
                "minus it, which is the EQ move stated directly.");
}

float SpectrumView::positionFor (double hz) const noexcept
{
    const double clamped = juce::jlimit (kLowHz, kHighHz, hz);
    return static_cast<float> (std::log (clamped / kLowHz) / std::log (kHighHz / kLowHz));
}

bool SpectrumView::update (const dsp::SpectrumCapture& capture)
{
    return analyser_.update (capture);
}

void SpectrumView::pushToCapture()
{
    if (reference_.isCapturing())
        reference_.push (analyser_.getMagnitudesDb().data(), analyser_.getNumBins());
}

void SpectrumView::setShowPinkSlope (bool shouldShow)
{
    showPinkSlope_ = shouldShow;
    repaint();
}

void SpectrumView::setShowDifference (bool shouldShow)
{
    showDifference_ = shouldShow;
    repaint();
}

void SpectrumView::paintGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setFont (juce::FontOptions (9.0f));

    for (const double hz : kGridHz)
    {
        const float x = area.getX() + area.getWidth() * positionFor (hz);

        g.setColour (palette_.dimText.withAlpha (0.14f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom() - 11.0f);

        g.setColour (palette_.dimText.withAlpha (0.6f));
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k"
                                 : juce::String (juce::roundToInt (hz)),
                    juce::Rectangle<float> { x - 16.0f, area.getBottom() - 11.0f, 32.0f, 11.0f },
                    juce::Justification::centred);
    }

    for (float db = -20.0f; db > kFloorDb; db -= 20.0f)
    {
        const float y = area.getY() + area.getHeight() * (kTopDb - db) / (kTopDb - kFloorDb);

        g.setColour (palette_.dimText.withAlpha (0.12f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }
}

void SpectrumView::paintCurve (juce::Graphics& g, juce::Rectangle<float> area,
                               const std::vector<float>& db, juce::Colour colour,
                               float thickness, bool fill) const
{
    if (db.size() < 2)
        return;

    juce::Path path;

    const auto yFor = [&area] (float value)
    {
        return area.getY() + area.getHeight()
             * juce::jlimit (0.0f, 1.0f, (kTopDb - value) / (kTopDb - kFloorDb));
    };

    for (std::size_t i = 0; i < db.size(); ++i)
    {
        const float x = area.getX() + area.getWidth()
                      * static_cast<float> (i) / static_cast<float> (db.size() - 1);
        const float y = yFor (db[i]);

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    if (fill)
    {
        juce::Path filled = path;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setColour (colour.withAlpha (0.18f));
        g.fillPath (filled);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness));
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    auto area = bounds.reduced (8.0f, 7.0f);

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("SPECTRUM", area.removeFromTop (13.0f), juce::Justification::centredLeft);

    paintGrid (g, area);

    // A pink slope through the middle of the display: -3 dB per octave, which
    // is what equal energy per octave looks like. A reference for the eye, not
    // a target to hit.
    if (showPinkSlope_)
    {
        const double octaves = std::log2 (kHighHz / kLowHz);

        juce::Path slope;
        slope.startNewSubPath (area.getX(),
                               area.getY() + area.getHeight() * 0.22f);
        slope.lineTo (area.getRight(),
                      area.getY() + area.getHeight()
                          * (0.22f + static_cast<float> (3.0 * octaves) / (kTopDb - kFloorDb)));

        g.setColour (palette_.dimText.withAlpha (0.45f));

        const float dashes[] { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (slope, slope, dashes, 2);
        g.strokePath (slope, juce::PathStrokeType (1.0f));
    }

    // The peak hold behind the live curve, so a transient stays visible.
    paintCurve (g, area, analyser_.getPeaksDb(), palette_.accent.withAlpha (0.45f), 1.0f, false);
    paintCurve (g, area, analyser_.getMagnitudesDb(), palette_.accentBright, 1.6f, true);

    // The captured reference, drawn at the live curve's own average so the two
    // sit on top of each other rather than at arbitrary heights.
    if (reference_.hasCurve())
    {
        const auto& live = analyser_.getMagnitudesDb();
        const auto& curve = reference_.getCurveDb();

        double sum = 0.0;
        std::size_t counted = 0;

        for (const float value : live)
            if (value > dsp::ReferenceCurve::kFloorDb)
            {
                sum += value;
                ++counted;
            }

        const double mean = counted > 0 ? sum / static_cast<double> (counted) : -40.0;

        std::vector<float> shifted (curve.size());

        for (std::size_t i = 0; i < curve.size(); ++i)
            shifted[i] = static_cast<float> (curve[i] + mean);

        paintCurve (g, area, shifted, palette_.secondary, 1.8f, false);

        if (showDifference_)
        {
            reference_.computeDifference (live.data(), live.size(), difference_);

            // Drawn about the middle of the display: above the line means the
            // mix has more there than the reference does.
            const float centre = area.getCentreY();

            juce::Path path;

            for (std::size_t i = 0; i < difference_.size(); ++i)
            {
                const float x = area.getX() + area.getWidth()
                              * static_cast<float> (i)
                              / static_cast<float> (std::max<std::size_t> (difference_.size() - 1, 1));

                const float y = centre - static_cast<float> (difference_[i])
                              * area.getHeight() / (kTopDb - kFloorDb);

                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, juce::jlimit (area.getY(), area.getBottom(), y));
            }

            g.setColour (palette_.over.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.4f));

            g.setColour (palette_.dimText.withAlpha (0.4f));
            g.drawHorizontalLine (juce::roundToInt (centre), area.getX(), area.getRight());
        }
    }

    // A progress bar while capturing, so thirty seconds is visibly thirty
    // seconds rather than a button that appears to have done nothing.
    if (reference_.isCapturing())
    {
        auto strip = area.removeFromTop (4.0f);

        g.setColour (palette_.background);
        g.fillRect (strip);

        g.setColour (palette_.secondary);
        g.fillRect (strip.withWidth (strip.getWidth()
                                     * static_cast<float> (reference_.getProgress())));
    }
}

// ---------------------------------------------------------------------------
// TranspectusEditor
// ---------------------------------------------------------------------------

TranspectusEditor::TranspectusEditor (TranspectusProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      transpectus_ (processorToUse),
      palette_ (kPalette)
{
    header_ = std::make_unique<ui::HeaderBar> (
        transpectus_.getState(), "TRANSPECTUS",
        "Loudness, true peak and stereo analysis", ids::bypass, palette_);

    header_->onSwapRequested = [this]
    {
        transpectus_.getAbCompare().swapSlots();
        header_->setActiveSlot (transpectus_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (transpectus_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        transpectus_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (transpectus_.getAbCompare().otherSlotFilled());
    };

    addAndMakeVisible (*header_);

    buildControls();

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (900, 560);

    startTimerHz (20);
}

void TranspectusEditor::buildControls()
{
    const auto makeReadout = [this] (const char* caption, const char* unit, const char* tooltip)
    {
        auto readout = std::make_unique<Readout> (palette_, caption, unit);
        readout->setTooltip (tooltip);
        addAndMakeVisible (*readout);
        return readout;
    };

    integrated_ = makeReadout ("INTEGRATED", "LUFS",
        "The whole programme, gated to ITU-R BS.1770-5: quiet passages below -70 LUFS, and "
        "anything more than 10 LU under the average, are thrown out before the mean is taken. "
        "This is the number a streaming platform measures.");

    shortTerm_ = makeReadout ("SHORT TERM", "LUFS",
        "Three seconds, ungated. How loud this section is, which is the reading to mix "
        "against.");

    momentary_ = makeReadout ("MOMENTARY", "LUFS",
        "Four hundred milliseconds, ungated. Moves with the bar.");

    truePeak_ = makeReadout ("TRUE PEAK", "dBTP",
        "The highest the waveform reaches between the samples, held until you reset. Samples "
        "all under full scale still reconstruct above it -- measured on this rig at +1.5 dB on "
        "dense content and +3.0 dB on a tone at a quarter of the sample rate, which is why "
        "delivery specs ask for -1 dBTP rather than 0.");

    plr_ = makeReadout ("PLR", "dB",
        "True peak minus integrated loudness: how much transient survived the whole "
        "programme. Roughly 15-20 dB is classical, 8-12 a pop master, 5-8 a loud EDM master. "
        "Below about 5 the transients are gone -- the kick has no click left. Guidance, not a "
        "rule.");

    psr_ = makeReadout ("PSR", "dB",
        "The same subtraction against short-term loudness, so it moves bar to bar. This is "
        "the one to watch while working: it shows the punch of what is playing rather than "
        "the average of everything.");

    delta_ = makeReadout ("VS TARGET", "dB",
        "What the selected platform will do to this master. Several of them only ever turn a "
        "loud master down -- a quiet one on YouTube simply plays quiet -- so this reads zero "
        "there rather than promising a boost that will not happen.");

    inputMeter_ = std::make_unique<ui::LevelMeter> (palette_);
    inputMeter_->setReferenceDb (static_cast<float> (kDeliveryTruePeakDb));
    inputMeter_->setScaleVisible (true);
    inputMeter_->setTooltip ("Sample peak, with the hold marked against -1 dBFS -- the delivery "
                             "ceiling every platform in the list expects. Click to clear.");
    addAndMakeVisible (*inputMeter_);

    fullCorrelation_ = std::make_unique<CorrelationBar> (palette_);
    fullCorrelation_->setCaption ("CORRELATION  full band");
    fullCorrelation_->setTooltip ("+1 the channels agree, 0 uncorrelated, -1 polarity inverted. "
                                  "A full-band reading is mostly mid and top, so it can look "
                                  "healthy while the sub underneath cancels -- which is what the "
                                  "reading below it is for.");
    addAndMakeVisible (*fullCorrelation_);

    lowCorrelation_ = std::make_unique<CorrelationBar> (palette_);
    lowCorrelation_->setCaption ("CORRELATION  sub");
    lowCorrelation_->setTooltip ("The one that matters on a club system. Below the Mono Check "
                                 "frequency this should sit near +1: anything lower and the bass "
                                 "loses level when the rig sums to mono, and headphones will "
                                 "never show it because nothing sums there.");
    addAndMakeVisible (*lowCorrelation_);

    // ---- controls ------------------------------------------------------------

    targetBox_.addItemList (choices::targetNames(), 1);
    targetBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    targetBox_.setColour (juce::ComboBox::textColourId, palette_.text);
    targetBox_.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    targetBox_.setTooltip ("Which platform the VS TARGET readout is computed against. The "
                           "figures were verified in August 2026 and are stored with that date, "
                           "because platforms change them.");
    addAndMakeVisible (targetBox_);

    targetAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        transpectus_.getState(), ids::target, targetBox_);

    truePeakBox_.addItemList (choices::truePeak, 1);
    truePeakBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    truePeakBox_.setColour (juce::ComboBox::textColourId, palette_.text);
    truePeakBox_.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    truePeakBox_.setTooltip ("Off is sample peak and reads up to 3 dB low. Standard is the ITU's "
                             "own filter and agrees with every other dBTP meter. Strict "
                             "interpolates 16x and is the one to believe, at about four times "
                             "the CPU.");
    addAndMakeVisible (truePeakBox_);

    truePeakAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        transpectus_.getState(), ids::truePeak, truePeakBox_);

    for (auto* label : { &targetLabel_, &truePeakLabel_ })
    {
        label->setJustificationType (juce::Justification::centredLeft);
        label->setColour (juce::Label::textColourId, palette_.dimText);
        label->setFont (juce::FontOptions (10.5f));
        addAndMakeVisible (label);
    }

    resetButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    resetButton_.setColour (juce::TextButton::textColourOffId, palette_.text);
    resetButton_.setTooltip ("Clears the integrated reading, the true-peak hold and the level "
                             "meter's hold, and starts measuring again. The filters are not "
                             "disturbed, so the momentary reading keeps running.");
    resetButton_.onClick = [this]
    {
        transpectus_.resetMeasurement();
        inputMeter_->resetHold();
    };
    addAndMakeVisible (resetButton_);

    spectrum_ = std::make_unique<SpectrumView> (palette_, transpectus_.getReferenceCurve());
    addAndMakeVisible (*spectrum_);

    captureButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    captureButton_.setColour (juce::TextButton::textColourOffId, palette_.text);
    captureButton_.setTooltip ("Plays a track through the plugin for thirty seconds and stores "
                               "its tonal balance. Thirty rather than two: a short capture is a "
                               "snapshot of one chord and says nothing about balance. The curve "
                               "is normalised, so a quiet reference and a loud one compare on "
                               "shape rather than volume, and it saves with the project.");
    captureButton_.onClick = [this]
    {
        auto& reference = transpectus_.getReferenceCurve();

        if (reference.isCapturing())
            reference.cancelCapture();
        else
            reference.beginCapture (dsp::ReferenceCurve::kDefaultSeconds, 20.0);
    };
    addAndMakeVisible (captureButton_);

    clearReferenceButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.2f));
    clearReferenceButton_.setColour (juce::TextButton::textColourOffId, palette_.dimText);
    clearReferenceButton_.setTooltip ("Throws the stored reference away.");
    clearReferenceButton_.onClick = [this]
    {
        transpectus_.getReferenceCurve().clear();
        differenceButton_.setToggleState (false, juce::sendNotification);
    };
    addAndMakeVisible (clearReferenceButton_);

    pinkButton_.setColour (juce::ToggleButton::textColourId, palette_.text);
    pinkButton_.setColour (juce::ToggleButton::tickColourId, palette_.accent);
    pinkButton_.setToggleState (true, juce::dontSendNotification);
    pinkButton_.setTooltip ("A -3 dB per octave slope: what equal energy per octave looks like. "
                            "Physics rather than a genre target, and the only reference curve "
                            "this plugin ships with.");
    pinkButton_.onClick = [this] { spectrum_->setShowPinkSlope (pinkButton_.getToggleState()); };
    addAndMakeVisible (pinkButton_);

    differenceButton_.setColour (juce::ToggleButton::textColourId, palette_.text);
    differenceButton_.setColour (juce::ToggleButton::tickColourId, palette_.over);
    differenceButton_.setTooltip ("Draws your spectrum minus the captured reference about the "
                                  "centre line. Above it you have more than the reference does; "
                                  "below it, less. Needs a captured reference.");
    differenceButton_.onClick = [this]
    {
        spectrum_->setShowDifference (differenceButton_.getToggleState());
    };
    addAndMakeVisible (differenceButton_);

    statusLabel_.setJustificationType (juce::Justification::centred);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (statusLabel_);
}

juce::String TranspectusEditor::formatLufs (double lufs)
{
    return lufs <= kSilenceThreshold ? juce::String::fromUTF8 ("-\xe2\x88\x9e")
                                     : juce::String (lufs, 1);
}

void TranspectusEditor::timerCallback()
{
    auto& engine = transpectus_.getEngine();

    const double integrated = engine.getIntegratedLufs();

    integrated_->setValue (formatLufs (integrated));
    shortTerm_->setValue (formatLufs (engine.getShortTermLufs()));
    momentary_->setValue (formatLufs (engine.getMomentaryLufs()));

    const double peak = engine.getTruePeakDb();
    truePeak_->setValue (peak <= kSilenceThreshold ? juce::String::fromUTF8 ("-\xe2\x88\x9e")
                                                   : juce::String (peak, 2));
    truePeak_->setWarning (peak > kDeliveryTruePeakDb);

    // PLR, with the interpretation the number needs to be worth reading.
    const double plr = engine.getPlr();

    if (plr <= 0.0)
    {
        plr_->setValue ("--");
        plr_->setWarning (false);
    }
    else
    {
        plr_->setValue (juce::String (plr, 1),
                        plr < kPlrSquashed ? "transients gone"
                      : plr < kPlrHealthy  ? "loud master"
                                           : "dynamics intact");
        plr_->setWarning (plr < kPlrSquashed);
    }

    const double psr = engine.getPsr();
    psr_->setValue (psr <= 0.0 ? "--" : juce::String (psr, 1));
    psr_->setWarning (psr > 0.0 && psr < kPlrSquashed);

    // What the platform does, in words rather than a signed number nobody
    // wants to interpret twice.
    const auto& target = engine.getTarget();
    const double delta = engine.getTargetDeltaDb();

    if (integrated <= kSilenceThreshold)
    {
        delta_->setValue ("--");
        delta_->setWarning (false);
    }
    else if (std::abs (delta) < 0.05)
    {
        delta_->setValue ("0.0", target.boostsQuietMaterial
                                     ? juce::String ("at ") + target.name
                                     : juce::String ("unchanged on ") + target.name);
        delta_->setWarning (false);
    }
    else
    {
        delta_->setValue (juce::String (std::abs (delta), 1),
                          delta > 0.0 ? juce::String ("turned down on ") + target.name
                                      : juce::String ("turned up on ") + target.name);
        delta_->setWarning (false);
    }

    fullCorrelation_->setValue (static_cast<float> (engine.getCorrelation()), false);

    const double low = engine.getBandCorrelation (dsp::StereoAnalyser::low);
    lowCorrelation_->setValue (static_cast<float> (low), ! engine.isLowBandMonoSafe());

    fullCorrelation_->repaint();
    lowCorrelation_->repaint();

    // Fold the latest window onto the display bins, then feed the capture if
    // one is running -- in that order, so the capture sees the same frame the
    // user is looking at.
    if (spectrum_->update (engine.getSpectrumCapture()))
    {
        spectrum_->pushToCapture();
        spectrum_->repaint();
    }

    auto& reference = transpectus_.getReferenceCurve();

    captureButton_.setButtonText (reference.isCapturing()
        ? "CAPTURING  " + juce::String (juce::roundToInt (reference.getProgress() * 100.0)) + "%"
        : "CAPTURE REFERENCE");

    clearReferenceButton_.setEnabled (reference.hasCurve());
    differenceButton_.setEnabled (reference.hasCurve());

    inputMeter_->setValues (static_cast<float> (peak), static_cast<float> (peak));
    inputMeter_->repaint();

    header_->setActiveSlot (transpectus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (transpectus_.getAbCompare().otherSlotFilled());

    // The line that says what is and is not being measured.
    juce::String status;

    if (! transpectus_.isPrepared())
    {
        status = "Waiting for the host to start audio.";
    }
    else if (transpectus_.getState().getRawParameterValue (ids::bypass)->load() > 0.5f)
    {
        status = "Bypassed \xe2\x80\x94 measurement is paused. The audio is untouched either way; "
                 "this plugin never changes it.";
    }
    else
    {
        status << "Measuring at " << juce::String (engine.getSampleRate() / 1000.0, 1)
               << " kHz \xe2\x80\xa2 true peak x"
               << dsp::truePeakFactorFor (
                      static_cast<dsp::TruePeakMode> (juce::jlimit (0, 2, static_cast<int> (
                          transpectus_.getState().getRawParameterValue (ids::truePeak)->load()))),
                      engine.getSampleRate())
               << " \xe2\x80\xa2 mono check below "
               << juce::String (juce::roundToInt (
                      transpectus_.getState().getRawParameterValue (ids::monoCheckHz)->load()))
               << " Hz \xe2\x80\xa2 latency 0";
    }

    statusLabel_.setText (status, juce::dontSendNotification);
}

void TranspectusEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void TranspectusEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));
    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 4));

    // Controls along the bottom, above the status line.
    {
        auto controls = bounds.removeFromBottom (kControlHeight).reduced (10, 4);

        auto target = controls.removeFromLeft (juce::jmin (280, controls.getWidth() / 2));
        targetLabel_.setBounds (target.removeFromTop (13));
        targetBox_.setBounds (target.reduced (0, 1));

        controls.removeFromLeft (12);

        auto truePeak = controls.removeFromLeft (150);
        truePeakLabel_.setBounds (truePeak.removeFromTop (13));
        truePeakBox_.setBounds (truePeak.reduced (0, 1));

        resetButton_.setBounds (controls.removeFromRight (juce::jmin (190, controls.getWidth()))
                                    .withSizeKeepingCentre (
                                        juce::jmin (190, controls.getWidth()), 26));
    }

    auto body = bounds.reduced (10, 6);

    // The level meter down the right-hand side, as in every other plugin here.
    auto meterColumn = body.removeFromRight (ui::LevelMeter::kMinimumWidth
                                             + ui::LevelMeter::kScaleWidth + 8).reduced (4, 0);
    inputMeter_->setBounds (meterColumn);

    body.removeFromRight (8);

    // Seven readouts at a fixed height rather than a share of the window. They
    // hold one number each; giving them a third of a tall window leaves the
    // number floating in the middle of an empty panel, and the room belongs to
    // the spectrum.
    constexpr int readoutHeight = 76;

    {
        auto top = body.removeFromTop (readoutHeight);
        const int width = top.getWidth() / 4;

        integrated_->setBounds (top.removeFromLeft (width).reduced (4));
        shortTerm_->setBounds  (top.removeFromLeft (width).reduced (4));
        momentary_->setBounds  (top.removeFromLeft (width).reduced (4));
        truePeak_->setBounds   (top.reduced (4));
    }

    {
        auto second = body.removeFromTop (readoutHeight);
        const int width = second.getWidth() / 4;

        plr_->setBounds   (second.removeFromLeft (width).reduced (4));
        psr_->setBounds   (second.removeFromLeft (width).reduced (4));
        delta_->setBounds (second.reduced (4));
    }

    // Correlation across the bottom, the spectrum filling whatever is left --
    // which is most of a tall window, and is where the eye spends its time.
    auto correlation = body.removeFromBottom (60);
    const int half = correlation.getWidth() / 2;
    fullCorrelation_->setBounds (correlation.removeFromLeft (half).reduced (4, 4));
    lowCorrelation_->setBounds (correlation.reduced (4, 4));

    // The reference controls sit under the spectrum they act on.
    auto spectrumControls = body.removeFromBottom (28).reduced (4, 2);

    captureButton_.setBounds (spectrumControls.removeFromLeft (160));
    spectrumControls.removeFromLeft (6);
    clearReferenceButton_.setBounds (spectrumControls.removeFromLeft (70));
    spectrumControls.removeFromLeft (14);
    pinkButton_.setBounds (spectrumControls.removeFromLeft (110));
    differenceButton_.setBounds (spectrumControls.removeFromLeft (110));

    spectrum_->setBounds (body.reduced (4, 2));
}

} // namespace tezla::transpectus
