#pragma once

// Band-limited oscillators, with the discontinuities corrected rather than
// left to alias.
//
// ---------------------------------------------------------------------------
// Why a corrector and not just polyBLEP inline
// ---------------------------------------------------------------------------
//
// The usual polyBLEP is written for a discontinuity that happens at a *known*
// phase -- a saw wraps at phase 0, a pulse steps at 0 and at its duty point --
// so the correction can be worked out from the oscillator's own phase and read
// off inline. That covers a plain saw and nothing else.
//
// Hard sync breaks it. When one oscillator resets another, the step lands
// wherever the master happened to wrap, which is an arbitrary fraction of a
// sample, and it is a step of arbitrary height because the slave was somewhere
// in the middle of its own cycle. There is no phase to read it off.
//
// So the correction is kept as an explicit two-sample accumulator instead:
// tell it a step of height h is coming in `frac` samples, and it spreads the
// residual across this sample and the next. The plain saw uses the same path --
// and a test asserts the two agree to 1e-12, which is what pins the indexing.
//
// ---------------------------------------------------------------------------
// The look-ahead, which is the whole of the indexing
// ---------------------------------------------------------------------------
//
// A band-limited step's residual straddles the discontinuity: it corrects the
// sample *before* it and the sample after. So the oscillator has to know about
// a step before it emits the sample preceding it -- which means looking one
// increment ahead rather than reacting to a wrap that has already happened.
//
// The first draft did react, scheduling the correction after the phase had
// already crossed. The sample that needed the "before" half had gone out a call
// earlier and could not be fixed, so only half the residual ever landed. It did
// not look like a bug. It looked like slightly more aliasing, which is exactly
// why the agreement test exists and why it is written to 1e-12 rather than to
// something forgiving.
//
// ---------------------------------------------------------------------------
// What band-limiting buys, measured
// ---------------------------------------------------------------------------
//
// PolyBLEP is a second-order correction, so it is very good where the step is
// slow relative to the sample rate and merely good where it is not. For a bass
// instrument that is exactly the right trade: at 40 Hz the step occupies a
// thousandth of a cycle and the correction is nearly exact. The numbers are in
// tests/test_Oscillator.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Exact.hpp"

namespace tezla::dsp {

/// Spreads the residual of a step discontinuity across two output samples.
///
/// A bandlimited step is not a step: it overshoots and rings. The difference
/// between it and the ideal step is the *residual*, and adding that residual to
/// the naive waveform is what removes the images. Two samples of it is the
/// second-order (polyBLEP) case -- cheap, and enough here.
class StepCorrector
{
public:
    void reset() noexcept
    {
        current_ = 0.0;
        next_ = 0.0;
    }

    /// A step of signed height `height` will happen `frac` samples *after* the
    /// sample about to be emitted, with `frac` in [0, 1).
    ///
    /// Ahead, not behind: the residual straddles the discontinuity, so the
    /// sample being emitted now is the one before it and needs its half of the
    /// correction now. The polynomials are the standard polyBLEP pair evaluated
    /// at the two instants either side:
    ///
    ///     this sample, `frac` before the step   ->   (1 - frac)^2
    ///     the next, `1-frac` after it           ->  -frac^2
    ///
    /// scaled by half the step height, because the polyBLEP's own swing is 2.
    void addStep (double height, double frac) noexcept
    {
        const double f = std::clamp (frac, 0.0, 1.0);
        const double half = 0.5 * height;

        const double before = 1.0 - f;

        current_ += half * before * before;
        next_    -= half * f * f;
    }

    /// The correction for the sample being emitted now. Advances the pair.
    [[nodiscard]] double take() noexcept
    {
        const double value = current_;
        current_ = next_;
        next_ = 0.0;
        return value;
    }

private:
    double current_ { 0.0 };
    double next_    { 0.0 };
};

/// The shapes one oscillator can make.
///
/// **Append-only.** A choice parameter stores an index, so inserting an entry
/// silently repoints every saved use of it -- CLAUDE.md section 8.
enum class OscShape
{
    saw = 0,
    pulse,
    triangle,
    sine,

    count
};

/// One band-limited oscillator, phase-modulatable and syncable.
///
/// Phase modulation rather than frequency modulation, and that is not a detail:
/// FM integrates its modulator, so any DC in it walks the pitch away and never
/// comes back. PM adds to the phase directly, so the pitch is exactly what it
/// was told and only the timbre moves. Every FM synth worth the name is
/// actually a PM synth for this reason.
class Oscillator
{
public:
    /// How far the pulse width can be pushed towards silence. A pulse of zero
    /// width is silence, and a control that reaches it has a dead end on it.
    static constexpr double kMinimumWidth = 0.02;

    void reset (double startPhase = 0.0) noexcept
    {
        phase_ = std::clamp (startPhase, 0.0, 1.0);
        corrector_.reset();
        triangleState_ = 0.0;
        wrapped_ = false;
        wrapFraction_ = 0.0;
        pendingSync_ = false;
        pendingSyncFrac_ = 0.0;
    }

    void setShape (OscShape shape) noexcept { shape_ = shape; }
    [[nodiscard]] OscShape getShape() const noexcept { return shape_; }

    /// Pulse width, 0.02 to 0.98. The pulse reads it as duty; the triangle
    /// reads it as skew -- at 0.5 the triangle is symmetric, and pushed to
    /// either end it leans towards a saw. No other shape reads it.
    void setWidth (double width) noexcept
    {
        width_ = std::clamp (width, kMinimumWidth, 1.0 - kMinimumWidth);
    }

    [[nodiscard]] double getWidth() const noexcept { return width_; }

    /// The shape's own tweak, 0 to 1, in the Surge sense: what it means
    /// depends on the shape, and 0 is always that shape's canonical self.
    ///
    /// **The four original shapes ignore it entirely** -- they are frozen for
    /// project compatibility, and their morphable descendants are the new
    /// shapes (Dome grows out of the sine, Double saw out of the saw; the
    /// triangle's skew was always Width). A test asserts the ignoring is
    /// total, at every morph value.
    void setMorph (double morph) noexcept
    {
        morph_ = std::clamp (morph, 0.0, 1.0);
    }

    [[nodiscard]] double getMorph() const noexcept { return morph_; }

    /// The ideal (un-band-limited) waveform of a shape, for anything that
    /// draws or checks one -- the on-panel preview renders exactly this, so
    /// the picture and the sound cannot drift apart.
    ///
    /// One deliberate divergence from the audio path: `triangle` here is the
    /// actual triangle -- rising to +1 at the skew point, falling after --
    /// while the audio path synthesises it by *integrating a corrected
    /// square*, because integrating the correction is what band-limits it.
    /// The two describe the same waveform; this is the shape, that is the
    /// method.
    [[nodiscard]] static double naiveShapeSample (OscShape shape, double phase,
                                                  double width, double morph) noexcept
    {
        const double clampedWidth = std::clamp (width, kMinimumWidth, 1.0 - kMinimumWidth);
        phase -= std::floor (phase);
        (void) morph;   // read by the shapes still to come

        switch (shape)
        {
            case OscShape::saw:
                return 2.0 * phase - 1.0;

            case OscShape::pulse:
                return phase < clampedWidth ? 1.0 : -1.0;

            case OscShape::triangle:
                return phase < clampedWidth
                    ? 2.0 * phase / clampedWidth - 1.0
                    : 1.0 - 2.0 * (phase - clampedWidth) / (1.0 - clampedWidth);

            case OscShape::sine:
                return std::sin (6.283185307179586 * phase);

            case OscShape::count:
            default:
                return 0.0;
        }
    }

    /// Cycles per sample. Kept below a half so the oscillator cannot be asked
    /// for something above Nyquist, which no amount of band-limiting fixes.
    void setIncrement (double increment) noexcept
    {
        increment_ = std::clamp (increment, 0.0, 0.49);
    }

    [[nodiscard]] double getIncrement() const noexcept { return increment_; }

    [[nodiscard]] double getPhase() const noexcept { return phase_; }

    /// Whether a cycle boundary falls within the *coming* sample, and how far
    /// into it. This is what a synced oscillator listens to -- and it reports
    /// ahead rather than behind, because a band-limited step has to be known
    /// about before the sample preceding it is emitted.
    [[nodiscard]] bool didWrap() const noexcept { return wrapped_; }
    [[nodiscard]] double getWrapFraction() const noexcept { return wrapFraction_; }

    /// Restarts the cycle, correcting the step it makes.
    ///
    /// This is hard sync. The slave is thrown back to the start of its cycle
    /// whenever the master completes one, so its waveform is chopped at the
    /// master's period -- the pitch you hear is the master's, and the *timbre*
    /// is set by how far through its own cycle the slave got. Sweep the slave's
    /// frequency and the chop point sweeps with it, which is the sound.
    ///
    /// `frac` is where in the coming sample the reset lands, which is exactly
    /// what the master's getWrapFraction() reports. Call it between the
    /// master's advance() and the slave's.
    ///
    /// Only the *time* is recorded here. The step's height is worked out inside
    /// advance(), because until then it is not known whether the slave's own
    /// cycle boundary falls before the reset -- and if it does, both
    /// discontinuities are real and both have to be corrected, in order.
    void sync (double frac) noexcept
    {
        pendingSync_ = true;
        pendingSyncFrac_ = std::clamp (frac, 0.0, 1.0);
    }

    /// One sample. `phaseOffset` is the phase modulation input, in cycles.
    [[nodiscard]] double advance (double phaseOffset = 0.0) noexcept
    {
        wrapped_ = false;
        wrapFraction_ = 0.0;

        // ---- read the waveform where the phase is now ------------------------
        //
        // Phase modulation is applied to the *reading*, not to the accumulator,
        // so it cannot drift the oscillator's own cycle -- the wrap below still
        // happens on the oscillator's own period, and so does sync.
        const double read = wrapUnit (phase_ + phaseOffset);
        double value = valueAtPhase (read);

        // ---- schedule every discontinuity in the coming sample, in order -----
        //
        // Before the correction is taken, because this sample is the one that
        // comes *before* the steps and needs its half of each residual now.
        scheduleEdges();

        value += corrector_.take();

        // ---- advance ---------------------------------------------------------

        if (pendingSync_)
        {
            // The reset landed partway through this sample, so what is left of
            // the increment is all the phase has moved since.
            phase_ = (1.0 - pendingSyncFrac_) * increment_;
            pendingSync_ = false;
        }
        else
        {
            phase_ += increment_;

            if (phase_ >= 1.0)
                phase_ -= 1.0;
        }

        if (shape_ == OscShape::triangle)
        {
            // A triangle is the integral of a square, and integrating the
            // *corrected* square is what makes the triangle band-limited too.
            // Leaky, or any asymmetry in the square walks it off centre.
            triangleState_ += 4.0 * increment_ * value - kTriangleLeak * triangleState_;
            return triangleState_;
        }

        return value;
    }

private:
    /// How much the waveform jumps at the phase-zero edge.
    [[nodiscard]] double edgeHeight() const noexcept
    {
        switch (shape_)
        {
            case OscShape::saw:      return -2.0;   // +1 falls to -1
            case OscShape::pulse:
            case OscShape::triangle: return  2.0;   // the square rises
            case OscShape::sine:
            case OscShape::count:
            default:                 return  0.0;
        }
    }

    /// Schedules every discontinuity falling within the coming increment.
    ///
    /// **In time order, and that is the whole of it.** Within one sample the
    /// oscillator may cross its own cycle boundary *and* be reset by a master,
    /// and when it does both steps are real. The first draft skipped its own
    /// wrap whenever a sync was pending, which is right when the reset
    /// pre-empts the wrap and wrong when the wrap comes first -- and at the
    /// same frequency, where the two coincide, it silently dropped the -2 the
    /// waveform genuinely makes. A synced oscillator running at its master's
    /// own rate stopped matching an unsynced one, which is the case that has
    /// no excuse.
    void scheduleEdges() noexcept
    {
        if (increment_ <= 0.0)
            return;

        // A sync, if one is pending, is the latest event worth scheduling:
        // anything after it is thrown away by the reset. No sync means nothing
        // is pre-empted, which is what a fraction past the end of the sample
        // says.
        const double syncAt = pendingSync_ ? pendingSyncFrac_ : kNoEvent;

        const double next = phase_ + increment_;

        // ---- the oscillator's own cycle boundary -----------------------------

        if (next >= 1.0)
        {
            const double frac = std::clamp ((1.0 - phase_) / increment_, 0.0, 1.0);

            // Reported whether or not it survives the reset, because a slave
            // downstream of this one is entitled to know the cycle completed.
            wrapped_ = true;
            wrapFraction_ = frac;

            if (frac <= syncAt && ! isExactlyZero (edgeHeight()))
                corrector_.addStep (edgeHeight(), frac);
        }

        // ---- the pulse's second edge, at the duty point ----------------------
        //
        // A pulse has two discontinuities per cycle, and correcting only the
        // one at zero leaves the other aliasing exactly as much as an
        // uncorrected oscillator does.
        if (shape_ == OscShape::pulse || shape_ == OscShape::triangle)
        {
            if (phase_ < width_ && next >= width_)
            {
                const double frac = std::clamp ((width_ - phase_) / increment_, 0.0, 1.0);

                if (frac <= syncAt)
                    corrector_.addStep (-2.0, frac);
            }
        }

        // ---- the reset itself ------------------------------------------------

        if (pendingSync_)
        {
            const double from = valueAtPhase (wrapUnit (phase_ + syncAt * increment_));
            const double to   = valueAtPhase (0.0);

            if (! isExactlyZero (to - from))
                corrector_.addStep (to - from, syncAt);
        }
    }

    /// The naive waveform, before any correction.
    [[nodiscard]] double valueAtPhase (double phase) const noexcept
    {
        switch (shape_)
        {
            case OscShape::saw:
                return 2.0 * phase - 1.0;

            case OscShape::pulse:
            case OscShape::triangle:
                return phase < width_ ? 1.0 : -1.0;

            case OscShape::sine:
                return std::sin (6.283185307179586 * phase);

            case OscShape::count:
            default:
                return 0.0;
        }
    }

    [[nodiscard]] static double wrapUnit (double value) noexcept
    {
        value -= std::floor (value);
        return value;
    }

    /// Slow enough to be far below any note, fast enough that a drifting
    /// offset cannot accumulate over a held chord.
    static constexpr double kTriangleLeak = 1.0e-4;

    /// A fraction past the end of the sample: "this does not happen".
    static constexpr double kNoEvent = 2.0;

    OscShape shape_ { OscShape::saw };

    double phase_     { 0.0 };
    double increment_ { 0.0 };
    double width_     { 0.5 };
    double morph_     { 0.0 };

    double triangleState_ { 0.0 };

    bool   wrapped_      { false };
    double wrapFraction_ { 0.0 };

    bool   pendingSync_     { false };
    double pendingSyncFrac_ { 0.0 };

    StepCorrector corrector_;
};

} // namespace tezla::dsp
