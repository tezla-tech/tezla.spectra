// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include <tezla/ui/TuningPanel.hpp>

#include <cmath>

#include <tezla/dsp/Scales.hpp>

namespace tezla::ui {

namespace dsp = tezla::dsp;

void TuningWrappingLabel::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (getFont());
    g.drawFittedText (getText(), getLocalBounds(), getJustificationType(), 4, 1.0f);
}

// ---------------------------------------------------------------------------
// TuningDegreeTable
// ---------------------------------------------------------------------------

void TuningDegreeTable::setScale (const dsp::Scale& scale, double rootHz)
{
    rows_.clear();
    rows_.reserve (static_cast<std::size_t> (scale.size()) + 1);

    const auto formatHz = [] (double hz)
    {
        return juce::String (hz, hz < 1000.0 ? 2 : 1);
    };

    for (int degree = 0; degree < scale.size(); ++degree)
    {
        const double ratio = scale.ratios[static_cast<std::size_t> (degree)];
        const double next = degree + 1 < scale.size()
                              ? scale.ratios[static_cast<std::size_t> (degree + 1)]
                              : scale.repeat;

        Row row;
        row.degree = juce::String (degree);

        // A fraction only when the degree is exactly one -- a tempered degree
        // shows a dash here and speaks through its cents instead.
        const auto fraction = dsp::nearestFraction (ratio);

        row.ratio = fraction.found
                      ? juce::String (fraction.numerator) + "/" + juce::String (fraction.denominator)
                      : juce::String ("-");

        row.cents = juce::String (scale.cents (degree), 1);
        row.step = juce::String (1200.0 * std::log2 (next / ratio), 1);
        row.hz = rootHz > 0.0 ? formatHz (rootHz * ratio) : juce::String ("-");

        rows_.push_back (std::move (row));
    }

    // The repeat interval as its own row: 2/1 for the octave scales, 3/1 for
    // Bohlen-Pierce, and the golden section's 1.618 has no fraction at all.
    Row repeat;
    repeat.degree = "R";

    const auto fraction = dsp::nearestFraction (scale.repeat);

    repeat.ratio = fraction.found
                     ? juce::String (fraction.numerator) + "/" + juce::String (fraction.denominator)
                     : juce::String (scale.repeat, 5);

    repeat.cents = juce::String (scale.repeatCents(), 1);
    repeat.step = "";
    repeat.hz = rootHz > 0.0 ? formatHz (rootHz * scale.repeat) : juce::String ("-");
    repeat.isRepeat = true;

    rows_.push_back (std::move (repeat));

    setSize (juce::jmax (getWidth(), 1), preferredHeight());
    repaint();
}

void TuningDegreeTable::paint (juce::Graphics& g)
{
    const auto mono = juce::FontOptions()
                        .withName (juce::Font::getDefaultMonospacedFontName())
                        .withHeight (11.0f);

    const int width = getWidth();

    // Five columns: degree, ratio, cents, step, and the sounding frequency.
    // The ratio column gets the most room because 177147/131072 is a real
    // resident; the Hz column is what moves when the A4 control does.
    const int degreeRight = 26;
    const int ratioRight = degreeRight + 100;
    const int centsRight = ratioRight + 58;
    const int stepRight = centsRight + 50;
    const int hzRight = juce::jmin (stepRight + 74, width - 4);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (palette_.dimText);
    g.drawText ("#", 0, 0, degreeRight, kHeaderHeight, juce::Justification::centredRight);
    g.drawText ("RATIO", degreeRight, 0, ratioRight - degreeRight, kHeaderHeight,
                juce::Justification::centredRight);
    g.drawText ("CENTS", ratioRight, 0, centsRight - ratioRight, kHeaderHeight,
                juce::Justification::centredRight);
    g.drawText ("STEP", centsRight, 0, stepRight - centsRight, kHeaderHeight,
                juce::Justification::centredRight);
    g.drawText ("HZ", stepRight, 0, hzRight - stepRight, kHeaderHeight,
                juce::Justification::centredRight);

    g.setFont (mono);

    for (std::size_t index = 0; index < rows_.size(); ++index)
    {
        const auto& row = rows_[index];
        const int y = kHeaderHeight + static_cast<int> (index) * kRowHeight;

        if (index % 2 == 0)
        {
            g.setColour (palette_.panel.brighter (0.06f));
            g.fillRect (0, y, width, kRowHeight);
        }

        g.setColour (row.isRepeat ? palette_.accent : palette_.dimText);
        g.drawText (row.degree, 0, y, degreeRight, kRowHeight, juce::Justification::centredRight);

        g.setColour (row.isRepeat ? palette_.accent : palette_.text);
        g.drawText (row.ratio, degreeRight, y, ratioRight - degreeRight, kRowHeight,
                    juce::Justification::centredRight);
        g.drawText (row.cents, ratioRight, y, centsRight - ratioRight, kRowHeight,
                    juce::Justification::centredRight);

        g.setColour (palette_.dimText);
        g.drawText (row.step, centsRight, y, stepRight - centsRight, kRowHeight,
                    juce::Justification::centredRight);

        g.setColour (row.isRepeat ? palette_.accent : palette_.text);
        g.drawText (row.hz, stepRight, y, hzRight - stepRight, kRowHeight,
                    juce::Justification::centredRight);
    }
}

// ---------------------------------------------------------------------------
// TuningPanel
// ---------------------------------------------------------------------------

TuningPanel::TuningPanel (TuningHost& host, Palette palette, juce::String explanationText)
    : host_ (host), palette_ (palette), degreeTable_ (palette)
{
    headingLabel_.setText ("TUNING", juce::dontSendNotification);
    headingLabel_.setColour (juce::Label::textColourId, palette_.accent);
    headingLabel_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (headingLabel_);

    // The built-in scales, listed by **name**. The name is what gets stored,
    // not the index -- which is the more robust choice for a list that will
    // grow, and the reason this is a menu rather than a choice parameter.
    scaleBox_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    scaleBox_.setColour (juce::ComboBox::textColourId, palette_.text);
    scaleBox_.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    scaleBox_.setTextWhenNothingSelected ("Loaded from a file");
    scaleBox_.setTooltip (
        "The built-in scales, each generated from its own definition rather than shipped as "
        "somebody's data file. The strange ones are the point: Bohlen-Pierce repeats at 3/1 "
        "rather than the octave, and the Carlos scales do not repeat at an octave at all.");

    {
        int id = 1;

        for (const auto& scale : dsp::scales::all())
            scaleBox_.addItem (juce::String (scale.name), id++);
    }

    scaleBox_.onChange = [this]
    {
        if (updating_ || scaleBox_.getSelectedId() <= 0)
            return;

        const auto reason = host_.selectBuiltInScale (scaleBox_.getText());

        if (reason.isNotEmpty())
            reportFailure ("scale", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    };

    addAndMakeVisible (scaleBox_);

    loadScaleButton_.setTooltip (
        "Load a Scala .scl scale file. The parser refuses a file it cannot fully read and says "
        "which line stopped it -- a tuning that half-loads is worse than one that will not load, "
        "because it plays.");
    loadScaleButton_.onClick = [this] { loadScaleFile(); };
    addAndMakeVisible (loadScaleButton_);

    loadMapButton_.setTooltip (
        "Load a Scala .kbm keyboard map: which MIDI note is the reference, what frequency it is, "
        "and which keys the scale's degrees land on. Optional -- without one the scale starts at "
        "middle C and every key is used.");
    loadMapButton_.onClick = [this] { loadKeyboardMapFile(); };
    addAndMakeVisible (loadMapButton_);

    resetButton_.setTooltip ("Back to twelve-tone equal temperament, with no keyboard map.");
    resetButton_.onClick = [this]
    {
        host_.resetTuning();
        errorLabel_.setText ({}, juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (resetButton_);

    for (auto* button : { &loadScaleButton_, &loadMapButton_, &resetButton_ })
    {
        button->setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.12f));
        button->setColour (juce::TextButton::textColourOffId, palette_.text);
    }

    descriptionLabel_.setColour (juce::Label::textColourId, palette_.text);
    descriptionLabel_.setFont (juce::FontOptions (13.0f));
    descriptionLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (descriptionLabel_);

    explanationLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    explanationLabel_.setFont (juce::FontOptions (11.0f));
    explanationLabel_.setJustificationType (juce::Justification::topLeft);
    explanationLabel_.setText (explanationText, juce::dontSendNotification);
    addAndMakeVisible (explanationLabel_);

    errorLabel_.setColour (juce::Label::textColourId, palette_.over);
    errorLabel_.setFont (juce::FontOptions (12.0f));
    errorLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (errorLabel_);

    // What the selected scale is: theorem, story, and every degree.
    constructionLabel_.setColour (juce::Label::textColourId, palette_.accent);
    constructionLabel_.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    constructionLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (constructionLabel_);

    storyLabel_.setColour (juce::Label::textColourId, palette_.text);
    storyLabel_.setFont (juce::FontOptions (12.0f));
    storyLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (storyLabel_);

    tableViewport_.setViewedComponent (&degreeTable_, false);
    tableViewport_.setScrollBarsShown (true, false);
    tableViewport_.setScrollBarThickness (14);
    addAndMakeVisible (tableViewport_);

    // The pitch standard: the tradition's own tuning practice, bold, with a
    // button when it names a number the A4 control can be set to.
    pitchLoreLabel_.setColour (juce::Label::textColourId, palette_.text);
    pitchLoreLabel_.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    pitchLoreLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (pitchLoreLabel_);

    applyPitchButton_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.12f));
    applyPitchButton_.setColour (juce::TextButton::textColourOffId, palette_.accent);
    applyPitchButton_.setTooltip (
        "Sets the A4 control to the pitch standard this scale's tradition names -- A415 for "
        "the baroque temperaments, ISO A440 for 12-TET. Scales whose tradition left no "
        "number (Babylon, Greece, Persia...) have no button, because inventing one would "
        "be a lie.");
    applyPitchButton_.onClick = [this]
    {
        const double suggested = host_.getScale().suggestedConcertHz;

        if (suggested > 0.0)
        {
            host_.setConcertPitch (suggested);
            refresh();
        }
    };
    applyPitchButton_.setComponentID ("apply-pitch");
    addAndMakeVisible (applyPitchButton_);

    // A4: the whole tuning scaled by one ratio against 440. The table's Hz
    // column follows the drag live.
    concertLabel_.setText ("A4", juce::dontSendNotification);
    concertLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    concertLabel_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    concertLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (concertLabel_);

    concertSlider_.setSliderStyle (juce::Slider::LinearBar);
    concertSlider_.setRange (dsp::Tuning::kMinimumConcertHz, dsp::Tuning::kMaximumConcertHz, 0.1);
    concertSlider_.setValue (440.0, juce::dontSendNotification);
    concertSlider_.setDoubleClickReturnValue (true, 440.0);
    concertSlider_.setTextValueSuffix (" Hz");
    concertSlider_.setNumDecimalPlacesToDisplay (1);
    concertSlider_.setColour (juce::Slider::trackColourId, palette_.panel.brighter (0.18f));
    concertSlider_.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    concertSlider_.setTooltip (
        "The pitch standard, as what A440 is moved to: the whole tuning -- keyboard map "
        "reference included -- scales by this against 440, so it means something even in a "
        "scale with no A in it. A440 has only been the standard since 1939 (ISO 16, 1955); "
        "the 19th-century French diapason normal was 435, scientific pitch C-256 gives "
        "430.5, and 432 is a modern preference with no historical orchestra behind it -- "
        "all one drag away. Double-click returns to 440. Saved with the project, and "
        "presets do not touch it.");
    concertSlider_.onValueChange = [this]
    {
        if (updating_)
            return;

        host_.setConcertPitch (concertSlider_.getValue());
        refresh();
    };
    concertSlider_.setComponentID ("concert-pitch");
    addAndMakeVisible (concertSlider_);

    refresh();
}

void TuningPanel::refresh()
{
    descriptionLabel_.setText (host_.describeTuning(), juce::dontSendNotification);

    // The info panel follows whatever is actually loaded -- built-in or file.
    // A built-in carries its construction and story; a file-loaded scale has
    // neither, so the panel says where it came from and lets the computed
    // table speak for the numbers.
    const auto& scale = host_.getScale();

    degreeTable_.setScale (scale, host_.getRootHz());

    constructionLabel_.setText (
        scale.construction.empty()
            ? juce::String ("Loaded from a Scala file: the degrees are the file's own.")
            : juce::String (scale.construction),
        juce::dontSendNotification);

    storyLabel_.setText (
        scale.story.empty()
            ? juce::String ("The table shows every degree as the instrument will play it -- "
                            "an exact fraction where the file gave a ratio, cents where it "
                            "gave cents.")
            : juce::String (scale.story),
        juce::dontSendNotification);

    // The pitch standard, bold -- and honestly generic when the scale is an
    // interval system with no frequency of its own.
    pitchLoreLabel_.setText (
        scale.pitchStandard.empty()
            ? juce::String ("No inherent pitch standard: this scale fixes intervals, not "
                            "frequencies. A440 is the modern default; the A4 control moves "
                            "the whole tuning together.")
            : juce::String (scale.pitchStandard),
        juce::dontSendNotification);

    // The Apply button exists only when the tradition names a number.
    const double suggested = scale.suggestedConcertHz;

    applyPitchButton_.setVisible (suggested > 0.0);

    if (suggested > 0.0)
        applyPitchButton_.setButtonText ("Apply A" + juce::String (suggested, 0));

    {
        const juce::ScopedValueSetter<bool> sliderGuard (updating_, true);
        concertSlider_.setValue (host_.getConcertPitch(), juce::dontSendNotification);
    }

    const auto name = host_.getScaleName();

    // The box follows the host rather than the other way round, so a state
    // load shows the scale that is actually playing. Guarded, or restoring
    // the selection would fire `onChange` and re-select it.
    const juce::ScopedValueSetter<bool> guard (updating_, true);

    for (int item = 0; item < scaleBox_.getNumItems(); ++item)
        if (scaleBox_.getItemText (item) == name)
        {
            scaleBox_.setSelectedItemIndex (item, juce::dontSendNotification);
            return;
        }

    // A scale loaded from a file is not in the list, and showing the last
    // built-in that happened to be selected would be a lie about what is
    // playing.
    scaleBox_.setSelectedId (0, juce::dontSendNotification);
}

void TuningPanel::reportFailure (const juce::String& what, const juce::String& reason)
{
    errorLabel_.setText ("Could not load that " + what + ". " + reason
                           + "\nThe tuning is unchanged.",
                         juce::dontSendNotification);
}

void TuningPanel::loadScaleFile()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Load a Scala scale", juce::File {}, "*.scl");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (! file.existsAsFile())
            return;

        const auto reason = host_.loadScalaText (file.loadFileAsString(),
                                                 file.getFileNameWithoutExtension());

        if (reason.isNotEmpty())
            reportFailure ("scale", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    });
}

void TuningPanel::loadKeyboardMapFile()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Load a Scala keyboard map",
                                                    juce::File {}, "*.kbm");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (! file.existsAsFile())
            return;

        const auto reason = host_.loadKeyboardMapText (file.loadFileAsString());

        if (reason.isNotEmpty())
            reportFailure ("keyboard map", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    });
}

void TuningPanel::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void TuningPanel::resized()
{
    auto bounds = getLocalBounds().reduced (12, 8);

    headingLabel_.setBounds (bounds.removeFromTop (20));

    auto row = bounds.removeFromTop (30);

    scaleBox_.setBounds (row.removeFromLeft (juce::jmin (280, row.getWidth() / 2)).reduced (0, 2));
    row.removeFromLeft (10);
    loadScaleButton_.setBounds (row.removeFromLeft (110).reduced (0, 2));
    row.removeFromLeft (6);
    loadMapButton_.setBounds (row.removeFromLeft (110).reduced (0, 2));
    row.removeFromLeft (6);
    resetButton_.setBounds (row.removeFromLeft (90).reduced (0, 2));

    // The pitch standard control lives on the same row: A4, then the value.
    row.removeFromLeft (14);
    concertLabel_.setBounds (row.removeFromLeft (24));
    row.removeFromLeft (4);
    concertSlider_.setBounds (row.removeFromLeft (juce::jmin (130, row.getWidth())).reduced (0, 3));

    bounds.removeFromTop (8);
    descriptionLabel_.setBounds (bounds.removeFromTop (18));

    bounds.removeFromTop (4);
    errorLabel_.setBounds (bounds.removeFromTop (18));

    bounds.removeFromTop (6);

    // The info panel: the degree table on the left; the theorem, the pitch
    // standard (with its Apply button when the tradition names a number) and
    // the story on the right. The table scrolls -- Partch has 43 rows and
    // 53-TET has 53 -- and the prose wraps in the room that remains.
    auto info = bounds;
    auto tableArea = info.removeFromLeft (juce::jmin (350, info.getWidth() * 2 / 5));

    tableViewport_.setBounds (tableArea);
    degreeTable_.setSize (tableArea.getWidth() - tableViewport_.getScrollBarThickness(),
                          degreeTable_.preferredHeight());

    info.removeFromLeft (12);

    constructionLabel_.setBounds (info.removeFromTop (44));
    info.removeFromTop (4);

    pitchLoreLabel_.setBounds (info.removeFromTop (46));

    auto applyRow = info.removeFromTop (22);
    applyPitchButton_.setBounds (applyRow.removeFromLeft (110).reduced (0, 1));
    info.removeFromTop (4);

    const int explanation = 46;
    storyLabel_.setBounds (info.removeFromTop (
        juce::jmax (40, info.getHeight() - explanation - 6)));

    info.removeFromTop (6);
    explanationLabel_.setBounds (info);
}

} // namespace tezla::ui
