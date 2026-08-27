#pragma once

// One valve gain stage, with the memory that makes it respond.
//
// ---------------------------------------------------------------------------
// Why this exists at all
// ---------------------------------------------------------------------------
//
// Triode.hpp is a curve: hand it a voltage, get a voltage. Everything a curve
// can do, it does instantly and identically every time. That is not what a
// valve stage does, and the difference is most of what people mean when they
// say an amp "responds".
//
// Three mechanisms give the stage a memory, and all three are components on the
// schematic rather than anybody's secret sauce:
//
// **The cathode bypass capacitor.** The cathode sits on a resistor, so the
// cathode voltage follows the average current through the valve. Drive the
// stage harder, the average current rises, the cathode lifts, and the grid is
// now sitting further below it -- the operating point has moved towards cutoff.
// It moves back over R_k*C_k, which on a typical stage is tens of milliseconds.
// So the curve you are distorting on is not the curve you started on, and how
// long it takes to come back is audible as breathing.
//
// **Grid conduction.** The grid is a piece of wire in the path of an electron
// beam. Drive it up to the cathode's potential and it starts catching electrons
// -- it becomes a diode. The current charges the coupling capacitor in front of
// it, and since that capacitor can only discharge through the grid-leak
// resistor, the *whole signal* is pushed negative and stays there. The recovery
// is R_g*C_c, which is usually slower than the bias shift. Hit a stage hard
// enough and it chokes, goes quiet and farty for a moment, and climbs back.
// That is blocking distortion, and it is a large part of what a cranked preamp
// actually sounds like.
//
// **The plate bottoming out.** The plate cannot swing below the cathode. There
// is a hard floor under the output, approached softly as the valve runs out of
// voltage to drop.
//
// ---------------------------------------------------------------------------
// The budget these have to account for
// ---------------------------------------------------------------------------
//
// This is not a guess about which mechanisms matter. Triode.hpp is fitted
// against Dempwolf's measured 12AX7 on a real load line, and it matches the
// cutoff half to 2.39% -- while diverging by **1.27 normalised units** at +3 V
// on the grid side, because the real stage compresses there and a bare power
// law expands.
//
// That gap is the specification for this file. Grid conduction and plate
// bottoming are what close it, and tests/test_TriodeStage.cpp measures whether
// they do rather than assuming it.
//
// ---------------------------------------------------------------------------
// The two feedback paths are bounded, by construction
// ---------------------------------------------------------------------------
//
// Both the bias shift and the grid charge are computed from the signal and then
// subtracted from it, which is a feedback loop around a nonlinearity --
// CLAUDE.md section 7 requires those to have a bound that cannot be defeated.
//
// Here the loop is inherently negative: more drive raises the current, which
// pushes the operating point towards cutoff, which lowers the drive. It is
// self-limiting in the same way the real circuit is. That is an argument, not a
// guarantee, so both states are also hard-clamped to a multiple of the knee,
// and both use the *previous* sample's value -- a capacitor's voltage cannot
// change instantaneously, so there is no algebraic loop to solve and no way for
// one sample to run away.

#include <algorithm>
#include <cmath>

#include "Exact.hpp"
#include "Adaa.hpp"
#include "DcBlocker.hpp"
#include "Triode.hpp"

namespace tezla::dsp {

/// The plate running out of voltage: a one-sided soft floor under the output.
///
/// Exponential approach, because that is both what the curve does as the valve
/// runs out of headroom and the shape whose integral stays closed-form -- so
/// this antialiases exactly, like the curve above it.
///
///   g(y) = y                          for y > -threshold
///        = -(t + s) + s*exp(-d/s)     for d = -(y + t) > 0
///
/// C1 at the join: the slope arrives at exp(0) = 1 from below and 1 from above.
/// The floor it approaches is -(threshold + softness).
class PlateCeiling
{
public:
    static constexpr double kMinSoftness = 1.0e-3;

    constexpr PlateCeiling() noexcept = default;

    constexpr PlateCeiling (double threshold, double softness) noexcept
        : threshold_ (threshold),
          softness_ (softness < kMinSoftness ? kMinSoftness : softness)
    {
    }

    /// The output can never go below this.
    [[nodiscard]] constexpr double getFloor() const noexcept
    {
        return -(threshold_ + softness_);
    }

    [[nodiscard]] double evaluate (double y) const noexcept
    {
        const double d = -(y + threshold_);

        if (d <= 0.0)
            return y;

        return getFloor() + softness_ * std::exp (-d / softness_);
    }

    [[nodiscard]] double antiderivative (double y) const noexcept
    {
        const double d = -(y + threshold_);

        if (d <= 0.0)
            return 0.5 * y * y;

        const double s = softness_;
        const double t = threshold_;

        return (t + s) * d + s * s * std::exp (-d / s) + 0.5 * t * t - s * s;
    }

private:
    double threshold_ { 1.0 };
    double softness_  { 0.25 };
};

struct PlateCeilingShaper
{
    PlateCeiling ceiling;

    [[nodiscard]] double evaluate (double x) const noexcept { return ceiling.evaluate (x); }
    [[nodiscard]] double antiderivative (double x) const noexcept { return ceiling.antiderivative (x); }
};

struct TriodeStageParameters
{
    // ---- the curve -----------------------------------------------------------

    /// Grid swing from quiescent to cutoff. Larger is cleaner.
    double knee { 1.76 };

    /// 1.585 is what fitting our curve to a measured 12AX7 returned; 1.5 is
    /// Child's law exactly. See Triode.hpp.
    double exponent { 1.585 };

    // ---- the cathode bypass capacitor ----------------------------------------

    /// How far the operating point moves for a given change in average current,
    /// as a fraction of the knee. Zero is a fully bypassed cathode, which does
    /// not move at all.
    double biasDepth { 0.35 };

    /// R_k * C_k. A 1.5k cathode resistor with 22uF across it is about 33 ms.
    double biasMs { 33.0 };

    // ---- grid conduction ------------------------------------------------------

    /// Where the grid starts catching electrons, relative to quiescent. In a
    /// real stage this is the bias voltage: the grid conducts once it reaches
    /// the cathode.
    double gridThreshold { 1.5 };

    /// How hard the charge pushes back, as a fraction of the excess.
    double gridDepth { 0.9 };

    /// The coupling capacitor charging through a conducting grid: fast, because
    /// a conducting grid is a low impedance.
    double gridAttackMs { 0.15 };

    /// R_g * C_c on the way back out. 1M and 22nF is about 22 ms; bigger
    /// capacitors block for longer and sound woollier when pushed.
    double gridRecoveryMs { 22.0 };

    // ---- the plate ------------------------------------------------------------

    /// How far the output can swing before the plate starts running out.
    ///
    /// Not a taste setting: on the reference stage the plate cannot go below
    /// the cathode, so the normalised output is bounded by the quiescent plate
    /// voltage divided by the stage gain -- 181.98/60.78 = **2.994**. Headroom
    /// plus softness is set to that, which is why our compression curve tracks
    /// the reference's instead of squeezing harder.
    double plateHeadroom { 2.40 };

    /// How gradually it runs out. Added to the headroom, this is the floor.
    double plateSoftness { 0.59 };

    // ---- the coupling capacitor out -------------------------------------------

    /// The interstage highpass. Real, and part of the sound: it is what stops
    /// the bias shift of one stage becoming a DC offset in the next.
    double couplingHz { 12.0 };

    /// The grid stopper working against the Miller capacitance, as a corner.
    ///
    /// Every high-gain amplifier built has a resistor in series with each grid,
    /// usually somewhere between 10k and 100k. It is there for two reasons and
    /// both matter here. It stops the stage oscillating, and -- with the Miller
    /// capacitance the stage's own gain creates, C_gp*(1+|A|), about 100 pF for
    /// a 12AX7 -- it is a lowpass that keeps the previous stage's harmonics out
    /// of this one's grid. A 68k stopper into 100 pF is a corner at 23 kHz.
    ///
    /// Set it far above the internal rate to disable it.
    double gridStopperHz { 30000.0 };

    /// The stage's own bandwidth, at the plate.
    ///
    /// This is the dominant pole of a real gain stage and the one that decides
    /// what leaves it. The plate load in parallel with the valve's own plate
    /// resistance -- 100k with 62k, so 38k -- drives whatever capacitance hangs
    /// on the plate: stray wiring of 20 pF or so, plus the next grid's Miller
    /// capacitance of about 100 pF. 38k into 120 pF is a corner at **35 kHz**,
    /// and that is the measured bandwidth of a 12AX7 gain stage.
    ///
    /// Together with the grid stopper it gives two poles per stage, which is
    /// what a cascade needs and why it is here rather than being left to the
    /// oversampler. Harmonics are generated *inside* a stage, after its grid
    /// stopper, so a filter on the input cannot touch them; only a filter on
    /// the output can, and the real circuit has one.
    ///
    /// Both poles are worth having and neither is worth much against aliasing,
    /// which is worth saying plainly because it is not what one expects.
    /// Measured on Anvil's three-valve lane at 36 dB of gain, x4 from 48 kHz:
    /// the grid stopper bought 5 dB and the plate pole another 1.8.
    ///
    /// The reason is that the folding happens *at* the shaper. A per-sample
    /// nonlinearity running at 192 kHz puts energy above 96 kHz and it folds
    /// down in the same instant; a filter after it can only remove what landed
    /// high, and what lands low is already indistinguishable from signal. The
    /// component that made this visible was harmonic 192 of a 1000.49 Hz input
    /// -- 192.09 kHz, folded to 93.75 Hz, at -47 dBFS.
    ///
    /// So these two poles are here because the circuit has them, and what
    /// actually fixed the aliasing was raising the internal rate. See
    /// AnvilEngine's autoFactorFor for the numbers.
    double plateCornerHz { 35000.0 };

    [[nodiscard]] bool operator== (const TriodeStageParameters&) const = default;
};

class TriodeStage
{
public:
    /// Both feedback states are clamped to this many knees, so neither path can
    /// run away however the parameters are set.
    static constexpr double kStateLimitInKnees = 4.0;

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        coupling_.prepare (sampleRate_, parameters_.couplingHz);
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        curveAdaa_.reset();
        ceilingAdaa_.reset();
        coupling_.reset();

        bias_ = 0.0;
        gridCharge_ = 0.0;
        gridState_ = 0.0;
        plateState_ = 0.0;
    }

    void setParameters (const TriodeStageParameters& parameters) noexcept
    {
        if (parameters == parameters_)
            return;

        // retune rather than prepare: a coupling capacitor's memory *is* its
        // last input and output, and zeroing it mid-stream steps the output by
        // the whole previous sample. CLAUDE.md section 7.
        if (! isExactly (parameters.couplingHz, parameters_.couplingHz))
            coupling_.retune (sampleRate_, parameters.couplingHz);

        parameters_ = parameters;
        updateCoefficients();
    }

    [[nodiscard]] const TriodeStageParameters& getParameters() const noexcept { return parameters_; }

    /// How far the operating point has drifted, in the same units as the input.
    /// Positive means towards cutoff.
    [[nodiscard]] double getBiasShift() const noexcept { return bias_; }

    /// How much charge the grid has pushed onto the coupling capacitor.
    [[nodiscard]] double getGridCharge() const noexcept { return gridCharge_; }

    [[nodiscard]] double process (double x) noexcept
    {
        const double limit = kStateLimitInKnees * parameters_.knee;

        // The grid stopper into the Miller capacitance, before anything else --
        // because in the circuit it is before anything else, in series with the
        // grid itself.
        gridState_ += gridStopper_ * (x - gridState_);

        // Last sample's capacitor voltages. A capacitor cannot change
        // instantaneously, so using the previous values is not an approximation
        // for convenience -- it is the reason there is no algebraic loop.
        const double v = gridState_ - bias_ - gridCharge_;

        // ---- grid conduction -------------------------------------------------
        //
        // Above the threshold the grid is a diode into the coupling capacitor.
        // Charging is fast and discharging is slow, which is what makes this
        // block rather than merely compress.
        const double excess = std::max (v - parameters_.gridThreshold, 0.0);
        const double target = excess * parameters_.gridDepth;

        gridCharge_ += (target > gridCharge_ ? gridAttack_ : gridRelease_) * (target - gridCharge_);
        gridCharge_ = std::clamp (gridCharge_, 0.0, limit);

        // ---- the curve -------------------------------------------------------

        const double shaped = curveAdaa_.process (v, curve_);
        const double bottomed = ceilingAdaa_.process (shaped, ceiling_);

        // The plate's own load: everything the stage makes leaves through it.
        plateState_ += plateCorner_ * (bottomed - plateState_);

        // ---- the cathode bypass capacitor ------------------------------------
        //
        // Tracks the *current*, not the voltage: e^p is what flows through the
        // cathode resistor, and it is the average of that which lifts the
        // cathode. At rest e = 1 and the shift is zero.
        const double e = std::max (1.0 + v / parameters_.knee, 0.0);
        const double current = std::pow (e, parameters_.exponent);

        bias_ += biasCoefficient_ * ((current - 1.0) * parameters_.biasDepth * parameters_.knee - bias_);
        bias_ = std::clamp (bias_, -limit, limit);

        // ---- the coupling capacitor out --------------------------------------

        return coupling_.process (plateState_);
    }

private:
    void updateCoefficients() noexcept
    {
        curve_.triode = Triode { parameters_.knee, parameters_.exponent };
        ceiling_.ceiling = PlateCeiling { parameters_.plateHeadroom, parameters_.plateSoftness };

        biasCoefficient_ = onePole (parameters_.biasMs);
        gridAttack_ = onePole (parameters_.gridAttackMs);
        gridRelease_ = onePole (parameters_.gridRecoveryMs);

        // The stopper is a coefficient change, never a reset: its state is the
        // last voltage on the Miller capacitance, and zeroing that mid-stream
        // would step the grid by a whole sample. CLAUDE.md section 7.
        constexpr double twoPi = 6.283185307179586;
        gridStopper_ = std::clamp (twoPi * parameters_.gridStopperHz / sampleRate_, 0.0, 1.0);
        plateCorner_ = std::clamp (twoPi * parameters_.plateCornerHz / sampleRate_, 0.0, 1.0);
    }

    /// The per-sample coefficient of a one-pole reaching 1 - 1/e in `ms`.
    [[nodiscard]] double onePole (double ms) const noexcept
    {
        const double seconds = std::max (ms, 0.001) * 0.001;
        return 1.0 - std::exp (-1.0 / (seconds * sampleRate_));
    }

    double sampleRate_ { 48000.0 };

    TriodeStageParameters parameters_;

    TriodeShaper       curve_;
    PlateCeilingShaper ceiling_;

    Adaa1<TriodeShaper>       curveAdaa_;
    Adaa1<PlateCeilingShaper> ceilingAdaa_;

    DcBlocker<double> coupling_;

    double bias_        { 0.0 };
    double gridCharge_  { 0.0 };
    double gridState_   { 0.0 };
    double plateState_  { 0.0 };

    double biasCoefficient_ { 0.0 };
    double gridAttack_      { 0.0 };
    double gridRelease_     { 0.0 };
    double gridStopper_     { 1.0 };
    double plateCorner_     { 1.0 };
};

} // namespace tezla::dsp
