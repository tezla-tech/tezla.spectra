#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include <tezla/dsp/Adsr.hpp>
#include <tezla/dsp/Scales.hpp>

namespace tezla::sonitus
{

namespace
{
// Sonitus's own accent: an acid green-yellow, against Emberdrive's ember,
// Halo's gold, Capstone's steel, Anvil's hot iron and Transpectus's green. The
// secondary carries the modulation -- the LFO bars, the playing step, an
// envelope's live level -- which is the one reading here that is not a level,
// and the two must not be confusable at a glance.
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

// The cell. Deliberately small: sixty controls on six pages only fit at a size
// the stock JUCE rotary cannot be read at, which is what the arc-and-pointer
// look and feel in shared/tezla-ui is for. 62 pixels is a 12-pixel name, a
// 35-pixel knob and a 14-pixel number, and every one of those is legible.
constexpr int kCellLabelHeight = 12;
constexpr int kCellValueHeight = 14;
constexpr int kMinCellHeight = 62;
constexpr int kMaxCellHeight = 80;

/// Cells stop widening past this, and the grid centres instead. A four-column
/// group on a wide window used to stretch each cell to two hundred pixels of
/// which thirty-nine were the knob; capping the width is most of the answer to
/// "too much space used".
constexpr int kMaxCellWidth = 172;

constexpr int kHeadingHeight = 19;
constexpr int kGroupGap = 7;
constexpr int kPagePad = 5;

/// The strip along the bottom of the *editor* that carries the current page's
/// note. It used to be the last thing on the page itself, which put it below
/// the fold on exactly the two pages long enough to scroll -- and the MANGLE
/// note is the one that says what oversampling is doing right now, which is the
/// least useful thing in the plugin to have to scroll to find.
constexpr int kNoteHeight = 38;

// The envelope page's block: a heading, then a graph beside two rows of knobs.
constexpr int kEnvKnobRows = 2;
constexpr int kEnvKnobColumns = 3;
constexpr int kEnvBodyHeight = kEnvKnobRows * kMinCellHeight + 4;
constexpr int kEnvBlockHeight = kHeadingHeight + kEnvBodyHeight + 4;

constexpr int kMinWidth  = 880;
constexpr int kMinHeight = 560;
constexpr int kMaxWidth  = 1720;
constexpr int kMaxHeight = 1240;

constexpr int kMeterWidth = ui::LevelMeter::kMinimumWidth + ui::LevelMeter::kScaleWidth;
constexpr int kTabHeight = 26;
constexpr int kStatusHeight = 26;
constexpr int kStepStripHeight = 108;

/// Splits "NAME -- what it is" into its two halves. Everything after the dashes
/// is set in dim text, which is what stops six headings on a page reading as six
/// paragraphs.
[[nodiscard]] std::pair<juce::String, juce::String> splitHeading (const juce::String& text)
{
    const auto separator = text.indexOf (" -- ");

    if (separator < 0)
        return { text, {} };

    return { text.substring (0, separator).trim(), text.substring (separator + 4).trim() };
}

/// Draws a group's heading: its name, its explanation, and a rule running out
/// to the right edge so the group reads as one thing.
void paintHeading (juce::Graphics& g, const ui::Palette& palette, juce::Rectangle<int> row,
                   const juce::String& name, const juce::String& detail)
{
    const auto text = row.reduced (8, 0);

    g.setColour (palette.accent);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));

    const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name);
    g.drawText (name, text, juce::Justification::centredLeft, false);

    int x = text.getX() + nameWidth + 8;

    if (detail.isNotEmpty())
    {
        g.setColour (palette.dimText);
        g.setFont (juce::FontOptions (10.5f));

        const auto detailWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), detail);

        g.drawText (detail, text.withX (x).withWidth (juce::jmax (0, text.getRight() - x)),
                    juce::Justification::centredLeft, false);

        x += juce::jmin (detailWidth, juce::jmax (0, text.getRight() - x)) + 8;
    }

    if (x < text.getRight())
    {
        g.setColour (palette.accent.withAlpha (0.16f));
        g.drawHorizontalLine (row.getCentreY(), static_cast<float> (x),
                              static_cast<float> (text.getRight()));
    }
}
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
// Cells
// ---------------------------------------------------------------------------

ParameterCell::ParameterCell (juce::String parameterId, const juce::String& name, ui::Palette palette)
    : id_ (std::move (parameterId)), palette_ (palette)
{
    // Upper case, because a name set beside a number wants to be read as a
    // label rather than as a word -- and at ten pixels the capitals are the
    // legible half of the alphabet anyway.
    label_.setText (name.toUpperCase(), juce::dontSendNotification);
    label_.setJustificationType (juce::Justification::centred);
    label_.setColour (juce::Label::textColourId, palette_.dimText);
    label_.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    label_.setMinimumHorizontalScale (0.65f);
    addAndMakeVisible (label_);

    // The parameter's own id, so `tezla-render editor hit:cutoff` can ask
    // whether that control is the thing a click at its centre would reach. The
    // Transpectus lesson: a buried control is still visible and still enabled,
    // and from the outside is indistinguishable from one that was never added.
    setComponentID (id_);
}

void ParameterCell::resized()
{
    label_.setBounds (getLocalBounds().removeFromTop (kCellLabelHeight));
}

juce::Rectangle<int> ParameterCell::controlBounds() const
{
    return getLocalBounds().withTrimmedTop (kCellLabelHeight);
}

KnobCell::KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                    const juce::String& name, const juce::String& tooltip, ui::Palette palette)
    : ParameterCell (parameterId, name, palette)
{
    slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, kCellValueHeight);
    slider_.setColour (juce::Slider::textBoxTextColourId, palette_.text);
    slider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider_.setTooltip (tooltip);
    label_.setTooltip (tooltip);

    // Double-click goes back to the parameter's own default rather than to the
    // middle of its range, which on a skewed range is somewhere else entirely.
    if (auto* parameter = state.getParameter (parameterId))
        slider_.setDoubleClickReturnValue (
            true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

    addAndMakeVisible (slider_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterId, slider_);
}

void KnobCell::setControlEnabled (bool enabled)
{
    slider_.setEnabled (enabled);

    // The number too. A greyed knob beside a bright reading says the control is
    // inert but its value still matters, which is the opposite of the truth.
    slider_.setColour (juce::Slider::textBoxTextColourId,
                       enabled ? palette_.text : palette_.dimText.withAlpha (0.4f));

    label_.setColour (juce::Label::textColourId,
                      enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
    slider_.repaint();
    label_.repaint();
}

void KnobCell::resized()
{
    ParameterCell::resized();
    slider_.setBounds (controlBounds().reduced (2, 0));
}

ChoiceCell::ChoiceCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                        const juce::String& name, const juce::String& tooltip, ui::Palette palette)
    : ParameterCell (parameterId, name, palette)
{
    // Populated from the parameter itself. A ComboBoxAttachment selects an item
    // by index and does not create one, so a box left empty here stays empty on
    // screen and cannot be operated at all.
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId)))
        box_.addItemList (parameter->choices, 1);
    else
        jassertfalse;   // a ChoiceCell on something that is not a choice parameter

    box_.setColour (juce::ComboBox::backgroundColourId, palette_.panel.brighter (0.10f));
    box_.setColour (juce::ComboBox::textColourId, palette_.text);
    box_.setTooltip (tooltip);
    label_.setTooltip (tooltip);
    addAndMakeVisible (box_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, parameterId, box_);
}

void ChoiceCell::setControlEnabled (bool enabled)
{
    box_.setEnabled (enabled);
    label_.setColour (juce::Label::textColourId,
                      enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
    label_.repaint();
}

void ChoiceCell::resized()
{
    ParameterCell::resized();
    auto area = controlBounds();
    box_.setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 6, 118), 22)
                        .withY (area.getY() + 4));
}

ToggleCell::ToggleCell (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                        const juce::String& name, const juce::String& tooltip, ui::Palette palette)
    : ParameterCell (parameterId, name, palette)
{
    button_.setButtonText ("");
    button_.setTooltip (tooltip);
    label_.setTooltip (tooltip);
    addAndMakeVisible (button_);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, parameterId, button_);
}

void ToggleCell::setControlEnabled (bool enabled)
{
    button_.setEnabled (enabled);
    label_.setColour (juce::Label::textColourId,
                      enabled ? palette_.dimText : palette_.dimText.withAlpha (0.35f));
    label_.repaint();
}

void ToggleCell::resized()
{
    ParameterCell::resized();
    auto area = controlBounds();
    button_.setBounds (area.withSizeKeepingCentre (40, 20).withY (area.getY() + 6));
}

// ---------------------------------------------------------------------------
// ControlPage
// ---------------------------------------------------------------------------

void ControlPage::addHeading (const juce::String& text, int columns)
{
    const auto parts = splitHeading (text);

    groups_.push_back ({ parts.first, parts.second, juce::jmax (1, columns), {}, {} });
}

ControlPage::Group& ControlPage::currentGroup()
{
    if (groups_.empty())
        groups_.push_back ({ {}, {}, 5, {}, {} });

    return groups_.back();
}

void ControlPage::add (std::unique_ptr<ParameterCell> cell)
{
    auto* raw = cell.get();

    addAndMakeVisible (*raw);
    owned_.push_back (std::move (cell));
    currentGroup().cells.push_back (raw);
}

void ControlPage::addKnob (const juce::String& parameterId, const juce::String& name,
                           const juce::String& tooltip)
{
    add (std::make_unique<KnobCell> (state_, parameterId, name, tooltip, palette_));
}

void ControlPage::addChoice (const juce::String& parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    add (std::make_unique<ChoiceCell> (state_, parameterId, name, tooltip, palette_));
}

void ControlPage::addToggle (const juce::String& parameterId, const juce::String& name,
                             const juce::String& tooltip)
{
    add (std::make_unique<ToggleCell> (state_, parameterId, name, tooltip, palette_));
}

void ControlPage::addGap()
{
    currentGroup().cells.push_back (nullptr);
}

void ControlPage::setControlEnabled (const juce::String& parameterId, bool enabled)
{
    for (auto& cell : owned_)
        if (cell->parameterId() == parameterId)
            cell->setControlEnabled (enabled);
}

int ControlPage::rowsIn (const Group& group) const
{
    return (static_cast<int> (group.cells.size()) + group.columns - 1) / group.columns;
}

int ControlPage::totalRows() const
{
    int rows = 0;

    for (const auto& group : groups_)
        rows += rowsIn (group);

    return rows;
}

int ControlPage::getPreferredHeight() const
{
    int height = 2 * kPagePad;

    for (const auto& group : groups_)
        height += kHeadingHeight + rowsIn (group) * kMinCellHeight + 4 + kGroupGap;

    return height;
}

void ControlPage::paint (juce::Graphics& g)
{
    // Only as far down as the content goes. A page with two groups in a tall
    // window used to paint a panel over the whole of it and leave two thirds of
    // that panel empty, which reads as a layout that has gone wrong rather than
    // as a page that is simply short.
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().withHeight (
        contentHeight_ > 0 ? contentHeight_ : getHeight()).toFloat(), 6.0f);

    for (const auto& group : groups_)
    {
        if (group.bounds.isEmpty())
            continue;

        g.setColour (palette_.panel.brighter (0.055f));
        g.fillRoundedRectangle (group.bounds.toFloat(), 5.0f);

        if (group.heading.isNotEmpty())
            paintHeading (g, palette_, group.bounds.withHeight (kHeadingHeight),
                          group.heading, group.detail);
    }
}

void ControlPage::resized()
{
    if (groups_.empty())
        return;

    auto bounds = getLocalBounds().reduced (6, kPagePad);

    const int rows = totalRows();
    const int groups = static_cast<int> (groups_.size());
    const int available = bounds.getHeight() - groups * (kHeadingHeight + 4 + kGroupGap);

    const int cellHeight = rows > 0
        ? juce::jlimit (kMinCellHeight, kMaxCellHeight, available / rows)
        : kMinCellHeight;

    int y = bounds.getY();

    for (auto& group : groups_)
    {
        const int groupHeight = kHeadingHeight + rowsIn (group) * cellHeight + 4;

        group.bounds = { bounds.getX(), y, bounds.getWidth(), groupHeight };

        auto inner = group.bounds.reduced (4, 2).withTrimmedTop (kHeadingHeight);

        // A group with fewer controls than columns centres on what it has
        // rather than on what it was allowed. Three knobs laid out on a
        // five-column grid used to sit against the left of a centred block,
        // which looks like two missing controls rather than like three.
        const int used = juce::jmax (1, juce::jmin (group.columns,
                                                    static_cast<int> (group.cells.size())));

        const int cellWidth = juce::jmin (kMaxCellWidth, inner.getWidth() / group.columns);
        const int left = inner.getX() + (inner.getWidth() - cellWidth * used) / 2;

        for (std::size_t i = 0; i < group.cells.size(); ++i)
        {
            if (group.cells[i] == nullptr)
                continue;

            const int column = static_cast<int> (i) % group.columns;
            const int row = static_cast<int> (i) / group.columns;

            group.cells[i]->setBounds ({ left + column * cellWidth,
                                         inner.getY() + row * cellHeight,
                                         cellWidth, cellHeight });
        }

        y += groupHeight + kGroupGap;
    }

    contentHeight_ = y - bounds.getY() - kGroupGap + 2 * kPagePad;
}

// ---------------------------------------------------------------------------
// EnvelopeEditor
// ---------------------------------------------------------------------------

namespace
{
// How much of the graph's width each segment is allotted. The sustain's slice
// is fixed and the other three are filled in proportion to their parameters, so
// the four together are exactly the full width when all three are at maximum.
constexpr float kAttackShare  = 0.26f;
constexpr float kDecayShare   = 0.26f;
constexpr float kHoldShare    = 0.16f;
constexpr float kReleaseShare = 0.32f;

constexpr float kHandleRadius = 4.5f;
constexpr float kGrabRadius = 11.0f;

/// The width the live-level column takes on the right.
constexpr float kLevelColumn = 7.0f;

/// The strip along the bottom that carries the A / D / S / R marks.
constexpr float kAxisHeight = 11.0f;
} // namespace

EnvelopeEditor::EnvelopeEditor (juce::AudioProcessorValueTreeState& state, ui::Palette palette,
                                juce::String attackId, juce::String decayId, juce::String sustainId,
                                juce::String releaseId, juce::String shapeId)
    : state_ (state), palette_ (palette),
      attackId_ (std::move (attackId)), decayId_ (std::move (decayId)),
      sustainId_ (std::move (sustainId)), releaseId_ (std::move (releaseId)),
      shapeId_ (std::move (shapeId))
{
    setTooltip ("Drag the three corners: the first sets the attack, the middle one sets the decay "
                "across and the sustain up and down, and the last sets the release. Double-click a "
                "corner to put it back to its default. The knobs do the same job to the sample -- "
                "this is for finding the shape, they are for pinning it down.\n\n"
                "The curve drawn is the curve that plays, Shape included. The bar on the right is "
                "this envelope's live output on the most recent note.");
}

double EnvelopeEditor::segment (double u, double from, double to, double overshoot)
{
    // The library's arithmetic, not an approximation of it: a segment aims past
    // its destination by `overshoot` and stops when it arrives, so it traverses
    // the first 1/T of an exponential -- the curved part -- rather than crawling
    // asymptotically into its own target. See shared/tezla-dsp Adsr.hpp.
    const double distance = to - from;

    if (std::abs (distance) < 1.0e-12)
        return to;

    const double target = to + distance * (overshoot - 1.0);
    const double decay = std::pow ((overshoot - 1.0) / overshoot, u);

    return target + (from - target) * decay;
}

float EnvelopeEditor::normalised (const juce::String& id) const
{
    if (auto* parameter = state_.getParameter (id))
        return parameter->getValue();

    return 0.0f;
}

void EnvelopeEditor::setNormalised (const juce::String& id, float value, bool gesture)
{
    auto* parameter = state_.getParameter (id);

    if (parameter == nullptr)
        return;

    const float clamped = juce::jlimit (0.0f, 1.0f, value);

    if (gesture)
        parameter->beginChangeGesture();

    parameter->setValueNotifyingHost (clamped);

    if (gesture)
        parameter->endChangeGesture();
}

EnvelopeEditor::Geometry EnvelopeEditor::geometry() const
{
    Geometry g;

    g.plot = getLocalBounds().toFloat().reduced (9.0f, 8.0f);
    g.plot.removeFromBottom (kAxisHeight);
    g.plot.removeFromRight (kLevelColumn + 5.0f);

    const float width = g.plot.getWidth();
    const float x0 = g.plot.getX();

    g.attackX = x0 + width * kAttackShare * normalised (attackId_);
    g.decayX = x0 + width * kAttackShare + width * kDecayShare * normalised (decayId_);
    g.holdEndX = g.decayX + width * kHoldShare;
    g.releaseX = g.holdEndX + width * kReleaseShare * normalised (releaseId_);

    g.sustainY = g.plot.getBottom() - normalised (sustainId_) * g.plot.getHeight();

    return g;
}

juce::Point<float> EnvelopeEditor::handlePosition (Handle handle, const Geometry& g) const
{
    switch (handle)
    {
        case Handle::attack:       return { g.attackX, g.plot.getY() };
        case Handle::decaySustain: return { g.decayX, g.sustainY };
        case Handle::release:      return { g.releaseX, g.plot.getBottom() };
        case Handle::none:         break;
    }

    return {};
}

EnvelopeEditor::Handle EnvelopeEditor::handleAt (juce::Point<float> position) const
{
    const auto g = geometry();

    Handle best = Handle::none;
    float bestDistance = kGrabRadius;

    for (auto handle : { Handle::attack, Handle::decaySustain, Handle::release })
    {
        const float distance = position.getDistanceFrom (handlePosition (handle, g));

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = handle;
        }
    }

    return best;
}

void EnvelopeEditor::refresh (double level)
{
    const float values[5] { normalised (attackId_), normalised (decayId_), normalised (sustainId_),
                            normalised (releaseId_), normalised (shapeId_) };

    bool changed = std::abs (static_cast<float> (level) - shownLevel_) > 1.0e-3f;

    for (int i = 0; i < 5; ++i)
        if (std::abs (values[i] - shown_[i]) > 1.0e-6f)
            changed = true;

    if (! changed)
        return;

    for (int i = 0; i < 5; ++i)
        shown_[i] = values[i];

    shownLevel_ = static_cast<float> (level);
    level_ = static_cast<float> (juce::jlimit (0.0, 1.0, level));

    repaint();
}

void EnvelopeEditor::appendSegment (juce::Path& path, float x0, float y0, float x1, float y1,
                                    double from, double to, double overshoot) const
{
    juce::ignoreUnused (y0, y1);

    constexpr int kSteps = 28;

    const auto g = geometry();

    // A segment with no width is a vertical edge -- a zero attack is a click,
    // and drawing it as one is honest.
    if (x1 - x0 < 0.5f)
    {
        path.lineTo (x1, g.plot.getBottom() - static_cast<float> (to) * g.plot.getHeight());
        return;
    }

    for (int step = 1; step <= kSteps; ++step)
    {
        const double u = static_cast<double> (step) / kSteps;
        const double level = segment (u, from, to, overshoot);

        path.lineTo (x0 + (x1 - x0) * static_cast<float> (u),
                     g.plot.getBottom() - static_cast<float> (level) * g.plot.getHeight());
    }
}

void EnvelopeEditor::paint (juce::Graphics& graphics)
{
    const auto g = geometry();
    const auto full = getLocalBounds().toFloat();

    graphics.setColour (palette_.panel.darker (0.45f));
    graphics.fillRoundedRectangle (full, 4.0f);

    // The grid: quarters, with the top and bottom rails brighter because those
    // two are the ones a value is read against.
    for (int line = 0; line <= 4; ++line)
    {
        const float y = g.plot.getBottom() - g.plot.getHeight() * static_cast<float> (line) / 4.0f;

        graphics.setColour (palette_.dimText.withAlpha (line == 0 || line == 4 ? 0.22f : 0.09f));
        graphics.drawHorizontalLine (juce::roundToInt (y), g.plot.getX(), g.plot.getRight());
    }

    const double sustain = normalised (sustainId_);
    const double overshoot = dsp::Adsr::kSharpestOvershoot
                           * std::pow (dsp::Adsr::kStraightestOvershoot / dsp::Adsr::kSharpestOvershoot,
                                       static_cast<double> (normalised (shapeId_)));

    juce::Path curve;
    curve.startNewSubPath (g.plot.getX(), g.plot.getBottom());
    appendSegment (curve, g.plot.getX(), 0.0f, g.attackX, 0.0f, 0.0, 1.0, overshoot);
    appendSegment (curve, g.attackX, 0.0f, g.decayX, 0.0f, 1.0, sustain, overshoot);
    curve.lineTo (g.holdEndX, g.sustainY);
    appendSegment (curve, g.holdEndX, 0.0f, g.releaseX, 0.0f, sustain, 0.0, overshoot);

    // Under the curve, so the shape reads as an amount rather than as a line.
    {
        juce::Path filled (curve);
        filled.lineTo (g.releaseX, g.plot.getBottom());
        filled.closeSubPath();

        graphics.setColour (palette_.accent.withAlpha (0.13f));
        graphics.fillPath (filled);
    }

    graphics.setColour (palette_.accent);
    graphics.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

    // The hold, overdrawn dashed: it lasts as long as the key is down, which is
    // the one part of the picture that is not a duration.
    {
        const float dashes[] { 3.0f, 3.0f };

        graphics.setColour (palette_.panel.darker (0.45f));
        graphics.drawLine (g.decayX, g.sustainY, g.holdEndX, g.sustainY, 2.6f);

        graphics.setColour (palette_.accent.withAlpha (0.8f));
        graphics.drawDashedLine ({ { g.decayX, g.sustainY }, { g.holdEndX, g.sustainY } },
                                 dashes, 2, 1.8f);
    }

    // The handles.
    for (auto handle : { Handle::attack, Handle::decaySustain, Handle::release })
    {
        const auto centre = handlePosition (handle, g);
        const bool lit = handle == dragging_ || handle == hovered_;
        const float radius = lit ? kHandleRadius + 1.0f : kHandleRadius;

        graphics.setColour (palette_.panel.darker (0.6f));
        graphics.fillEllipse (juce::Rectangle<float> { radius * 2.0f + 2.0f, radius * 2.0f + 2.0f }
                                .withCentre (centre));

        graphics.setColour (lit ? palette_.accentBright : palette_.accent);
        graphics.fillEllipse (juce::Rectangle<float> { radius * 2.0f, radius * 2.0f }
                                .withCentre (centre));
    }

    // The axis marks, under the segments they name rather than under the slices
    // those segments were allotted. Under the allotments they stay put while a
    // handle is dragged, which is tidier and wrong: with a 5 ms attack the "A"
    // sat a quarter of the way across a graph whose attack was three pixels
    // wide. A mark that does not point at the thing it names is decoration.
    {
        const auto axis = juce::Rectangle<float> { g.plot.getX(), g.plot.getBottom() + 1.0f,
                                                   g.plot.getWidth(), kAxisHeight };

        graphics.setFont (juce::FontOptions (9.0f, juce::Font::bold));

        const float edges[5] { g.plot.getX(), g.attackX, g.decayX, g.holdEndX, g.releaseX };
        static const char* names[4] { "A", "D", "S", "R" };

        for (int i = 0; i < 4; ++i)
        {
            const float width = edges[i + 1] - edges[i];

            // A segment too narrow to hold its own letter is left unlabelled --
            // a zero attack has nothing to point at, and a letter squeezed
            // between two others reads as belonging to neither.
            if (width < 11.0f)
                continue;

            graphics.setColour (palette_.dimText.withAlpha (0.65f));
            graphics.drawText (names[i], axis.withX (edges[i]).withWidth (width),
                               juce::Justification::centred, false);
        }
    }

    // The live level, in the modulation colour rather than the accent: it is a
    // reading, and everything else on this panel is a control.
    {
        const auto column = juce::Rectangle<float> { full.getRight() - kLevelColumn - 4.0f,
                                                     g.plot.getY(), kLevelColumn,
                                                     g.plot.getHeight() };

        graphics.setColour (palette_.panel.darker (0.25f));
        graphics.fillRoundedRectangle (column, 2.0f);

        if (level_ > 1.0e-4f)
        {
            graphics.setColour (palette_.secondary);
            graphics.fillRoundedRectangle (
                column.withTop (column.getBottom() - level_ * column.getHeight()), 2.0f);
        }
    }
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& event)
{
    dragging_ = handleAt (event.position);

    if (dragging_ == Handle::none)
        return;

    const auto begin = [this] (const juce::String& id)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->beginChangeGesture();
    };

    switch (dragging_)
    {
        case Handle::attack:       begin (attackId_); break;
        case Handle::decaySustain: begin (decayId_); begin (sustainId_); break;
        case Handle::release:      begin (releaseId_); break;
        case Handle::none:         break;
    }
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging_ == Handle::none)
        return;

    const auto g = geometry();
    const float width = g.plot.getWidth();

    if (width <= 0.0f)
        return;

    switch (dragging_)
    {
        case Handle::attack:
            setNormalised (attackId_,
                           (event.position.x - g.plot.getX()) / (width * kAttackShare), false);
            break;

        case Handle::decaySustain:
            setNormalised (decayId_,
                           (event.position.x - g.plot.getX() - width * kAttackShare)
                             / (width * kDecayShare), false);
            setNormalised (sustainId_,
                           (g.plot.getBottom() - event.position.y) / g.plot.getHeight(), false);
            break;

        case Handle::release:
            setNormalised (releaseId_,
                           (event.position.x - g.holdEndX) / (width * kReleaseShare), false);
            break;

        case Handle::none:
            break;
    }
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    const auto end = [this] (const juce::String& id)
    {
        if (auto* parameter = state_.getParameter (id))
            parameter->endChangeGesture();
    };

    switch (dragging_)
    {
        case Handle::attack:       end (attackId_); break;
        case Handle::decaySustain: end (decayId_); end (sustainId_); break;
        case Handle::release:      end (releaseId_); break;
        case Handle::none:         break;
    }

    dragging_ = Handle::none;
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& event)
{
    const auto hovered = handleAt (event.position);

    if (hovered == hovered_)
        return;

    hovered_ = hovered;
    setMouseCursor (hovered_ == Handle::none ? juce::MouseCursor::NormalCursor
                                             : juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
{
    if (hovered_ == Handle::none)
        return;

    hovered_ = Handle::none;
    repaint();
}

void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto handle = handleAt (event.position);

    const auto reset = [this] (const juce::String& id)
    {
        if (auto* parameter = state_.getParameter (id))
            setNormalised (id, parameter->getDefaultValue(), true);
    };

    switch (handle)
    {
        case Handle::attack:       reset (attackId_); break;
        case Handle::decaySustain: reset (decayId_); reset (sustainId_); break;
        case Handle::release:      reset (releaseId_); break;
        case Handle::none:         break;
    }
}

// ---------------------------------------------------------------------------
// EnvelopePage
// ---------------------------------------------------------------------------

EnvelopePage::EnvelopePage (juce::AudioProcessorValueTreeState& state, ui::Palette palette)
    : palette_ (palette)
{
    addBlock (state, "AMPLITUDE", "the voice's own level",
              ids::ampAttack, ids::ampDecay, ids::ampSustain, ids::ampRelease, ids::ampShape,
              ids::ampVelocity, "Velocity",
              "How much of the level comes from how hard the note was played.");

    addBlock (state, "MOD ENVELOPE 1", "point it at something in MOD",
              ids::env1Attack, ids::env1Decay, ids::env1Sustain, ids::env1Release, ids::env1Shape,
              nullptr, {}, {});

    addBlock (state, "MOD ENVELOPE 2", "and the mangle takes these too",
              ids::env2Attack, ids::env2Decay, ids::env2Sustain, ids::env2Release, ids::env2Shape,
              nullptr, {}, {});
}

void EnvelopePage::addBlock (juce::AudioProcessorValueTreeState& state, const juce::String& heading,
                             const juce::String& detail, const char* attackId, const char* decayId,
                             const char* sustainId, const char* releaseId, const char* shapeId,
                             const char* extraId, const juce::String& extraName,
                             const juce::String& extraTooltip)
{
    Block block;

    block.heading = heading;
    block.detail = detail;

    block.graph = std::make_unique<EnvelopeEditor> (state, palette_, attackId, decayId, sustainId,
                                                    releaseId, shapeId);

    // Named after its attack parameter, which is unique per envelope and needs
    // no second list to keep in step.
    block.graph->setComponentID (juce::String ("graph-") + attackId);
    addAndMakeVisible (*block.graph);

    const auto knob = [&] (const char* id, const juce::String& name, const juce::String& tooltip)
    {
        auto cell = std::make_unique<KnobCell> (state, id, name, tooltip, palette_);
        addAndMakeVisible (*cell);
        block.knobs.push_back (std::move (cell));
    };

    knob (attackId, "Attack",
          "How long from nothing to full. Skewed so the short end has room: a tenth of the travel "
          "is 5 ms and half of it is a second and a quarter.");
    knob (decayId, "Decay", "How long from full down to the sustain level.");
    knob (sustainId, "Sustain", "Where it holds while the key is down.");
    knob (releaseId, "Release", "How long it takes to fall away after the key is up.");
    knob (shapeId, "Shape",
          "How curved each segment is, and **not** how long any of them lasts -- the time "
          "constant is corrected for the shape, so this is a tone control rather than a second "
          "time control. At zero it is nearly a straight line, which sounds mechanical because "
          "nothing physical decays linearly. Turned up it is the sharp exponential of a capacitor "
          "discharging. The graph bends with it.");

    if (extraId != nullptr)
        knob (extraId, extraName, extraTooltip);

    blocks_.push_back (std::move (block));
}

void EnvelopePage::refresh (const SonitusProcessor& processor)
{
    for (std::size_t i = 0; i < blocks_.size(); ++i)
        blocks_[i].graph->refresh (processor.getEnvelopeLevel (static_cast<int> (i)));
}

int EnvelopePage::getPreferredHeight() const
{
    return 2 * kPagePad
         + static_cast<int> (blocks_.size()) * (kEnvBlockHeight + kGroupGap);
}

void EnvelopePage::paint (juce::Graphics& g)
{
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    for (const auto& block : blocks_)
    {
        if (block.bounds.isEmpty())
            continue;

        g.setColour (palette_.panel.brighter (0.055f));
        g.fillRoundedRectangle (block.bounds.toFloat(), 5.0f);

        paintHeading (g, palette_, block.bounds.withHeight (kHeadingHeight),
                      block.heading, block.detail);
    }
}

void EnvelopePage::resized()
{
    auto bounds = getLocalBounds().reduced (6, kPagePad);

    // The blocks share whatever is going spare equally, so the page fills a
    // tall window rather than stacking against the top of it.
    const int blocks = juce::jmax (1, static_cast<int> (blocks_.size()));
    const int height = juce::jmax (kEnvBlockHeight,
                                   (bounds.getHeight() - blocks * kGroupGap) / blocks);

    for (auto& block : blocks_)
    {
        block.bounds = bounds.removeFromTop (height);
        bounds.removeFromTop (kGroupGap);

        auto inner = block.bounds.reduced (5, 3).withTrimmedTop (kHeadingHeight);

        const int columns = kEnvKnobColumns;
        const int rows = kEnvKnobRows;

        const int cellWidth = juce::jlimit (86, kMaxCellWidth, inner.getWidth() / (2 * columns));
        const int cellHeight = juce::jlimit (kMinCellHeight, kMaxCellHeight, inner.getHeight() / rows);

        auto knobArea = inner.removeFromRight (cellWidth * columns);

        for (std::size_t i = 0; i < block.knobs.size(); ++i)
        {
            const int column = static_cast<int> (i) % columns;
            const int row = static_cast<int> (i) / columns;

            block.knobs[i]->setBounds ({ knobArea.getX() + column * cellWidth,
                                         knobArea.getY() + row * cellHeight,
                                         cellWidth, cellHeight });
        }

        block.graph->setBounds (inner.withTrimmedRight (8).reduced (0, 1));
    }
}

// ---------------------------------------------------------------------------
// StepStrip
// ---------------------------------------------------------------------------

namespace
{
/// The row of step numbers along the bottom.
constexpr int kStepNumberHeight = 11;
} // namespace

StepStrip::StepStrip (juce::AudioProcessorValueTreeState& state, ui::Palette palette)
    : palette_ (palette)
{
    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
    {
        auto& slider = sliders_[static_cast<std::size_t> (step)];

        slider.setSliderStyle (juce::Slider::LinearBarVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::trackColourId, palette_.accent.withAlpha (0.85f));
        slider.setColour (juce::Slider::backgroundColourId, palette_.panel.brighter (0.04f));
        slider.setTooltip ("Step " + juce::String (step + 1)
                             + ". Bipolar: the centre is no modulation, and either end is full "
                               "depth in that direction. Drag; double-click to centre.");

        if (auto* parameter = state.getParameter (ids::step (step)))
            slider.setDoubleClickReturnValue (
                true, parameter->convertFrom0to1 (parameter->getDefaultValue()));

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
    g.fillRoundedRectangle (bounds, 5.0f);

    paintHeading (g, palette_, getLocalBounds().withHeight (kHeadingHeight),
                  "PATTERN", "sixteen steps, bipolar; the centre is no modulation");

    bounds.removeFromTop (static_cast<float> (kHeadingHeight));

    auto numbers = bounds.removeFromBottom (static_cast<float> (kStepNumberHeight));
    bounds = bounds.reduced (3.0f, 2.0f);

    const float slotWidth = bounds.getWidth() / dsp::StepSequencer::kMaxSteps;

    g.setFont (juce::FontOptions (9.0f));

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
            g.fillRect (slot.reduced (1.0f, 0.0f));
        }

        if (step == playing_)
        {
            g.setColour (palette_.secondary.withAlpha (0.26f));
            g.fillRect (slot.reduced (1.0f, 0.0f));

            g.setColour (palette_.secondary);
            g.drawRect (slot.reduced (1.0f, 0.0f), 1.2f);
        }

        // Every fourth step numbered, and the beats brighter -- sixteen numbers
        // in a row is noise, four is a bar.
        const bool onBeat = step % 4 == 0;

        g.setColour (step == playing_ ? palette_.secondary
                                      : palette_.dimText.withAlpha (onBeat ? 0.75f : 0.30f));

        g.drawText (juce::String (step + 1),
                    numbers.withWidth (slotWidth)
                           .withX (bounds.getX() + slotWidth * static_cast<float> (step)),
                    juce::Justification::centred, false);
    }
}

void StepStrip::resized()
{
    auto bounds = getLocalBounds().withTrimmedTop (kHeadingHeight)
                                  .withTrimmedBottom (kStepNumberHeight)
                                  .reduced (3, 2);

    const int slotWidth = bounds.getWidth() / dsp::StepSequencer::kMaxSteps;

    for (int step = 0; step < dsp::StepSequencer::kMaxSteps; ++step)
        sliders_[static_cast<std::size_t> (step)].setBounds (
            juce::Rectangle<int> { bounds.getX() + slotWidth * step, bounds.getY(),
                                   slotWidth, bounds.getHeight() }.reduced (1, 0));
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
      lookAndFeel_ (kPalette),
      outputMeter_ (std::make_unique<ui::LevelMeter> (kPalette))
{
    // Set before anything is constructed under it, so every child inherits it
    // rather than half of them being built against the default.
    setLookAndFeel (&lookAndFeel_);

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

    viewport_.setComponentID ("pages");
    viewport_.setScrollBarsShown (true, false);
    viewport_.setScrollBarThickness (9);
    addAndMakeVisible (viewport_);

    steps_ = std::make_unique<StepStrip> (sonitus_.getState(), palette_);

    // **Added as a child, which it was not.** `setVisible` on a component with
    // no parent does nothing at all -- it does not throw, it does not warn, and
    // the component simply never paints. The MOD page showed a black gap where
    // the sequencer should be and the TUNING tab was blank, and both were this
    // one missing line. Nothing headless could have caught it: the editor is the
    // one part of this plugin no test can run. The tuning page is no longer in
    // this position to be forgotten -- it is a `Page` and the viewport owns its
    // visibility now.
    steps_->setComponentID ("steps");
    addAndMakeVisible (*steps_);

    static const char* tabNames[kNumPages] { "OSC", "FILTER", "ENV", "MOD", "MANGLE", "TUNING" };

    for (int i = 0; i < kNumPages; ++i)
    {
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        tab.setButtonText (tabNames[i]);
        tab.setClickingTogglesState (false);
        tab.onClick = [this, i] { showPage (i); };

        // Named so `tezla-render editor` can drive the panel: `click:tab-mod`
        // changes page, `hit:tab-mod` asks whether a click there would actually
        // reach it. Both matter here -- this editor has already shipped a page
        // that existed, was told to be visible, and was not on screen.
        tab.setComponentID ("tab-" + juce::String (tabNames[i]).toLowerCase());

        addAndMakeVisible (tab);
    }

    // An instrument's output has no ceiling parameter to measure against, so
    // the reference is 0 dBFS and "over" means over full scale.
    outputMeter_->setComponentID ("meter");
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
    outputMeterLabel_.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    addAndMakeVisible (outputMeterLabel_);

    noteLabel_.setJustificationType (juce::Justification::centred);
    noteLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    noteLabel_.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (noteLabel_);

    statusLabel_.setJustificationType (juce::Justification::centred);
    statusLabel_.setColour (juce::Label::textColourId, palette_.dimText);
    statusLabel_.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statusLabel_);

    showPage (0);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (1000, 660);

    startTimerHz (30);
}

SonitusEditor::~SonitusEditor()
{
    // Before any child is destroyed, and before the look and feel is. A
    // component holding a dangling pointer to one is a use-after-free with no
    // symptom until the host next repaints.
    setLookAndFeel (nullptr);
}

ControlPage* SonitusEditor::controlPage (int index) const
{
    if (index < 0 || index >= kNumPages)
        return nullptr;

    return dynamic_cast<ControlPage*> (pages_[static_cast<std::size_t> (index)].get());
}
void SonitusEditor::buildPages()
{
    auto& state = sonitus_.getState();

    // ---- OSC -----------------------------------------------------------------

    auto osc = std::make_unique<ControlPage> (state, palette_);

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

    osc->addHeading ("OSCILLATOR A -- the sync master", 5);
    addOscillator (*osc, ids::shapeA, ids::octaveA, ids::semitonesA, ids::centsA, ids::widthA,
                   ids::levelA, ids::unisonA, ids::detuneA, ids::spreadA, ids::driftA, "A");

    osc->addHeading ("OSCILLATOR B -- the sync slave and the PM target", 5);
    addOscillator (*osc, ids::shapeB, ids::octaveB, ids::semitonesB, ids::centsB, ids::widthB,
                   ids::levelB, ids::unisonB, ids::detuneB, ids::spreadB, ids::driftB, "B");

    osc->addHeading ("SYNC AND PHASE MODULATION -- B is the target of both", 2);

    osc->addToggle (ids::syncB, "Sync B",
        "Hard sync: B's phase is reset every time the played note's period comes round, so B's "
        "own pitch stops being a pitch and becomes a formant -- a peak in the spectrum that "
        "sweeps when you sweep B. This is the Pro-53 sound, and it is worth nothing standing "
        "still: put an envelope on Pitch B in the matrix and sweep it.");

    osc->addKnob (ids::pmIndex, "PM",
        "Phase modulation of B by A. Frequency modulation's better-behaved sibling -- the same "
        "sidebands with no DC drift, which is why every FM synth since the DX7 has actually been "
        "a PM synth. At small amounts it thickens; past about 2 it is a different instrument.");

    osc->addHeading ("SUB AND DESTRUCTION", 5);

    osc->addChoice (ids::subShape, "Sub shape",
        "Sine is pure weight and disappears on a laptop; square has odd harmonics that carry it "
        "through a small speaker.");

    osc->addKnob (ids::subOctave, "Sub oct",
        "Which octave the sub sits in, from two below the note to two above. Zero doubles the "
        "note rather than underpinning it, which is a thickener rather than a sub -- and above "
        "the note it stops being a sub at all and becomes a fixed-interval second voice that no "
        "amount of filtering can detune.");
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

    pages_[kOscPage] = std::move (osc);

    // ---- FILTER --------------------------------------------------------------

    auto filter = std::make_unique<ControlPage> (state, palette_);

    filter->addHeading ("FILTER -- zero-delay state variable, drive inside the loop", 4);

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

    filter->addHeading ("KEYBOARD", 4);

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

    pages_[kFilterPage] = std::move (filter);

    // ---- ENV -----------------------------------------------------------------

    // Its own page rather than a grid of fifteen knobs: an envelope's shape is
    // the thing being edited and five numbers do not show it. See EnvelopePage.
    pages_[kEnvPage] = std::make_unique<EnvelopePage> (state, palette_);

    // ---- MOD -----------------------------------------------------------------

    auto mod = std::make_unique<ControlPage> (state, palette_);

    mod->addHeading ("LFO 1 -- the one the sequencer can drive the rate of", 5);

    mod->addChoice (ids::lfo1Wave, "Wave", "Its shape. Sample & hold steps; smooth random glides.");
    mod->addKnob (ids::lfo1Rate, "Rate",
        "How fast, in hertz. **Zero is a legitimate setting and is the brief's original trick** "
        "-- the rate pinned at nothing so the depth is drawn from somewhere else entirely. Here "
        "that somewhere else is the sequencer below, or the host's automation on the depth.");
    mod->addKnob (ids::lfo1Smooth, "Smooth",
        "Rounds the corners off a square or a sample-and-hold, so a step becomes a slide.");
    mod->addToggle (ids::lfo1Retrig, "Retrig",
        "Restarts the LFO from the top of its cycle every time a note is pressed. Free-running is right for a wobble that should keep its place across a phrase; retriggered is right for anything that has to line up with the note -- a sweep, a stab, a phase that has to start in the same place every time. On a reese the difference is the whole sound: with this on, every note gets the same phase relationship and the growl is repeatable.");
    mod->addKnob (ids::lfo1Key, "Key track",
        "How far the LFO's rate follows the played note, referenced to middle C. At 100% an octave up doubles the rate, so the modulation stays in the same relationship to the pitch all the way up the keyboard -- which is how you get a phase or a wobble that reads as part of the tone rather than as an effect laid over it. At 0 the rate is the same at every pitch.");

    mod->addHeading ("LFO 2", 5);

    mod->addChoice (ids::lfo2Wave, "Wave", "Its shape.");
    mod->addKnob (ids::lfo2Rate, "Rate", "How fast, in hertz.");
    mod->addKnob (ids::lfo2Smooth, "Smooth", "Rounds its corners off.");
    mod->addToggle (ids::lfo2Retrig, "Retrig",
        "Restarts the LFO from the top of its cycle every time a note is pressed. Free-running is right for a wobble that should keep its place across a phrase; retriggered is right for anything that has to line up with the note -- a sweep, a stab, a phase that has to start in the same place every time. On a reese the difference is the whole sound: with this on, every note gets the same phase relationship and the growl is repeatable.");
    mod->addKnob (ids::lfo2Key, "Key track",
        "How far the LFO's rate follows the played note, referenced to middle C. At 100% an octave up doubles the rate, so the modulation stays in the same relationship to the pitch all the way up the keyboard -- which is how you get a phase or a wobble that reads as part of the tone rather than as an effect laid over it. At 0 the rate is the same at every pitch.");

    mod->addHeading ("SEQUENCER", 4);

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

    mod->addHeading ("VOICE MATRIX -- one set of these per sounding note", 6);

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
            "How much, and which way. **The law is square, so this knob is fine at the bottom and "
            "enormous at the top**: a tenth of the travel on Pitch is 72 cents and the end of it "
            "is six octaves. Ten octaves on Cutoff, an octave on Detune, sixteen on PM. "
            "Full depth is meant to be too much -- that is what it is for.");
    }

    mod->addHeading ("GLOBAL MATRIX -- one chain, shared by every note", 6);

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
            "How much, and which way, on the same square law as the voice matrix. Comb time and "
            "Phase centre move by six octaves at full depth -- a delay and a filter centre are "
            "pitches in disguise -- which is deliberately further than either control's own range, "
            "so a full-depth sweep drives into the ends and stays there. Tube moves 36 dB, "
            "Harmonic walks 23 partials. Output is the one held back at 24 dB: it is a level "
            "rather than a character, and a level swinging under an envelope is a hazard.");
    }

    pages_[kModPage] = std::move (mod);

    // ---- MANGLE --------------------------------------------------------------

    auto mangle = std::make_unique<ControlPage> (state, palette_);

    mangle->addHeading ("THE SPLIT -- the sub bypasses everything below", 5);

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

    mangle->addHeading ("THE COMB -- what this instrument is for", 5);

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

    mangle->addHeading ("VOWEL -- the comb, shaped like a mouth", 3);

    mangle->addKnob (ids::formantMorph, "Vowel",
        "Morphs across ee - eh - ah - oh - oo. Three resonant peaks at the frequencies a human "
        "tract actually puts them, from Peterson and Barney's measurements of adult male vowels. "
        "The same idea as the comb, shaped like a mouth.");

    mangle->addKnob (ids::formantSharp, "Sharpness",
        "How narrow the three peaks are. The gain is divided by the Q, so this sharpens the "
        "vowel rather than turning it up.");

    mangle->addKnob (ids::formantMix, "Vowel mix",
        "Dry against vowelled. At 0 the formant filter is bit-exactly out of the path.");

    mangle->addHeading ("OVERTONE -- the same key tracking, on the vowel", 4);

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

    mangle->addHeading ("OUTPUT", 2);

    mangle->addKnob (ids::output, "Output",
        "Trim, after everything. Defaults to -6 dB because an instrument with unison and a tube "
        "can comfortably exceed full scale, and clipping the host's bus is not a feature.");

    mangle->addChoice (ids::oversampling, "Oversampling",
        "How much headroom the nonlinear stages get. Auto targets about 192 kHz internally and "
        "reads your session's rate to decide. See the note below for what it is doing right now.");

    pages_[kManglePage] = std::move (mangle);

    // ---- TUNING --------------------------------------------------------------

    // A page like any other, and hosted by the viewport like any other. It was
    // neither before: it was a component the editor parented by hand, and the
    // hand-parenting is what got forgotten.
    pages_[kTuningPage] = std::make_unique<TuningPage> (sonitus_, palette_);
}

void SonitusEditor::showPage (int index)
{
    currentPage_ = juce::jlimit (0, kNumPages - 1, index);

    for (int i = 0; i < kNumPages; ++i)
    {
        auto& tab = tabs_[static_cast<std::size_t> (i)];

        const bool active = i == currentPage_;

        tab.setColour (juce::TextButton::buttonColourId,
                       active ? palette_.accent.withAlpha (0.55f) : palette_.panel.brighter (0.06f));
        tab.setColour (juce::TextButton::textColourOffId, active ? palette_.background : palette_.dimText);
    }

    // `false`: the viewport must not take ownership -- the pages outlive the
    // page changes and are owned by the array.
    viewport_.setViewedComponent (pages_[static_cast<std::size_t> (currentPage_)].get(), false);

    // The step strip belongs to the MOD page, so it follows the tab rather than
    // being always on screen.
    if (steps_ != nullptr)
        steps_->setVisible (currentPage_ == kModPage);

    if (auto* tuning = dynamic_cast<TuningPage*> (pages_[static_cast<std::size_t> (currentPage_)].get()))
        tuning->refresh();

    noteLabel_.setText (notes_[static_cast<std::size_t> (currentPage_)], juce::dontSendNotification);

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

    auto* osc = controlPage (kOscPage);
    auto* filter = controlPage (kFilterPage);
    auto* mangle = controlPage (kManglePage);

    if (combChanged && mangle != nullptr)
    {
        // Greyed rather than hidden: a knob that moves and does nothing reads
        // as a broken plugin rather than as a mode.
        const bool isFlange = combMode == static_cast<int> (CombMode::flange);
        const bool isPhase = combMode == static_cast<int> (CombMode::phase);
        const bool anyComb = isFlange || isPhase;

        for (const char* id : { ids::combTime, ids::combTrack, ids::combDamp })
            mangle->setControlEnabled (id, isFlange);

        for (const char* id : { ids::phaseFreq, ids::phaseStages })
            mangle->setControlEnabled (id, isPhase);

        for (const char* id : { ids::combFeed, ids::combSpread, ids::combMix, ids::combInvert })
            mangle->setControlEnabled (id, anyComb);
    }

    if (shapesChanged && osc != nullptr)
    {
        // Width does nothing to a saw or a sine: both are fully described
        // without it.
        const auto hasWidth = [] (int shape)
        {
            return shape == static_cast<int> (dsp::OscShape::pulse)
                || shape == static_cast<int> (dsp::OscShape::triangle);
        };

        osc->setControlEnabled (ids::widthA, hasWidth (shapeA));
        osc->setControlEnabled (ids::widthB, hasWidth (shapeB));
    }

    if (scaleChanged)
        if (auto* tuning = dynamic_cast<TuningPage*> (pages_[kTuningPage].get()))
            tuning->refresh();

    juce::ignoreUnused (osc, filter);

    notes_[kManglePage] = sonitus_.describeComb() + "  " + sonitus_.describeOversampling();

    notes_[kOscPage] = syncB != 0
        ? juce::String ("Sync is on: B restarts every time the note's period comes round, so its "
                        "Semis and Fine knobs are sweeping a formant rather than setting a pitch. "
                        "Put a mod envelope on Pitch B in the matrix -- standing still it is just "
                        "a bright waveform.")
        : juce::String ("Two dense sources, and the denser the better: a comb can only cut "
                        "harmonics that are there. Saw plus saw a few cents apart is already a "
                        "moving comb before anything on the MANGLE page is switched on.");

    notes_[kFilterPage] = keyMode == static_cast<int> (KeyboardMode::poly)
        ? juce::String ("Poly. Eight voices with seven-way unison on both oscillators is 112 "
                        "oscillators -- the number to watch. A reese does not need any of it: "
                        "switch to Mono and spend the CPU on oversampling instead.")
        : juce::String ("Mono, so the whole instrument is one voice and the CPU is free. Legato "
                        "differs in one thing and it matters: it does not retrigger the envelopes, "
                        "so a phrase played without gaps runs through a single envelope and glides "
                        "between its notes.");

    noteLabel_.setText (notes_[static_cast<std::size_t> (currentPage_)], juce::dontSendNotification);
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

    // Only the page on screen. Three envelope graphs each comparing five
    // parameters is cheap, but repainting a page nobody is looking at is not
    // cheap enough to do thirty times a second.
    if (auto* envelopes = dynamic_cast<EnvelopePage*> (pages_[kEnvPage].get()))
        if (envelopes->isVisible())
            envelopes->refresh (sonitus_);

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

    statusLabel_.setBounds (bounds.removeFromBottom (kStatusHeight).reduced (12, 3));

    // Reserved before anything else takes the space, and only when the page on
    // screen has something to say -- the ENV, MOD and TUNING pages do not, and
    // an empty strip is 38 pixels of nothing.
    noteLabel_.setVisible (notes_[static_cast<std::size_t> (currentPage_)].isNotEmpty());

    if (noteLabel_.isVisible())
        noteLabel_.setBounds (bounds.removeFromBottom (kNoteHeight).reduced (16, 2));

    auto right = bounds.removeFromRight (kMeterWidth + 8).reduced (3, 5);
    outputMeterLabel_.setBounds (right.removeFromBottom (11));
    outputMeter_->setBounds (right);

    auto tabRow = bounds.removeFromTop (kTabHeight).reduced (4, 1);
    const int tabWidth = tabRow.getWidth() / kNumPages;

    for (int i = 0; i < kNumPages; ++i)
        tabs_[static_cast<std::size_t> (i)].setBounds (
            tabRow.removeFromLeft (i == kNumPages - 1 ? tabRow.getWidth() : tabWidth).reduced (2, 0));

    auto body = bounds.reduced (4, 2);

    if (steps_ != nullptr && steps_->isVisible())
        steps_->setBounds (body.removeFromBottom (kStepStripHeight).withTrimmedTop (4));

    viewport_.setBounds (body);

    if (auto* page = pages_[static_cast<std::size_t> (currentPage_)].get())
    {
        // Sized before it is asked how tall it wants to be: the note's height
        // depends on nothing, but the row count does not fit until the width
        // is known, and a page that fits gets the viewport's full height so it
        // fills the window rather than sitting against the top.
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
