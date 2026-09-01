// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tezla::malleus {

namespace
{
// Malleus's own accent: bone ivory, against Anvil's steel and Ferrite's
// oxide. The hammer bone. The bypass glow is the same in every plugin --
// though an instrument has nothing to bypass, so it never shows here.
const ui::Palette kPalette {
    juce::Colour { 0xff141413 },   // background
    juce::Colour { 0xff1d1c1a },   // panel
    juce::Colour { 0xffdedad0 },   // text
    juce::Colour { 0xff8b867d },   // dim text
    juce::Colour { 0xffe4dcc6 },   // accent: bone
    juce::Colour { 0xfff6f0e0 },   // accent bright: polished bone
    juce::Colour { 0xff9c8f6d },   // secondary: aged ivory
    juce::Colour { 0xffff7a18 }    // bypass glow, the same in every plugin
};

constexpr int kLabelHeight = 16;
constexpr int kValueHeight = 18;
constexpr int kMaxCellHeight = 124;
constexpr int kNoteHeight = 56;

constexpr int kHeaderHeight = 40;
constexpr int kStackHeight = 132;
constexpr int kTabHeight = 28;

constexpr double kLowHz = 20.0;
constexpr double kHighHz = 20000.0;
} // namespace

// ---------------------------------------------------------------------------
// ControlPage
// ---------------------------------------------------------------------------

void ControlPage::addKnob (const char* parameterId, const juce::String& name,
                           const juce::String& tooltip)
{
    auto knob = std::make_unique<Knob>();

    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, kValueHeight);
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, palette_.accent);
    knob->slider.setColour (juce::Slider::rotarySliderOutlineColourId,
                            palette_.panel.brighter (0.25f));
    knob->slider.setColour (juce::Slider::thumbColourId, palette_.accentBright);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId,
                            juce::Colours::transparentBlack);
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

void ControlPage::addChoice (const char* parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    auto choice = std::make_unique<Choice>();

    // Populated from the parameter itself. A ComboBoxAttachment selects an
    // item by index and does not create one, so a box left empty here stays
    // empty on screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (
            state_.getParameter (parameterId)))
        choice->box.addItemList (parameter->choices, 1);
    else
        jassertfalse;   // addChoice used on something that is not a choice

    choice->box.setColour (juce::ComboBox::backgroundColourId,
                           palette_.panel.brighter (0.15f));
    choice->box.setColour (juce::ComboBox::textColourId, palette_.text);
    choice->box.setColour (juce::ComboBox::outlineColourId,
                           palette_.panel.brighter (0.3f));
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

void ControlPage::setControlEnabled (const char* parameterId, bool enabled)
{
    const juce::String id { parameterId };

    for (auto& knob : knobs_)
        if (knob->id == id)
        {
            knob->slider.setEnabled (enabled);
            knob->label.setColour (juce::Label::textColourId,
                                   enabled ? palette_.dimText
                                           : palette_.dimText.withAlpha (0.35f));
            knob->label.repaint();
        }

    for (auto& choice : choices_)
        if (choice->id == id)
        {
            choice->box.setEnabled (enabled);
            choice->label.setColour (juce::Label::textColourId,
                                     enabled ? palette_.dimText
                                             : palette_.dimText.withAlpha (0.35f));
            choice->label.repaint();
        }
}

void ControlPage::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    if (note_.isEmpty() || noteArea_.isEmpty())
        return;

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (12.0f));

    // Four lines, and the box is sized for four: these notes carry the
    // measured numbers behind each page, and a note silently cut in half is
    // worse than no note.
    g.drawFittedText (note_, noteArea_, juce::Justification::centredTop, 4, 1.0f);
}

void ControlPage::resized()
{
    if (cells_.empty())
        return;

    auto bounds = getLocalBounds().reduced (4, 2);

    if (! note_.isEmpty())
        noteArea_ = bounds.removeFromBottom (kNoteHeight).reduced (6, 0);
    else
        noteArea_ = {};

    const int rows = (static_cast<int> (cells_.size()) + columns_ - 1) / columns_;
    const int cellWidth = bounds.getWidth() / columns_;
    const int cellHeight = juce::jmin (bounds.getHeight() / juce::jmax (1, rows),
                                       kMaxCellHeight);

    const int top = bounds.getY()
                  + juce::jmax (0, (bounds.getHeight() - rows * cellHeight) / 2);

    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const int column = static_cast<int> (i) % columns_;
        const int row = static_cast<int> (i) / columns_;

        juce::Rectangle<int> cell { bounds.getX() + column * cellWidth,
                                    top + row * cellHeight,
                                    cellWidth, cellHeight };

        switch (cells_[i].kind)
        {
            case Cell::Kind::knob:
            {
                auto& knob = *knobs_[static_cast<std::size_t> (cells_[i].index)];
                knob.label.setBounds (cell.removeFromTop (kLabelHeight));
                knob.slider.setBounds (cell.reduced (4, 0));
                break;
            }

            case Cell::Kind::choice:
            {
                auto& choice = *choices_[static_cast<std::size_t> (cells_[i].index)];
                choice.label.setBounds (cell.removeFromTop (kLabelHeight));
                choice.box.setBounds (cell.withSizeKeepingCentre (
                    juce::jmin (cell.getWidth() - 12, 132), 26));
                break;
            }

            case Cell::Kind::gap:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// ModeStackView
// ---------------------------------------------------------------------------

ModeStackView::ModeStackView (MalleusProcessor& processor, ui::Palette palette)
    : processor_ (processor), palette_ (palette)
{
    // Hover reaches it (the tooltip below is the point) but clicks pass
    // through: there is nothing here to drag.
    setInterceptsMouseClicks (false, true);
    setTooltip (
        "The object's partials, as lines on a log-frequency axis, with the "
        "loaded scale's degrees ghosted behind them. Turn Overtone Lock up "
        "and watch the partials be pulled onto the ghosts: that is the "
        "instrument's whole trick, and no other physical modeller does it.");

    // Once now, so the very first paint shows the object rather than an
    // empty box waiting on the timer's first tick.
    refresh();
}

float ModeStackView::xForHz (double hz, juce::Rectangle<float> area)
{
    const double clamped = juce::jlimit (kLowHz, kHighHz, hz);
    const double along = std::log (clamped / kLowHz) / std::log (kHighHz / kLowHz);

    return area.getX() + static_cast<float> (along) * area.getWidth();
}

void ModeStackView::refresh()
{
    const auto snapshot = processor_.snapshotModeStack();
    const auto& stack = snapshot.frequencies;

    double lock = 0.0;

    if (auto* raw = processor_.getState().getRawParameterValue (ids::lockAmount))
        lock = raw->load();

    // The lattice: every degree of the scale, replicated across the repeats
    // that fall in the visible range, rooted where the tuning's own root
    // sits. Rebuilt only when the lock amount or the scale has changed --
    // this runs at 15 Hz.
    const double root = processor_.getRootHz();
    const auto& scale = processor_.getScale();

    // Partials above the axis are NOT drawn. Clamping them to the right
    // edge would pile several lines onto one place and claim they sit at
    // 20 kHz, which is a lie the picture cannot afford; the caption counts
    // them instead. They are real and they do sound -- they are simply
    // above hearing, which is the honest thing to say about them.
    int aboveAxis = 0;

    for (const double hz : stack)
        if (hz > kHighHz)
            ++aboveAxis;

    const int drawn = static_cast<int> (stack.size()) - aboveAxis;

    juce::String caption;

    if (stack.empty())
        caption = "no object";
    else
        caption = juce::String (drawn) + " partials, "
                    + juce::String (stack.front(), 1) + " Hz to "
                    + juce::String (drawn > 0 ? stack[static_cast<std::size_t> (drawn - 1)]
                                              : stack.front(), 0) + " Hz"
                    + (aboveAxis > 0 ? "  (+" + juce::String (aboveAxis)
                                           + " above 20 kHz)"
                                     : juce::String())
                    + (snapshot.sounding ? "" : "   -- preview at the tuning's root");

    const bool latticeStale = degrees_.empty()
                            || std::abs (lock - lockAmount_) > 1.0e-9;

    if (latticeStale && root > 0.0 && scale.repeat > 1.0 && ! scale.ratios.empty())
    {
        degrees_.clear();

        // Down to 20 Hz and up to 20 kHz, whatever the repeat interval is
        // -- a tritave lattice has fewer repeats across the range than an
        // octave one, and both must reach the ends.
        const int lowestRepeat = static_cast<int> (
            std::floor (std::log (kLowHz / root) / std::log (scale.repeat)));
        const int highestRepeat = static_cast<int> (
            std::ceil (std::log (kHighHz / root) / std::log (scale.repeat)));

        for (int k = lowestRepeat; k <= highestRepeat; ++k)
        {
            const double base = root * std::pow (scale.repeat, k);

            for (const double ratio : scale.ratios)
            {
                const double hz = base * ratio;

                if (hz >= kLowHz && hz <= kHighHz)
                    degrees_.push_back (hz);
            }
        }
    }

    if (stack == modes_ && std::abs (lock - lockAmount_) < 1.0e-9
        && caption == caption_)
        return;

    modes_ = stack;
    lockAmount_ = lock;
    caption_ = caption;

    repaint();
}

void ModeStackView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (palette_.panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto plot = bounds.reduced (10.0f, 8.0f);
    plot.removeFromBottom (14.0f);   // the caption's line

    // Octave gridlines, so the axis reads as frequency rather than as a
    // decorative smear.
    g.setColour (palette_.panel.brighter (0.16f));

    for (double hz = 31.25; hz <= kHighHz; hz *= 2.0)
    {
        const float x = xForHz (hz, plot);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }

    // The scale's degrees, ghosted along the BOTTOM as a ruler rather than
    // as full-height lines. Drawn the obvious way, a ten-octave 12-TET
    // lattice is about a hundred and twenty verticals in seventeen hundred
    // pixels -- a picket fence that swamps the dozen partials it exists to
    // explain, which is exactly how it first looked. As a ruler the scale
    // stays legible, the partials stay the subject, and a partial standing
    // on a degree still visibly stands on it.
    if (lockAmount_ > 0.0)
    {
        const float tick = plot.getHeight() * 0.22f;

        g.setColour (palette_.secondary.withAlpha (
            static_cast<float> (0.25 + 0.45 * lockAmount_)));

        for (const double hz : degrees_)
        {
            const float x = xForHz (hz, plot);
            g.drawVerticalLine (juce::roundToInt (x), plot.getBottom() - tick,
                                plot.getBottom());
        }
    }

    // The partials themselves. Height falls with the index, which is how
    // the ear ranks them and is also what keeps a 64-mode stack legible.
    for (std::size_t i = 0; i < modes_.size(); ++i)
    {
        if (modes_[i] > kHighHz)
            continue;   // above the axis: counted in the caption, not drawn

        const float x = xForHz (modes_[i], plot);
        const float fall = 1.0f - 0.75f * static_cast<float> (i)
                                    / static_cast<float> (std::max<std::size_t> (1, modes_.size()));
        const float height = plot.getHeight() * fall;

        g.setColour (i == 0 ? palette_.accentBright
                            : palette_.accent.withAlpha (0.35f + 0.5f * fall));

        g.fillRect (juce::Rectangle<float> (x - 0.75f, plot.getBottom() - height,
                                            1.5f, height));
    }

    g.setColour (palette_.dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (caption_, bounds.reduced (10.0f, 4.0f),
                juce::Justification::bottomLeft);

    g.drawText (lockAmount_ > 0.0
                    ? "OVERTONE LOCK " + juce::String (juce::roundToInt (lockAmount_ * 100.0))
                          + "%  --  ghosts are the scale"
                    : juce::String ("OVERTONE LOCK off  --  the object's own partials"),
                bounds.reduced (10.0f, 4.0f), juce::Justification::bottomRight);
}

// ---------------------------------------------------------------------------
// MalleusEditor
// ---------------------------------------------------------------------------

MalleusEditor::MalleusEditor (MalleusProcessor& owner)
    : juce::AudioProcessorEditor (&owner),
      malleus_ (owner),
      palette_ (kPalette),
      // Three lines is what the panel gives this, and a fourth is silently
      // cut mid-word -- which is how the first draft of it shipped into a
      // screenshot reading "travels with the proje...".
      tuningPanel_ (owner, kPalette,
                    "Here the tuning also decides where the OBJECT'S OWN PARTIALS land, "
                    "through Overtone Lock on the OBJECT page, and how the sympathetic "
                    "strings are tuned. Load Bohlen-Pierce and a bell moves onto the "
                    "tritave lattice; load a slendro and a gong agrees with the gamelan.")
{
    titleLabel_.setText ("MALLEUS", juce::dontSendNotification);
    titleLabel_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel_.setColour (juce::Label::textColourId, palette_.text);
    addAndMakeVisible (titleLabel_);

    subtitleLabel_.setText ("Struck objects, tuned overtones", juce::dontSendNotification);
    subtitleLabel_.setFont (juce::FontOptions (12.0f));
    subtitleLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    subtitleLabel_.setTooltip (
        "The malleus is the hammer: the ear bone that strikes the incus -- and incus "
        "is Latin for anvil. This suite already ships Anvil; this is the bone that "
        "hits it, and the bone you hear with.");
    addAndMakeVisible (subtitleLabel_);

    vendorLabel_.setText ("Tezla Tech", juce::dontSendNotification);
    vendorLabel_.setFont (juce::FontOptions (12.0f));
    vendorLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    vendorLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (vendorLabel_);

    voicesLabel_.setFont (juce::FontOptions (11.0f));
    voicesLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    voicesLabel_.setJustificationType (juce::Justification::centredRight);
    voicesLabel_.setTooltip (
        "Sounding voices of the 16 available. A voice is counted while its key is "
        "held or its gate still conducts; once the vactrol reads fully dark the "
        "voice is gone and costs nothing. Measured at 17% of a core for sixteen "
        "bowed voices, 0.37% once they have died.");
    addAndMakeVisible (voicesLabel_);

    tooltipsButton_.setClickingTogglesState (true);
    tooltipsButton_.setToggleState (malleus_.getTooltipsEnabled(),
                                    juce::dontSendNotification);
    tooltipsButton_.setTooltip ("Tooltips on or off. This one is always on.");
    tooltipsButton_.onClick = [this]
    {
        malleus_.setTooltipsEnabled (tooltipsButton_.getToggleState());
        tooltips_.setEnabled (tooltipsButton_.getToggleState());
    };
    addAndMakeVisible (tooltipsButton_);

    modeStack_ = std::make_unique<ModeStackView> (malleus_, palette_);
    addAndMakeVisible (*modeStack_);

    buildPages();

    addChildComponent (tuningPanel_);

    const char* tabNames[kNumPages] { "OBJECT", "EXCITE", "RESONANCE", "TUNING" };
    const char* tabIds[kNumPages] { "tab-object", "tab-excite", "tab-resonance",
                                    "tab-tuning" };

    for (int i = 0; i < kNumPages; ++i)
    {
        tabs_[static_cast<std::size_t> (i)].setButtonText (tabNames[i]);
        tabs_[static_cast<std::size_t> (i)].setComponentID (tabIds[i]);
        tabs_[static_cast<std::size_t> (i)].setClickingTogglesState (false);
        tabs_[static_cast<std::size_t> (i)].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs_[static_cast<std::size_t> (i)]);
    }

    tooltips_.setEnabled (malleus_.getTooltipsEnabled());

    showPage (0);

    setResizable (true, true);
    setResizeLimits (760, 560, 1500, 1100);
    setSize (900, 660);

    startTimerHz (15);
}

MalleusEditor::~MalleusEditor() = default;

void MalleusEditor::buildPages()
{
    auto& state = malleus_.getState();

    // ---- OBJECT ----------------------------------------------------------

    auto object = std::make_unique<ControlPage> (state, palette_, 4);

    object->addKnob (ids::material, "Material",
        "What the object IS: a continuous morph across String, Bar, Membrane, "
        "Plate and Bell, interpolated in log-frequency. The integer positions "
        "are the pure tables to the bit -- bar ratios 1 : 2.756 : 5.404 : 8.933 "
        "from the roots of cos x cosh x = 1, membrane from the Bessel zeros, "
        "both computed here rather than copied.");

    object->addKnob (ids::stretch, "Stretch",
        "Inharmonicity, as a power law on every ratio: 0 is the physical table, "
        "positive spreads the partials apart (the stiff-string direction, taken "
        "far past any real string), negative squeezes them in. The prime never "
        "moves, so the pitch stays put and only the colour changes.");

    object->addKnob (ids::lockAmount, "Overtone lock",
        "Quantises the object's OWN PARTIALS onto the loaded scale, 0 to 100%. "
        "This is the flagship: at 100% every partial of a bell sits within half "
        "a cent of a degree (measured: 0.0000 cents on Bohlen-Pierce), so the "
        "object agrees with the tuning instead of fighting it. Between 0 and "
        "100 it blends in cents. Watch the stack above as you turn it.");

    object->addKnob (ids::partials, "Partials",
        "How many modes sound, 8 to 64. Costs CPU roughly in proportion: a "
        "64-mode bank measures 0.32% of a core. Partials that would land above "
        "0.45x the sample rate are muted rather than folded, so raising this "
        "can add nothing at a high pitch -- which is correct, and is why "
        "nothing here aliases.");

    object->addKnob (ids::decay, "Decay",
        "The prime partial's T60 -- how long the OBJECT rings; the other "
        "partials follow from Damping tilt. The audible note is shorter, "
        "measured at about half (0.47x), because the vactrol gate is an "
        "amplitude envelope as well as a filter and its early fall is fast. "
        "That shortening is the west-coast ping, not a loss.");

    object->addKnob (ids::tilt, "Damping tilt",
        "How much faster the upper partials die than the prime. 0 is a bell "
        "that keeps its shimmer forever; 1 is felt, wood and skin, where the "
        "top is gone almost at once. This is the difference between metal and "
        "membrane far more than the Material knob is.");

    object->addKnob (ids::position, "Position",
        "Where the object is struck, as a fraction of its length. Modes are "
        "weighted by sin(k pi p), so at 0.5 every even mode sits on a node and "
        "gets EXACTLY nothing -- the hollow, clarinet-like sound of a bar hit "
        "in the middle. At 1/3, every third mode goes.");

    object->addKnob (ids::bloom, "Bloom",
        "The modes talk to each other. Hit a tam-tam hard and the sound BUILDS "
        "after the strike -- a shimmer that was not there at contact climbing "
        "out of the low modes -- and that is energy migrating upward through "
        "the bank, which nothing linear can do. Measured on a 32-mode bar: the "
        "high band's share falls to 0.78x of its early value with this off and "
        "rises to 37x with it full. Amplitude-dependent, so a quiet hit blooms "
        "less; zero is bit-exact off, and the loop is bounded by a term that "
        "cannot exceed unity for any input.");

    object->addKnob (ids::damp, "Damp",
        "A hand on the object, and it is played rather than set -- push it "
        "while a note rings and the ring changes. The loss is proportional to "
        "FREQUENCY, so the top goes first and the object turns dull before it "
        "turns quiet, which is what soft tissue does and what a volume pedal "
        "does not. Measured on a 4 s decay: at full damp a 250 Hz mode reads "
        "0.368 s and a 500 Hz mode 0.194 s. Zero is bit-exact off.");

    object->addKnob (ids::outputTrim, "Output",
        "Output trim. The floor (-60 dB) is quiet, not silent: with nothing "
        "played the plugin's output is bit-exact zero at any trim.");

    object->setNote (
        "The object's spectrum comes from geometry and material, not from a waveform. "
        "Mode tables are derived from the physics and pinned by test against the "
        "classic figures; the morph between them is bit-exact at the integer positions, "
        "so a project saved on a pure material reopens sounding identical.");

    pages_[0] = std::move (object);
    addChildComponent (*pages_[0]);

    // ---- EXCITE ----------------------------------------------------------

    auto excite = std::make_unique<ControlPage> (state, palette_, 4);

    excite->addChoice (ids::exciter, "Exciter",
        "How the object is hit. Mallet and Pluck inject the closed form of a "
        "contact pulse straight into each mode -- band-limited by construction, "
        "with nothing to alias. Roll is a bouncing-ball retrigger clock. Bow is "
        "stick-slip friction, which sustains on any object here, including ones "
        "no bow could reach.");

    excite->addKnob (ids::hardness, "Hardness",
        "Contact time, 8 ms of felt down to 0.15 ms of brass, log-spaced. "
        "Measured spectral centroid on a 32-mode object at 100 Hz: 155 Hz soft, "
        "430 Hz middle, 1438 Hz hard. Mallet and the scrape only.");

    excite->addChoice (ids::exciterB, "Exciter B",
        "The second contact. A real strike is a contact AND a scrape: a mallet "
        "with a fingernail on it, or a bow started with a pluck, which is how a "
        "bowed string is actually begun. Inaudible until Blend leaves zero.");

    excite->addKnob (ids::exciterBlend, "Blend",
        "How much of Exciter B there is. A lerp on the excitation amounts, so "
        "0 is bit for bit Exciter A alone and 1 is bit for bit B alone -- both "
        "ends, not just the off one. Measured on a mallet blended into a pluck "
        "at 220 Hz, the strike's spectral centroid walks 390 Hz to 227 Hz "
        "across the control. Setting both slots the same is the single "
        "exciter, exactly, at every position.");

    excite->addKnob (ids::hardnessVel, "Hardness from velocity",
        "How much of Hardness comes from how hard you play instead of from the "
        "knob. On a real drum a soft hit is felt and a hard hit is stick, "
        "because the same mallet compresses differently -- this is what "
        "connects that to the keyboard. At full, velocity IS the hardness: "
        "measured, the strike's centroid runs 216 Hz at velocity 0.1 to 542 Hz "
        "at 1.0. Zero is bit-exact the knob.");

    excite->addKnob (ids::noiseAmount, "Scrape",
        "A seeded, hardness-darkened noise burst mixed into the strike -- the "
        "sound of the contact itself rather than the object. Finite, and "
        "exactly silent once spent.");

    excite->addKnob (ids::dropDepth, "Drop depth",
        "Per-hit tension glide, in semitones, signed. This is the membrane "
        "physics behind an 808's drop and a tabla's gliss: the whole object is "
        "struck at raised tension and every partial glides down together, "
        "through a retune that preserves the ring. Negative rises instead.");

    excite->addKnob (ids::dropTime, "Drop time",
        "How long the tension takes to land. Under 1% of the depth remains at "
        "the stated time, and shortly after it snaps home exactly -- so a "
        "settled voice costs nothing to hold.");

    excite->addKnob (ids::bowPressure, "Bow pressure",
        "How hard the bow presses, scaled by velocity. Below the onset the "
        "object will not speak (measured RMS 0.0003 at pressure 0.01); above "
        "it, it sings (0.13 at 0.05, 0.60 at 0.20). Bow only.");

    excite->addKnob (ids::bowSpeed, "Bow speed",
        "How fast the bow moves. A crawling bow cannot sustain either -- the "
        "onset gates on both axes. Bow only.");

    excite->addGap();

    excite->addKnob (ids::rollStart, "Roll start",
        "The first re-strike's delay after the note. Roll only.");

    excite->addKnob (ids::rollRatio, "Roll ratio",
        "Each interval is the last one times this. Below 1 the roll "
        "accelerates, exactly as a dropped mallet does; above 1 it slows. Roll "
        "only.");

    excite->addKnob (ids::rollMinimum, "Roll floor",
        "Where an accelerating roll settles into its buzz. Roll only.");

    excite->addKnob (ids::rollHumanise, "Roll humanise",
        "Jitters the roll's timing and velocity, seeded per hit: two hits of "
        "the same key are different performances, and the same take replays "
        "bit-exactly. Roll only.");

    excite->setNote (
        "The strike never exists as a time-domain pulse: each mode is injected with "
        "the closed form of what a contact would have given it, so the excitation "
        "cannot contain a frequency that was never computed. Measured between-modes "
        "floor at maximum hardness with 64 partials: 121 dB below the loudest mode.");

    pages_[1] = std::move (excite);
    addChildComponent (*pages_[1]);

    // ---- RESONANCE -------------------------------------------------------

    auto resonance = std::make_unique<ControlPage> (state, palette_, 4);

    resonance->addKnob (ids::sympCount, "Strings",
        "Sympathetic strings, 0 to 12 -- a sitar's taraf. They are not played; "
        "they pick up whatever the object does and answer in tune, because they "
        "are tuned to consecutive degrees of the loaded scale. Zero is the "
        "default and costs nothing.");

    resonance->addKnob (ids::sympRoot, "String root",
        "The lowest sympathetic string's key. The rest walk up the scale's "
        "degrees from there.");

    resonance->addKnob (ids::sympLevel, "String level",
        "How loud the strings' answer is in the output.");

    resonance->addKnob (ids::sympCoupling, "Coupling",
        "How hard the object drives the strings -- the bridge, in effect.");

    resonance->addKnob (ids::sympDrone, "Drone",
        "Feeds the strings' own output back into them, so they sustain past "
        "their decay instead of dying at it (measured: 0.04% of the early "
        "energy left after 2 s with this off, 97% with it up). Bounded by a "
        "soft clip inside the loop and a cap below unity, swept across the "
        "whole plane -- and it cannot start from silence.");

    resonance->addKnob (ids::sympDecay, "String decay",
        "How long the sympathetic strings ring. This is usually what decides "
        "the plugin's reported tail.");

    resonance->addKnob (ids::sympBrightness, "String tone",
        "How much of the strings' upper partials speaks. A voicing control on "
        "what they say, not on how hard they are driven.");

    resonance->addGap();

    resonance->addKnob (ids::listenAmount, "Listen amount",
        "Where you STAND, blended in from where the plugin used to put you. At "
        "0 every mode is heard equally -- which is what a plain modal sum is, "
        "and is not any real point on a real object -- and the plugin is the "
        "mono instrument it has always been, bit for bit. At 1 the two knobs "
        "below are two real listening points and the stereo is the geometry "
        "rather than a widener.");

    resonance->addKnob (ids::listenLeft, "Listen left",
        "The left ear's position along the object. Modes are weighted by "
        "sin(k pi q), the same law the strike uses, so an ear at 0.5 hears "
        "EXACTLY nothing of every even mode. Measured: listening at the middle "
        "reads 0.76 of the flat sum's level, not half, because a struck "
        "object's energy sits in its low modes.");

    resonance->addKnob (ids::listenRight, "Listen right",
        "The right ear's position. **Width and mono compatibility trade off "
        "directly** and nothing escapes it: mirrored pairs at 0.05/0.95 read "
        "-0.36 correlation and keep 0.57 of their level in mono, while "
        "0.45/0.55 read +0.86 and keep 0.96. At MATCHED width an asymmetric "
        "pair survives better -- 0.10/0.75 and 0.20/0.80 are equally wide and "
        "keep 0.64 against 0.60 -- so offset the two rather than mirroring "
        "them if the mix folds down.");

    resonance->setNote (
        "The taraf reads the same scale the keys do: fed pitchless noise, the weakest "
        "degree rings 857 times louder than the loudest gap between degrees. Drone is a "
        "feedback loop around a nonlinearity and carries the full section 7 kit -- "
        "clip inside the loop, cap below unity, and a swept test with teeth.");

    pages_[2] = std::move (resonance);
    addChildComponent (*pages_[2]);

    // TUNING is the shared panel, not a ControlPage; pages_[3] stays null
    // and showPage() knows it.
}

void MalleusEditor::showPage (int index)
{
    currentPage_ = index;

    for (int i = 0; i < kNumPages; ++i)
    {
        if (pages_[static_cast<std::size_t> (i)] != nullptr)
            pages_[static_cast<std::size_t> (i)]->setVisible (i == index);

        tabs_[static_cast<std::size_t> (i)].setColour (
            juce::TextButton::buttonColourId,
            i == index ? palette_.accent.withAlpha (0.30f)
                       : palette_.panel.brighter (0.12f));
        tabs_[static_cast<std::size_t> (i)].setColour (
            juce::TextButton::textColourOffId,
            i == index ? palette_.accentBright : palette_.text);
    }

    const bool tuning = index == kNumPages - 1;

    tuningPanel_.setVisible (tuning);

    // The stack picture yields the room on the TUNING page, where the scale
    // itself is the subject and the table needs the height.
    modeStack_->setVisible (! tuning);

    if (tuning)
        tuningPanel_.refresh();

    resized();
}

void MalleusEditor::updateForExciter()
{
    // **Either slot can be any exciter**, so a control belongs to the note if
    // *either* slot reaches it -- and only if that slot is actually audible.
    // Blend at 0 makes slot B silent, so its controls grey out; the moment the
    // blend leaves zero they come back. Getting this from one slot would grey
    // out the bow controls on a mallet-plus-bow blend, which is precisely the
    // patch the second slot exists for.
    const bool bAudible = shownBlend_ > 0.0f;
    const bool aAudible = shownBlend_ < 1.0f;

    const auto uses = [&] (Exciter which)
    {
        const int index = static_cast<int> (which);

        return (aAudible && shownExciter_ == index)
            || (bAudible && shownExciterB_ == index);
    };

    const bool mallet = uses (Exciter::Mallet);
    const bool roll = uses (Exciter::Roll);
    const bool bow = uses (Exciter::Bow);

    auto& excite = *pages_[1];

    // Hardness and the scrape are contact properties: a pluck is a
    // displacement release and a bow never leaves the surface.
    excite.setControlEnabled (ids::hardness, mallet || roll);
    excite.setControlEnabled (ids::hardnessVel, mallet || roll);
    excite.setControlEnabled (ids::noiseAmount, mallet || roll);

    excite.setControlEnabled (ids::bowPressure, bow);
    excite.setControlEnabled (ids::bowSpeed, bow);

    excite.setControlEnabled (ids::rollStart, roll);
    excite.setControlEnabled (ids::rollRatio, roll);
    excite.setControlEnabled (ids::rollMinimum, roll);
    excite.setControlEnabled (ids::rollHumanise, roll);

    // Slot B's choice is inert until the blend leaves zero.
    excite.setControlEnabled (ids::exciterB, bAudible);

    // The bow sustains while the key is held, so a tension drop that lands
    // in 80 ms is a bend at the start of a held note rather than a drop.
    excite.setControlEnabled (ids::dropDepth, ! bow);
    excite.setControlEnabled (ids::dropTime, ! bow);

    auto& resonance = *pages_[2];
    const bool taraf = shownSympCount_ > 0;

    resonance.setControlEnabled (ids::sympRoot, taraf);
    resonance.setControlEnabled (ids::sympLevel, taraf);
    resonance.setControlEnabled (ids::sympCoupling, taraf);
    resonance.setControlEnabled (ids::sympDrone, taraf);
    resonance.setControlEnabled (ids::sympDecay, taraf);
    resonance.setControlEnabled (ids::sympBrightness, taraf);

    // The two positions do nothing until there is an amount to blend them in.
    const bool listening = shownListen_ > 0.0f;

    resonance.setControlEnabled (ids::listenLeft, listening);
    resonance.setControlEnabled (ids::listenRight, listening);
}

void MalleusEditor::timerCallback()
{
    voicesLabel_.setText (juce::String (malleus_.getActiveVoiceCount()) + " / 16 voices",
                          juce::dontSendNotification);

    if (modeStack_->isVisible())
        modeStack_->refresh();

    // Greying is recomputed only when a switch actually moved -- doing it
    // fifteen times a second would repaint the panel for nothing.
    int exciter = 0;
    int exciterB = 0;
    int sympCount = 0;
    float blend = 0.0f;
    float listen = 0.0f;

    const auto read = [this] (const char* id, auto& into)
    {
        if (auto* raw = malleus_.getState().getRawParameterValue (id))
            into = static_cast<std::remove_reference_t<decltype (into)>> (raw->load());
    };

    read (ids::exciter, exciter);
    read (ids::exciterB, exciterB);
    read (ids::sympCount, sympCount);
    read (ids::exciterBlend, blend);
    read (ids::listenAmount, listen);

    // The blend and the amount are continuous, so only their *audibility*
    // decides the greying -- comparing the values themselves would repaint
    // the panel on every frame of a knob drag.
    if (exciter != shownExciter_
        || exciterB != shownExciterB_
        || (sympCount > 0) != (shownSympCount_ > 0)
        || (blend > 0.0f) != (shownBlend_ > 0.0f)
        || (blend < 1.0f) != (shownBlend_ < 1.0f)
        || (listen > 0.0f) != (shownListen_ > 0.0f))
    {
        shownExciter_ = exciter;
        shownExciterB_ = exciterB;
        shownSympCount_ = sympCount;
        shownBlend_ = blend;
        shownListen_ = listen;
        updateForExciter();
    }
}

void MalleusEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette_.background);

    // The rule under the header, in the plugin's own colour.
    g.setColour (palette_.accent.withAlpha (0.35f));
    g.fillRect (8, kHeaderHeight - 2, getWidth() - 16, 1);
}

void MalleusEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (kHeaderHeight).reduced (10, 4);

    titleLabel_.setBounds (header.removeFromLeft (110));
    tooltipsButton_.setBounds (header.removeFromRight (28).withSizeKeepingCentre (24, 22));
    vendorLabel_.setBounds (header.removeFromRight (80));
    voicesLabel_.setBounds (header.removeFromRight (110));
    subtitleLabel_.setBounds (header);

    bounds.reduce (8, 6);

    if (modeStack_->isVisible())
        modeStack_->setBounds (bounds.removeFromTop (kStackHeight));

    auto tabRow = bounds.removeFromTop (kTabHeight + 4).reduced (0, 2);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth)
                  .reduced (2, 0));

    bounds.removeFromTop (4);

    for (auto& page : pages_)
        if (page != nullptr)
            page->setBounds (bounds);

    tuningPanel_.setBounds (bounds);
}

} // namespace tezla::malleus
