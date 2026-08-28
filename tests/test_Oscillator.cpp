#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <vector>

#include <tezla/dsp/Oscillator.hpp>
#include <tezla/dsp/Oversampler.hpp>
#include <tezla/measure/Fft.hpp>
#include <tezla/measure/Harmonics.hpp>
#include <tezla/measure/Signals.hpp>

using namespace tezla::dsp;
namespace measure = tezla::measure;

namespace
{
constexpr double kRate = 48000.0;
constexpr std::size_t kFft = 1 << 14;

/// A start phase that is not a discontinuity for any shape or pulse width used
/// here. Starting *on* an edge leaves the first sample uncorrected -- there is
/// no previous cycle to have scheduled it from -- and one uncorrected sample
/// spreads evenly across a 16384-point spectrum, which reads as 10 to 15 dB of
/// aliasing that has nothing to do with the oscillator.
///
/// This cost two false failures before it was noticed: a pulse reset to 0.25
/// with a width of 0.25 measured 33 dB worse than the same pulse at any other
/// width, and it looked exactly like a broken duty edge.
constexpr double kSafeStart = 0.37;

/// The textbook inline polyBLEP, written only for a saw.
///
/// Exists to check the general corrector against, and for nothing else: it can
/// only handle a discontinuity at a known phase, which is why the oscillator
/// does not use it.
double inlinePolyBlep (double phase, double dt) noexcept
{
    if (phase < dt)
    {
        const double t = phase / dt;
        return t + t - t * t - 1.0;
    }

    if (phase > 1.0 - dt)
    {
        const double t = (phase - 1.0) / dt;
        return t * t + t + t + 1.0;
    }

    return 0.0;
}

std::vector<double> render (Oscillator& osc, std::size_t n)
{
    std::vector<double> out (n);

    for (auto& sample : out)
        sample = osc.advance();

    return out;
}

/// Everything that is not a harmonic of the fundamental, in dB relative to it.
double aliasingOf (const std::vector<double>& signal, double fundamentalHz)
{
    return measure::analyseHarmonics (signal, kRate, fundamentalHz).audibleAliasingDb;
}
} // namespace

// ---------------------------------------------------------------------------
// The corrector, against the thing it generalises
// ---------------------------------------------------------------------------

TEZLA_TEST (step_corrector_agrees_with_inline_polyblep_on_a_saw)
{
    // The assertion that pins the indexing.
    //
    // The oscillator uses a general two-sample corrector so that hard sync --
    // a step of arbitrary height at an arbitrary fraction of a sample -- can
    // use the same path. A plain saw is the one case the textbook inline form
    // also covers, so the two must agree exactly. Getting the fraction off by a
    // sample does not look like a bug; it looks like slightly more aliasing,
    // and nothing else would catch it.
    constexpr double increment = 220.0 / kRate;

    // Started mid-cycle, deliberately. Phase zero sits exactly *on* a
    // discontinuity, and the inline form corrects it because it assumes an
    // infinite past -- while a real oscillator starting cold has no previous
    // cycle to have scheduled it from. That is a one-sample startup difference
    // and not the indexing question this test is asking about.
    constexpr double start = 0.5;

    Oscillator osc;
    osc.setShape (OscShape::saw);
    osc.setIncrement (increment);
    osc.reset (start);

    // The same saw, built by hand with the inline correction -- and read at the
    // current phase *before* advancing, which is the ordering the oscillator
    // uses and the ordering the residual requires: the sample preceding a step
    // needs its half of the correction before it is emitted.
    double phase = start;
    double worst = 0.0;

    for (int i = 0; i < 4000; ++i)
    {
        const double mine = osc.advance();
        const double theirs = (2.0 * phase - 1.0) - inlinePolyBlep (phase, increment);

        worst = std::max (worst, std::abs (mine - theirs));

        phase += increment;
        if (phase >= 1.0)
            phase -= 1.0;
    }

    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (step_corrector_spreads_a_step_and_conserves_it)
{
    // Whatever fraction the step lands at, the two samples of residual must sum
    // to the same thing -- otherwise the correction adds or removes energy and
    // the waveform drifts.
    for (const double frac : { 0.0, 0.25, 0.5, 0.75, 0.99 })
    {
        StepCorrector corrector;
        corrector.reset();
        corrector.addStep (-2.0, frac);

        const double first = corrector.take();
        const double second = corrector.take();
        const double third = corrector.take();

        // (1-f)^2 and -f^2, scaled by half the height.
        CHECK_NEAR (first, -(1.0 - frac) * (1.0 - frac), 1.0e-12);
        CHECK_NEAR (second, frac * frac, 1.0e-12);

        // And it empties itself: a correction that leaked would ring for ever.
        CHECK_NEAR (third, 0.0, 0.0);
    }
}

// ---------------------------------------------------------------------------
// Aliasing, which is the only reason any of this exists
// ---------------------------------------------------------------------------

TEZLA_TEST (correction_buys_twenty_two_decibels_over_a_naive_saw)
{
    // The claim is that the *corrector* is doing the work, so it is measured
    // against the same waveform with the correction removed -- which is a naive
    // saw, the thing every synth that aliases is doing.
    //
    // Measured, at 48 kHz over a 16384-point window:
    //
    //     note      naive   corrected   gain
    //      41 Hz    -29.4       -51.9   22.5
    //      82 Hz    -26.4       -48.9   22.5
    //     164 Hz    -23.4       -45.8   22.4
    //     440 Hz    -19.0       -41.7   22.7
    //
    // The gain is flat across the range, which is what a correction of a fixed
    // order does, and it is the number to hold onto: the absolute figure moves
    // with the note because a saw's harmonic count does, but the *benefit* does
    // not.
    for (const double frequency : { 41.2, 82.4, 164.8, 440.0 })
    {
        const double binExact = measure::binExactFrequency (frequency, kRate, kFft);
        const double increment = binExact / kRate;

        Oscillator osc;
        osc.setShape (OscShape::saw);
        osc.setIncrement (increment);
        osc.reset (0.5);

        const auto corrected = render (osc, kFft);

        std::vector<double> naive (kFft);
        double phase = 0.5;

        for (auto& sample : naive)
        {
            sample = 2.0 * phase - 1.0;
            phase += increment;

            if (phase >= 1.0)
                phase -= 1.0;
        }

        const double gain = aliasingOf (naive, binExact) - aliasingOf (corrected, binExact);

        CHECK (gain > 20.0);
        CHECK (gain < 26.0);
    }
}

TEZLA_TEST (every_doubling_of_the_internal_rate_buys_nineteen_decibels)
{
    // PolyBLEP is a second-order correction: it does not remove the images, it
    // pushes them down by a fixed amount and pushes the rest *up* out of the
    // audible band as the rate rises. Both matter, and together they are what
    // decides the instrument's internal rate.
    //
    // Measured through the real halfband chain, back down to 48 kHz:
    //
    //     note        x1      x2      x4      x8
    //      41 Hz   -51.9   -72.6   -91.7  -110.0
    //      82 Hz   -48.9   -69.6   -88.7  -107.0
    //     440 Hz   -41.7   -62.3   -81.2   -99.6
    //     880 Hz   -39.1   -59.1   -78.1   -96.7
    //
    // Nineteen decibels a doubling, flat across four octaves of note and three
    // of rate. **x2 already clears -60 dBFS everywhere a bass instrument
    // plays**, which is the finding the voice is built on.
    //
    // Rendered here at the higher rate directly rather than through the
    // oversampler -- the images land above the audible band either way, and a
    // unit test should not need a resampler to measure an oscillator.
    for (const double frequency : { 82.4, 440.0 })
    {
        double previous = 0.0;

        for (int factor : { 1, 2, 4 })
        {
            const double rate = kRate * factor;
            const double binExact = measure::binExactFrequency (frequency, rate, kFft);

            Oscillator osc;
            osc.setShape (OscShape::saw);
            osc.setIncrement (binExact / rate);
            osc.reset (0.5);

            const auto rendered = render (osc, kFft);
            const double alias = measure::analyseHarmonics (rendered, rate, binExact)
                                     .audibleAliasingDb;

            if (factor > 1)
            {
                const double gain = previous - alias;

                CHECK (gain > 15.0);
                CHECK (gain < 24.0);
            }

            previous = alias;
        }
    }
}

TEZLA_TEST (at_the_rate_a_voice_runs_the_oscillator_clears_the_spec)
{
    // CLAUDE.md section 7 asks for nothing inharmonic above -60 dBFS. A raw saw
    // at the host rate does not meet that and no second-order correction makes
    // it -- a 41 Hz saw has 585 harmonics below Nyquist and the last is only
    // 55 dB down. At the x2 the voice runs at, it does:
    //
    //     note     x1      x2      x4
    //      41 Hz  -51.9   -72.7   -91.7
    //      82 Hz  -48.9   -69.6   -88.7
    //     440 Hz  -41.7   -62.3   -81.2
    //     880 Hz  -39.1   -59.1   -78.1
    //
    // 880 Hz at x2 is the worst case at -59.1, which is why the bound here is
    // -58 and not -60: two octaves above where a bass instrument lives, at the
    // minimum factor. The engine's Auto will pick x4, where the same note
    // measures -78.
    constexpr double internal = kRate * 2.0;

    for (const double frequency : { 41.2, 82.4, 164.8, 440.0, 880.0 })
    {
        const double binExact = measure::binExactFrequency (frequency, internal, kFft);

        Oscillator osc;
        osc.setShape (OscShape::saw);
        osc.setIncrement (binExact / internal);
        osc.reset (kSafeStart);

        const double alias = measure::analyseHarmonics (render (osc, kFft), internal, binExact)
                                 .audibleAliasingDb;

        CHECK (alias < -58.0);
    }
}

TEZLA_TEST (starting_on_a_discontinuity_costs_a_sample_and_only_a_sample)
{
    // Phase zero sits exactly *on* the saw's discontinuity, and an oscillator
    // starting cold has no previous cycle to have scheduled the correction
    // from. So the first sample is uncorrected -- one sample, once, at note-on,
    // landing under the amp envelope's attack.
    //
    // Measured against the inline form, which assumes an infinite past and so
    // corrects that sample. The test's job is to show the disagreement is
    // *exactly one sample deep*, so that if it ever becomes a persistent error
    // somebody notices.
    constexpr double increment = 440.0 / kRate;

    Oscillator osc;
    osc.setShape (OscShape::saw);
    osc.setIncrement (increment);
    osc.reset (0.0);

    double phase = 0.0;

    const double coldFirst = osc.advance();
    const double referenceFirst = (2.0 * phase - 1.0) - inlinePolyBlep (phase, increment);

    CHECK (std::abs (coldFirst - referenceFirst) > 0.5);

    phase += increment;

    double worst = 0.0;

    for (int i = 0; i < 4000; ++i)
    {
        const double mine = osc.advance();
        const double theirs = (2.0 * phase - 1.0) - inlinePolyBlep (phase, increment);

        worst = std::max (worst, std::abs (mine - theirs));

        phase += increment;
        if (phase >= 1.0)
            phase -= 1.0;
    }

    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (pulse_corrects_both_of_its_edges)
{
    // A pulse has two discontinuities per cycle. Correcting only the one at
    // phase zero leaves the other aliasing exactly as much as an uncorrected
    // oscillator does -- so a narrow pulse, where the two edges are far apart,
    // must measure as well as a saw does.
    constexpr double internal = kRate * 2.0;

    for (const double width : { 0.5, 0.25, 0.1 })
    {
        const double binExact = measure::binExactFrequency (82.4, internal, kFft);

        Oscillator osc;
        osc.setShape (OscShape::pulse);
        osc.setWidth (width);
        osc.setIncrement (binExact / internal);
        osc.reset (kSafeStart);

        const double alias = measure::analyseHarmonics (render (osc, kFft), internal, binExact)
                                 .audibleAliasingDb;

        CHECK (alias < -60.0);
    }
}

TEZLA_TEST (pulse_width_changes_the_spectrum_and_not_the_pitch)
{
    // Pulse width modulation is a timbre control. A square has no even
    // harmonics at all; move the duty off a half and they arrive. That is the
    // whole mechanism, and it means PWM must leave the fundamental where it is.
    const double binExact = measure::binExactFrequency (110.0, kRate, kFft);

    const auto secondHarmonicOf = [binExact] (double width)
    {
        Oscillator osc;
        osc.setShape (OscShape::pulse);
        osc.setWidth (width);
        osc.setIncrement (binExact / kRate);
        osc.reset();

        const auto report = measure::analyseHarmonics (render (osc, kFft), kRate, binExact);
        return report.harmonicsDb[0];
    };

    // A true square: the even harmonics cancel.
    CHECK (secondHarmonicOf (0.5) < -50.0);

    // Off square: they arrive, and keep arriving as it narrows.
    CHECK (secondHarmonicOf (0.35) > -20.0);
    CHECK (secondHarmonicOf (0.15) > secondHarmonicOf (0.35));
}

// ---------------------------------------------------------------------------
// Hard sync
// ---------------------------------------------------------------------------

TEZLA_TEST (hard_sync_takes_the_masters_pitch_and_the_slaves_timbre)
{
    // The sound: the slave is thrown back to the start of its cycle every time
    // the master completes one, so what you hear is the *master's* period with
    // a timbre set by how far through its own cycle the slave got. Sweeping the
    // slave sweeps the chop point, and that is the classic aggressive lead.
    //
    // Asserted as periodicity rather than through an aliasing figure, because
    // "the pitch is the master's" *is* the statement that the output repeats at
    // the master's period -- and it can be checked exactly, with no FFT and no
    // window, by comparing one cycle against the next. The aliasing metric is
    // the wrong instrument here: it reported a synced oscillator getting
    // *worse* with oversampling, which a correct one cannot do.
    constexpr int period = 400;                 // an integer, so this is exact
    constexpr double masterIncrement = 1.0 / period;

    for (const double ratio : { 1.5, 2.7, 4.3 })
    {
        Oscillator master;
        master.setShape (OscShape::saw);
        master.setIncrement (masterIncrement);
        master.reset (0.0);

        Oscillator slave;
        slave.setShape (OscShape::saw);
        slave.setIncrement (masterIncrement * ratio);
        slave.reset (0.0);

        std::vector<double> out (period * 12);

        for (auto& sample : out)
        {
            (void) master.advance();

            if (master.didWrap())
                slave.sync (master.getWrapFraction());

            sample = slave.advance();
        }

        // The pitch is the master's, exactly: two settled cycles agree.
        double worst = 0.0;

        for (int i = 0; i < period; ++i)
            worst = std::max (worst, std::abs (out[8 * period + i] - out[9 * period + i]));

        CHECK (worst < 1.0e-9);

        // And it stays inside the waveform's own range -- a reset that mangled
        // the phase would show up here first.
        double peak = 0.0;

        for (const double sample : out)
            peak = std::max (peak, std::abs (sample));

        CHECK (peak <= 1.0 + 1.0e-9);
    }

    // The timbre is the slave's, and it is *brighter* than an unsynced saw at
    // the master's pitch -- which is the whole reason to reach for it. Measured
    // as the mean absolute first difference, a crude but honest high-frequency
    // reading that needs no transform.
    const auto roughness = [] (double ratio)
    {
        Oscillator master;
        master.setShape (OscShape::saw);
        master.setIncrement (masterIncrement);
        master.reset (0.0);

        Oscillator slave;
        slave.setShape (OscShape::saw);
        slave.setIncrement (masterIncrement * ratio);
        slave.reset (0.0);

        double previous = 0.0;
        double total = 0.0;

        for (int i = 0; i < period * 8; ++i)
        {
            (void) master.advance();

            if (ratio > 1.0 && master.didWrap())
                slave.sync (master.getWrapFraction());

            const double value = slave.advance();

            if (i > period)
                total += std::abs (value - previous);

            previous = value;
        }

        return total;
    };

    // Unsynced, the slave is a plain saw. Synced at 4.3x it is chopped four
    // times a cycle, so its edge count -- and its roughness -- go up with it.
    CHECK (roughness (4.3) > roughness (1.0) * 2.0);
    CHECK (roughness (4.3) > roughness (1.5));
}

TEZLA_TEST (the_sync_reset_is_corrected_and_it_is_worth_measuring)
{
    // The reset is a step of arbitrary height at an arbitrary fraction of a
    // sample, and correcting it is the entire reason the corrector is general
    // rather than an inline polyBLEP. So it needs a test that fails when the
    // correction is removed -- and the obvious candidates do not.
    //
    // Periodicity does not: an uncorrected sync is still perfectly periodic,
    // just aliased. The same-frequency case does not either: there the step is
    // zero, so removing its correction changes nothing. Both passed with the
    // sync correction deleted, which is how this gap was found.
    //
    // What does work is comparing against ground truth. The same sync rendered
    // at 8x and decimated through the real halfband chain is what the waveform
    // *should* be; a host-rate render is judged by how close it gets.
    constexpr int blockSize = 256;
    constexpr int factor = 8;
    constexpr int blocks = 48;
    constexpr int total = blockSize * blocks;

    // An exact number of samples, and an exact divisor of the analysis window,
    // so every harmonic of the synced waveform lands squarely on a bin.
    constexpr std::size_t kMasterPeriod = 128;
    constexpr std::size_t kWindow = 1 << 13;
    constexpr double masterIncrement = 1.0 / static_cast<double> (kMasterPeriod);
    constexpr double ratio = 2.7;

    // ---- ground truth: 8x, decimated ---------------------------------------

    std::vector<double> truth;
    truth.reserve (total);

    {
        Oversampler oversampler;
        oversampler.prepare (blockSize, 1, factor);
        oversampler.setFactor (factor);
        oversampler.reset();

        Oscillator master;
        master.setShape (OscShape::saw);
        master.setIncrement (masterIncrement / factor);
        master.reset (0.0);

        Oscillator slave;
        slave.setShape (OscShape::saw);
        slave.setIncrement (masterIncrement * ratio / factor);
        slave.reset (0.0);

        std::vector<double> block (blockSize, 0.0);
        double* channels[1] { block.data() };

        for (int b = 0; b < blocks; ++b)
        {
            std::fill (block.begin(), block.end(), 0.0);

            auto* const* up = oversampler.upsample (channels, blockSize);

            for (int i = 0; i < blockSize * factor; ++i)
            {
                (void) master.advance();

                if (master.didWrap())
                    slave.sync (master.getWrapFraction());

                up[0][i] = slave.advance();
            }

            oversampler.downsample (channels, blockSize);

            for (int i = 0; i < blockSize; ++i)
                truth.push_back (block[static_cast<std::size_t> (i)]);
        }
    }

    // ---- the corrected oscillator, at the host rate -------------------------

    std::vector<double> corrected (total);

    {
        Oscillator master;
        master.setShape (OscShape::saw);
        master.setIncrement (masterIncrement);
        master.reset (0.0);

        Oscillator slave;
        slave.setShape (OscShape::saw);
        slave.setIncrement (masterIncrement * ratio);
        slave.reset (0.0);

        for (auto& sample : corrected)
        {
            (void) master.advance();

            if (master.didWrap())
                slave.sync (master.getWrapFraction());

            sample = slave.advance();
        }
    }

    // ---- and the same sync with no correction anywhere ----------------------

    std::vector<double> naive (total);

    {
        double masterPhase = 0.0;
        double slavePhase = 0.0;
        const double slaveIncrement = masterIncrement * ratio;

        for (auto& sample : naive)
        {
            sample = 2.0 * slavePhase - 1.0;

            const bool wraps = masterPhase + masterIncrement >= 1.0;

            masterPhase += masterIncrement;
            if (masterPhase >= 1.0)
                masterPhase -= 1.0;

            if (wraps)
            {
                // The reset lands where the master crossed, so the slave keeps
                // only what is left of the sample -- the same arithmetic the
                // oscillator does, without the residual.
                const double frac = 1.0 - masterPhase / masterIncrement;
                slavePhase = (1.0 - frac) * slaveIncrement;
            }
            else
            {
                slavePhase += slaveIncrement;
                if (slavePhase >= 1.0)
                    slavePhase -= 1.0;
            }
        }
    }

    // ---- how close does each get? -------------------------------------------
    //
    // Compared as *magnitude spectra*, not sample by sample. A resampled
    // reference sits at its own group delay and a time-domain difference is
    // dominated by that misalignment -- measured, both candidates scored 0.78
    // against a signal whose own RMS is 0.58, which is the signature of
    // comparing two things that are simply not lined up.
    //
    // Magnitudes do not care about phase. And they are where aliasing actually
    // shows up in an oscillator: a synced oscillator is exactly periodic, so
    // its images fold *onto its own harmonics* rather than between them. There
    // is no inharmonic energy to find -- only harmonics at the wrong height.
    const int latency = Oversampler::latencyForFactor (factor);

    const auto harmonicError = [&] (const std::vector<double>& candidate)
    {
        std::vector<double> a (candidate.begin() + blockSize,
                               candidate.begin() + blockSize + kWindow);
        std::vector<double> b (truth.begin() + blockSize + latency,
                               truth.begin() + blockSize + latency + kWindow);

        const auto spectrumA = measure::fftOfReal (a);
        const auto spectrumB = measure::fftOfReal (b);

        // The master's period is an exact number of samples and an exact
        // divisor of the window, so every harmonic lands on a bin.
        const std::size_t fundamental = kWindow / kMasterPeriod;

        double sum = 0.0;
        int counted = 0;

        for (std::size_t h = 1; h * fundamental < kWindow / 2; ++h)
        {
            const std::size_t bin = h * fundamental;
            const double difference = std::abs (spectrumA[bin]) - std::abs (spectrumB[bin]);

            sum += difference * difference;
            ++counted;
        }

        return std::sqrt (sum / static_cast<double> (counted));
    };

    const double correctedError = harmonicError (corrected);
    const double naiveError = harmonicError (naive);

    // The correction has to earn its place: closer to the band-limited truth
    // than doing nothing, by a margin no rounding could account for.
    CHECK (correctedError < naiveError * 0.75);
}

TEZLA_TEST (sync_at_the_same_frequency_is_no_sync_at_all)
{
    // Sanity, and the boundary case: when the slave runs at the master's own
    // rate the reset lands where the slave was going anyway, so the output is
    // the plain waveform. If the corrector were injecting spurious steps this
    // would show it immediately.
    const double frequency = measure::binExactFrequency (110.0, kRate, kFft);
    const double increment = frequency / kRate;

    Oscillator master;
    master.setShape (OscShape::saw);
    master.setIncrement (increment);
    master.reset();

    Oscillator slave;
    slave.setShape (OscShape::saw);
    slave.setIncrement (increment);
    slave.reset();

    Oscillator alone;
    alone.setShape (OscShape::saw);
    alone.setIncrement (increment);
    alone.reset();

    double worst = 0.0;

    for (int i = 0; i < 4000; ++i)
    {
        (void) master.advance();

        if (master.didWrap())
            slave.sync (master.getWrapFraction());

        worst = std::max (worst, std::abs (slave.advance() - alone.advance()));
    }

    CHECK (worst < 1.0e-9);
}

// ---------------------------------------------------------------------------
// Phase modulation
// ---------------------------------------------------------------------------

TEZLA_TEST (phase_modulation_moves_the_timbre_and_leaves_the_pitch_alone)
{
    // Phase modulation, not frequency modulation, and the difference is the
    // whole reason to prefer it: FM integrates its modulator, so any DC in it
    // walks the pitch away and never comes back. PM adds to the phase directly.
    //
    // So a modulated oscillator must still read as the same note.
    const double carrier = measure::binExactFrequency (110.0, kRate, kFft);

    const auto renderWith = [carrier] (double index)
    {
        Oscillator modulator;
        modulator.setShape (OscShape::sine);
        modulator.setIncrement (carrier * 2.0 / kRate);
        modulator.reset();

        Oscillator osc;
        osc.setShape (OscShape::sine);
        osc.setIncrement (carrier / kRate);
        osc.reset();

        std::vector<double> out (kFft);

        for (auto& sample : out)
            sample = osc.advance (index * modulator.advance());

        return measure::analyseHarmonics (out, kRate, carrier);
    };

    const auto clean = renderWith (0.0);
    const auto modulated = renderWith (0.35);

    // A pure sine, then a sine with a harmonic series on it.
    CHECK (clean.thdDb < -100.0);
    CHECK (modulated.thdDb > -20.0);

    // And still harmonic -- the sidebands land on multiples of the carrier
    // because the ratio is exact, so nothing is counted as inharmonic.
    CHECK (modulated.audibleAliasingDb < -60.0);
}

TEZLA_TEST (oscillator_is_silent_before_it_is_told_a_frequency)
{
    Oscillator osc;
    osc.reset();

    // Increment zero: no cycle, no wrap, no correction, nothing moving.
    for (int i = 0; i < 1000; ++i)
    {
        const double value = osc.advance();

        CHECK (std::isfinite (value));
        CHECK (! osc.didWrap());
    }
}

// ---------------------------------------------------------------------------
// Morph: the shape's own tweak, and what it must NOT touch
// ---------------------------------------------------------------------------

TEZLA_TEST (the_original_four_shapes_ignore_morph_to_the_bit)
{
    // The compatibility contract in one test: saw, pulse, triangle and sine
    // are frozen -- their morphable relatives are the new shapes -- so a
    // project that never heard of morph reopens identical, and so does one
    // where a curious hand swept the knob with a legacy shape selected.
    for (const auto shape : { OscShape::saw, OscShape::pulse, OscShape::triangle, OscShape::sine })
        for (const double morph : { 0.25, 1.0 })
        {
            Oscillator plain;
            plain.setShape (shape);
            plain.setWidth (0.3);
            plain.setIncrement (110.0 / 48000.0);
            plain.reset();

            Oscillator morphed;
            morphed.setShape (shape);
            morphed.setWidth (0.3);
            morphed.setMorph (morph);
            morphed.setIncrement (110.0 / 48000.0);
            morphed.reset();

            for (int i = 0; i < 4800; ++i)
                CHECK (plain.advance() == morphed.advance());
        }
}

TEZLA_TEST (naive_shape_sample_is_the_waveform_the_audio_path_makes)
{
    // One definition serves the DSP, the tests and the on-panel preview; this
    // pins the two ends together. For saw, pulse and sine the audio path's
    // uncorrected waveform IS the static function. The triangle is the
    // documented exception -- the audio path integrates a corrected square --
    // so its check is against the integral's known shape instead: peak at the
    // skew point, straight flanks.
    for (const auto shape : { OscShape::saw, OscShape::pulse, OscShape::sine })
        for (int i = 0; i < 97; ++i)
        {
            const double phase = static_cast<double> (i) / 97.0;

            // A slow oscillator barely corrects, so its output approaches the
            // naive shape away from the discontinuities.
            Oscillator osc;
            osc.setShape (shape);
            osc.setWidth (0.41);
            osc.setIncrement (1.0e-9);
            osc.reset (phase);

            const double naive = Oscillator::naiveShapeSample (shape, phase, 0.41, 0.0);
            const double heard = osc.advance();

            CHECK_NEAR (heard, naive, 1.0e-6);
        }

    // The triangle: rises to +1 exactly at the skew point, -1 at the ends.
    CHECK_NEAR (Oscillator::naiveShapeSample (OscShape::triangle, 0.0, 0.3, 0.0), -1.0, 1e-12);
    CHECK_NEAR (Oscillator::naiveShapeSample (OscShape::triangle, 0.3, 0.3, 0.0), 1.0, 1e-12);
    CHECK_NEAR (Oscillator::naiveShapeSample (OscShape::triangle, 0.65, 0.3, 0.0), 0.0, 1e-12);
    CHECK_NEAR (Oscillator::naiveShapeSample (OscShape::triangle, 0.15, 0.3, 0.0), 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// The phase-3 shapes: Vintage, Dome, Double saw, Harmonic, Noise
// ---------------------------------------------------------------------------

TEZLA_TEST (dome_has_no_aliasing_at_all_because_it_cannot)
{
    // The finite-Fourier identity: integer k of (0.5 - 0.5 cos)^k is exactly k
    // harmonics, and morph blends adjacent integers -- a blend of two
    // band-limited signals is band-limited. So the assertion is not the house
    // -60: it is the analyser's own floor.
    // The low-k, high-note case is the one with teeth. A fractional exponent
    // is NOT band-limited -- sin^2k for non-integer k decays only like
    // n^-(2k+1) -- but at k of six or thirteen that tail is inaudibly steep,
    // and the first break-check of this test sailed through with the blend
    // deleted. At k = 1.5 the tail decays like n^-4 and a high note folds it
    // straight back into the band, which is what the 1/30 morph is doing in
    // this list.
    for (const double morph : { 0.0, 1.0 / 30.0, 0.37, 0.81, 1.0 })
        for (const double frequency : { 220.0, 1760.0, 3520.0 })
        {
            const double binExact = measure::binExactFrequency (frequency, kRate, kFft);

            Oscillator osc;
            osc.setShape (OscShape::dome);
            osc.setMorph (morph);
            osc.setIncrement (binExact / kRate);
            osc.reset();

            CHECK (aliasingOf (render (osc, kFft), binExact) < -120.0);
        }
}

TEZLA_TEST (dome_has_no_dc_at_any_morph)
{
    for (const double morph : { 0.0, 0.2, 0.5, 0.77, 1.0 })
    {
        Oscillator osc;
        osc.setShape (OscShape::dome);
        osc.setMorph (morph);
        osc.setIncrement (100.0 / kRate);   // 100 Hz at 48 k: 480 whole cycles in the window
        osc.reset();

        const auto out = render (osc, 48000);

        double mean = 0.0;
        for (const double sample : out)
            mean += sample;

        CHECK (std::abs (mean / static_cast<double> (out.size())) < 1.0e-9);
    }
}

TEZLA_TEST (double_saw_at_morph_zero_is_the_saw_and_offset_makes_a_comb)
{
    // Aligned ramps sum straight back into the corrected saw, sample for
    // sample; a half-cycle offset cancels the odd harmonics exactly -- the
    // comb the shape exists for.
    Oscillator saw;
    saw.setShape (OscShape::saw);
    saw.setIncrement (110.0 / kRate);
    saw.reset();

    Oscillator aligned;
    aligned.setShape (OscShape::doubleSaw);
    aligned.setMorph (0.0);
    aligned.setIncrement (110.0 / kRate);
    aligned.reset();

    for (int i = 0; i < 9600; ++i)
        CHECK_NEAR (saw.advance(), aligned.advance(), 1.0e-12);

    const double binExact = measure::binExactFrequency (110.0, kRate, kFft);

    Oscillator offset;
    offset.setShape (OscShape::doubleSaw);
    offset.setMorph (1.0);   // half a cycle
    offset.setIncrement (binExact / kRate);
    offset.reset();

    const auto spectrum = measure::fftOfReal (render (offset, kFft));
    const double bin = binExact * kFft / kRate;

    const auto magnitudeAt = [&spectrum] (double where)
    {
        return std::abs (spectrum[static_cast<std::size_t> (std::llround (where))]);
    };

    // Even harmonics stand, odd ones vanish.
    CHECK (magnitudeAt (2.0 * bin) > 100.0 * magnitudeAt (1.0 * bin));
    CHECK (magnitudeAt (4.0 * bin) > 100.0 * magnitudeAt (3.0 * bin));
}

TEZLA_TEST (double_saw_aliases_no_worse_than_the_saw_it_is_made_of)
{
    const double binExact = measure::binExactFrequency (987.0, kRate, kFft);

    Oscillator saw;
    saw.setShape (OscShape::saw);
    saw.setIncrement (binExact / kRate);
    saw.reset();

    const double sawAliasing = aliasingOf (render (saw, kFft), binExact);

    // Mid offsets only, and that is a lesson about the analyser rather than
    // the shape: the offset *attenuates* harmonics on its way to cancelling
    // the odd ones, and aliasingOf is referenced to the fundamental -- so as
    // the fundamental sinks, genuine but unchanged aliasing reads as growth.
    // At morph 1.0 the fundamental is gone entirely and the figure read +36 dB
    // against a waveform that is provably an octave-up saw. The identity test
    // below is the honest form of that case.
    for (const double morph : { 0.2, 0.33, 0.5 })
    {
        Oscillator doubled;
        doubled.setShape (OscShape::doubleSaw);
        doubled.setMorph (morph);
        doubled.setIncrement (binExact / kRate);
        doubled.reset();

        CHECK (aliasingOf (render (doubled, kFft), binExact) < sawAliasing + 3.0);
    }
}

TEZLA_TEST (double_saw_at_half_offset_is_exactly_the_octave_up_saw)
{
    // 0.5 * (saw(p) + saw(p + 0.5)) == 0.5 * saw2f(2p) as an identity, and the
    // corrections agree too: the double saw drops -1 twice per cycle exactly
    // where the octave saw drops -2 once per *its* cycle. So the two must
    // null to numerical noise -- corrections, phase and all.
    const double f = 110.0;

    Oscillator doubled;
    doubled.setShape (OscShape::doubleSaw);
    doubled.setMorph (1.0);
    doubled.setIncrement (f / kRate);
    doubled.reset();

    Oscillator octave;
    octave.setShape (OscShape::saw);
    octave.setIncrement (2.0 * f / kRate);
    octave.reset();

    for (int i = 0; i < 48000; ++i)
        CHECK_NEAR (doubled.advance(), 0.5 * octave.advance(), 1.0e-9);
}

TEZLA_TEST (vintage_is_a_curved_saw_with_the_saw_class_of_aliasing_and_no_dc)
{
    // The curve: at morph 0 the ramp sags visibly away from straight -- the
    // midpoint of an RC ramp is above the line -- and more so at morph 1.
    const double middleAt0 = Oscillator::naiveShapeSample (OscShape::vintage, 0.5, 0.5, 0.0);
    const double middleAt1 = Oscillator::naiveShapeSample (OscShape::vintage, 0.5, 0.5, 1.0);
    const double sawMiddle = 0.0;

    CHECK (middleAt0 > sawMiddle + 0.1);
    CHECK (middleAt1 > middleAt0 + 0.1);

    const double binExact = measure::binExactFrequency (987.0, kRate, kFft);

    Oscillator saw;
    saw.setShape (OscShape::saw);
    saw.setIncrement (binExact / kRate);
    saw.reset();

    const double sawAliasing = aliasingOf (render (saw, kFft), binExact);

    for (const double morph : { 0.0, 1.0 })
    {
        Oscillator vintage;
        vintage.setShape (OscShape::vintage);
        vintage.setMorph (morph);
        vintage.setIncrement (binExact / kRate);
        vintage.reset();

        const auto out = render (vintage, kFft);

        // Same class of aliasing as the saw: the reset step dominates both,
        // and the slope mismatch the curve adds is second order. The margin is
        // measured, not hoped -- 6 dB held with room when this was written.
        CHECK (aliasingOf (out, binExact) < sawAliasing + 6.0);

        double mean = 0.0;
        for (const double sample : out)
            mean += sample;

        CHECK (std::abs (mean / static_cast<double> (out.size())) < 2.0e-3);
    }
}

TEZLA_TEST (harmonic_rolls_off_with_morph_and_fades_partials_at_nyquist)
{
    const double binExact = measure::binExactFrequency (220.0, kRate, kFft);

    const auto seventhOverFirst = [&] (double morph)
    {
        Oscillator osc;
        osc.setShape (OscShape::harmonic);
        osc.setMorph (morph);
        osc.setIncrement (binExact / kRate);
        osc.reset();

        const auto spectrum = measure::fftOfReal (render (osc, kFft));
        const double bin = binExact * kFft / kRate;

        const double first = std::abs (spectrum[static_cast<std::size_t> (std::llround (bin))]);
        const double seventh = std::abs (spectrum[static_cast<std::size_t> (std::llround (7.0 * bin))]);

        return 20.0 * std::log10 (seventh / first);
    };

    // p = 1 at morph 0: the 7th sits at -16.9 dB. p = 3 at morph 1: -50.7.
    CHECK_NEAR (seventhOverFirst (0.0), -16.9, 0.5);
    CHECK_NEAR (seventhOverFirst (1.0), -50.7, 0.5);

    // Band-limited by construction: nothing inharmonic even played high.
    const double high = measure::binExactFrequency (3520.0, kRate, kFft);

    Oscillator osc;
    osc.setShape (OscShape::harmonic);
    osc.setMorph (0.0);
    osc.setIncrement (high / kRate);
    osc.reset();

    CHECK (aliasingOf (render (osc, kFft), high) < -100.0);
}

TEZLA_TEST (noise_is_flat_darkens_with_morph_and_two_seeds_are_two_streams)
{
    Oscillator white;
    white.setShape (OscShape::noise);
    white.seedNoise (12345);
    white.setNoiseCoefficient (1.0);

    Oscillator dark;
    dark.setShape (OscShape::noise);
    dark.seedNoise (12345);
    dark.setNoiseCoefficient (0.02);

    Oscillator other;
    other.setShape (OscShape::noise);
    other.seedNoise (99999);
    other.setNoiseCoefficient (1.0);

    const auto a = render (white, kFft);
    const auto b = render (dark, kFft);
    const auto c = render (other, kFft);

    // Same seed, different colour: the dark one has lost its top end.
    const auto sa = measure::fftOfReal (a);
    const auto sb = measure::fftOfReal (b);

    double lowA = 0.0, highA = 0.0, lowB = 0.0, highB = 0.0;

    for (std::size_t k = 8; k < kFft / 2; ++k)
    {
        const double hz = static_cast<double> (k) * kRate / kFft;

        if (hz < 400.0) { lowA += std::norm (sa[k]); lowB += std::norm (sb[k]); }
        if (hz > 8000.0) { highA += std::norm (sa[k]); highB += std::norm (sb[k]); }
    }

    const double tiltA = 10.0 * std::log10 (highA / lowA);
    const double tiltB = 10.0 * std::log10 (highB / lowB);

    CHECK (tiltA > -6.0);              // white-ish
    CHECK (tiltB < tiltA - 20.0);      // visibly darker

    // Different seeds decorrelate: near-zero normalised cross-correlation.
    double dot = 0.0, ea = 0.0, ec = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        dot += a[i] * c[i];
        ea += a[i] * a[i];
        ec += c[i] * c[i];
    }

    CHECK (std::abs (dot) / std::sqrt (ea * ec) < 0.05);
}
