#include <tezla/ui/ModRing.hpp>

#include <cmath>

#include <tezla/dsp/Exact.hpp>

namespace tezla::ui
{

namespace
{
/// How far the ring sits outside the knob's own arc.
///
/// LookAndFeel_V4 draws a rotary inside its bounds reduced by 10, and the arc's
/// outer edge lands exactly on that inscribed radius -- so there are ten pixels
/// of clear margin to draw in, and four of them puts the ring clear of the knob
/// without reaching the label above it.
constexpr float kRingGap = 4.0f;

constexpr float kArmedThickness   = 3.5f;
constexpr float kUnarmedThickness = 2.5f;

/// Pixels of vertical drag for the full -1 .. +1 travel. A knob's own drag is
/// 250 px for its whole range; depth is a smaller quantity and wants a finer
/// hand, so it gets rather more.
constexpr double kDragPixels = 320.0;
} // namespace

ModRing::ModRing (ModulationView& view, juce::Slider& slider, int destination,
                  const juce::String& destinationName)
    : view_ (view), slider_ (slider), destination_ (destination),
      destinationName_ (destinationName)
{
    setInterceptsMouseClicks (false, false);
    view_.addChangeListener (this);
    changeListenerCallback (nullptr);
}

ModRing::~ModRing()
{
    view_.removeChangeListener (this);
}

void ModRing::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Armed means this overlay is live. hitTest() still narrows it to the knob
    // face, so the value box underneath keeps working while a source is armed.
    setInterceptsMouseClicks (view_.getArmedSource() != ModulationView::none, false);
    updateTooltip();
    refresh();
}

void ModRing::updateTooltip()
{
    const int armed = view_.getArmedSource();

    if (armed == ModulationView::none)
    {
        setTooltip ({});
        return;
    }

    const juce::String source = ModulationView::nameForSource (armed);
    const int slot = view_.findSlot (armed, destination_);

    if (slot < 0 && view_.getSlotsUsed() >= ModulationView::numSlots)
    {
        setTooltip ("All " + juce::String (ModulationView::numSlots) + " modulation slots are in use. "
                    "Drag an existing ring to zero to free one.");
        return;
    }

    setTooltip (source + " -> " + destinationName_ + "\n\n"
                "Drag up or down to set how far " + source + " moves this control; up is positive, "
                "down inverts it. Hold Shift for a fine drag. Double-click, or drag back to zero, to "
                "remove the assignment.\n\n"
                "At exactly zero the control is untouched -- not nearly, exactly -- so a ring parked "
                "in the middle cannot change the sound by a single bit.");
}

ModRing::Geometry ModRing::geometry() const
{
    // Asked of the LookAndFeel, which is the same call the slider itself makes,
    // so the ring is concentric with the knob whatever the text box is doing.
    const auto layout = slider_.getLookAndFeel().getSliderLayout (slider_);
    const auto face = layout.sliderBounds.toFloat().reduced (10.0f);

    const auto parameters = slider_.getRotaryParameters();

    Geometry g;
    g.centre = face.getCentre();
    g.radius = juce::jmin (face.getWidth(), face.getHeight()) * 0.5f + kRingGap;
    g.startAngle = parameters.startAngleRadians;
    g.endAngle   = parameters.endAngleRadians;

    return g;
}

float ModRing::angleFor (Geometry g, double proportion) const
{
    const auto clamped = static_cast<float> (juce::jlimit (0.0, 1.0, proportion));
    return g.startAngle + clamped * (g.endAngle - g.startAngle);
}

void ModRing::drawArc (juce::Graphics& g, Geometry geo, double from, double to,
                       juce::Colour colour, float thickness) const
{
    if (std::abs (to - from) < 1.0e-4)
        return;

    const float a = angleFor (geo, juce::jmin (from, to));
    const float b = angleFor (geo, juce::jmax (from, to));

    juce::Path arc;
    arc.addCentredArc (geo.centre.x, geo.centre.y, geo.radius, geo.radius, 0.0f, a, b, true);

    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

bool ModRing::hitTest (int x, int y)
{
    if (view_.getArmedSource() == ModulationView::none)
        return false;

    // The knob face only. Leaving the text box alone means a value can still be
    // typed while a source is armed, and it keeps the mode from feeling like the
    // panel has been taken away.
    const auto layout = slider_.getLookAndFeel().getSliderLayout (slider_);
    return layout.sliderBounds.contains (x, y);
}

void ModRing::paint (juce::Graphics& g)
{
    if (! slider_.isEnabled())
        return;

    const auto geo = geometry();
    const double base = view_.baseProportionFor (destination_);
    const int armed = view_.getArmedSource();

    if (armed == ModulationView::none)
    {
        const double total = view_.totalDepthFor (destination_);

        if (total <= 0.0)
            return;

        // The reach, not the direction: an LFO swings both ways, a follower only
        // one, and the ring does not know which is pointed here. Drawing the
        // span symmetrically overstates a follower's downward half by design --
        // it is a "this much can move" mark, and the dot below says where it is.
        //
        // In the owning source's colour when there is exactly one, so a page at
        // rest already says which LFO is on which knob. Grey when two share a
        // destination, because a blend of two colours would name a third source.
        const int sole = view_.soleSourceFor (destination_);
        const auto colour = sole == ModulationView::none ? view_.getPalette().dimText
                                                         : ModulationView::colourForSource (sole);

        drawArc (g, geo, base - total, base + total, colour.withAlpha (0.6f), kUnarmedThickness);
    }
    else
    {
        const auto colour = ModulationView::colourForSource (armed);
        const int slot = view_.findSlot (armed, destination_);
        const bool assignable = slot >= 0 || view_.getSlotsUsed() < ModulationView::numSlots;

        // Every modulatable knob shows its ring while something is armed, so
        // what can be assigned is visible rather than discovered by dragging.
        drawArc (g, geo, 0.0, 1.0,
                 (assignable ? colour : view_.getPalette().dimText).withAlpha (0.18f),
                 kArmedThickness);

        if (slot >= 0)
        {
            const double depth = view_.getSlotDepth (slot);
            drawArc (g, geo, base, base + depth, colour, kArmedThickness);
        }
    }

    // Where modulation has actually moved this control, right now. The one part
    // of the picture that is not a setting.
    const double offset = view_.liveOffsetFor (destination_);

    if (std::abs (offset) > 1.0e-6)
    {
        const float angle = angleFor (geo, base + offset) - juce::MathConstants<float>::halfPi;
        const juce::Point<float> dot { geo.centre.x + geo.radius * std::cos (angle),
                                       geo.centre.y + geo.radius * std::sin (angle) };

        const int owner = armed == ModulationView::none ? view_.soleSourceFor (destination_) : armed;

        g.setColour (owner == ModulationView::none
                         ? view_.getPalette().text
                         : ModulationView::colourForSource (owner).brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (5.5f, 5.5f).withCentre (dot));
    }
}

void ModRing::refresh()
{
    const int armed = view_.getArmedSource();
    const double base = view_.baseProportionFor (destination_);
    const double total = view_.totalDepthFor (destination_);
    const double offset = view_.liveOffsetFor (destination_);
    const int slot = armed == ModulationView::none ? -1 : view_.findSlot (armed, destination_);
    const double depth = slot >= 0 ? view_.getSlotDepth (slot) : 0.0;

    // A repaint per knob per frame would be thirty full redraws a second of a
    // page that mostly is not moving. Half a pixel on this radius is about
    // 0.002 of the travel, so anything below that would not have been visible.
    const auto same = [] (double a, double b) { return std::abs (a - b) < 0.002; };

    if (armed == shownArmed_ && same (base, shownBase_) && same (total, shownTotal_)
        && same (offset, shownOffset_) && same (depth, shownDepth_))
        return;

    shownArmed_  = armed;
    shownBase_   = base;
    shownTotal_  = total;
    shownOffset_ = offset;
    shownDepth_  = depth;

    repaint();
}

void ModRing::mouseDown (const juce::MouseEvent&)
{
    const int armed = view_.getArmedSource();

    if (armed == ModulationView::none)
        return;

    // Allocating on mouse-down rather than on first movement means a slot is
    // taken by a click that goes nowhere -- and handed straight back by mouseUp,
    // because a depth of zero is not an assignment. That is cheaper than
    // guessing at what counts as a drag.
    draggingSlot_ = view_.allocateSlot (armed, destination_);

    if (draggingSlot_ < 0)
        return;

    depthAtDragStart_ = view_.getSlotDepth (draggingSlot_);
    view_.beginDepthGesture (draggingSlot_);
}

void ModRing::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingSlot_ < 0)
        return;

    const double scale = event.mods.isShiftDown() ? 0.25 : 1.0;
    const double moved = -static_cast<double> (event.getDistanceFromDragStartY())
                       * scale * 2.0 / kDragPixels;

    view_.setSlotDepth (draggingSlot_, depthAtDragStart_ + moved);
    refresh();
}

void ModRing::mouseUp (const juce::MouseEvent&)
{
    if (draggingSlot_ < 0)
        return;

    view_.endDepthGesture (draggingSlot_);

    // Dragged back to nothing -- or never dragged at all -- gives the slot back,
    // so the eight are a budget the user spends rather than one they leak.
    if (dsp::isExactlyZero (view_.getSlotDepth (draggingSlot_)))
        view_.freeSlot (draggingSlot_);

    draggingSlot_ = -1;
    refresh();
}

void ModRing::mouseDoubleClick (const juce::MouseEvent&)
{
    const int armed = view_.getArmedSource();

    if (armed == ModulationView::none)
        return;

    if (const int slot = view_.findSlot (armed, destination_); slot >= 0)
    {
        view_.freeSlot (slot);
        refresh();
    }
}

} // namespace tezla::ui
