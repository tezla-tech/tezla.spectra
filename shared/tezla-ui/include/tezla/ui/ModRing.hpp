#pragma once

// A modulation depth ring, drawn around a knob and dragged to set it.
//
// A transparent overlay rather than a custom LookAndFeel, because the depth is
// per-knob state and a LookAndFeel is shared by every knob in the plugin. It
// also means the ring can take the mouse only while a source is armed
// (`hitTest` below), so the knob underneath behaves exactly as it always has the
// rest of the time -- including its text box, which stays live even while armed.
//
// What it draws, in normalised parameter space, because that is the space
// modulation is added in:
//
//   nothing armed   a thin pale arc spanning how far modulation can move this
//                   control, and a dot where it currently has. A glance at the
//                   page then says what is moving without arming anything.
//
//   a source armed  every modulatable knob shows a faint ring in that source's
//                   colour, so what can be assigned is visible rather than
//                   guessed; the ones already assigned to it fill that ring in
//                   from the knob's own position.

#include <juce_gui_basics/juce_gui_basics.h>

#include "ModulationView.hpp"

namespace tezla::ui
{

class ModRing final : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::ChangeListener
{
public:
    /// `slider` is the knob this ring wraps; `destination` its index in the
    /// plugin's destination table.
    ModRing (ModulationView& view, juce::Slider& slider, int destination,
             const juce::String& destinationName);
    ~ModRing() override;

    /// Re-reads the live position and repaints if it moved. Called from the
    /// editor's timer; cheap enough to call on every knob every frame, and it
    /// repaints nothing when nothing has changed.
    void refresh();

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /// Where the knob's rotary actually is, asked of the LookAndFeel rather than
    /// reconstructed. Getting this from the slider's bounds directly would be
    /// wrong the moment a text box position or a thumb radius changed, and wrong
    /// in a way that only shows up as a ring sitting slightly off its knob.
    struct Geometry
    {
        juce::Point<float> centre;
        float radius {};
        float startAngle {};
        float endAngle {};
    };

    [[nodiscard]] Geometry geometry() const;
    [[nodiscard]] float angleFor (Geometry, double proportion) const;
    void drawArc (juce::Graphics&, Geometry, double from, double to,
                  juce::Colour, float thickness) const;

    void updateTooltip();

    ModulationView& view_;
    juce::Slider& slider_;
    int destination_;
    juce::String destinationName_;

    /// The slot being dragged, and what its depth was when the drag started.
    int   draggingSlot_ { -1 };
    double depthAtDragStart_ { 0.0 };

    /// What refresh() last drew, so it can tell whether anything moved.
    double shownBase_   { -1.0 };
    double shownTotal_  { -1.0 };
    double shownOffset_ { -99.0 };
    double shownDepth_  { -99.0 };
    int    shownArmed_  { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModRing)
};

} // namespace tezla::ui
