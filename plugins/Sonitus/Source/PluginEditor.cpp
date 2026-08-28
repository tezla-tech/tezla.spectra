#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Scales.hpp>

namespace tezla::sonitus
{

namespace
{
// Sonitus's own accent: an acid green-yellow, against Emberdrive's ember,
// Halo's gold, Capstone's steel, Anvil's hot iron and Transpectus's green. The
// secondary carries the modulation -- the LFO bars, the playing step -- which
// is the one reading here that is not a level, and the two must not be
// confusable at a glance.
const ui::Palette kPalette {
    juce::Colour { 0xff101312 },   // background
    juce::Colour { 0xff191d1c },   // panel
    juce::Colour { 0xffd7ddd6 },   // text
    juce::Colour { 0xff7f8a83 },   // dim text
    juce::Colour { 0xffa8c93a },   // accent: acid
    juce::Colour { 0xffcbe960 },   // accent bright
    juce::Colour { 0xff9a6bd8 },   // secondary: modulation
    juce::Colour { 0xffff7a18 },   // bypass glow, the same in every plugin
    juce::Colour { 0xffe2483d },   // over
    juce::Colour { 0xffab9bf5 }    // hold
};

constexpr int kLabelHeight = 15;
constexpr int kValueHeight = 17;
constexpr int kMinCellHeight = 84;
constexpr int kMaxCellHeight = 120;
constexpr int kHeadingHeight = 22;
constexpr int kNoteHeight = 58;

constexpr int kMinWidth  = 860;
constexpr int kMinHeight = 620;
constexpr int kMaxWidth  = 1720;
constexpr int kMaxHeight = 1240;

constexpr int kMeterWidth = ui::LevelMeter::kMinimumWidth + ui::LevelMeter::kScaleWidth;
constexpr int kTabHeight = 28;
constexpr int kStatusHeight = 34;
constexpr int kStepStripHeight = 132;
} // namespace

// ---------------------------------------------------------------------------
// WrappingLabel
// ---------------------------------------------------------------------------

void WrappingLabel::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (getFont());
    g.drawFittedText (getText(), getLocalBounds(), getJustificationType(), 4, 1.0f);
}

// ---------------------------------------------------------------------------
// ControlPage
// ---------------------------------------------------------------------------

void ControlPage::addKnob (const juce::String& parameterId, const juce::String& name,
                           const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 112, kValueHeight);
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, palette_.accent);
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId, palette_.panel.brighter (0.25f));
    knob->slider.setColour (juce::Slider::thumbColourId, palette_.accentBright);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider.setTooltip (tooltip);
    addAndMakeVisible (knob->slider);

    knob->label.setText (name, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, palette_.dimText);
    knob->label.setFont (juce::FontOptions (12.0f));
    knob->label.setTooltip (tooltip);
    addAndMakeVisible (knob->label);

    knob->id = parameterId;
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state_, parameterId, knob->slider);

    cells_.push_back ({ Cell::Kind::knob, static_cast<int> (knobs_.size()) });
    knobs_.push_back (std::move (knob));
}

void ControlPage::addChoice (const juce::String& parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    auto choice = std::make_unique<Choice>();

    // Populated from the parameter itself. A ComboBoxAttachment selects an item
    // by index and does not create one, so a box left empty here stays empty on
    // screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state_.getParameter (parameterId)))
        choice->box.addItemList (parameter->choices, 1);
    else
        jassertfalse;   // addChoice used on something that is not a choice parameter

    choice->box.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.15f));
    choice->box.setColour (juce::ComboBox::textColourId, palette_.text);
    choice->box.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    choice->box.setTooltip (tooltip);
    addAndMakeVisible (choice->box);

    choice->label.setText (name, juce::dontSendNotification);
    choice->label.setJustificationType (juce::Justification::centred);
    choice->label.setColour (juce::Label::textColourId, palette_.dimText);
    choice->label.setFont (juce::FontOptions (12.0f));
    choice->label.setTooltip (tooltip);
    addAndMakeVisible (choice->label);

    choice->id = parameterId;
    choice->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state_, parameterId, choice->box);

    cells_.push_back ({ Cell::Kind::choice, static_cast<int> (choices_.size()) });
    choices_.push_back (std::move (choice));
}

void ControlPage::addToggle (const juce::String& parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    auto toggle = std::make_unique<Toggle>();

    toggle->button.setButtonText ("");
    toggle->button.setColour (juce::ToggleButton::tickColourId, palette_.accentBright);
    toggle->button.setColour (juce::ToggleButton::tickDisabledColourId, palette_.panel.brighter (0.35f));
    toggle->button.setTooltip (tooltip);
    addAndMakeVisible (toggle->button);

    toggle->label.setText (name, juce::dontSendNotification);
    toggle->label.setJustificationType (juce::Justification::centred);
    toggle->label.setColour (juce::Label::textColourId, palette_.dimText);
    toggle->label.setFont (juce::FontOptions (12.0f));
    toggle->label.setTooltip (tooltip);
    addAndMakeVisible (toggle->label);

    toggle->id = parameterId;
    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state_, parameterId, toggle->button);

    cells_.push_back ({ Cell::Kind::toggle, static_cast<int> (toggles_.size()) });
    toggles_.push_back (std::move (toggle));
}

void ControlPage::addHeading (const juce::String& text)
{
    // Pad to the end of the current row first, so a group always starts at the
    // left edge rather than halfway across.
    while (! cells_.empty() && static_cast<int> (cells_.size()) % columns_ != 0)
        cells_.push_back ({ Cell::Kind::gap, 0 });

    auto heading = std::make_unique<Heading>();

    heading->label.setText (text, juce::dontSendNotification);
    heading->label.setJustificationType (juce::Justification::centredLeft);
    heading->label.setColour (juce::Label::textColourId, palette_.accent);
    heading->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (heading->label);

    cells_.push_back ({ Cell::Kind::heading, static_cast<int> (headings_.size()) });
    headings_.push_back (std::move (heading));

    // A heading owns its whole row.
    for (int column = 1; column < columns_; ++column)
        cells_.push_back ({ Cell::Kind::gap, 0 });
}

void ControlPage::addGap()
{
    cells_.push_back ({ Cell::Kind::gap, 0 });
}

void ControlPage::setNote (const juce::String& note)
{
    if (note_ == note)
        return;

    note_ = note;
    resized();
    repaint();
}

void ControlPage::setControlEnabled (const juce::String& parameterId, bool enabled)
{
    const auto dim = [&] (juce::Label& label)
    {
        label.setColour (juce::Label::textColourId,
                         enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
        label.repaint();
    };

    for (auto& knob : knobs_)
        if (knob->id == parameterId)
        {
            knob->slider.setEnabled (enabled);
            dim (knob->label);
        }

    for (auto& choice : choices_)
        if (choice->id == parameterId)
        {
            choice->box.setEnabled (enabled);
            dim (choice->label);
        }

    for (auto& toggle : toggles_)
        if (toggle->id == parameterId)
        {
            toggle->button.setEnabled (enabled);
            dim (toggle->label);
        }
}

int ControlPage::rowCount() const
{
    return (static_cast<int> (cells_.size()) + columns_ - 1) / columns_;
}

int ControlPage::getPreferredHeight() const
{
    int height = 4;

    for (int row = 0; row < rowCount(); ++row)
    {
        const auto index = static_cast<std::size_t> (row * columns_);

        height += index < cells_.size() && cells_[index].kind == Cell::Kind::heading
                    ? kHeadingHeight : kMinCellHeight;
    }

    return height + (note_.isEmpty() ? 0 : kNoteHeight);
}

void ControlPage::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    if (note_.isEmpty() || noteArea_.isEmpty())
        return;

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (12.0f));

    // Four lines, and the box is sized for four. These notes carry the measured
    // numbers behind each control -- a note silently cut in half is worse than
    // no note, because the half that survives still reads as a whole sentence.
    g.drawFittedText (note_, noteArea_, juce::Justification::centredTop, 4, 1.0f);
}

void ControlPage::resized()
{
    if (cells_.empty())
        return;

    auto bounds = getLocalBounds().reduced (4, 2);

    // The note gets its space reserved before the grid takes any, rather than
    // living on whatever is left over.
    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (6, 0);
    else
        noteArea_ = {};

    const int rows = rowCount();
    const int cellWidth = bounds.getWidth() / columns_;

    // Headings are a fixed height whatever the window is, so the knob rows get
    // everything left over between them.
    int headingRows = 0;

    for (int row = 0; row < rows; ++row)
    {
        const auto index = static_cast<std::size_t> (row * columns_);

        if (index < cells_.size() && cells_[index].kind == Cell::Kind::heading)
            ++headingRows;
    }

    const int knobRows = juce::jmax (1, rows - headingRows);
    const int available = bounds.getHeight() - headingRows * kHeadingHeight;

    const int cellHeight = juce::jlimit (kMinCellHeight, kMaxCellHeight, available / knobRows);

    int y = bounds.getY();

    for (int row = 0; row < rows; ++row)
    {
        const auto first = static_cast<std::size_t> (row * columns_);
        const bool isHeading = first < cells_.size() && cells_[first].kind == Cell::Kind::heading;
        const int height = isHeading ? kHeadingHeight : cellHeight;

        for (int column = 0; column < columns_; ++column)
        {
            const auto index = first + static_cast<std::size_t> (column);

            if (index >= cells_.size())
                break;

            juce::Rectangle<int> cell { bounds.getX() + column * cellWidth, y, cellWidth, height };

            switch (cells_[index].kind)
            {
                case Cell::Kind::knob:
                {
                    auto& knob = *knobs_[static_cast<std::size_t> (cells_[index].index)];
                    knob.label.setBounds (cell.removeFromTop (kLabelHeight));
                    knob.slider.setBounds (cell.reduced (3, 0));
                    break;
                }
                case Cell::Kind::choice:
                {
                    auto& choice = *choices_[static_cast<std::size_t> (cells_[index].index)];
                    choice.label.setBounds (cell.removeFromTop (kLabelHeight));
                    choice.box.setBounds (cell.withSizeKeepingCentre (
                        juce::jmin (cell.getWidth() - 10, 152), 26));
                    break;
                }
                case Cell::Kind::toggle:
                {
                    auto& toggle = *toggles_[static_cast<std::size_t> (cells_[index].index)];
                    toggle.label.setBounds (cell.removeFromTop (kLabelHeight));
                    toggle.button.setBounds (cell.withSizeKeepingCentre (30, 30));
                    break;
                }
                case Cell::Kind::heading:
                {
                    auto& heading = *headings_[static_cast<std::size_t> (cells_[index].index)];
                    heading.label.setBounds (juce::Rectangle<int> { bounds.getX() + 6, y,
                                                                    bounds.getWidth() - 12, height });
                    break;
                }
                case Cell::Kind::gap:
                    break;
            }
        }

        y += height;
    }
}

// ---------------------------------------------------------------------------
// StepStrip
// ---------------------------------------------------------------------------

StepStrip::StepStrip (juce::AudioProcessorValueTreeState& state, ui::Palette palette)
    : palette_ (palette)
{
    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
    {
        auto& slider = sliders_[static_cast<std::size_t> (step)];

        slider.setSliderStyle (juce::Slider::LinearBarVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::trackColourId, palette_.accent.withAlpha (0.85f));
        slider.setColour (juce::Slider::backgroundColourId, palette_.panel.darker (0.4f));
        slider.setTooltip ("Step " + juce::String (step + 1)
                             + ". Bipolar: the centre is no modulation, and either end is full "
                               "depth in that direction. Drag; double-click to centre.");
        addAndMakeVisible (slider);

        attachments_[static_cast<std::size_t> (step)]
          = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, ids::step (step), slider);
    }

    setTooltip ("The sixteen steps, drawn as a pattern rather than as sixteen knobs -- the shape "
                "is the thing being edited. Point a global slot at Comb time and this draws the "
                "comb sweep in time with the track, which is the thing an automation lane cannot "
                "do. The playing step is lit.");
}

void StepStrip::setPlaying (int step, int length)
{
    if (step == playing_ && length == length_)
        return;

    playing_ = step;
    length_ = length;

    repaint();
}

void StepStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 4.0f);

    const float slotWidth = bounds.getWidth() / dsp::StepSequencer::kMaxSteps;

    // The centre line: a bipolar fader with no zero mark is a fader whose
    // neutral position has to be guessed.
    g.setColour (palette_.dimText.withAlpha (0.30f));
    g.drawHorizontalLine (juce::roundToInt (bounds.getCentreY()),
                          bounds.getX() + 2.0f, bounds.getRight() - 2.0f);

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
    {
        const auto slot = bounds.withWidth (slotWidth)
                                .withX (bounds.getX() + slotWidth * static_cast<float> (step));

        // Steps past the pattern's length are dimmed rather than hidden: the
        // values are still there and shortening the pattern is reversible, so
        // hiding them would lose an edit the player expects to get back.
        if (step >= length_)
        {
            g.setColour (palette_.background.withAlpha (0.55f));
            g.fillRect (slot.reduced (1.0f, 1.0f));
        }

        if (step == playing_)
        {
            g.setColour (palette_.secondary.withAlpha (0.28f));
            g.fillRect (slot.reduced (1.0f, 1.0f));

            g.setColour (palette_.secondary);
            g.drawRect (slot.reduced (1.0f, 1.0f), 1.2f);
        }
    }
}

void StepStrip::resized()
{
    auto bounds = getLocalBounds().reduced (2);

    const int slotWidth = bounds.getWidth() / dsp::StepSequencer::kMaxSteps;

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
        sliders_[static_cast<std::size_t> (step)].setBounds (
            juce::Rectangle<int> { bounds.getX() + slotWidth * step, bounds.getY(),
                                   slotWidth, bounds.getHeight() }.reduced (2, 2));
}

// ---------------------------------------------------------------------------
// TuningPage
// ---------------------------------------------------------------------------

TuningPage::TuningPage (SonitusProcessor& processorToUse, ui::Palette palette)
    : sonitus_ (processorToUse), palette_ (palette)
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
        "rather than the octave, and the Carlos scales do not repeat at an octave at all. "
        "With the comb key-tracked, a harmonic scale makes the instrument agree with itself -- "
        "intervals beat at the rate the comb is combing at, instead of against it.");

    {
        int id = 1;

        for (const auto& scale : dsp::scales::all())
            scaleBox_.addItem (juce::String (scale.name), id++);
    }

    scaleBox_.onChange = [this]
    {
        if (updating_ || scaleBox_.getSelectedId() <= 0)
            return;

        const auto reason = sonitus_.selectBuiltInScale (scaleBox_.getText());

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
        sonitus_.resetTuning();
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
    explanationLabel_.setFont (juce::FontOptions (12.0f));
    explanationLabel_.setJustificationType (juce::Justification::topLeft);
    explanationLabel_.setText (
        "Microtuning is here rather than bolted on because the comb key-tracks onto harmonics of "
        "the played note. In twelve-tone equal temperament a major third is fourteen cents sharp "
        "of the real 5/4 and beats against its own comb; in just intonation it does not, and a "
        "sustained chord locks instead of churning. The difference is large on a bass.\n\n"
        "The scale travels with the project: the .scl text is saved into the plugin's state, so "
        "a session opened on another machine is in tune without the file. Detune and glide stay "
        "in cents -- they are a spread around a pitch, not a scale degree.",
        juce::dontSendNotification);
    addAndMakeVisible (explanationLabel_);

    errorLabel_.setColour (juce::Label::textColourId, palette_.over);
    errorLabel_.setFont (juce::FontOptions (12.0f));
    errorLabel_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (errorLabel_);

    refresh();
}

void TuningPage::refresh()
{
    descriptionLabel_.setText (sonitus_.describeTuning(), juce::dontSendNotification);

    const auto name = sonitus_.getScaleName();

    // The box follows the processor rather than the other way round, so a state
    // load from the host shows the scale that is actually playing. Guarded, or
    // restoring the selection would fire `onChange` and re-select it.
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

void TuningPage::reportFailure (const juce::String& what, const juce::String& reason)
{
    errorLabel_.setText ("Could not load that " + what + ". " + reason
                           + "\nThe tuning is unchanged.",
                         juce::dontSendNotification);
}

void TuningPage::loadScaleFile()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Load a Scala scale", juce::File {}, "*.scl");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (! file.existsAsFile())
            return;

        const auto reason = sonitus_.loadScalaText (file.loadFileAsString(),
                                                    file.getFileNameWithoutExtension());

        if (reason.isNotEmpty())
            reportFailure ("scale", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    });
}

void TuningPage::loadKeyboardMapFile()
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

        const auto reason = sonitus_.loadKeyboardMapText (file.loadFileAsString());

        if (reason.isNotEmpty())
            reportFailure ("keyboard map", reason);
        else
            errorLabel_.setText ({}, juce::dontSendNotification);

        refresh();
    });
}

void TuningPage::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void TuningPage::resized()
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

    bounds.removeFromTop (10);
    descriptionLabel_.setBounds (bounds.removeFromTop (38));

    bounds.removeFromTop (6);
    errorLabel_.setBounds (bounds.removeFromTop (34));

    bounds.removeFromTop (6);
    explanationLabel_.setBounds (bounds);
}

// ---------------------------------------------------------------------------
// SonitusEditor
// ---------------------------------------------------------------------------

SonitusEditor::SonitusEditor (SonitusProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      sonitus_ (processorToUse),
      palette_ (kPalette),
      outputMeter_ (std::make_unique<ui::LevelMeter> (kPalette))
{
    // No bypass parameter: an instrument that is bypassed is an instrument that
    // is silent, which is what muting the track already does. The header takes
    // a null id and simply leaves the button out.
    header_ = std::make_unique<ui::HeaderBar> (
        sonitus_.getState(), "SONITUS",
        "Growl and reese instrument", nullptr, palette_);

    header_->onSwapRequested = [this]
    {
        sonitus_.getAbCompare().swapSlots();
        header_->setActiveSlot (sonitus_.getAbCompare().isSlotB());
        header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());
    };

    header_->onCopyRequested = [this]
    {
        sonitus_.getAbCompare().copyToOtherSlot();
        header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());
    };

    header_->setActiveSlot (sonitus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());
    addAndMakeVisible (*header_);

    buildPages();

    viewport_.setScrollBarsShown (true, false);
    viewport_.setColour (juce::ScrollBar::thumbColourId, palette_.accent.withAlpha (0.5f));
    addAndMakeVisible (viewport_);

    steps_ = std::make_unique<StepStrip> (sonitus_.getState(), palette_);
    tuning_ = std::make_unique<TuningPage> (sonitus_, palette_);

    static const char* tabNames[kNumPages] { "OSC", "FILTER", "ENV", "MOD", "MANGLE", "TUNING" };

    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (tabNames[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    // An instrument's output has no ceiling parameter to measure against, so
    // the reference is 0 dBFS and "over" means over full scale.
    outputMeter_->setReferenceDb (0.0f);
    outputMeter_->setScaleVisible (true);
    outputMeter_->setTooltip (
        "Output level. The number is the worst peak since it was last cleared -- click to clear. "
        "The bar is VU-ballistic and the number is peak, and on a reese those differ by ten "
        "decibels or more: the bar says how loud it sounds and the number says whether it is "
        "clipping the converter.");
    addAndMakeVisible (*outputMeter_);

    outputMeterLabel_.setJustificationType (juce::Justification::centred);
    outputMeterLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    outputMeterLabel_.setFont (juce::FontOptions (10.0f));
    addAndMakeVisible (outputMeterLabel_);

    statusLabel_.setJustificationType (juce::Justification::centred);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel_);

    showPage (0);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (1020, 720);

    startTimerHz (30);
}

void SonitusEditor::buildPages()
{
    auto& state = sonitus_.getState();

    // ---- OSC -----------------------------------------------------------------

    auto& osc = pages_[0];
    osc = std::make_unique<ControlPage> (state, palette_, 6);

    const auto addOscillator = [] (ControlPage& page, const char* shapeId, const char* octaveId,
                                   const char* semitoneId, const char* centsId, const char* widthId,
                                   const char* levelId, const char* unisonId, const char* detuneId,
                                   const char* spreadId, const char* driftId, const juce::String& which)
    {
        page.addChoice (shapeId, "Shape",
            "Saw is the dense one and where a reese starts -- every harmonic present, which is "
            "what gives a comb something to cut. Pulse is hollow and its Width knob sweeps which "
            "harmonics survive. Triangle is soft, Sine has nothing above the fundamental and is "
            "for sub and for driving PM.");

        page.addKnob (octaveId, "Octave", "Whole octaves, -3 to +3.");
        page.addKnob (semitoneId, "Semis", "Semitones, -24 to +24. Snapped, so an interval stays an interval.");
        page.addKnob (centsId, "Fine",
            "Cents. Two oscillators a few cents apart beat, and the beating *is* a comb whose "
            "notches sweep at the difference frequency -- which is the whole reese, before the "
            "flanger is even switched on.");

        page.addKnob (widthId, "Width",
            "Pulse width, and the triangle's skew. At 50% a pulse is a square and has only odd "
            "harmonics; away from it the even ones come in. Modulate it for the classic PWM "
            "shimmer -- it is in the voice matrix as Width " + which + ".");

        page.addKnob (levelId, "Level", "How much of this oscillator reaches the mix.");

        page.addKnob (unisonId, "Unison",
            "How many copies, 1 to 7. Seven each on both oscillators with eight voices down is "
            "112 oscillators, which is the CPU number to keep an eye on: three is usually thicker "
            "than it sounds and costs less than half.");

        page.addKnob (detuneId, "Detune",
            "How far the copies spread, in cents. This is the comb that costs nothing -- the "
            "notches move at the beat rate and there is no delay line involved.");

        page.addKnob (spreadId, "Spread",
            "How far the copies spread across the stereo field. The centre copy stays centred, "
            "so the mono sum keeps its fundamental.");

        page.addKnob (driftId, "Drift",
            "Slow random wander on each copy's pitch, in cents. What an analogue oscillator bank "
            "does because its components are warm and imperfect. A little is life; a lot is a "
            "broken machine, which is occasionally what you want.");
    };

    osc->addHeading ("OSCILLATOR A -- the sync master");
    addOscillator (*osc, ids::shapeA, ids::octaveA, ids::semitonesA, ids::centsA, ids::widthA,
                   ids::levelA, ids::unisonA, ids::detuneA, ids::spreadA, ids::driftA, "A");

    osc->addHeading ("OSCILLATOR B -- the sync slave and the PM target");
    addOscillator (*osc, ids::shapeB, ids::octaveB, ids::semitonesB, ids::centsB, ids::widthB,
                   ids::levelB, ids::unisonB, ids::detuneB, ids::spreadB, ids::driftB, "B");

    osc->addToggle (ids::syncB, "Sync B",
        "Hard sync: B's phase is reset every time the played note's period comes round, so B's "
        "own pitch stops being a pitch and becomes a formant -- a peak in the spectrum that "
        "sweeps when you sweep B. This is the Pro-53 sound, and it is worth nothing standing "
        "still: put an envelope on Pitch B in the matrix and sweep it.");

    osc->addKnob (ids::pmIndex, "PM",
        "Phase modulation of B by A. Frequency modulation's better-behaved sibling -- the same "
        "sidebands with no DC drift, which is why every FM synth since the DX7 has actually been "
        "a PM synth. At small amounts it thickens; past about 2 it is a different instrument.");

    osc->addHeading ("SUB AND DESTRUCTION");

    osc->addChoice (ids::subShape, "Sub shape",
        "Sine is pure weight and disappears on a laptop; square has odd harmonics that carry it "
        "through a small speaker.");

    osc->addKnob (ids::subOctave, "Sub oct", "One or two octaves below the played note.");
    osc->addKnob (ids::subLevel, "Sub",
        "The sub oscillator's level. It is generated in the voice and then taken *out* of the "
        "mangle by the split, so nothing downstream can smear it.");

    osc->addKnob (ids::ringAmount, "Ring",
        "Ring modulation: A times B, which produces the sum and difference of every pair of "
        "their harmonics and almost nothing at either original pitch. Inharmonic on purpose -- "
        "put B a fifth up for metal, an octave up for something still tuned.");

    osc->addKnob (ids::foldAmount, "Fold",
        "A sine wave folder. Past full scale the transfer curve turns round and comes back, so "
        "the harder you push the more harmonics appear -- the opposite of a clipper, which runs "
        "out. Antialiased, and at full fold it is the widest-band thing in the instrument: this "
        "is the one control that genuinely wants x8 oversampling.");

    // ---- FILTER --------------------------------------------------------------

    auto& filter = pages_[1];
    filter = std::make_unique<ControlPage> (state, palette_, 4);

    filter->addHeading ("FILTER -- zero-delay state variable, drive inside the loop");

    filter->addChoice (ids::filterMode, "Mode",
        "Lowpass is the reese's shape. Bandpass throws the fundamental away and leaves the growl. "
        "Highpass and notch are there because the filter is a state-variable and they cost "
        "nothing.");

    filter->addKnob (ids::cutoff, "Cutoff",
        "Where the filter sits. The corner lands where theory puts it at every sample rate -- "
        "the prewarp guarantees that much. What it does not guarantee is the whole curve: a "
        "discrete filter's response is symmetric about Nyquist and Nyquist moves, so two octaves "
        "above a 4 kHz corner the 48 kHz and 192 kHz curves differ by 10.6 dB. Inside the "
        "oversampled section, which this is, that difference is pushed out of the audible band.");

    filter->addKnob (ids::resonance, "Resonance",
        "Q, on a geometric law: 0.5 at nothing and 500 at full, which is 15 dB of peak per quarter "
        "turn all the way up. A linear law would put Q at 1.0 halfway and cram twenty decibels "
        "into the last one percent of the travel.");

    filter->addKnob (ids::filterDrive, "Drive",
        "Overdrives the filter's own integrators, which is what a ladder does when you push it. "
        "The rail is fixed rather than falling with drive, so this adds harmonics instead of "
        "acting as a volume control -- with a matching trim behind it, the level barely moves.");

    filter->addKnob (ids::filterTrack, "Key track",
        "How far the cutoff follows the played note. At 100% a note two octaves up gets a cutoff "
        "two octaves up, so the timbre is constant across the keyboard. At 0 the filter is a fixed "
        "formant and the low notes are darker, which is usually what a bass wants.");

    filter->addKnob (ids::filterFm,  "FM",
        "Oscillator A modulating the cutoff at audio rate. Not a wobble: at these speeds the "
        "modulation makes sidebands of its own and the filter becomes part of the oscillator. "
        "Small amounts add a metallic edge, large amounts are chaos.");

    filter->addKnob (ids::filterVel, "Velocity",
        "How far velocity opens the filter. The standard expressive link, and the reason a "
        "programmed bassline can breathe.");

    filter->addHeading ("KEYBOARD");

    filter->addChoice (ids::keyMode, "Mode",
        "Poly is many notes. Mono retriggers the envelopes on every note; Legato does not, so a "
        "phrase played without gaps glides through one envelope -- which is what makes a bassline "
        "sound played rather than typed. A reese is one voice: mono costs a fourteenth of poly.");

    filter->addKnob (ids::polyphony, "Voices",
        "How many notes at once, up to eight. Stealing takes a free voice first, then the "
        "quietest released one, then the oldest held one -- so a held chord survives a passing "
        "melody.");

    filter->addKnob (ids::glide, "Glide",
        "How long a slide from one note to the next takes. In Legato it only happens between "
        "overlapping notes, which is how a glide becomes a performance control.");

    filter->addKnob (ids::bendRange, "Bend",
        "How far the pitch wheel reaches, in semitones.");

    // ---- ENV -----------------------------------------------------------------

    auto& env = pages_[2];
    env = std::make_unique<ControlPage> (state, palette_, 6);

    const auto addEnvelope = [] (ControlPage& page, const char* attackId, const char* decayId,
                                 const char* sustainId, const char* releaseId, const char* shapeId)
    {
        page.addKnob (attackId, "Attack", "How long from nothing to full.");
        page.addKnob (decayId, "Decay", "How long from full down to the sustain level.");
        page.addKnob (sustainId, "Sustain", "Where it holds while the key is down.");
        page.addKnob (releaseId, "Release", "How long it takes to fall away after the key is up.");
        page.addKnob (shapeId, "Shape",
            "How curved each segment is. At zero it is nearly a straight line -- which sounds "
            "mechanical, because nothing physical decays linearly. Turned up it is the sharp "
            "exponential of a capacitor discharging, which is what every analogue envelope does "
            "and what the ear expects.");
    };

    env->addHeading ("AMPLITUDE");
    addEnvelope (*env, ids::ampAttack, ids::ampDecay, ids::ampSustain, ids::ampRelease, ids::ampShape);

    env->addKnob (ids::ampVelocity, "Velocity",
        "How much of the level comes from how hard the note was played.");

    env->addHeading ("MOD ENVELOPE 1 -- point it at something in MOD");
    addEnvelope (*env, ids::env1Attack, ids::env1Decay, ids::env1Sustain, ids::env1Release, ids::env1Shape);

    env->addHeading ("MOD ENVELOPE 2");
    addEnvelope (*env, ids::env2Attack, ids::env2Decay, ids::env2Sustain, ids::env2Release, ids::env2Shape);

    // ---- MOD -----------------------------------------------------------------

    auto& mod = pages_[kModPage];
    mod = std::make_unique<ControlPage> (state, palette_, 6);

    mod->addHeading ("SOURCES");

    mod->addChoice (ids::lfo1Wave, "LFO 1", "Its shape. Sample & hold steps; smooth random glides.");
    mod->addKnob (ids::lfo1Rate, "Rate 1",
        "How fast, in hertz. **Zero is a legitimate setting and is the brief's original trick** "
        "-- the rate pinned at nothing so the depth is drawn from somewhere else entirely. Here "
        "that somewhere else is the sequencer below, or the host's automation on the depth.");
    mod->addKnob (ids::lfo1Smooth, "Smooth 1",
        "Rounds the corners off a square or a sample-and-hold, so a step becomes a slide.");

    mod->addChoice (ids::lfo2Wave, "LFO 2", "Its shape.");
    mod->addKnob (ids::lfo2Rate, "Rate 2", "How fast, in hertz.");
    mod->addKnob (ids::lfo2Smooth, "Smooth 2", "Rounds its corners off.");

    mod->addKnob (ids::seqRate, "Seq rate",
        "Steps per beat. With the transport running the pattern locks to it, so a sixteen-step "
        "figure at 4 per beat is exactly one bar. Stopped, it free-runs at the same speed.");

    mod->addKnob (ids::seqLength, "Steps",
        "How many of the sixteen are in the pattern. The rest are dimmed rather than hidden -- "
        "their values are still there when you lengthen it again.");

    mod->addKnob (ids::seqGlide, "Seq glide",
        "How much of each step is spent sliding to the next. At 0 it steps; at 1 it never holds "
        "still and the pattern is a curve rather than a staircase.");

    mod->addKnob (ids::seqToLfoRate, "Seq to rate",
        "The old automation trick, wired in: the sequencer steps LFO 1's *rate* through a pattern "
        "of speeds, in octaves. With LFO 1 on the cutoff this is a wobble that changes tempo on "
        "the step, which is the thing that used to take an automation lane and a steady hand.");

    mod->addHeading ("VOICE MATRIX -- one set of these per sounding note");

    for (int slot = 0; slot < VoiceParameters::kSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);

        mod->addChoice (ids::modSource (slot), "Src " + number,
            "What drives slot " + number + ". These are the per-note sources: each voice has its "
            "own envelopes, its own velocity and its own note-on random, so eight notes modulate "
            "eight different ways.");

        mod->addChoice (ids::modDest (slot), "To " + number,
            "What slot " + number + " drives. Continuous controls only -- a switch reconfigures "
            "rather than adjusts, and modulating one would mean rebuilding a filter every chunk.");

        mod->addKnob (ids::modDepth (slot), "Depth " + number,
            "How much, and which way. The depth is stored as a percentage and scaled into each "
            "destination's own units, so full depth on Cutoff is six octaves and full depth on "
            "Pitch is two.");
    }

    mod->addHeading ("GLOBAL MATRIX -- one chain, shared by every note");

    for (int slot = 0; slot < EngineParameters::kGlobalSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);

        mod->addChoice (ids::globalSource (slot), "Src " + number,
            "What drives global slot " + number + ". Only the sources that exist once rather than "
            "once per note: an amp envelope has no single value when eight notes are down.");

        mod->addChoice (ids::globalDest (slot), "To " + number,
            "What global slot " + number + " drives. **Comb time is the one this instrument was "
            "built for** -- the brief's flanger-at-rate-zero with a sequencer or an LFO behind it "
            "instead of a hand on a fader.");

        mod->addKnob (ids::globalDepth (slot), "Depth " + number,
            "How much, and which way. Comb time and Phase centre move in octaves -- three and "
            "four at full depth -- because a delay and a filter centre are pitches in disguise. "
            "Tube and Output move by 24 dB.");
    }

    // ---- MANGLE --------------------------------------------------------------

    auto& mangle = pages_[4];
    mangle = std::make_unique<ControlPage> (state, palette_, 5);

    mangle->addHeading ("THE SPLIT -- the sub bypasses everything below");

    mangle->addKnob (ids::splitHz, "Split",
        "Where the sub is taken out of the mangle. Below this the signal gets a DC blocker and "
        "nothing else -- no tube, no comb, no formant. This is inside the instrument rather than "
        "three plugins later because it is what makes the rest of the page usable on a real "
        "track: you can destroy the body without touching the weight.");

    mangle->addToggle (ids::subMono, "Sub mono",
        "Sums the sub band to mono. On by default: a wide sub is the single most common way to "
        "lose a bass on a club system, where the two sides are summed in the amplifier and "
        "anything out of phase cancels.");

    mangle->addChoice (ids::order, "Order",
        "Where the tube sits relative to the comb, and it is two different instruments. Comb "
        "first: the tube generates harmonics of a signal that already has holes in it, and the "
        "holes stay holes -- tuned and hollow. Tube first: the tube fills the notches with "
        "harmonics it made itself and the comb then cuts those too, which is denser and less "
        "tuned. The same distinction as a tone stack in front of a distortion or behind it.");

    mangle->addKnob (ids::tubeDrive, "Tube",
        "A triode stage, straight from Anvil. Its grid conducts on the positive half and blocks, "
        "so the operating point drifts under load -- which is why the hundredth bar sounds unlike "
        "the first. At 0 dB it is bit-exactly out of the path.");

    mangle->addKnob (ids::tilt, "Tilt",
        "One knob of tone: two shelves moving in opposite directions about 700 Hz. A balance "
        "rather than a boost -- it moves where the energy sits rather than how much there is.");

    mangle->addHeading ("THE COMB -- what this instrument is for");

    mangle->addChoice (ids::combMode, "Comb",
        "Flange is a delay: every frequency is shifted by the same *time*, so the notches are "
        "evenly spaced and it rings and sounds metallic. Phase is an allpass cascade: every "
        "frequency is shifted by a different *phase*, so the notches bunch and it sounds vocal "
        "and does not ring. Two topologies, one control surface.");

    mangle->addKnob (ids::combTime, "Time",
        "The delay, and the first notch sits at 1/(2 x time). Point a global slot at this and it "
        "sweeps -- that is the whole instrument. Three octaves at full depth.");

    mangle->addKnob (ids::combTrack, "Key track",
        "Pulls the delay onto the played note's period, so the notches land on that note's own "
        "harmonics. The growl comes out *tuned* rather than random, which is most of why a "
        "well-made growl sits in a mix instead of fighting it. Anywhere between free and locked.");

    mangle->addKnob (ids::combFeed, "Feedback",
        "How much comes back round, and **negative is the invert-feedback switch** as a "
        "continuous control -- it moves the whole notch pattern by half a spacing, which is a "
        "different sound rather than a smaller one. Capped below unity, so it cannot run away.");

    mangle->addKnob (ids::combDamp, "Damp",
        "A lowpass inside the feedback loop, so each pass round is darker than the last. Takes "
        "the glassiness off a high-feedback setting.");

    mangle->addKnob (ids::combSpread, "Spread",
        "Offsets the two channels' delays. What makes a flanger wide -- and what makes it "
        "collapse in mono, so watch the correlation if the track is going to a club.");

    mangle->addKnob (ids::combMix, "Mix",
        "Dry against combed. At 0 the comb is bit-exactly out of the path, not merely quiet.");

    mangle->addToggle (ids::combInvert, "Invert wet",
        "Flips the combed signal's polarity, which turns the notches into peaks and the peaks "
        "into notches. On a feedforward comb it also halves the spacing -- the first notch moves "
        "from 1/(2T) to 1/T.");

    mangle->addKnob (ids::phaseFreq, "Phase centre",
        "Where the allpass cascade's notches are bunched. Only in Phase mode.");

    mangle->addKnob (ids::phaseStages, "Stages",
        "How many allpass sections, 2 to 16. Each pair makes one notch, so 8 stages is 4 notches. "
        "Only in Phase mode.");

    mangle->addHeading ("VOWEL AND OUTPUT");

    mangle->addKnob (ids::formantMorph, "Vowel",
        "Morphs across ee - eh - ah - oh - oo. Three resonant peaks at the frequencies a human "
        "tract actually puts them, from Peterson and Barney's measurements of adult male vowels. "
        "The same idea as the comb, shaped like a mouth.");

    mangle->addKnob (ids::formantSharp, "Sharpness",
        "How narrow the three peaks are. The gain is divided by the Q, so this sharpens the "
        "vowel rather than turning it up.");

    mangle->addKnob (ids::formantMix, "Vowel mix",
        "Dry against vowelled. At 0 the formant filter is bit-exactly out of the path.");

    mangle->addHeading ("OVERTONE -- the same key tracking, on the vowel");

    mangle->addKnob (ids::formantLock, "Harmonic lock",
        "Pulls the three resonances off the vowel and onto **harmonics of the played note**. "
        "This is what overtone singing is: not a second voice, but one source with a resonance "
        "sharp enough to pick a single partial out of the drone and make it a melody. Because it "
        "can only land on a harmonic, it is always in tune with the bass under it.\n\n"
        "The lock sharpens as it engages -- selecting one partial takes a bandwidth of about "
        "1.6 Hz where a spoken vowel has eighty. At 0 the vowel is bit-exactly untouched.");

    mangle->addKnob (ids::formantHarmonic, "Harmonic",
        "Which partial the lock selects, counting the fundamental as 1. Continuous, because it is "
        "a modulation destination -- point the sequencer at Harmonic in the global matrix and the "
        "overtone line walks the series in time with the track. Partials 6 to 12 are where sygyt "
        "actually sings.");

    mangle->addKnob (ids::formantNotch, "Notch",
        "The anti-formant. A nasal is not a vowel with different peaks -- it is a vowel with a "
        "**zero**: the nasal cavity is a side branch, and a side branch cancels rather than "
        "resonates. That is what a vowel filter with only peaks cannot make, and why none of them "
        "can say \"m\" or the ending of a chanted \"AUM\".\n\n"
        "Set aside from the vocal reading, it is simply a hole you can put anywhere in the growl.");

    mangle->addKnob (ids::formantNotchDepth, "Notch depth",
        "How deep the hole goes -- 26.6 dB at the centre when full, and localised: two octaves "
        "away it is within 3 dB of untouched. At 0 it is bit-exactly out of the path.");

    mangle->addKnob (ids::output, "Output",
        "Trim, after everything. Defaults to -6 dB because an instrument with unison and a tube "
        "can comfortably exceed full scale, and clipping the host's bus is not a feature.");

    mangle->addChoice (ids::oversampling, "Oversampling",
        "How much headroom the nonlinear stages get. Auto targets about 192 kHz internally and "
        "reads your session's rate to decide. See the note below for what it is doing right now.");
}

void SonitusEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        const bool active = i == currentPage_;

        tab.setColour (juce::TextButton::buttonColourId,
                       active ? palette_.accent.withAlpha (0.55f) : palette_.panel.brighter (0.08f));
        tab.setColour (juce::TextButton::textColourOffId, active ? palette_.text : palette_.dimText);
    }

    // `false`: the viewport must not take ownership -- the pages outlive the
    // page changes and are owned by the array.
    viewport_.setViewedComponent (pages_[static_cast<std::size_t> (currentPage_)].get(), false);
    viewport_.setVisible (currentPage_ != kTuningPage);

    // The step strip belongs to the MOD page and the tuning panel is its own
    // page, so both follow the tab rather than being always on screen.
    if (steps_ != nullptr)
        steps_->setVisible (currentPage_ == kModPage);

    if (tuning_ != nullptr)
    {
        tuning_->setVisible (currentPage_ == kTuningPage);

        if (currentPage_ == kTuningPage)
            tuning_->refresh();
    }

    resized();
}

void SonitusEditor::updateForSwitches()
{
    auto& state = sonitus_.getState();

    const auto index = [&state] (const char* id)
    {
        return static_cast<int> (std::lround (state.getRawParameterValue (id)->load()));
    };

    const int combMode = index (ids::combMode);
    const int keyMode = index (ids::keyMode);
    const int oversample = index (ids::oversampling);
    const int syncB = index (ids::syncB);
    const int shapeA = index (ids::shapeA);
    const int shapeB = index (ids::shapeB);
    const int latency = sonitus_.isPrepared() ? sonitus_.getLatencySamples() : -1;

    // The notch moves continuously, so it is rounded before being compared --
    // otherwise the note is rebuilt thirty times a second while anything sweeps
    // and the page repaints for nothing.
    const int notch = juce::roundToInt (sonitus_.getCombNotchHz());

    const auto scale = sonitus_.getScaleName();

    if (combMode == shownCombMode_ && keyMode == shownKeyMode_ && oversample == shownOversample_
        && latency == shownLatency_ && syncB == shownSyncB_ && shapeA == shownShapeA_
        && shapeB == shownShapeB_ && notch == shownNotch_ && scale == shownScale_)
        return;

    const bool combChanged = combMode != shownCombMode_;
    const bool shapesChanged = shapeA != shownShapeA_ || shapeB != shownShapeB_
                            || syncB != shownSyncB_;
    const bool scaleChanged = scale != shownScale_;

    shownCombMode_ = combMode;
    shownKeyMode_ = keyMode;
    shownOversample_ = oversample;
    shownLatency_ = latency;
    shownSyncB_ = syncB;
    shownShapeA_ = shapeA;
    shownShapeB_ = shapeB;
    shownNotch_ = notch;
    shownScale_ = scale;

    if (combChanged)
    {
        // Greyed rather than hidden: a knob that moves and does nothing reads
        // as a broken plugin rather than as a mode.
        const bool isFlange = combMode == static_cast<int> (CombMode::flange);
        const bool isPhase = combMode == static_cast<int> (CombMode::phase);
        const bool anyComb = isFlange || isPhase;

        for (const char* id : { ids::combTime, ids::combTrack, ids::combDamp })
            pages_[4]->setControlEnabled (id, isFlange);

        for (const char* id : { ids::phaseFreq, ids::phaseStages })
            pages_[4]->setControlEnabled (id, isPhase);

        for (const char* id : { ids::combFeed, ids::combSpread, ids::combMix, ids::combInvert })
            pages_[4]->setControlEnabled (id, anyComb);
    }

    if (shapesChanged)
    {
        // Width does nothing to a saw or a sine: both are fully described
        // without it.
        const auto hasWidth = [] (int shape)
        {
            return shape == static_cast<int> (dsp::OscShape::pulse)
                || shape == static_cast<int> (dsp::OscShape::triangle);
        };

        pages_[0]->setControlEnabled (ids::widthA, hasWidth (shapeA));
        pages_[0]->setControlEnabled (ids::widthB, hasWidth (shapeB));
    }

    if (scaleChanged && tuning_ != nullptr)
        tuning_->refresh();

    pages_[4]->setNote (sonitus_.describeComb() + "  " + sonitus_.describeOversampling());

    pages_[0]->setNote (syncB != 0
        ? juce::String ("Sync is on: B restarts every time the note's period comes round, so its "
                        "Semis and Fine knobs are sweeping a formant rather than setting a pitch. "
                        "Put a mod envelope on Pitch B in the matrix -- standing still it is just "
                        "a bright waveform.")
        : juce::String ("Two dense sources, and the denser the better: a comb can only cut "
                        "harmonics that are there. Saw plus saw a few cents apart is already a "
                        "moving comb before anything on the MANGLE page is switched on."));

    pages_[1]->setNote (keyMode == static_cast<int> (KeyboardMode::poly)
        ? juce::String ("Poly. Eight voices with seven-way unison on both oscillators is 112 "
                        "oscillators -- the number to watch. A reese does not need any of it: "
                        "switch to Mono and spend the CPU on oversampling instead.")
        : juce::String ("Mono, so the whole instrument is one voice and the CPU is free. Legato "
                        "differs in one thing and it matters: it does not retrigger the envelopes, "
                        "so a phrase played without gaps runs through a single envelope and glides "
                        "between its notes."));
}

void SonitusEditor::timerCallback()
{
    auto& meters = sonitus_.getMeterValues();

    outputMeter_->setValues (meters.outputVuDb.load (std::memory_order_relaxed),
                             meters.outputPeakDb.load (std::memory_order_relaxed));
    outputMeter_->repaint();

    if (steps_ != nullptr && steps_->isVisible())
    {
        const int length = static_cast<int> (std::lround (
            sonitus_.getState().getRawParameterValue (ids::seqLength)->load()));

        steps_->setPlaying (sonitus_.getSequencerStep(), length);
    }

    const double rate = sonitus_.getSampleRate() > 0.0 ? sonitus_.getSampleRate() : 48000.0;

    juce::String status;
    status << "VOICES " << sonitus_.getActiveVoiceCount()
           << "   \xe2\x80\xa2   " << sonitus_.getScaleName()
           << "   \xe2\x80\xa2   OUT " << juce::String (
                  meters.outputPeakDb.load (std::memory_order_relaxed), 1) << " dB peak"
           << "   \xe2\x80\xa2   LATENCY ";

    // Until the host has started audio there is no latency figure, only an
    // uninitialised one -- and printing "0 sm" for it says the plugin has none,
    // which is a different claim entirely.
    if (sonitus_.isPrepared())
    {
        const int latency = sonitus_.getLatencySamples();

        status << latency << " sm (" << juce::String (1000.0 * latency / rate, 2) << " ms)";
    }
    else
    {
        status << "--";
    }

    statusLabel_.setText (status, juce::dontSendNotification);

    header_->setActiveSlot (sonitus_.getAbCompare().isSlotB());
    header_->setOtherSlotFilled (sonitus_.getAbCompare().otherSlotFilled());

    updateForSwitches();
}

void SonitusEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);
}

void SonitusEditor::resized()
{
    auto bounds = getLocalBounds();

    header_->setBounds (bounds.removeFromTop (ui::HeaderBar::getPreferredHeight()));

    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 4));

    auto right = bounds.removeFromRight (kMeterWidth + 10).reduced (4, 6);
    outputMeterLabel_.setBounds (right.removeFromBottom (12));
    outputMeter_->setBounds (right);

    auto tabRow = bounds.removeFromTop (kTabHeight).reduced (4, 2);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth).reduced (2, 0));

    auto body = bounds.reduced (4, 2);

    if (steps_ != nullptr && steps_->isVisible())
        steps_->setBounds (body.removeFromBottom (kStepStripHeight).reduced (0, 4));

    if (tuning_ != nullptr && tuning_->isVisible())
        tuning_->setBounds (body);

    viewport_.setBounds (body);

    if (auto* page = pages_[static_cast<std::size_t> (currentPage_)].get())
    {
        // Sized before it is asked how tall it wants to be: the note's height
        // depends on nothing, but the row count does not fit until the width
        // is known, and a page that fits gets the viewport's full height so it
        // centres rather than sitting against the top.
        // Twice, because the two are circular: whether the scroll bar is shown
        // depends on the page's height, and the width the page gets depends on
        // whether the scroll bar is shown. One pass settles it, and the second
        // is what makes the result the same whichever page was on screen
        // before.
        for (int pass = 0; pass < 2; ++pass)
            page->setSize (viewport_.getMaximumVisibleWidth(),
                           juce::jmax (viewport_.getMaximumVisibleHeight(),
                                       page->getPreferredHeight()));
    }
}

} // namespace tezla::sonitus
