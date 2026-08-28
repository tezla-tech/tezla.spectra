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

    // ---- phase 3 ------------------------------------------------------------
    //
    // Every one of these reads Morph, and 0 is that shape's canonical self.
    // The four above ignore Morph forever -- they are frozen for project
    // compatibility, and these are their morphable relatives.

    /// An analogue saw core: an RC charging curve instead of a straight ramp,
    /// with the same -2 reset the saw has. Morph deepens the curve.
    vintage,

    /// A pressed sine, `(0.5 - 0.5 cos)^k` -- **band-limited by construction**:
    /// integer k has exactly k harmonics, and Morph blends between adjacent
    /// integers, so there is nothing above k+1 times the fundamental, ever.
    /// The identity is the one the kargyraa work proved.
    dome,

    /// Two saw ramps, the second offset by Morph x half a cycle. At 0 they
    /// align into a plain saw; moving the offset is a one-oscillator flanger.
    doubleSaw,

    /// Additive: sixteen harmonics at n^-p, Morph setting the roll-off from
    /// bright to dark. Band-limited by construction, with each partial faded
    /// out as it nears Nyquist so a pitch sweep cannot pop one in.
    harmonic,

    /// White noise, one-poled darker as Morph rises. No pitch: frequency,
    /// sync, PM, detune and drift all have no effect, and the tooltip says so.
    noise,

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

    void setShape (OscShape shape) noexcept
    {
        shape_ = shape;
        refreshShapeState();
    }
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
        const double clamped = std::clamp (morph, 0.0, 1.0);

        if (isExactly (clamped, morph_))
            return;

        morph_ = clamped;
        refreshShapeState();
    }

    [[nodiscard]] double getMorph() const noexcept { return morph_; }

    /// Seeds the Noise shape's generator. The unison bank hands every voice a
    /// different one, which is what makes a noise stack wide instead of mono.
    void seedNoise (std::uint64_t seed) noexcept
    {
        noiseState_ = seed | 1ull;
    }

    /// The Noise shape's one-pole coefficient, 0..1, computed by whoever knows
    /// the sample rate -- this class deliberately does not. 1 is white.
    void setNoiseCoefficient (double g) noexcept
    {
        noiseCoefficient_ = std::clamp (g, 1.0e-4, 1.0);
    }

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
        const double clampedMorph = std::clamp (morph, 0.0, 1.0);
        phase -= std::floor (phase);

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

            case OscShape::vintage:
            {
                const double a = 1.5 + 6.0 * clampedMorph;
                const double e = std::exp (-a);
                const double norm = 2.0 / (1.0 - e);
                const double mean = norm * (1.0 - (1.0 - e) / a) - 1.0;

                return norm * (1.0 - std::exp (-a * phase)) - 1.0 - mean;
            }

            case OscShape::dome:
            {
                const double k = 1.0 + 15.0 * clampedMorph;
                const int kLo = std::max (1, static_cast<int> (k));
                const int kHi = kLo + 1;
                const double blend = std::clamp (k - static_cast<double> (kLo), 0.0, 1.0);

                double meanLo = 0.5;
                for (int n = 2; n <= kLo; ++n)
                    meanLo *= (2.0 * n - 1.0) / (2.0 * n);
                const double meanHi = meanLo * (2.0 * kHi - 1.0) / (2.0 * kHi);

                const double x = 0.5 - 0.5 * std::cos (6.283185307179586 * phase);
                const double lo = (std::pow (x, static_cast<double> (kLo)) - meanLo)
                                    / (1.0 - meanLo);
                const double hi = (std::pow (x, static_cast<double> (kHi)) - meanHi)
                                    / (1.0 - meanHi);

                return lo + blend * (hi - lo);
            }

            case OscShape::doubleSaw:
            {
                const double offset = 0.5 * clampedMorph;
                double second = phase + offset;
                second -= std::floor (second);

                return 0.5 * ((2.0 * phase - 1.0) + (2.0 * second - 1.0));
            }

            case OscShape::harmonic:
            {
                // The ideal series, with no Nyquist fades: this is the shape,
                // and the audio path's fades are the method.
                const double p = 1.0 + 2.0 * clampedMorph;

                double norm = 0.0;
                for (int n = 1; n <= kHarmonicCount; ++n)
                    norm += std::pow (static_cast<double> (n), -p);

                double sum = 0.0;
                for (int n = 1; n <= kHarmonicCount; ++n)
                    sum += std::pow (static_cast<double> (n), -p) / norm
                             * std::sin (6.283185307179586 * static_cast<double> (n) * phase);

                return sum;
            }

            case OscShape::noise:
            {
                // Drawable, not audible: a phase-hashed value so a preview has
                // something honest-looking to stroke. The audio path runs a
                // real generator.
                auto z = static_cast<std::uint64_t> (phase * 4096.0) * 0x9e3779b97f4a7c15ull;
                z ^= z >> 30;
                z *= 0xbf58476d1ce4e5b9ull;
                z ^= z >> 27;

                return 2.0 * (static_cast<double> (z >> 11) / 9007199254740992.0) - 1.0;
            }

            case OscShape::count:
            default:
                return 0.0;
        }
    }

    /// Cycles per sample. Kept below a half so the oscillator cannot be asked
    /// for something above Nyquist, which no amount of band-limiting fixes.
    void setIncrement (double increment) noexcept
    {
        const double clamped = std::clamp (increment, 0.0, 0.49);

        if (isExactly (clamped, increment_))
            return;

        increment_ = clamped;

        // Only what the pitch actually feeds is re-derived on a pitch change:
        // Dome's harmonic-count clamp and Harmonic's Nyquist fades, both a
        // short loop of cheap arithmetic. The morph-derived work -- Harmonic's
        // sixteen powers above all -- must NOT be re-run from here. The unison
        // bank pushes increments on its drift timer, and when this call did
        // the full re-derivation on a push that arrived every sample, one
        // three-note Harmonic chord measured 226% of a core against 16% for
        // saw -- millions of pow() calls a second, all producing the answers
        // already cached. The no-op guard above is the other half of the same
        // rule (CLAUDE.md section 7): an unchanged pitch re-derives nothing.
        if (shape_ == OscShape::dome)
            refreshDomeClamp();
        else if (shape_ == OscShape::harmonic)
            refreshHarmonicFades();
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

        // Noise stands outside the phase machinery entirely: no cycle, no
        // wrap, nothing for a sync slave to reset to. A pending sync is
        // consumed and ignored rather than left to fire on the next shape.
        if (shape_ == OscShape::noise)
        {
            pendingSync_ = false;

            noiseState_ ^= noiseState_ << 13;
            noiseState_ ^= noiseState_ >> 7;
            noiseState_ ^= noiseState_ << 17;

            const double white = 2.0 * (static_cast<double> (noiseState_ >> 11)
                                          / 9007199254740992.0) - 1.0;

            noiseLowpass_ += noiseCoefficient_ * (white - noiseLowpass_);
            return noiseLowpass_;
        }

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
            case OscShape::vintage:  return -2.0;   // the curve spans the same -1..+1
            case OscShape::pulse:
            case OscShape::triangle: return  2.0;   // the square rises

            // Half the saw's step: the second ramp's own wrap carries the
            // other half, scheduled as a duty-point edge below.
            case OscShape::doubleSaw: return -1.0;

            // Smooth at the wrap by construction: dome and harmonic are sums
            // of sines, and noise has no cycle at all.
            case OscShape::sine:
            case OscShape::dome:
            case OscShape::harmonic:
            case OscShape::noise:
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

        // The double saw's second ramp wraps at 1 - offset, dropping its half
        // of the amplitude. The edge sits in [0.5, 1], which the increment's
        // 0.49 ceiling can never straddle from below zero -- so the simple
        // crossing test is exhaustive, the same argument the pulse edge makes.
        if (shape_ == OscShape::doubleSaw)
        {
            // At offset 0 the edge sits at exactly 1.0 and fires together with
            // the wrap above -- two -1 steps at the same fraction, which is
            // precisely the saw's -2. The first draft guarded offset > 0 and
            // thereby corrected only half the aligned step: the morph-0 test
            // caught it as a doubled saw that did not equal the saw.
            const double edge = 1.0 - doubleOffset_;

            if (phase_ < edge && next >= edge)
            {
                const double frac = std::clamp ((edge - phase_) / increment_, 0.0, 1.0);

                if (frac <= syncAt)
                    corrector_.addStep (-1.0, frac);
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

    /// The naive waveform, before any correction. The new shapes read the
    /// coefficients `refreshShapeState` cached, so this stays cheap per
    /// sample; the public static recomputes the same formulas from scratch.
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

            case OscShape::vintage:
                return vintageNorm_ * (1.0 - std::exp (-vintageA_ * phase)) - 1.0 - vintageMean_;

            case OscShape::dome:
            {
                // Integer powers by squaring, not std::pow: the exponents are
                // small integers by construction, the two in use are adjacent
                // (hi is lo times one more x), and libm's general pow at this
                // call rate was the bulk of the shape's measured cost.
                const double x = 0.5 - 0.5 * std::cos (6.283185307179586 * phase);
                const double powerLo = powInteger (x, domeKLo_);
                const double powerHi = domeKHi_ > domeKLo_ ? powerLo * x : powerLo;

                const double lo = (powerLo - domeMeanLo_) * domeScaleLo_;
                const double hi = (powerHi - domeMeanHi_) * domeScaleHi_;

                return lo + domeBlend_ * (hi - lo);
            }

            case OscShape::doubleSaw:
            {
                const double second = wrapUnit (phase + doubleOffset_);
                return 0.5 * ((2.0 * phase - 1.0) + (2.0 * second - 1.0));
            }

            case OscShape::harmonic:
            {
                // sin(n w) by recurrence: one sincos, then a multiply-add per
                // partial. The amplitudes carry the roll-off, the Nyquist
                // fades and the normalisation, all pre-baked.
                const double w = 6.283185307179586 * phase;
                const double c = 2.0 * std::cos (w);

                double previous = 0.0;
                double current = std::sin (w);
                double sum = harmonicAmp_[0] * current;

                for (int n = 1; n < kHarmonicCount; ++n)
                {
                    const double next = c * current - previous;
                    previous = current;
                    current = next;
                    sum += harmonicAmp_[static_cast<std::size_t> (n)] * current;
                }

                return sum;
            }

            case OscShape::noise:   // handled in advance(); 0 here keeps the
            case OscShape::count:   // generic sync arithmetic inert
            default:
                return 0.0;
        }
    }

    /// Re-derives every cached coefficient the current shape needs, from the
    /// morph down. Called from setShape and setMorph -- the pitch-only parts
    /// have their own refreshers below, so a pitch change never pays for this.
    void refreshShapeState() noexcept
    {
        switch (shape_)
        {
            case OscShape::vintage:
            {
                vintageA_ = 1.5 + 6.0 * morph_;

                const double e = std::exp (-vintageA_);

                vintageNorm_ = 2.0 / (1.0 - e);
                vintageMean_ = vintageNorm_ * (1.0 - (1.0 - e) / vintageA_) - 1.0;
                break;
            }

            case OscShape::dome:
                // Integer exponents blended, because the finite-series
                // identity -- sin^2k has exactly k harmonics -- holds for
                // integers only. A blend of two band-limited waveforms is
                // band-limited; a fractional exponent is not.
                domeK_ = 1.0 + 15.0 * morph_;
                refreshDomeClamp();
                break;

            case OscShape::doubleSaw:
                doubleOffset_ = 0.5 * morph_;
                break;

            case OscShape::harmonic:
            {
                // The sixteen powers are the expensive part and depend on the
                // morph alone, so they are cached here and the Nyquist fades
                // -- which depend on the pitch -- are applied from the cache.
                const double p = 1.0 + 2.0 * morph_;

                double norm = 0.0;
                for (int n = 1; n <= kHarmonicCount; ++n)
                    norm += std::pow (static_cast<double> (n), -p);

                for (int n = 1; n <= kHarmonicCount; ++n)
                    harmonicRaw_[static_cast<std::size_t> (n - 1)]
                        = std::pow (static_cast<double> (n), -p) / norm;

                refreshHarmonicFades();
                break;
            }

            case OscShape::saw:
            case OscShape::pulse:
            case OscShape::triangle:
            case OscShape::sine:
            case OscShape::noise:
            case OscShape::count:
            default:
                break;
        }
    }

    /// The pitch-fed half of Dome's state: how many harmonics fit under
    /// Nyquist, and the means and scales for the two integer exponents in
    /// use. A short loop of rationals, cheap enough for every drift tick.
    void refreshDomeClamp() noexcept
    {
        // No harmonic above Nyquist: k harmonics of the fundamental must fit
        // under half the rate, with the house 0.45 margin.
        const int kMax = increment_ > 0.0
            ? std::max (1, static_cast<int> (0.45 / increment_))
            : 16;

        domeKLo_ = std::clamp (static_cast<int> (domeK_), 1, kMax);
        domeKHi_ = std::min (domeKLo_ + 1, kMax);
        domeBlend_ = std::clamp (domeK_ - static_cast<double> (domeKLo_), 0.0, 1.0);

        // The mean of x^k over a cycle, exactly: m(1) = 1/2 and
        // m(k) = m(k-1) * (2k-1) / 2k -- the central binomial over 4^k
        // without computing either.
        domeMeanLo_ = 0.5;
        for (int n = 2; n <= domeKLo_; ++n)
            domeMeanLo_ *= (2.0 * n - 1.0) / (2.0 * n);

        domeMeanHi_ = domeMeanLo_;
        if (domeKHi_ > domeKLo_)
            domeMeanHi_ *= (2.0 * domeKHi_ - 1.0) / (2.0 * domeKHi_);

        domeScaleLo_ = 1.0 / (1.0 - domeMeanLo_);
        domeScaleHi_ = 1.0 / (1.0 - domeMeanHi_);
    }

    /// The pitch-fed half of Harmonic's state: each cached amplitude, faded
    /// out across 0.40..0.48 of the rate's half so a pitch sweep slides
    /// partials away instead of popping them. The powers in harmonicRaw_ are
    /// deliberately not recomputed here.
    void refreshHarmonicFades() noexcept
    {
        for (int n = 1; n <= kHarmonicCount; ++n)
        {
            const double x = static_cast<double> (n) * increment_;
            double gain = 1.0;

            if (x >= 0.24)
                gain = 0.0;
            else if (x > 0.20)
            {
                const double u = (0.24 - x) / 0.04;
                gain = u * u * (3.0 - 2.0 * u);
            }

            harmonicAmp_[static_cast<std::size_t> (n - 1)]
                = harmonicRaw_[static_cast<std::size_t> (n - 1)] * gain;
        }
    }

    /// x^k for small non-negative integer k, by squaring: five multiplies at
    /// the largest exponent Dome uses, against a libm pow call per sample.
    [[nodiscard]] static double powInteger (double base, int exponent) noexcept
    {
        double result = 1.0;
        double power = base;

        for (int e = exponent; e > 0; e >>= 1)
        {
            if ((e & 1) != 0)
                result *= power;

            power *= power;
        }

        return result;
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

    static constexpr int kHarmonicCount = 16;

    // Cached by refreshShapeState for the shape that needs them.
    double vintageA_    { 1.5 };
    double vintageNorm_ { 0.0 };
    double vintageMean_ { 0.0 };

    /// The unclamped exponent the morph asked for; the clamp against the
    /// current pitch happens in refreshDomeClamp.
    double domeK_       { 1.0 };

    int    domeKLo_     { 1 };
    int    domeKHi_     { 2 };
    double domeBlend_   { 0.0 };
    double domeMeanLo_  { 0.5 };
    double domeMeanHi_  { 0.375 };
    double domeScaleLo_ { 2.0 };
    double domeScaleHi_ { 1.6 };

    double doubleOffset_ { 0.0 };

    double harmonicAmp_[static_cast<std::size_t> (kHarmonicCount)] {};

    /// The morph-derived amplitudes before the Nyquist fades -- the sixteen
    /// pow() results refreshHarmonicFades reads instead of recomputing.
    double harmonicRaw_[static_cast<std::size_t> (kHarmonicCount)] {};

    std::uint64_t noiseState_ { 0x9e3779b97f4a7c15ull };
    double noiseLowpass_     { 0.0 };
    double noiseCoefficient_ { 1.0 };

    double triangleState_ { 0.0 };

    bool   wrapped_      { false };
    double wrapFraction_ { 0.0 };

    bool   pendingSync_     { false };
    double pendingSyncFrac_ { 0.0 };

    StepCorrector corrector_;
};

} // namespace tezla::dsp
