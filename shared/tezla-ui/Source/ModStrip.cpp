// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include <tezla/ui/ModStrip.hpp>

#include <cmath>

namespace tezla::ui
{

namespace
{
constexpr int kRowHeight   = 18;
constexpr int kRowGap      = 3;
constexpr int kArmWidth    = 74;
constexpr int kCellGap     = 4;
constexpr int kColumns     = 3;
constexpr int kPanelGap    = 8;
constexpr int kSideMargin  = 12;

constexpr float kNameFont  = 10.0f;
constexpr float kValueFont = 10.5f;

/// One little waveform per source in the closed row.
constexpr int kClosedGraphWidth = 46;
} // namespace

ModStrip::ModStrip (ModulationView& view)
    : view_ (view), palette_ (view.getPalette())
{
    disclosure_.setButtonText ("MOD");
    disclosure_.setColour (juce::TextButton::buttonColourId, palette_.panel.brighter (0.1f));
    disclosure_.setColour (juce::TextButton::textColourOffId, palette_.text);
    disclosure_.setTooltip (
        "Three LFOs and a level follower, assignable to any continuous control.\n\n"
        "Open this, click a source to arm it, then drag the ring that appears around any knob to set "
        "how far that source moves it. Eight assignments are available.\n\n"
        "With nothing assigned the plugin is bit-for-bit what it was before modulation existed -- "
        "that is a test, not an intention.");
    disclosure_.onClick = [this] { setOpen (! open_); };
    addAndMakeVisible (disclosure_);

    hint_.setColour (juce::Label::textColourId, palette_.dimText);
    hint_.setFont (juce::FontOptions (10.5f));
    hint_.setJustificationType (juce::Justification::centredRight);
    hint_.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hint_);

    for (int i = 0; i < ModulationView::numLfos; ++i)
    {
        panels_[static_cast<std::size_t> (i)].source = ModulationView::lfo1 + i;
        buildLfoPanel (panels_[static_cast<std::size_t> (i)], i);
    }

    auto& env = panels_[static_cast<std::size_t> (ModulationView::numLfos)];
    env.source = ModulationView::level;
    buildLevelPanel (env);

    for (auto& panel : panels_)
    {
        const int source = panel.source;

        panel.arm.setButtonText (ModulationView::nameForSource (source));
        panel.arm.setTooltip (
            "Arm " + ModulationView::nameForSource (source) + " for assignment.\n\n"
            "While it is armed every modulatable knob grows a ring in this colour. Drag one to set how "
            "far this source moves that control; drag it back to zero, or double-click it, to give the "
            "slot back. Click here again to disarm.");
        panel.arm.onClick = [this, source]
        {
            view_.setArmedSource (view_.getArmedSource() == source ? ModulationView::none : source);
        };
        addAndMakeVisible (panel.arm);
    }

    view_.addChangeListener (this);
    updateArmButtons();
    setOpen (false);
}

ModStrip::~ModStrip()
{
    view_.removeChangeListener (this);
}

void ModStrip::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateArmButtons();
    hint_.setText (summaryText(), juce::dontSendNotification);
    repaint();
}

// ============================================================================

ModStrip::Control& ModStrip::addControl (Panel& panel, const char* parameterId,
                                         const juce::String& name, const juce::String& tooltip)
{
    auto control = std::make_unique<Control>();
    control->name = name;

    auto& slider = control->slider;
    slider.setSliderStyle (juce::Slider::LinearBar);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::trackColourId,
                      ModulationView::colourForSource (panel.source).withAlpha (0.30f));
    slider.setColour (juce::Slider::backgroundColourId, palette_.background);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, palette_.background);
    slider.setTooltip (tooltip);
    addAndMakeVisible (slider);

    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        view_.getState(), parameterId, slider);

    panel.cells.push_back ({ Panel::Cell::Kind::control, static_cast<int> (panel.controls.size()) });
    panel.controls.push_back (std::move (control));

    return *panel.controls.back();
}

ModStrip::Choice& ModStrip::addChoice (Panel& panel, const char* parameterId,
                                       const juce::String& tooltip)
{
    auto choice = std::make_unique<Choice>();

    // Straight out of the parameter, so the strip lists exactly the waveforms
    // and divisions the plugin declared and cannot drift from them.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (
            view_.getState().getParameter (parameterId)))
        choice->box.addItemList (parameter->choices, 1);
    else
        jassertfalse;

    choice->box.setColour (juce::ComboBox::backgroundColourId, palette_.background);
    choice->box.setColour (juce::ComboBox::textColourId, palette_.text);
    choice->box.setColour (juce::ComboBox::outlineColourId, palette_.panel.brighter (0.3f));
    choice->box.setColour (juce::ComboBox::arrowColourId, palette_.dimText);
    choice->box.setTooltip (tooltip);
    addAndMakeVisible (choice->box);

    choice->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        view_.getState(), parameterId, choice->box);

    panel.cells.push_back ({ Panel::Cell::Kind::choice, static_cast<int> (panel.choices.size()) });
    panel.choices.push_back (std::move (choice));

    return *panel.choices.back();
}

ModStrip::Toggle& ModStrip::addToggle (Panel& panel, const char* parameterId,
                                       const juce::String& text, const juce::String& tooltip)
{
    auto toggle = std::make_unique<Toggle>();

    toggle->button.setButtonText (text);
    toggle->button.setClickingTogglesState (true);
    toggle->button.setColour (juce::TextButton::buttonColourId, palette_.background);
    toggle->button.setColour (juce::TextButton::buttonOnColourId,
                              ModulationView::colourForSource (panel.source).withAlpha (0.55f));
    toggle->button.setColour (juce::TextButton::textColourOffId, palette_.dimText.brighter (0.35f));
    toggle->button.setColour (juce::TextButton::textColourOnId, palette_.text);
    toggle->button.setTooltip (tooltip);
    addAndMakeVisible (toggle->button);

    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        view_.getState(), parameterId, toggle->button);

    panel.cells.push_back ({ Panel::Cell::Kind::toggle, static_cast<int> (panel.toggles.size()) });
    panel.toggles.push_back (std::move (toggle));

    return *panel.toggles.back();
}

void ModStrip::buildLfoPanel (Panel& panel, int lfoIndex)
{
    const auto i = static_cast<std::size_t> (lfoIndex);
    const juce::String name = ModulationView::nameForSource (panel.source);

    addChoice (panel, modIds::lfoWave[i],
        "The shape " + name + " puts out.\n\n"
        "SINE and TRIANGLE are the musical ones. SAW UP and SAW DOWN ramp and reset, which is what a "
        "filter sweep on the bar wants. SQUARE jumps -- use Smooth to round the corner off rather than "
        "reaching for a slower rate.\n\n"
        "S&H holds a new random level each cycle; RANDOM interpolates between the same levels, so it "
        "drifts instead of stepping. Both are hashed from which cycle it is rather than drawn in "
        "sequence, so a loop repeats identically and a bounce matches what you heard.");

    addControl (panel, modIds::lfoRate[i], "RATE",
        "How fast " + name + " runs when it is not synced, in cycles per second.\n\n"
        "Greyed out while SYNC is on, because the division decides the rate then.");

    addChoice (panel, modIds::lfoDivision[i],
        "The note value one cycle of " + name + " lasts, while SYNC is on.\n\n"
        "T is a triplet -- three in the space of two. D is dotted -- one and a half times as long. "
        "A whole bar is the usual starting point for a sweep.");

    addToggle (panel, modIds::lfoSync[i], "SYNC",
        "Locks " + name + " to the host's transport.\n\n"
        "The phase is taken from the song position every block rather than accumulated from a clock, "
        "so there is nothing to drift: bar 33 is the same phase as bar 1, a loop repeats identically, "
        "and a bounce matches what you heard.\n\n"
        "With no transport -- a standalone, or a stopped host -- it free-runs at the RATE setting "
        "rather than freezing.");

    addControl (panel, modIds::lfoPhase[i], "PHASE",
        "Rotates " + name + " without moving the clock.\n\n"
        "Two LFOs on the same division a half cycle apart give you a push-pull; a saw started at 0.5 "
        "lands its reset somewhere other than the downbeat.");

    addControl (panel, modIds::lfoSmooth[i], "SMOOTH",
        "Rounds the corners off " + name + ".\n\n"
        "The corner frequency is a fraction of the LFO's own period, so half smooth means the same "
        "amount of rounding at 0.1 Hz as at 10 Hz. This is how a square or an S&H stops clicking "
        "without slowing it down.");
}

void ModStrip::buildLevelPanel (Panel& panel)
{
    addControl (panel, modIds::envAttack, "ATT",
        "How fast the follower rises when the input gets louder.\n\n"
        "Short is percussive -- harmonics that arrive on the hit. Long ignores transients and follows "
        "the shape of the phrase instead.");

    addControl (panel, modIds::envRelease, "REL",
        "How fast it falls back when the input gets quieter.\n\n"
        "Too short and it chatters on a decaying note; too long and it never comes back down between "
        "hits. 100-300 ms suits most drum material.");

    addControl (panel, modIds::envSensitivity, "SENS",
        "What input level reads as full travel, in dBFS. The range below it is 40 dB.\n\n"
        "Set it to about where your loudest material sits. Too high and the follower barely moves; too "
        "low and it sits pinned at the top and stops following anything.");

    panel.caption =
        "Reads the input level. No MIDI and no note trigger -- the audio is what moves it, "
        "the way a compressor's detector does. Negative depths invert it.";
}

// ============================================================================

void ModStrip::setOpen (bool shouldBeOpen)
{
    open_ = shouldBeOpen;

    disclosure_.setButtonText (open_ ? "MOD  v" : "MOD  >");

    for (auto& panel : panels_)
    {
        for (auto& control : panel.controls) control->slider.setVisible (open_);
        for (auto& choice  : panel.choices)  choice->box.setVisible (open_);
        for (auto& toggle  : panel.toggles)  toggle->button.setVisible (open_);

        panel.arm.setVisible (open_);
    }

    // Disarming on close is deliberate. The rings are drawn on a page the strip
    // cannot see, and leaving a source armed behind a closed panel would leave
    // every knob in a mode with nothing on screen saying why.
    if (! open_)
        view_.setArmedSource (ModulationView::none);

    resized();
    repaint();

    if (onHeightChanged != nullptr)
        onHeightChanged();
}

void ModStrip::updateArmButtons()
{
    const int armed = view_.getArmedSource();

    for (auto& panel : panels_)
    {
        const auto colour = ModulationView::colourForSource (panel.source);
        const bool isArmed = panel.source == armed;

        panel.arm.setColour (juce::TextButton::buttonColourId,
                             isArmed ? colour : colour.withAlpha (0.16f));
        panel.arm.setColour (juce::TextButton::textColourOffId,
                             isArmed ? juce::Colours::black.withAlpha (0.85f) : colour);
        panel.arm.repaint();
    }
}

void ModStrip::updateSyncStates()
{
    for (int i = 0; i < ModulationView::numLfos; ++i)
    {
        auto& panel = panels_[static_cast<std::size_t> (i)];

        if (panel.controls.empty() || panel.choices.size() < 2)
            continue;

        const bool synced =
            view_.getState().getRawParameterValue (modIds::lfoSync[static_cast<std::size_t> (i)])->load() > 0.5f;

        // controls[0] is RATE and choices[1] is the division, in the order
        // buildLfoPanel added them.
        auto& rate = panel.controls[0]->slider;
        auto& division = panel.choices[1]->box;

        if (rate.isEnabled() == ! synced && division.isEnabled() == synced)
            continue;

        rate.setEnabled (! synced);
        division.setEnabled (synced);
        rate.setAlpha (synced ? 0.35f : 1.0f);
        division.setAlpha (synced ? 1.0f : 0.35f);
        repaint();
    }
}

juce::String ModStrip::summaryText() const
{
    const int used = view_.getSlotsUsed();
    const int armed = view_.getArmedSource();

    if (armed != ModulationView::none)
        return ModulationView::nameForSource (armed) + " armed -- drag a ring on any knob   |   "
             + juce::String (used) + "/" + juce::String (ModulationView::numSlots) + " slots";

    if (used >= ModulationView::numSlots)
        return "All " + juce::String (ModulationView::numSlots)
             + " slots in use -- drag a ring to zero to free one";

    return juce::String (used) + "/" + juce::String (ModulationView::numSlots) + " slots used";
}

void ModStrip::refresh()
{
    updateSyncStates();

    // Label::setText compares before it repaints, so this costs a string compare
    // rather than a redraw on the frames where nothing has changed.
    hint_.setText (summaryText(), juce::dontSendNotification);

    bool moved = false;

    for (auto& panel : panels_)
    {
        const double value = view_.liveSourceValue (panel.source);
        const double phase = view_.liveSourcePhase (panel.source);
        const int wave = static_cast<int> (view_.waveOf (panel.source));

        if (std::abs (value - panel.shownValue) < 0.004
            && std::abs (phase - panel.shownPhase) < 0.004
            && wave == panel.shownWave)
            continue;

        panel.shownValue = value;
        panel.shownPhase = phase;
        panel.shownWave  = wave;
        moved = true;

        // Open, the bars are the only part of the strip that moves, so repaint
        // those and not the panels behind them.
        if (open_ && ! panel.valueArea.isEmpty())
            repaint (panel.valueArea);
    }

    // Closed, the bars live in the header row and there is nothing else there
    // to spare, so the whole 24 px goes.
    if (moved && ! open_)
        repaint();
}

// ============================================================================

void ModStrip::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (getCollapsedHeight()).reduced (kSideMargin, 2);
    disclosure_.setBounds (header.removeFromLeft (66));
    header.removeFromLeft (10);

    // The graphs get their strip first and the summary takes what is left. The
    // summary grows by a whole sentence when a source is armed, and laying it
    // out over the whole row would put that sentence straight through them.
    closedGraphs_ = open_ ? juce::Rectangle<int> {}
                          : header.removeFromRight (juce::jmin (header.getWidth() / 2,
                                                                kNumPanels * (kClosedGraphWidth + 6)));

    hint_.setBounds (header.reduced (0, 0).withTrimmedRight (8));

    if (! open_)
    {
        for (auto& area : panelBounds_)
            area = {};

        // Cleared too, or refresh() would repaint a rectangle from the last time
        // the strip was open and leave the closed bars stale.
        for (auto& panel : panels_)
            panel.valueArea = {};

        return;
    }

    auto body = bounds.reduced (kSideMargin, 0);
    body.removeFromBottom (6);

    const int panelHeight = (body.getHeight() - kPanelGap) / 2;

    auto top = body.removeFromTop (panelHeight);
    body.removeFromTop (kPanelGap);
    auto bottom = body.removeFromTop (panelHeight);

    const int panelWidth = (top.getWidth() - kPanelGap) / 2;

    panelBounds_[0] = top.removeFromLeft (panelWidth);
    top.removeFromLeft (kPanelGap);
    panelBounds_[1] = top.removeFromLeft (panelWidth);

    panelBounds_[2] = bottom.removeFromLeft (panelWidth);
    bottom.removeFromLeft (kPanelGap);
    panelBounds_[3] = bottom.removeFromLeft (panelWidth);

    for (std::size_t i = 0; i < panels_.size(); ++i)
        layoutPanel (panels_[i], panelBounds_[i]);
}

void ModStrip::layoutPanel (Panel& panel, juce::Rectangle<int> area)
{
    auto inner = area.reduced (6, 3);

    auto left = inner.removeFromLeft (kArmWidth);
    panel.arm.setBounds (left.removeFromTop (kRowHeight));
    left.removeFromTop (kRowGap);
    panel.valueArea = left.removeFromTop (kRowHeight);

    inner.removeFromLeft (8);

    auto row1 = inner.removeFromTop (kRowHeight);
    inner.removeFromTop (kRowGap);
    auto row2 = inner.removeFromTop (kRowHeight);

    const int cellWidth = (row1.getWidth() - (kColumns - 1) * kCellGap) / kColumns;

    for (std::size_t i = 0; i < panel.cells.size(); ++i)
    {
        const int column = static_cast<int> (i) % kColumns;
        auto& row = static_cast<int> (i) < kColumns ? row1 : row2;

        auto cell = row.removeFromLeft (cellWidth);

        if (column < kColumns - 1)
            row.removeFromLeft (kCellGap);

        switch (panel.cells[i].kind)
        {
            case Panel::Cell::Kind::control:
                panel.controls[static_cast<std::size_t> (panel.cells[i].index)]->slider.setBounds (cell);
                break;
            case Panel::Cell::Kind::choice:
                panel.choices[static_cast<std::size_t> (panel.cells[i].index)]->box.setBounds (cell);
                break;
            case Panel::Cell::Kind::toggle:
                panel.toggles[static_cast<std::size_t> (panel.cells[i].index)]->button.setBounds (cell);
                break;
            case Panel::Cell::Kind::empty:
                break;
        }
    }
}

void ModStrip::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel.darker (0.35f));
    g.fillRect (getLocalBounds());

    g.setColour (palette_.panel.brighter (0.12f));
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    if (! open_)
    {
        // Closed, the strip still shows what all four sources are set to and
        // whether they are running. That is the part worth 24 px.
        auto strip = closedGraphs_;

        for (const auto& panel : panels_)
        {
            if (strip.getWidth() < kClosedGraphWidth)
                break;

            auto cell = strip.removeFromLeft (kClosedGraphWidth);
            strip.removeFromLeft (6);

            paintSource (g, cell.toFloat().reduced (0.0f, 1.0f), panel.source);
        }

        return;
    }

    for (std::size_t i = 0; i < panels_.size(); ++i)
    {
        if (! panelBounds_[i].isEmpty())
            paintPanel (g, panels_[i]);
    }
}

void ModStrip::paintPanel (juce::Graphics& g, const Panel& panel) const
{
    const auto colour = ModulationView::colourForSource (panel.source);
    const bool isArmed = panel.source == view_.getArmedSource();

    const auto index = static_cast<std::size_t> (&panel - panels_.data());
    const auto area = panelBounds_[index].toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (area, 3.0f);

    if (isArmed)
    {
        g.setColour (colour.withAlpha (0.55f));
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.5f);
    }

    if (! panel.valueArea.isEmpty())
        paintSource (g, panel.valueArea.toFloat(), panel.source);

    // The track behind each bar. A LinearBar draws only the filled part, so a
    // control sitting at zero -- PHASE and SMOOTH, most of the time -- would
    // otherwise be a name and a number floating on the panel with nothing to
    // say it can be dragged.
    for (const auto& control : panel.controls)
    {
        const auto bounds = control->slider.getBounds().toFloat();

        if (bounds.isEmpty())
            continue;

        g.setColour (palette_.background);
        g.fillRoundedRectangle (bounds, 2.0f);
    }

    if (panel.caption.isNotEmpty() && panel.cells.size() <= static_cast<std::size_t> (kColumns))
    {
        auto captionArea = panelBounds_[index].reduced (6, 3);
        captionArea.removeFromLeft (kArmWidth + 8);
        captionArea.removeFromTop (kRowHeight + kRowGap);

        g.setColour (palette_.dimText);
        g.setFont (juce::FontOptions (kNameFont));
        g.drawFittedText (panel.caption, captionArea, juce::Justification::topLeft, 2, 1.0f);
    }
}

void ModStrip::paintSource (juce::Graphics& g, juce::Rectangle<float> area, int source) const
{
    const auto colour = ModulationView::colourForSource (source);

    g.setColour (palette_.background);
    g.fillRoundedRectangle (area, 2.0f);

    auto plot = area.reduced (3.0f, 2.5f);

    if (plot.getWidth() < 6.0f || plot.getHeight() < 4.0f)
        return;

    if (source == ModulationView::level)
    {
        // A follower has no cycle to draw. Its bar fills from the left because
        // its output is 0 to 1 and never goes below.
        const auto value = static_cast<float> (juce::jlimit (0.0, 1.0, view_.liveSourceValue (source)));

        g.setColour (colour.withAlpha (0.22f));
        g.fillRect (plot);

        g.setColour (colour);
        g.fillRect (plot.withWidth (plot.getWidth() * value));
        return;
    }

    const auto wave = view_.waveOf (source);
    const float centreY = plot.getCentreY();
    const float halfHeight = plot.getHeight() * 0.5f;

    // One cycle across the box. Sampled per pixel rather than per waveform
    // segment, so a square's edges land where they actually are and sample &
    // hold shows its own steps rather than a straight line between them.
    juce::Path shape;
    const int steps = juce::jmax (8, static_cast<int> (plot.getWidth()));

    for (int i = 0; i <= steps; ++i)
    {
        const double phase = static_cast<double> (i) / static_cast<double> (steps);
        const float x = plot.getX() + plot.getWidth() * static_cast<float> (phase);
        const float y = centreY - halfHeight * static_cast<float> (dsp::Lfo::shapeAt (wave, phase));

        if (i == 0)
            shape.startNewSubPath (x, y);
        else
            shape.lineTo (x, y);
    }

    g.setColour (palette_.dimText.withAlpha (0.25f));
    g.fillRect (plot.getX(), centreY - 0.5f, plot.getWidth(), 1.0f);

    g.setColour (colour.withAlpha (0.75f));
    g.strokePath (shape, juce::PathStrokeType (1.2f));

    // The dot only appears while the source is running, and the sources only run
    // when something is assigned to them. That is the honest reading: an LFO
    // with nothing pointed at it is not oscillating, it is a setting.
    const double phase = view_.liveSourcePhase (source);
    const double value = view_.liveSourceValue (source);

    if (phase > 0.0 || std::abs (value) > 1.0e-9)
    {
        const juce::Point<float> dot {
            plot.getX() + plot.getWidth() * static_cast<float> (phase),
            centreY - halfHeight * static_cast<float> (juce::jlimit (-1.0, 1.0, value))
        };

        g.setColour (colour.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (dot));
    }
}

void ModStrip::paintOverChildren (juce::Graphics& g)
{
    if (! open_)
        return;

    // The bar sliders carry no text box, so their name and value are painted
    // here, over the bar itself. At 18 px there is nowhere else to put either,
    // and a bar with neither would be a mystery control.
    for (const auto& panel : panels_)
    {
        for (const auto& control : panel.controls)
        {
            const auto bounds = control->slider.getBounds().reduced (5, 0);

            if (bounds.isEmpty())
                continue;

            const float alpha = control->slider.isEnabled() ? 1.0f : 0.35f;

            g.setColour (palette_.dimText.withMultipliedAlpha (alpha));
            g.setFont (juce::FontOptions (kNameFont));
            g.drawText (control->name, bounds, juce::Justification::centredLeft, false);

            g.setColour (palette_.text.withMultipliedAlpha (alpha));
            g.setFont (juce::FontOptions (kValueFont));
            g.drawText (control->slider.getTextFromValue (control->slider.getValue()),
                        bounds, juce::Justification::centredRight, false);
        }
    }
}

} // namespace tezla::ui
