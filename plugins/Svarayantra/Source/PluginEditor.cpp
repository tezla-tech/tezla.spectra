// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

namespace tezla::svarayantra {

namespace
{
/// Svarayantra's own colour: saffron-gold against the house dark panel, so
/// the two instruments are never mistaken for one another in a session.
[[nodiscard]] ui::Palette makePalette()
{
    ui::Palette palette;
    palette.accent = juce::Colour (0xffd9a441);
    palette.accentBright = juce::Colour (0xfff3c95e);
    return palette;
}

/// The name in its own script. Written as escaped UTF-8 bytes rather than a
/// literal, so no compiler's source-encoding guess can corrupt it.
[[nodiscard]] juce::String devanagariName()
{
    return juce::String::fromUTF8 (
        "\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xB5\xE0\xA4\xB0"
        "\xE0\xA4\xAF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA4"
        "\xE0\xA5\x8D\xE0\xA4\xB0");
}
} // namespace

// ---------------------------------------------------------------------------
// PresetListModel
// ---------------------------------------------------------------------------

int PresetListModel::getNumRows()
{
    return static_cast<int> (svarayantra_.getPresetChoices().size());
}

void PresetListModel::paintListBoxItem (int row, juce::Graphics& g, int width,
                                        int height, bool rowIsSelected)
{
    const auto& choices = svarayantra_.getPresetChoices();

    if (row < 0 || row >= static_cast<int> (choices.size()))
        return;

    if (rowIsSelected)
    {
        g.setColour (palette_.accent.withAlpha (0.22f));
        g.fillRect (0, 0, width, height);
    }

    const auto& choice = choices[static_cast<std::size_t> (row)];
    const bool sounding = choice.bank == svarayantra_.getCurrentBank()
                       && choice.program == svarayantra_.getCurrentProgramNumber();

    g.setColour (sounding ? palette_.accent : palette_.text);
    g.setFont (juce::FontOptions()
                 .withName (juce::Font::getDefaultMonospacedFontName())
                 .withHeight (12.0f));
    g.drawText (choice.label, 6, 0, width - 10, height, juce::Justification::centredLeft);
}

void PresetListModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    const auto& choices = svarayantra_.getPresetChoices();

    if (row < 0 || row >= static_cast<int> (choices.size()))
        return;

    const auto& choice = choices[static_cast<std::size_t> (row)];
    svarayantra_.setPresetChoice (choice.bank, choice.program);

    if (onChoice_ != nullptr)
        onChoice_();
}

// ---------------------------------------------------------------------------
// SvarayantraEditor
// ---------------------------------------------------------------------------

SvarayantraEditor::SvarayantraEditor (SvarayantraProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      svarayantra_ (processorToUse),
      palette_ (makePalette()),
      lookAndFeel_ (palette_),
      tuningPanel_ (processorToUse, makePalette(),
                    "Microtuning applied to samples: the tuning names the frequency each key "
                    "should sound, and every zone of the soundfont is repitched to it -- "
                    "Bohlen-Pierce on a piano, 22 shruti on a flute. A drum zone (scale "
                    "tuning 0 in the font) ignores it, as drums should. The scale travels "
                    "with the project; the soundfont travels as a path.")
{
    setLookAndFeel (&lookAndFeel_);

    // ---- header ----
    titleLabel_.setText ("SVARAYANTRA", juce::dontSendNotification);
    titleLabel_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel_.setColour (juce::Label::textColourId, palette_.text);
    addAndMakeVisible (titleLabel_);

    devanagariLabel_.setText (devanagariName(), juce::dontSendNotification);
    devanagariLabel_.setFont (juce::FontOptions (20.0f));
    devanagariLabel_.setColour (juce::Label::textColourId, palette_.accent);
    devanagariLabel_.setTooltip (
        "svara-yantra: the note-machine. svara is the musical note the shruti divide; "
        "yantra the instrument or engine, as in Jantar Mantar.");
    addAndMakeVisible (devanagariLabel_);

    vendorLabel_.setText ("Tezla Tech", juce::dontSendNotification);
    vendorLabel_.setFont (juce::FontOptions (12.0f));
    vendorLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    vendorLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (vendorLabel_);

    tooltipsButton_.setClickingTogglesState (true);
    tooltipsButton_.setToggleState (svarayantra_.getTooltipsEnabled(),
                                    juce::dontSendNotification);
    tooltipsButton_.setTooltip ("Tooltips on or off. This one is always on.");
    tooltipsButton_.onClick = [this]
    {
        svarayantra_.setTooltipsEnabled (tooltipsButton_.getToggleState());
        tooltips_.setEnabled (tooltipsButton_.getToggleState());
    };
    addAndMakeVisible (tooltipsButton_);

    const char* pageNames[] = { "FONT", "TUNING" };
    const char* pageIds[] = { "tab-font", "tab-tuning" };

    for (int i = 0; i < 2; ++i)
    {
        pageButtons_[i].setButtonText (pageNames[i]);
        pageButtons_[i].setComponentID (pageIds[i]);
        pageButtons_[i].setClickingTogglesState (false);
        pageButtons_[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (pageButtons_[i]);
    }

    // ---- FONT page ----
    addAndMakeVisible (fontPage_);

    loadButton_.setTooltip (
        "Load a SoundFont (.sf2). Parsed and checked whole before it replaces what is "
        "playing -- a file that cannot be fully read is refused with the reason, and "
        "the previous font keeps sounding. The project saves the file's PATH, not its "
        "megabytes.");
    loadButton_.onClick = [this] { loadFontDialog(); };
    fontPage_.addAndMakeVisible (loadButton_);

    clearButton_.setTooltip ("Unload the soundfont. The instrument falls silent.");
    clearButton_.onClick = [this]
    {
        svarayantra_.clearFont();
        presetList_.updateContent();
        refreshFontLabels();
    };
    fontPage_.addAndMakeVisible (clearButton_);

    fontNameLabel_.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    fontNameLabel_.setColour (juce::Label::textColourId, palette_.text);
    fontPage_.addAndMakeVisible (fontNameLabel_);

    fontPathLabel_.setFont (juce::FontOptions (11.0f));
    fontPathLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    fontPage_.addAndMakeVisible (fontPathLabel_);

    fontErrorLabel_.setFont (juce::FontOptions (12.0f));
    fontErrorLabel_.setColour (juce::Label::textColourId, palette_.over);
    fontPage_.addAndMakeVisible (fontErrorLabel_);

    presetsHeading_.setText ("PRESETS", juce::dontSendNotification);
    presetsHeading_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    presetsHeading_.setColour (juce::Label::textColourId, palette_.accent);
    fontPage_.addAndMakeVisible (presetsHeading_);

    presetModel_ = std::make_unique<PresetListModel> (
        svarayantra_, palette_, [this]
        {
            presetList_.repaint();
        });
    presetList_.setModel (presetModel_.get());
    presetList_.setRowHeight (20);
    presetList_.setColour (juce::ListBox::backgroundColourId,
                           palette_.panel.brighter (0.04f));
    fontPage_.addAndMakeVisible (presetList_);

    auto setUpKnob = [this] (juce::Slider& slider, juce::Label& label,
                             const juce::String& name, const juce::String& tip)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

        // What a house knob is lives in ui/HouseControls.hpp: relief, a
        // machined skirt, a tinted track, the value font, and the wheel turned
        // off so it scrolls the panel instead of editing.
        ui::styleKnob (slider, palette_, palette_.accent);
        slider.setTooltip (tip);
        fontPage_.addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        ui::styleName (label, palette_, palette_.accent);
        fontPage_.addAndMakeVisible (label);
    };

    setUpKnob (trimSlider_, trimLabel_, "TRIM",
               "Output level in dB, smoothed so a sweep never zippers. Costs nothing.");
    trimAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        svarayantra_.getState(), ids::outputTrim, trimSlider_);

    setUpKnob (bendSlider_, bendLabel_, "BEND",
               "Pitch-wheel range in semitones, both directions. The MIDI default is 2; "
               "dubstep wobble usually wants more.");
    bendAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        svarayantra_.getState(), ids::bendRange, bendSlider_);

    voicesLabel_.setFont (juce::FontOptions (11.0f));
    voicesLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    voicesLabel_.setJustificationType (juce::Justification::centred);
    voicesLabel_.setTooltip (
        "Sounding voices of the 64 available. Every layered zone is its own voice, so "
        "one key can cost several.");
    fontPage_.addAndMakeVisible (voicesLabel_);

    // ---- TUNING page ----
    addChildComponent (tuningPanel_);

    tooltips_.setEnabled (svarayantra_.getTooltipsEnabled());
    tooltipsButton_.setToggleState (svarayantra_.getTooltipsEnabled(),
                                    juce::dontSendNotification);

    refreshFontLabels();
    presetList_.updateContent();
    showPage (0);

    setResizable (true, true);
    setResizeLimits (780, 480, 1600, 1000);
    setSize (940, 560);

    startTimerHz (10);
}

SvarayantraEditor::~SvarayantraEditor()
{
    setLookAndFeel (nullptr);
}

void SvarayantraEditor::timerCallback()
{
    voicesLabel_.setText (juce::String (svarayantra_.getActiveVoiceCount()) + " / 64 voices",
                          juce::dontSendNotification);

    // A MIDI program change moves the sounding preset without touching the
    // editor; follow it.
    const int program = svarayantra_.getCurrentProgram();

    if (program != lastSeenProgram_)
    {
        lastSeenProgram_ = program;
        presetList_.selectRow (program);
        presetList_.repaint();
    }
}

void SvarayantraEditor::showPage (int index)
{
    currentPage_ = index;
    fontPage_.setVisible (index == 0);
    tuningPanel_.setVisible (index == 1);

    for (int i = 0; i < 2; ++i)
    {
        pageButtons_[i].setColour (juce::TextButton::buttonColourId,
                                   i == index ? palette_.accent.withAlpha (0.35f)
                                              : palette_.panel.brighter (0.12f));
        pageButtons_[i].setColour (juce::TextButton::textColourOffId,
                                   i == index ? palette_.accentBright : palette_.text);
    }

    if (index == 1)
        tuningPanel_.refresh();
}

void SvarayantraEditor::loadFontDialog()
{
    chooser_ = std::make_unique<juce::FileChooser> ("Load a SoundFont", juce::File {},
                                                    "*.sf2");

    chooser_->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                           [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (! file.existsAsFile())
            return;

        (void) svarayantra_.loadFontFile (file);

        presetList_.updateContent();
        presetList_.selectRow (svarayantra_.getCurrentProgram());
        refreshFontLabels();
    });
}

void SvarayantraEditor::refreshFontLabels()
{
    const auto path = svarayantra_.getFontPath();
    const auto error = svarayantra_.getFontError();

    fontNameLabel_.setText (
        svarayantra_.getFontName().isNotEmpty() ? svarayantra_.getFontName()
        : path.isEmpty() ? juce::String ("No soundfont loaded")
                         : juce::String (),
        juce::dontSendNotification);

    fontPathLabel_.setText (path, juce::dontSendNotification);
    fontErrorLabel_.setText (error, juce::dontSendNotification);
}

void SvarayantraEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);

    // The header rule, in the accent.
    g.setColour (palette_.accent.withAlpha (0.6f));
    g.fillRect (0, 43, getWidth(), 1);
}

void SvarayantraEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (43).reduced (10, 6);
    titleLabel_.setBounds (header.removeFromLeft (170));
    devanagariLabel_.setBounds (header.removeFromLeft (130));

    tooltipsButton_.setBounds (header.removeFromRight (26).reduced (0, 3));
    header.removeFromRight (8);
    vendorLabel_.setBounds (header.removeFromRight (90));

    header.removeFromLeft (12);

    for (auto& button : pageButtons_)
    {
        button.setBounds (header.removeFromLeft (86).reduced (0, 3));
        header.removeFromLeft (6);
    }

    bounds.reduce (10, 8);
    fontPage_.setBounds (bounds);
    tuningPanel_.setBounds (bounds);

    // ---- FONT page ----
    auto page = fontPage_.getLocalBounds();

    auto right = page.removeFromRight (130);
    page.removeFromRight (10);

    auto row = page.removeFromTop (28);
    loadButton_.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (6);
    clearButton_.setBounds (row.removeFromLeft (70));

    page.removeFromTop (8);
    fontNameLabel_.setBounds (page.removeFromTop (24));
    fontPathLabel_.setBounds (page.removeFromTop (16));
    fontErrorLabel_.setBounds (page.removeFromTop (18));

    page.removeFromTop (6);
    presetsHeading_.setBounds (page.removeFromTop (16));
    presetList_.setBounds (page);

    // The right column: two knobs and the voice count.
    right.removeFromTop (36);
    trimLabel_.setBounds (right.removeFromTop (14));
    trimSlider_.setBounds (right.removeFromTop (92));
    right.removeFromTop (14);
    bendLabel_.setBounds (right.removeFromTop (14));
    bendSlider_.setBounds (right.removeFromTop (92));
    right.removeFromTop (18);
    voicesLabel_.setBounds (right.removeFromTop (18));
}

} // namespace tezla::svarayantra
