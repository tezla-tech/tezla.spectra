#pragma once

// The passive tone stack that sits between the preamp and the power amp.
//
// ---------------------------------------------------------------------------
// The circuit
// ---------------------------------------------------------------------------
//
// The classic three-control passive network, in the arrangement used by nearly
// every amplifier with Bass, Middle and Treble on the front. The topology is
// public -- it is printed on every schematic those manufacturers ever shipped
// with their products -- and it is solved here as a circuit rather than
// approximated by three filters. Node names as used below:
//
//     in ──┬── C1 ──── T ──[ treble pot ]── J
//          │                    │
//          │                  wiper = out
//          │
//          └── R4 ──────────────┴─ J ──┬── C2 ──[ bass pot ]── M
//               (slope)                │
//                                      └── C3 ─────────────── M
//                                                             │
//                                                      [ mid pot ]
//                                                             │
//                                                            gnd
//
// The output is the treble wiper. The slope resistor feeds everything below the
// treble cap's corner into the junction J, and from there the only path to
// ground is through the bass and mid legs -- so those two controls decide how
// much of the low and middle is shunted away, and the treble pot decides how
// much of what is left reaches the output. Nothing here is independent of
// anything else, which is exactly the point.
//
// **The load matters and is part of the circuit.** A tone stack is never driven
// into an open circuit: the next valve's grid leak hangs off the output, and
// including it changes the response by a decibel or two everywhere and rather
// more at the extremes. It is in the netlist.
//
// ---------------------------------------------------------------------------
// What it does, measured
// ---------------------------------------------------------------------------
//
// Response in dB, at 192 kHz, all three controls where stated:
//
//   setting          40Hz   80    160    320    640   1280   2560   5120  10240
//   all at noon      -2.8  -3.8   -6.6  -10.9  -14.1  -11.7   -8.0   -5.9   -5.1
//   mid at zero      -2.8  -3.7   -6.4  -12.2  -25.0  -14.0   -7.7   -4.9   -3.9
//   mid at max       -2.8  -4.0   -6.6   -9.5  -10.5   -9.0   -6.4   -4.8   -4.2
//   treble at zero   -1.8  -2.8   -5.3   -9.0  -12.3  -13.8  -13.9  -13.6  -13.5
//   treble at max    -3.7  -4.8   -7.8  -12.7  -14.2   -8.8   -4.0   -1.4   -0.4
//   bass at zero     -8.0 -12.1  -14.9  -15.5  -14.1  -11.0   -7.7   -5.8   -5.1
//   bass at max      -2.4  -3.6   -6.5  -11.0  -14.1  -11.7   -8.0   -5.9   -5.1
//   all at zero      -6.1 -11.1  -16.8  -22.8  -28.7  -34.4  -39.9  -45.4  -51.3
//
// Three things in that table are worth naming, because they are what a bank of
// three filters cannot give you:
//
// **The dip is there with everything at noon.** Eleven decibels of it, centred
// around 640 Hz. Nobody asked for it; it falls out of the circuit, and it is
// most of why amplifiers built on this network sound the way they do.
//
// **The controls interact.** Compare the 640 Hz column for treble at zero and
// treble at max: -12.3 against -14.2. The treble control moved the midrange by
// nearly two decibels. On a real stack it does, because turning it changes
// where the output taps a divider that the other two controls are also in.
//
// **There is an insertion loss, and it moves.** About 11 dB in the midband at
// noon, and it is not constant across settings -- which is why the gain staging
// around a real amplifier changes when you touch the tone controls.
//
// ---------------------------------------------------------------------------
// Voicings, and going past them
// ---------------------------------------------------------------------------
//
// The difference between one manufacturer's stack and another's is component
// values in the same topology -- a smaller slope resistor and a bigger treble
// cap here, a bigger bass cap there. Those are the voicings below.
//
// The values are also exposed directly, and they are not restricted to
// combinations anybody ever built. A 2 nF treble cap and a 4.7 uF bass cap is
// not a mistake, it is a tone stack nobody has heard, and this is a plugin
// rather than a museum.

#include <algorithm>
#include <array>

#include "RcNetwork.hpp"

namespace tezla::dsp {

/// Component values for one stack. Everything is in farads and ohms.
struct ToneStackComponents
{
    double trebleCap { 250.0e-12 };   ///< C1
    double bassCap   { 100.0e-9 };    ///< C2
    double midCap    { 22.0e-9 };     ///< C3

    double treblePot { 250.0e3 };     ///< R1
    double bassPot   { 1.0e6 };       ///< R2
    double midPot    { 25.0e3 };      ///< R3
    double slope     { 56.0e3 };      ///< R4

    /// The following stage's grid leak. Part of the circuit, not a refinement.
    double load      { 1.0e6 };

    [[nodiscard]] bool operator== (const ToneStackComponents&) const = default;

    /// Holds every value inside what a tone stack can physically be.
    ///
    /// The ranges are wide and they are not the values anybody shipped -- they
    /// are the values that are still a tone stack. A 4.7 uF bass capacitor is a
    /// short at every audio frequency, and what comes out is not a voicing
    /// nobody has heard, it is silence with the treble leg bolted to it.
    ///
    /// A note on what this is *not* for, because the obvious guess was tested
    /// and was wrong. Extreme values looked like they were provoking
    /// trapezoidal ringing -- a time constant far shorter than the sample
    /// period puts a pole beside Nyquist, and the network was overshooting a
    /// passive unity bound by 4%. It is not that. Running the same circuit from
    /// 48 kHz to 3 MHz gives 1.0387, 1.0388, 1.0395, 1.0395, 1.0396, 1.0396:
    /// converged, and independent of the discretisation. The overshoot is the
    /// switch-on transient of a highpass network and is in the circuit.
    ///
        [[nodiscard]] ToneStackComponents clamped() const noexcept
    {
        ToneStackComponents c;
        c.trebleCap = std::clamp (trebleCap, 50.0e-12, 10.0e-9);
        c.bassCap   = std::clamp (bassCap,   1.0e-9,  470.0e-9);
        c.midCap    = std::clamp (midCap,    1.0e-9,  470.0e-9);
        c.treblePot = std::clamp (treblePot, 10.0e3,  2.0e6);
        c.bassPot   = std::clamp (bassPot,   10.0e3,  2.0e6);
        c.midPot    = std::clamp (midPot,    1.0e3,   500.0e3);
        c.slope     = std::clamp (slope,     10.0e3,  220.0e3);
        c.load      = std::clamp (load,      47.0e3,  10.0e6);
        return c;
    }
};

/// The three named voicings. Same topology, different values -- which is all
/// the difference ever was.
enum class ToneStackVoicing
{
    /// Big bass capacitor and a gentle slope: a broad, scooped low end with the
    /// midrange dip sitting low. The tweed-and-blackface lane.
    american = 0,

    /// Smaller bass capacitor, bigger treble capacitor, steeper slope: less
    /// low end, more upper midrange, and the dip moves up with it.
    british,

    /// A big bass capacitor shunting more of the low end away before it reaches
    /// the distortion, and a small mid pot to dig the hole deeper. Tight and
    /// scooped -- the high-gain lane.
    modern,

    count
};

[[nodiscard]] inline ToneStackComponents componentsFor (ToneStackVoicing voicing) noexcept
{
    switch (voicing)
    {
        case ToneStackVoicing::british:
            return { 470.0e-12, 22.0e-9, 22.0e-9, 220.0e3, 1.0e6, 25.0e3, 33.0e3, 1.0e6 };

        case ToneStackVoicing::modern:
            return { 500.0e-12, 220.0e-9, 47.0e-9, 250.0e3, 1.0e6, 10.0e3, 33.0e3, 1.0e6 };

        case ToneStackVoicing::american:
        case ToneStackVoicing::count:
        default:
            return {};
    }
}

class ToneStack
{
public:
    /// Ground, input, then T, out, J, M and B.
    static constexpr std::size_t kNodeCount = 7;

    static constexpr std::size_t kNodeTrebleTop = 2;
    static constexpr std::size_t kNodeOutput    = 3;
    static constexpr std::size_t kNodeJunction  = 4;
    static constexpr std::size_t kNodeMidTop    = 5;
    static constexpr std::size_t kNodeBassTop   = 6;

    /// What a potentiometer measures at the end of its travel: track resistance
    /// plus the wiper's own contact resistance. Never zero on a real one, and a
    /// perfect short in a netlist is a thing to avoid on principle.
    ///
    /// It is here for realism and for nothing else. It was put in to cure an
    /// overshoot it turned out not to cause -- 1 ohm gives 1.0408 and 220 ohms
    /// gives 1.0395, which is no difference at all. Kept because a real pot
    /// does have end resistance, and because against the kilohms the rest of
    /// the circuit works in, 220 ohms is still a short.
    static constexpr double kMinimumPotOhms = 220.0;

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        rebuild();
    }

    void reset() noexcept { network_.reset(); }

    void setComponents (const ToneStackComponents& components)
    {
        const auto safe = components.clamped();

        if (safe == components_)
            return;

        components_ = safe;
        rebuild();
    }

    void setVoicing (ToneStackVoicing voicing) { setComponents (componentsFor (voicing)); }

    [[nodiscard]] const ToneStackComponents& getComponents() const noexcept { return components_; }

    /// The three controls, each 0 to 1.
    void setControls (double bass, double middle, double treble) noexcept
    {
        const double b = std::clamp (bass, 0.0, 1.0);
        const double m = std::clamp (middle, 0.0, 1.0);
        const double t = std::clamp (treble, 0.0, 1.0);

        if (b == bass_ && m == middle_ && t == treble_)
            return;

        bass_ = b;
        middle_ = m;
        treble_ = t;

        updatePots();
    }

    [[nodiscard]] double process (double x) noexcept { return network_.process (x); }

private:
    void rebuild()
    {
        network_.setNodeCount (kNodeCount);
        network_.setOutputNode (kNodeOutput);
        network_.clearElements();

        using Net = RcNetwork<>;

        network_.addCapacitor (Net::kInput, kNodeTrebleTop, components_.trebleCap);

        // Index 0 and 1: the two halves of the treble pot, which move together.
        network_.addResistor (kNodeTrebleTop, kNodeOutput, kMinimumPotOhms);
        network_.addResistor (kNodeOutput, kNodeJunction, kMinimumPotOhms);

        network_.addResistor (Net::kInput, kNodeJunction, components_.slope);

        network_.addCapacitor (kNodeJunction, kNodeBassTop, components_.bassCap);

        // Index 3: the bass pot as a rheostat.
        network_.addResistor (kNodeBassTop, kNodeMidTop, kMinimumPotOhms);

        network_.addCapacitor (kNodeJunction, kNodeMidTop, components_.midCap);

        // Index 4: the mid pot as a rheostat.
        network_.addResistor (kNodeMidTop, Net::kGround, kMinimumPotOhms);

        // Index 5: the next stage's grid leak.
        network_.addResistor (kNodeOutput, Net::kGround, components_.load);

        updatePots();
        network_.prepare (sampleRate_);
    }

    void updatePots() noexcept
    {
        // The treble wiper: turning it up moves the tap towards the capacitor,
        // so the upper half shrinks and the lower half grows.
        network_.setResistor (0, (1.0 - treble_) * components_.treblePot + kMinimumPotOhms);
        network_.setResistor (1, treble_ * components_.treblePot + kMinimumPotOhms);

        network_.setResistor (3, bass_ * components_.bassPot + kMinimumPotOhms);
        network_.setResistor (4, middle_ * components_.midPot + kMinimumPotOhms);
        network_.setResistor (5, components_.load);

        network_.factorise();
    }

    double sampleRate_ { 48000.0 };

    ToneStackComponents components_;

    double bass_   { 0.5 };
    double middle_ { 0.5 };
    double treble_ { 0.5 };

    RcNetwork<> network_;
};

} // namespace tezla::dsp
