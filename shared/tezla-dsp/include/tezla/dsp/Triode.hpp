#pragma once

// The static transfer of a common-cathode triode stage, from the space-charge
// law rather than from a curve somebody drew.
//
// ---------------------------------------------------------------------------
// The physics
// ---------------------------------------------------------------------------
//
// In a space-charge-limited vacuum diode or triode the plate current follows
// Child's law: the current is proportional to the 3/2 power of the accelerating
// voltage, and it is exactly zero once that voltage goes negative -- there is no
// tail, because there is no mechanism for current to flow. For a triode the
// accelerating term is the grid voltage plus the plate voltage divided by the
// amplification factor.
//
// Write `e` for that term normalised so that e = 1 at the quiescent point and
// e = 0 at cutoff. Then, for a plate resistor turning current into voltage:
//
//      I(e) = e^p                          p = 3/2 for space-charge flow
//      Vout = B+ - I * Rp                  inverting, because more current
//                                          pulls the plate down
//
// Normalised to unity small-signal gain, with `k` the grid swing from quiescent
// to cutoff:
//
//      e(v) = 1 + v/k                      clamped at zero
//      f(v) = -(e^p - 1) * k/p             for e > 0
//             +k/p                         for e <= 0, the cutoff shelf
//
// ---------------------------------------------------------------------------
// What that shape actually does, and why it is worth having
// ---------------------------------------------------------------------------
//
// The two directions are not the same, and that asymmetry is the whole reason a
// single triode stage sounds like one:
//
//   - Driving the grid **negative** runs the stage towards cutoff. The curve
//     compresses and then stops dead at +k/p. Current cannot go below zero, so
//     this side is a hard ceiling.
//   - Driving the grid **positive** runs it into more current. The curve
//     *expands*: transconductance rises with current, so the gain goes up as the
//     signal gets louder. Nothing here bounds it -- a real stage is bounded by
//     the plate bottoming out near ground and by the grid starting to conduct,
//     and both of those are separate mechanisms with their own dynamics. They
//     belong in TriodeStage, not in this curve.
//
// So this is deliberately only half the story: the cutoff half, exactly, in
// closed form. Composing it with a soft ceiling afterwards is how the other half
// arrives, and keeping them apart is what lets each be antialiased exactly.
//
// The asymmetry generates even harmonics, and the stage inverts. Cascade two and
// the second stage's asymmetry partly cancels the first's; cascade three and it
// does not. That is not a curiosity -- it is a large part of why a two-stage
// preamp and a three-stage preamp sound different at the same gain.
//
// ---------------------------------------------------------------------------
// How close this is to a real valve, measured
// ---------------------------------------------------------------------------
//
// Not asserted -- fitted. The reference is Dempwolf's 12AX7, which is a model
// whose constants were fitted to a measured bottle, solved here on an actual
// load line (100k from 250 V, grid biased 1.5 V down) and normalised the same
// way this curve is. tools/include/tezla/measure/Triode12AX7.hpp holds it and
// tests/test_Triode.cpp pins the comparison.
//
// Over the cutoff half, vg in [-3, 0] volts:
//
//     rms error   0.0118 of a normalised unit
//     worst       0.0267, which is 2.39% of the 1.12 peak swing there
//     exponent    1.585 -- *fitted*, not assumed
//
// That last number is the one worth pausing on. The exponent was left free and
// came back within 6% of Child's 3/2, from a fit against somebody else's
// measurement of a real valve. It also sits inside the range the published
// 12AX7 fits already disagree over: Dempwolf 1.303, Koren 1.4, Cardarilli 1.5,
// and the quadric surface model of Giampiccolo et al. -- which turns out to be
// exactly a squared linear form, so 2.0. The spread between published models of
// the same bottle is wider than the error of this one against any of them.
//
// And the other side diverges, hard: at vg = +3 V this curve is 1.27 normalised
// units away from the reference, because the real stage compresses there and a
// power law above 1 expands. Fitting both halves at once drags the exponent
// down to 1.075 and still leaves 6.2% of error. That divergence is not a
// shortcoming to be tuned away -- it is the exact size of the two mechanisms
// this curve deliberately does not contain, and a test asserts it stays large.
// If it ever shrinks on its own, something has been folded in that does not
// belong.
//
// ---------------------------------------------------------------------------
// C1 at the cutoff corner, which is what keeps it quiet
// ---------------------------------------------------------------------------
//
// At the join the value matches (both give k/p) and so does the slope: the
// Child branch's derivative is -e^(p-1), which goes to zero as e does, and the
// shelf's derivative is zero. For p > 1 the curve is therefore C1 there, and a
// corner that is C1 aliases far less than one that is only C0.
//
// At p = 1 exactly that stops being true -- the slope arrives at -1 and meets a
// shelf at 0, which is a hard-clipper corner. That setting is allowed, because
// it is a usable sound, but it costs the C1 property and the aliasing shows it.
// The tests pin both facts.
//
// ---------------------------------------------------------------------------
// The exponent as a control
// ---------------------------------------------------------------------------
//
// p = 3/2 is physics. It is the default and it is what "tube" means here.
// Everything else on the dial is ours: below it the knee tightens towards a
// clipper, above it the expansion steepens into something no bottle does. The
// antiderivative generalises for any p > 0, so the whole range antialiases with
// the same machinery.

#include <algorithm>
#include <cmath>

namespace tezla::dsp {

/// The static curve of one common-cathode stage. Shaper interface: evaluate()
/// and antiderivative(), so it drops straight into Adaa1.
class Triode
{
public:
    /// Space-charge flow. The physical value, and the default.
    static constexpr double kChildExponent = 1.5;

    /// Below 1 the cutoff corner stops being C1; above 3 the expansion is steep
    /// enough that the stage is mostly a multiplier.
    static constexpr double kMinExponent = 1.0;
    static constexpr double kMaxExponent = 3.0;

    /// Grid swing from quiescent to cutoff, in the same units as the input.
    /// Bigger means more headroom before the hard side bites.
    static constexpr double kMinKnee = 0.05;

    constexpr Triode() noexcept = default;

    constexpr Triode (double knee, double exponent) noexcept
        : knee_ (knee < kMinKnee ? kMinKnee : knee),
          exponent_ (std::clamp (exponent, kMinExponent, kMaxExponent))
    {
    }

    [[nodiscard]] constexpr double getKnee() const noexcept { return knee_; }
    [[nodiscard]] constexpr double getExponent() const noexcept { return exponent_; }

    /// The cutoff shelf: the highest output the stage can produce, reached when
    /// the grid is driven a full knee below quiescent and held there.
    [[nodiscard]] constexpr double getCeiling() const noexcept
    {
        return knee_ / exponent_;
    }

    [[nodiscard]] double evaluate (double v) const noexcept
    {
        const double e = 1.0 + v / knee_;

        if (e <= 0.0)
            return getCeiling();

        return -(std::pow (e, exponent_) - 1.0) * knee_ / exponent_;
    }

    /// The exact integral of evaluate(), with F(0) = 0.
    ///
    ///   F(v) = -(k^2/p) * ( e^(p+1)/(p+1) - e )  -  k^2/(p+1)
    ///
    /// and below cutoff the shelf integrates to a straight line continuing from
    /// F(-k) = -k^2/(p+1).
    [[nodiscard]] double antiderivative (double v) const noexcept
    {
        const double p = exponent_;
        const double k = knee_;
        const double e = 1.0 + v / k;
        const double tail = -(k * k) / (p + 1.0);

        if (e <= 0.0)
            return tail + (k / p) * (v + k);

        return -(k * k / p) * (std::pow (e, p + 1.0) / (p + 1.0) - e) + tail;
    }

private:
    double knee_     { 1.0 };
    double exponent_ { kChildExponent };
};

/// Adaptor so Adaa1 can drive a Triode directly.
struct TriodeShaper
{
    Triode triode;

    [[nodiscard]] double evaluate (double x) const noexcept { return triode.evaluate (x); }
    [[nodiscard]] double antiderivative (double x) const noexcept { return triode.antiderivative (x); }
};

} // namespace tezla::dsp
