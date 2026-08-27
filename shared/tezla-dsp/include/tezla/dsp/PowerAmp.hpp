#pragma once

// The output stage: a push-pull pair, a supply that sags, a feedback loop that
// loosens its grip as things get loud, and a transformer whose core saturates
// on flux rather than on voltage.
//
// ---------------------------------------------------------------------------
// The one that matters
// ---------------------------------------------------------------------------
//
// Everything a preamp does, it does the same way at every pitch. The output
// transformer does not, and that single fact is most of the difference between
// a power amp being worked and a waveshaper being driven.
//
// A transformer's core carries magnetic flux, and flux is the *integral* of the
// voltage across the winding:
//
//     phi(t) = (1/N) * integral( v dt )
//
// For a sine of amplitude A at frequency f, the peak of that integral is
// A/(2*pi*f). So the flux a note puts into the core is inversely proportional
// to its pitch. Two notes at identical voltage, an octave apart, put twice as
// much flux into the core for the lower one. A low E saturates the core while a
// lead line two octaves up sails through it untouched -- and the amp gets
// woollier as you play lower, at the same volume.
//
// Nothing here special-cases frequency to get that. Integrate the voltage,
// let the permeability fall as the flux rises, and the behaviour falls out.
//
// Measured on this rig at 192 kHz, with the valves held bit-exactly linear so
// nothing else could be responsible:
//
//     40 Hz   flux 1.269   thd  -20.1 dB
//     80 Hz   flux 0.902   thd  -32.0 dB
//    160 Hz   flux 0.487   thd  -47.8 dB
//    320 Hz   flux 0.248   thd  -66.0 dB
//    640 Hz   flux 0.125   thd  -83.8 dB
//   1280 Hz   flux 0.062   thd -102.2 dB
//
// The flux falls 6 dB per octave, as an integral must. The distortion it
// produces falls about **18 dB per octave** -- the permeability term goes as
// the square of the flux, and the corner it moves is itself further from the
// note each time. Five octaves separate a filthy low E from a lead line that
// is beyond clean, at exactly the same voltage.
//
// The capacity is specified as a frequency, `coreFrequencyHz`, because that is
// the unit transformers are sold in: a core stores volt-seconds, so its size is
// naturally quoted as the lowest note it will pass at full power.
//
// The papers this was built alongside use a *linear* transformer model -- Cohen
// and Helie's DAFx-10 power amplifier is explicit that theirs is "a simple
// linear model", parameterised from datasheet inductances. That is the right
// call for what they were measuring and it leaves this on the table.
//
// ---------------------------------------------------------------------------
// The other three
// ---------------------------------------------------------------------------
//
// **Crossover.** A class AB pair hands the signal from one side to the other
// near zero, and neither valve is fully in charge during the handover. Modelled
// as x - c*w*tanh(x/w): a gain dip of exactly (1 - c) at the origin, becoming a
// constant offset once either side has taken over. tanh integrates to
// log(cosh), so this antialiases exactly like everything else here.
//
// **Sag.** The supply is a rectifier and a capacitor, not a laboratory bench.
// Average current pulls the rail down and it climbs back over tens of
// milliseconds, so the headroom available to a chord depends on what the last
// chord did. This is the compression people hear as "bloom" and attribute to
// the speaker.
//
// **Feedback that lets go.** A loop around the output stage trades gain for
// linearity, and the trade is only available while there is gain to trade. As
// the valves clip, their incremental gain collapses, the loop gain goes with
// it, and the correction the loop was making quietly stops arriving. An
// amplifier with heavy feedback is clean and then abruptly is not; one with
// little feedback is never quite clean and never abruptly anything. That
// difference is a large part of what separates a blackface from a plexi, and it
// needs no code of its own -- it is what a feedback loop around a saturating
// element does.

#include <algorithm>
#include <cmath>

#include "Adaa.hpp"
#include "Waveshapers.hpp"

namespace tezla::dsp {

/// Class AB crossover: a gain dip at the origin that becomes an offset once one
/// side has taken over.
///
///     f(x) = x - c*w*tanh(x/w)          f'(0) = 1 - c
///     F(x) = x^2/2 - c*w^2*log(cosh(x/w))
class Crossover
{
public:
    constexpr Crossover() noexcept = default;

    /// `depth` is how far the gain dips at the handover: 0 is pure class A,
    /// 1 is a dead zone. `width` is how much of the signal the handover
    /// occupies.
    Crossover (double depth, double width) noexcept
        : depth_ (std::clamp (depth, 0.0, 1.0)),
          width_ (std::max (width, 1.0e-6))
    {
    }

    [[nodiscard]] double evaluate (double x) const noexcept
    {
        return x - depth_ * width_ * std::tanh (x / width_);
    }

    [[nodiscard]] double antiderivative (double x) const noexcept
    {
        const double u = x / width_;

        // log(cosh(u)) without overflowing: for large |u| it is |u| - log 2.
        const double logCosh = std::abs (u) > 20.0
                                   ? std::abs (u) - 0.6931471805599453
                                   : std::log (std::cosh (u));

        return 0.5 * x * x - depth_ * width_ * width_ * logCosh;
    }

private:
    double depth_ { 0.0 };
    double width_ { 0.05 };
};

struct CrossoverShaper
{
    Crossover crossover;

    [[nodiscard]] double evaluate (double x) const noexcept { return crossover.evaluate (x); }
    [[nodiscard]] double antiderivative (double x) const noexcept { return crossover.antiderivative (x); }
};

struct PowerAmpParameters
{
    /// How hard the output stage is driven, linear.
    double drive { 1.0 };

    // ---- the valves ----------------------------------------------------------

    /// Where the pair runs out of swing.
    double ceiling { 1.0 };

    /// 0 is a hard corner, 1 is a full tanh. Power valves are softer than
    /// preamp ones because the screen supply gives way first.
    double knee { 0.7 };

    // ---- class AB --------------------------------------------------------------

    /// 0 is class A -- both valves conducting throughout, no handover. Real
    /// class AB amplifiers sit somewhere well below 1.
    double crossoverDepth { 0.18 };

    /// How much of the signal the handover occupies.
    double crossoverWidth { 0.035 };

    // ---- the supply ------------------------------------------------------------

    /// How far the rail falls when the stage is working, as a fraction.
    double sagDepth { 0.25 };

    /// How quickly it falls, and climbs back. A valve rectifier sags more and
    /// slower than a solid-state one.
    double sagMs { 45.0 };

    // ---- the feedback loop -----------------------------------------------------

    /// How much of the output is fed back. Zero is no loop at all, which is a
    /// perfectly ordinary way to build a guitar amplifier.
    double feedback { 0.35 };

    // ---- the output transformer ------------------------------------------------

    /// Where the primary inductance gives up the low end, with the core
    /// unsaturated.
    double transformerLowHz { 32.0 };

    /// Where the leakage inductance gives up the top.
    double transformerHighHz { 9000.0 };

    /// The frequency at which a full-scale swing just fills the core.
    ///
    /// This is the unit transformers are actually specified in. A core stores
    /// volt-seconds, and the peak flux a sine of amplitude A at frequency f
    /// puts into it is A/(2*pi*f) -- so a transformer's capacity is naturally
    /// quoted as the lowest frequency it will pass at full power. Below that it
    /// is working; above it, the same voltage does not touch it.
    ///
    /// A large output transformer is clean to 40 Hz or below; a small combo's
    /// gives way well over 100. Raising it is how the low end is made to bloom
    /// and thicken while everything above stays exactly as it was -- and for
    /// music that lives under 60 Hz, that is the control that matters.
    ///
    /// The first version of this was a dimensionless `coreHeadroom` compared
    /// against the raw integral, and it was wrong in a way no test caught:
    /// filling a core of 0.55 at 82 Hz needed an amplitude of **283**, so the
    /// mechanism this whole stage is built around never once engaged. The
    /// measurement that found it was the deliberate break -- switching core
    /// saturation off changed nothing at all.
    double coreFrequencyHz { 45.0 };

    /// Zero disables core saturation entirely, leaving a linear transformer of
    /// the kind the literature usually models.
    double coreSaturation { 1.0 };

    [[nodiscard]] bool operator== (const PowerAmpParameters&) const = default;
};

class PowerAmp
{
public:
    /// The flux state is bounded to this multiple of the core's capacity, so
    /// the integrator cannot wind up however it is driven.
    static constexpr double kFluxLimit = 12.0;

    /// How far the rail is allowed to fall, as a fraction of nominal.
    ///
    /// Physical, and load-bearing for a reason that is not obvious. A rectifier
    /// and a reservoir capacitor never sag to nothing -- an amplifier being
    /// hammered gets quieter and browner, it does not switch off. So a floor
    /// belongs here anyway.
    ///
    /// What makes it load-bearing is what the valves are fed. The clipper works
    /// in units of the rail, so the signal is divided by it, and a rail
    /// approaching zero sends that ratio towards infinity. ADAA returns the
    /// *average* of the shaper over the segment between two samples, which is
    /// exactly right for a curve that stands still -- but the excess is added
    /// back to the segment's endpoint, deliberately, because that is what keeps
    /// the linear region bit-exact instead of half-a-sample lowpassed.
    ///
    /// Those two are compatible while the input moves smoothly and stop being
    /// compatible when it jumps by orders of magnitude in one sample. With the
    /// rail allowed to reach 1e-4 the ratio jumped from about 100 to about 1e7
    /// between neighbours, the average excess over that segment came out around
    /// half the endpoint's, and what survived the cancellation was a peak of
    /// **206 times full scale**.
    ///
    /// At 0.15 the ratio spans under 7:1 and neighbouring samples differ by a
    /// percent or so, which is the regime the method is documented for.
    static constexpr double kMinimumRailFraction = 0.15;

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        clipAdaa_.reset();
        crossoverAdaa_.reset();

        sag_ = 0.0;
        flux_ = 0.0;
        lowState_ = 0.0;
        highState_ = 0.0;
        feedbackState_ = 0.0;
    }

    void setParameters (const PowerAmpParameters& parameters) noexcept
    {
        if (parameters == parameters_)
            return;

        parameters_ = parameters;
        updateCoefficients();
    }

    [[nodiscard]] const PowerAmpParameters& getParameters() const noexcept { return parameters_; }

    /// How far the rail has fallen, 0 to 1.
    [[nodiscard]] double getSag() const noexcept { return sag_; }

    /// Core flux, in units of the core's capacity. Above 1 the permeability is
    /// falling and the low end is going with it.
    [[nodiscard]] double getFlux() const noexcept { return flux_ * fluxScale_; }

    [[nodiscard]] double process (double x) noexcept
    {
        // ---- the loop --------------------------------------------------------
        //
        // Previous sample's output, so there is no algebraic loop to solve. The
        // loop's authority is whatever the valves' incremental gain still is,
        // which is the point: it lets go on its own when they clip.
        const double driven = x * parameters_.drive - parameters_.feedback * feedbackState_;

        // ---- class AB handover ------------------------------------------------

        const double handed = crossoverAdaa_.process (driven, crossover_);

        // ---- the valves, into a rail that has sagged --------------------------

        const double nominal = std::max (parameters_.ceiling, 1.0e-4);
        const double headroom = nominal * (1.0 - sag_ * parameters_.sagDepth);
        const double scale = std::max (headroom, nominal * kMinimumRailFraction);

        // The excess, not the whole curve: below the knee both it and its
        // antiderivative are exactly zero, so a stage that is not clipping is
        // bit-exactly linear rather than merely nearly so. CLAUDE.md section 7.
        const double normalisedDrive = handed / scale;
        const double clipped = scale * (normalisedDrive + clipAdaa_.process (normalisedDrive, clip_));

        // Average current pulls the rail down; it climbs back on its own.
        const double demand = std::min (std::abs (handed) / nominal, 4.0);
        sag_ += sagCoefficient_ * (demand - sag_);
        sag_ = std::clamp (sag_, 0.0, 1.0);

        feedbackState_ = clipped;

        // ---- the output transformer -------------------------------------------

        return transformer (clipped);
    }

private:
    /// Primary inductance below, leakage above, and a core whose permeability
    /// falls as the flux rises.
    [[nodiscard]] double transformer (double v) noexcept
    {
        // Permeability falls as the core fills, using the flux the *previous*
        // sample left behind. The low-frequency corner is R/(2*pi*L), so as L
        // falls the corner rises -- which is what a saturating transformer
        // does: it loses bass and gains harmonics together, and only where the
        // flux is high, which is to say only down low.
        //
        // Symmetric in flux, so it generates odd harmonics and no hysteresis
        // loop. Real cores have one, and it is what makes a transformer sound
        // different going up from going down -- but a Jiles-Atherton core is
        // a project of its own and belongs to the tape machine, not here.
        const double normalised = flux_ * fluxScale_;
        const double fill = 1.0 + parameters_.coreSaturation * normalised * normalised;

        const double g = std::min (lowCoefficient_ * fill, 0.9);

        lowState_ += g * (v - lowState_);
        const double afterPrimary = v - lowState_;

        // Flux is the integral of the voltage *across the primary*, which is
        // the voltage across the load -- the inductance shunts current, not
        // voltage, so the source's own swing is not what fills it.
        //
        // That distinction is what makes the model self-limiting rather than
        // merely clamped: as the core fills, the corner rises, the low end
        // that was filling it goes away, and the flux stops climbing on its
        // own. Integrating the source voltage instead would need the clamp
        // below to do real work; here it is only a guard.
        //
        // Leaky, because the winding has resistance and the core has a path
        // back to zero, so a sustained offset does not park it off-centre.
        flux_ += afterPrimary / sampleRate_;
        flux_ -= flux_ * fluxLeak_;

        const double limit = fluxScale_ > 0.0 ? kFluxLimit / fluxScale_ : kFluxLimit;
        flux_ = std::clamp (flux_, -limit, limit);

        // Leakage inductance in series with the load: a first-order roll-off on
        // the top, and the reason a guitar amplifier is done at 5 kHz before
        // the speaker has said anything.
        highState_ += highCoefficient_ * (afterPrimary - highState_);

        return highState_;
    }

    void updateCoefficients() noexcept
    {
        clip_.setKnee (parameters_.knee);
        crossover_.crossover = Crossover { parameters_.crossoverDepth, parameters_.crossoverWidth };

        // One-pole coefficients. The corners are far below Nyquist, so the
        // small-angle form is exact enough and costs no transcendental.
        constexpr double twoPi = 6.283185307179586;

        lowCoefficient_ = std::clamp (twoPi * parameters_.transformerLowHz / sampleRate_, 0.0, 0.9);
        highCoefficient_ = std::clamp (twoPi * parameters_.transformerHighHz / sampleRate_, 0.0, 1.0);

        // The flux leak sits an octave or so below the primary's corner: slow
        // enough not to be part of the audio response, fast enough that a
        // sustained offset does not park the core off-centre for ever.
        fluxLeak_ = std::clamp (twoPi * parameters_.transformerLowHz * 0.5 / sampleRate_, 0.0, 0.5);

        // Volt-seconds to fractions of the core's capacity. A sine of amplitude
        // `ceiling` at `coreFrequencyHz` integrates to a peak of
        // ceiling/(2*pi*f), so dividing by that reads 1.0 exactly there -- and
        // 2.0 an octave below it, which is the whole mechanism in one line.
        const double capacity = std::max (parameters_.ceiling, 1.0e-4)
                              / (twoPi * std::max (parameters_.coreFrequencyHz, 1.0));

        fluxScale_ = 1.0 / capacity;

        sagCoefficient_ = 1.0 - std::exp (-1.0 / (std::max (parameters_.sagMs, 0.1) * 0.001 * sampleRate_));
    }

    double sampleRate_ { 48000.0 };

    PowerAmpParameters parameters_;

    SoftClipExcess  clip_;
    CrossoverShaper crossover_;

    Adaa1<SoftClipExcess>  clipAdaa_;
    Adaa1<CrossoverShaper> crossoverAdaa_;

    double sag_            { 0.0 };
    double flux_           { 0.0 };
    double lowState_       { 0.0 };
    double highState_      { 0.0 };
    double feedbackState_  { 0.0 };

    double fluxScale_       { 1.0 };
    double lowCoefficient_  { 0.0 };
    double highCoefficient_ { 0.0 };
    double fluxLeak_        { 0.0 };
    double sagCoefficient_  { 0.0 };
};

} // namespace tezla::dsp
