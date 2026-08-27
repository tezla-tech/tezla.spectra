#pragma once

// A published, measured 12AX7 -- as a reference to be measured against, not as
// something to ship.
//
// This lives in tools/ rather than in shared/tezla-dsp for a reason. It is not
// part of any plugin and never runs on an audio thread: it exists so that the
// closed-form stage we *do* ship has something real to be compared with, and so
// that the comparison is a number in a test rather than a claim in a comment.
//
// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------
//
// Dempwolf, Holters and Zoelzer's physically-motivated triode model, with the
// constants they fitted to a measured 12AX7. Both are reproduced in Table 1 and
// section 4.1 of:
//
//   J. Macak, J. Schimmel and M. Holters, "Simulation of Fender Type Guitar
//   Preamp Using Approximation and State-Space Model", Proc. DAFx-12, York,
//   September 2012.
//
// citing K. Dempwolf and U. Zoelzer, "A physically-motivated triode model for
// circuit simulations", Proc. DAFx-11, Paris, 2011.
//
//   i_g = G_g * ( log(1 + exp(C_g * v_g)) / C_g ) ^ xi_g
//   i_p = G_k * ( log(1 + exp(C_k * (v_p/mu + v_g))) / C_k ) ^ xi_k  -  i_g
//
// The paper writes both with a leading minus, for a sign convention in which
// grid and plate current point *out* at the terminals. The magnitudes are
// identical and the signs here follow the ordinary convention.
//
// This is exactly the case CLAUDE.md section 9 describes as a legitimate copy:
// the constants are the result of somebody's measurements of a real bottle, and
// no measurement of ours could produce them. They are typed in verbatim and
// attributed here and in docs/DSP-REFERENCES.md.
//
// ---------------------------------------------------------------------------
// The shape of the equation, which is the interesting part
// ---------------------------------------------------------------------------
//
// Strip the constants and the model is a softplus raised to a power:
//
//   log(1 + exp(C*x)) / C     -- a smooth rectifier; as C grows it becomes
//                                max(x, 0), the hard cutoff
//   ( . ) ^ xi                -- the space-charge power law
//
// So the published fit is Child's law with a *smoothed* cutoff corner, and
// xi_k = 1.303 rather than the ideal 3/2 -- a real measured departure from
// ideal space-charge flow. The other published triode models are the same
// structure at different exponents: Cardarilli's plate law is a 3/2 power of
// the same drive term, Koren's is 1.4, and the quadric surface model of
// Giampiccolo et al. (DAFx-23) turns out to be exactly a squared linear form,
// which is the same law at 2.0. Published fits of one 12AX7 therefore span
// 1.303 to 2.0 depending on the model family, which is a wider spread than most
// of the arguments about tube models admit to.
//
// What none of them are is integrable in closed form, which is why we do not
// ship this one: ADAA needs an antiderivative, and a softplus to a
// non-integer power has none.

#include <cmath>

namespace tezla::measure {

/// The published fit, and a common-cathode stage built around it.
class Triode12AX7
{
public:
    // Dempwolf's fitted constants for a 12AX7, verbatim.
    static constexpr double kGg = 6.06e-4;
    static constexpr double kCg = 13.9;
    static constexpr double kXg = 1.354;

    static constexpr double kGk = 2.14e-3;
    static constexpr double kCk = 3.04;
    static constexpr double kXk = 1.303;
    static constexpr double kMu = 100.8;

    /// log(1 + exp(c*x)) / c, without overflowing for large arguments.
    [[nodiscard]] static double softplus (double c, double x) noexcept
    {
        const double t = c * x;
        return (t > 30.0 ? t : std::log1p (std::exp (t))) / c;
    }

    /// Grid current. Zero for a comfortably negative grid, rising once the grid
    /// approaches the cathode.
    [[nodiscard]] static double gridCurrent (double vg) noexcept
    {
        return kGg * std::pow (softplus (kCg, vg), kXg);
    }

    /// Plate current, for a grid and plate voltage.
    [[nodiscard]] static double plateCurrent (double vg, double vp) noexcept
    {
        return kGk * std::pow (softplus (kCk, vp / kMu + vg), kXk) - gridCurrent (vg);
    }
};

/// One common-cathode stage: the triode above with a plate resistor to a supply.
///
/// The plate voltage appears on both sides of the current law, so the operating
/// point is the solution of vp = Vsupply - ip(vg, vp) * Rp. Solved here by
/// bisection, which is far too slow for audio and exactly right for a reference
/// that has to be trusted.
class Triode12AX7Stage
{
public:
    /// A Fender-ish first stage: 100k from 250 V, grid biased 1.5 V below the
    /// cathode. Close to the values in the DAFx-12 schematic.
    static constexpr double kDefaultSupply = 250.0;
    static constexpr double kDefaultPlateResistor = 100.0e3;
    static constexpr double kDefaultBias = -1.5;

    constexpr Triode12AX7Stage() noexcept = default;

    constexpr Triode12AX7Stage (double supply, double plateResistor, double bias) noexcept
        : supply_ (supply), plateResistor_ (plateResistor), bias_ (bias)
    {
    }

    /// The plate voltage for a grid voltage, absolute.
    [[nodiscard]] double plateVoltage (double vg) const noexcept
    {
        double lo = 0.0;
        double hi = supply_;

        // 200 halvings takes the interval far below double precision; this is
        // an offline reference and there is no reason to be clever.
        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);

            if (mid > supply_ - Triode12AX7::plateCurrent (vg, mid) * plateResistor_)
                hi = mid;
            else
                lo = mid;
        }

        return 0.5 * (lo + hi);
    }

    /// The quiescent plate voltage, with no signal on the grid.
    [[nodiscard]] double quiescentPlateVoltage() const noexcept
    {
        return plateVoltage (bias_);
    }

    /// Small-signal voltage gain about the operating point. Negative: the stage
    /// inverts.
    [[nodiscard]] double smallSignalGain() const noexcept
    {
        constexpr double h = 1.0e-4;
        return (plateVoltage (bias_ + h) - plateVoltage (bias_ - h)) / (2.0 * h);
    }

    /// The stage's transfer, normalised the way tezla::dsp::Triode is: zero in
    /// gives zero out, and the small-signal gain is unity and inverting. That
    /// is what makes the two directly comparable.
    [[nodiscard]] double normalised (double v) const noexcept
    {
        const double quiescent = quiescentPlateVoltage();
        return (plateVoltage (bias_ + v) - quiescent) / std::abs (smallSignalGain());
    }

private:
    double supply_        { kDefaultSupply };
    double plateResistor_ { kDefaultPlateResistor };
    double bias_          { kDefaultBias };
};

} // namespace tezla::measure
