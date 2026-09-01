// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <tezla/dsp/ModalResonator.hpp>

using namespace tezla::dsp;

namespace
{
/// Frequency by interpolated positive-going zero crossings -- the house
/// estimator (RMS-safe, works on decaying rings).
[[nodiscard]] double ringFrequency (const std::vector<double>& signal, double rate)
{
    double first = -1.0, last = -1.0;
    int cycles = 0;

    for (std::size_t i = 1; i < signal.size(); ++i)
    {
        if (! (signal[i - 1] < 0.0 && signal[i] >= 0.0))
            continue;

        const double frac = -signal[i - 1] / (signal[i] - signal[i - 1]);
        const double at = static_cast<double> (i - 1) + frac;

        if (first < 0.0)
            first = at;
        else
        {
            last = at;
            ++cycles;
        }
    }

    return cycles < 1 ? 0.0 : cycles * rate / (last - first);
}

/// Peak absolute value inside [from, from + span).
[[nodiscard]] double peakIn (const std::vector<double>& signal,
                             std::size_t from, std::size_t span)
{
    double peak = 0.0;

    for (std::size_t i = from; i < from + span && i < signal.size(); ++i)
        peak = std::max (peak, std::abs (signal[i]));

    return peak;
}
} // namespace

TEZLA_TEST (a_mode_rings_at_the_frequency_it_was_given_at_every_rate)
{
    // The bank's first duty, held at 48 and 192 kHz alike: the pole angle is
    // computed from the actual rate, never from a baked-in one (CLAUDE.md
    // section 6).
    for (const double rate : { 48000.0, 192000.0 })
    {
        ModalResonator bank;
        bank.prepare (rate);
        bank.setModeCount (1);
        bank.setMode (0, 440.0, 1.0, 1.0);
        bank.excite (0, 1.0);

        std::vector<double> out (static_cast<std::size_t> (rate / 2.0));
        for (auto& sample : out)
            sample = bank.process();

        CHECK_NEAR (ringFrequency (out, rate), 440.0, 0.5);
    }
}

TEZLA_TEST (t60_is_the_time_it_says)
{
    // Excite, then compare the ring's peak envelope half a T60 apart: the
    // drop must be 30 dB within half a decibel. Getting r's formula wrong by
    // a factor of the sample rate -- the classic slip -- lands hundreds of
    // decibels away, not half of one.
    constexpr double rate = 48000.0;
    constexpr double t60 = 0.5;

    ModalResonator bank;
    bank.prepare (rate);
    bank.setModeCount (1);
    bank.setMode (0, 300.0, t60, 1.0);
    bank.excite (0, 1.0);

    const auto total = static_cast<std::size_t> (rate * t60);
    std::vector<double> out (total);
    for (auto& sample : out)
        sample = bank.process();

    // Windows one cycle wide would ripple; 50 ms holds many cycles of 300 Hz.
    const auto window = static_cast<std::size_t> (rate * 0.05);
    const double early = peakIn (out, 0, window);
    const double late = peakIn (out, total / 2, window);

    const double dropDb = 20.0 * std::log10 (early / late);

    CHECK_NEAR (dropDb, 30.0, 0.5);
}

TEZLA_TEST (retune_mid_ring_is_amplitude_continuous_and_lands)
{
    // The property the complex form was chosen for, and the one a two-pole
    // implementation fails: spinning the pole to a new angle keeps the
    // phasor's length, so a ringing mode glides without a click. This is
    // what the per-hit tension Drop stands on.
    constexpr double rate = 48000.0;

    ModalResonator bank;
    bank.prepare (rate);
    bank.setModeCount (1);
    bank.setMode (0, 200.0, 2.0, 1.0);
    bank.excite (0, 1.0);

    std::vector<double> out;
    out.reserve (24000);

    double steadyStep = 0.0, previous = 0.0;

    // 12100 samples is 50.42 cycles of 200 Hz: the retune lands mid-swing,
    // where a discontinuity is largest. Landing it on a cycle boundary
    // (12000 = exactly 50 cycles) let a state-zeroing break slip past the
    // step check, caught only by the frequency check -- measured, then moved.
    for (int i = 0; i < 12100; ++i)
    {
        const double sample = bank.process();

        if (i > 0)
            steadyStep = std::max (steadyStep, std::abs (sample - previous));

        previous = sample;
        out.push_back (sample);
    }

    // The retune, mid-ring.
    bank.setMode (0, 300.0, 2.0, 1.0);

    double retuneStep = 0.0;
    std::vector<double> after;
    after.reserve (12000);

    for (int i = 0; i < 12000; ++i)
    {
        const double sample = bank.process();
        retuneStep = std::max (retuneStep, std::abs (sample - previous));
        previous = sample;
        after.push_back (sample);
    }

    // A 300 Hz sine steps at most 1.5x as far per sample as a 200 Hz one of
    // the same amplitude, so anything under 2x steady is glide, not click.
    // Break-checked: zeroing the state on retune fails BOTH checks -- the
    // mid-swing stop is a step far outside this bound, and the dead ring
    // reads 0 Hz below.
    CHECK (retuneStep < 2.0 * steadyStep);

    CHECK_NEAR (ringFrequency (after, rate), 300.0, 1.0);
}

TEZLA_TEST (energy_decays_monotonically_and_actually_reaches_the_floor)
{
    // The retirement number, asserted the way the zombie-voice lesson
    // demands: not "the output is quiet" but "the energy readout itself
    // falls, and falls far enough that a threshold will genuinely fire".
    constexpr double rate = 48000.0;

    ModalResonator bank;
    bank.prepare (rate);
    bank.setModeCount (8);

    for (int mode = 0; mode < 8; ++mode)
    {
        bank.setMode (mode, 100.0 * (mode + 1), 0.3, 1.0 / (mode + 1));
        bank.excite (mode, 1.0);
    }

    const double initial = bank.energy();
    CHECK (initial > 0.0);

    double last = initial;
    bool monotone = true;

    for (int step = 0; step < 20; ++step)
    {
        for (int i = 0; i < 2400; ++i)
            (void) bank.process();

        const double now = bank.energy();
        monotone = monotone && now < last;
        last = now;
    }

    CHECK (monotone);

    // One second is over three T60s: the ring must sit at least 100 dB below
    // where it started, or a retirement threshold could never fire.
    CHECK (last < initial * 1.0e-10);
}

TEZLA_TEST (modal_silence_in_is_exact_silence_out)
{
    ModalResonator bank;
    bank.prepare (48000.0);
    bank.setModeCount (ModalResonator::kMaxModes);

    for (int mode = 0; mode < ModalResonator::kMaxModes; ++mode)
        bank.setMode (mode, 50.0 * (mode + 1), 1.0, 1.0);

    bool allZero = true;

    for (int i = 0; i < 4096; ++i)
        allZero = allZero && bank.process() == 0.0;

    CHECK (allZero);
    CHECK (bank.energy() == 0.0);
}

TEZLA_TEST (modal_no_op_setters_do_not_disturb_the_ring)
{
    // The house rule: pushing the same settings every control tick must be
    // free of side effects, to the bit.
    ModalResonator quiet, pushed;

    for (auto* bank : { &quiet, &pushed })
    {
        bank->prepare (48000.0);
        bank->setModeCount (4);

        for (int mode = 0; mode < 4; ++mode)
        {
            bank->setMode (mode, 220.0 * (mode + 1), 0.8, 0.5);
            bank->excite (mode, 0.7);
        }
    }

    double worst = 0.0;

    for (int i = 0; i < 8192; ++i)
    {
        const double a = quiet.process();

        for (int mode = 0; mode < 4; ++mode)
            pushed.setMode (mode, 220.0 * (mode + 1), 0.8, 0.5);
        pushed.setModeCount (4);

        const double b = pushed.process();
        worst = std::max (worst, std::abs (a - b));
    }

    CHECK (worst == 0.0);
}

TEZLA_TEST (input_weights_scale_the_continuous_drive)
{
    // The bow's coupling path: the same input through weight 0.5 must ring
    // the mode at exactly half the amplitude of weight 1.0.
    ModalResonator full, half;

    for (auto* bank : { &full, &half })
    {
        bank->prepare (48000.0);
        bank->setModeCount (1);
        bank->setMode (0, 440.0, 0.5, 1.0);
    }

    full.setInputWeight (0, 1.0);
    half.setInputWeight (0, 0.5);

    double worst = 0.0;

    for (int i = 0; i < 4800; ++i)
    {
        const double input = std::sin (0.13 * i);
        const double a = full.process (input);
        const double b = half.process (input);

        worst = std::max (worst, std::abs (a - 2.0 * b));
    }

    CHECK (worst < 1.0e-12);
}

TEZLA_TEST (contact_velocity_reads_the_contact_points_derivative)
{
    // contactVelocity() claims to be d/dt of the contact-point signal, in
    // output units per second. Checked against the centred finite
    // difference of the actual output for one ringing mode with gain ==
    // weight (so output and contact point are the same signal), and
    // checked to be the SAME number at 192 kHz as at 48 kHz -- the
    // angular-frequency scaling is physical, not per-sample, which is what
    // the bow's rate independence stands on.
    double rms48 = 0.0;

    for (const double fs : { 48000.0, 192000.0 })
    {
        ModalResonator bank;
        bank.prepare (fs);
        bank.setModeCount (1);
        bank.setMode (0, 100.0, 1.0, 0.7);
        bank.setInputWeight (0, 0.7);
        bank.excite (0, 1.0);

        const int samples = static_cast<int> (0.1 * fs);

        std::vector<double> output;
        std::vector<double> velocity;

        for (int n = 0; n < samples; ++n)
        {
            velocity.push_back (bank.contactVelocity());
            output.push_back (bank.process());
        }

        // The readout at n sees the state process() is ABOUT to advance, and
        // output[n] is taken after that advance -- so velocity[n] is the
        // trajectory's slope at output index n - 1, and the centred
        // difference must straddle that point.
        double errorSq = 0.0;
        double signalSq = 0.0;

        for (int n = 2; n < samples; ++n)
        {
            const double slope = 0.5 * (output[static_cast<std::size_t> (n)]
                                        - output[static_cast<std::size_t> (n - 2)]) * fs;
            const double read = velocity[static_cast<std::size_t> (n)];

            errorSq += (read - slope) * (read - slope);
            signalSq += read * read;
        }

        CHECK (signalSq > 0.0);
        CHECK (std::sqrt (errorSq / signalSq) < 0.02);

        double sumSq = 0.0;

        for (const double v : velocity)
            sumSq += v * v;

        const double rms = std::sqrt (sumSq / velocity.size());

        if (fs < 96000.0)
            rms48 = rms;
        else
            CHECK_NEAR (rms / rms48, 1.0, 0.02);   // same physical reading
    }
}

TEZLA_TEST (sixty_four_modes_cost_what_the_plan_budgeted)
{
    // The estimate the defaults will be chosen against: one voice's worth of
    // bank, one second of audio, printed so regressions show in the log.
    ModalResonator bank;
    bank.prepare (48000.0);
    bank.setModeCount (ModalResonator::kMaxModes);

    for (int mode = 0; mode < ModalResonator::kMaxModes; ++mode)
    {
        bank.setMode (mode, 40.0 + 90.0 * mode, 2.0, 1.0);
        bank.excite (mode, 0.1);
    }

    double sink = 0.0;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 48000; ++i)
        sink += bank.process();

    const double seconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - start).count();

    std::printf ("        [modal cpu] 64 modes: %.2f%% of one core (sink %g)\n",
                 100.0 * seconds, sink);

    CHECK (seconds < 0.05);   // 5% of a core for one full-fat voice
}

// ---------------------------------------------------------------------------
// Bloom -- the modes talk to each other
// ---------------------------------------------------------------------------

namespace
{
/// A bar-like object: inharmonic, so the coupling's sum and difference tones
/// land between the modes rather than on them, which is the hard case.
void buildBar (ModalResonator& bank, double fundamental, double t60, int modes)
{
    static constexpr double kRatios[8] { 1.0, 2.756, 5.404, 8.933, 13.345,
                                         18.638, 24.811, 31.864 };

    bank.setModeCount (modes);

    for (int index = 0; index < modes; ++index)
    {
        // Beyond the tabulated eight, continue the bar's asymptotic
        // (n + 0.5)^2 spacing so the object stays inharmonic all the way up.
        const double n = static_cast<double> (index) + 1.0;
        const double ratio = index < 8 ? kRatios[index]
                                       : (n + 0.5) * (n + 0.5) / 2.25;

        bank.setMode (index, fundamental * ratio, t60 / (1.0 + 0.1 * n),
                      1.0 / n);
    }
}

/// The fraction of the bank's energy sitting above `splitHz`, measured over a
/// window -- which is what "the shimmer climbed out of the low modes" means
/// as a number.
double highFraction (const std::vector<double>& x, std::size_t from,
                     std::size_t count, double rate, double splitHz)
{
    double high = 0.0;
    double total = 0.0;

    const std::size_t n = std::min (count, x.size() - from);

    // A one-pole split rather than an FFT: the question is a ratio of band
    // energies over time, not a spectrum, and a filter answers it without a
    // window function's leakage confusing a moving target.
    const double coefficient = std::exp (-2.0 * std::numbers::pi * splitHz / rate);
    double low = 0.0;

    for (std::size_t i = 0; i < n; ++i)
    {
        const double sample = x[from + i];

        low = coefficient * low + (1.0 - coefficient) * sample;

        const double top = sample - low;

        high += top * top;
        total += sample * sample;
    }

    return total > 0.0 ? high / total : 0.0;
}

std::vector<double> strikeAndRing (ModalResonator& bank, double amplitude,
                                   std::size_t samples)
{
    for (int index = 0; index < bank.getModeCount(); ++index)
        bank.excite (index, amplitude / (1.0 + 0.15 * static_cast<double> (index)));

    std::vector<double> out (samples);

    for (auto& sample : out)
        sample = bank.process();

    return out;
}
} // namespace

TEZLA_TEST (bloom_at_zero_is_the_bank_bit_for_bit)
{
    // `ModalResonator` lives in shared/, so the phase-2 addition has to be
    // invisible when it is off -- not nearly, the same doubles. The guard is
    // in `process()`: the coupling term is only computed when the amount is
    // non-zero.
    ModalResonator plain;
    ModalResonator bloomed;

    for (auto* bank : { &plain, &bloomed })
    {
        bank->prepare (48000.0);
        buildBar (*bank, 110.0, 2.0, 32);
    }

    bloomed.setBloom (0.0);

    for (int index = 0; index < 32; ++index)
    {
        plain.excite (index, 0.5);
        bloomed.excite (index, 0.5);
    }

    for (std::size_t n = 0; n < 48000; ++n)
        CHECK (isExactly (plain.process(), bloomed.process()));
}

TEZLA_TEST (bloom_moves_energy_upward_after_the_strike)
{
    // The claim the feature exists for. A struck bank's high-frequency
    // fraction can only *fall* as it rings, because the high modes are
    // damped hardest -- unless something is feeding them. Bloom is that
    // something, and the measurement is the fraction late in the ring
    // against the fraction early in it.
    //
    // Measured on a 32-mode bar at 110 Hz, split at 4 * the fundamental,
    // comparing the window 0.3-0.5 s after the strike against 0.02-0.22 s:
    //
    //                    early     late    late/early
    //     bloom 0.00    0.1521   0.1187        0.780
    //     bloom 1.00    0.0035   0.3883      111.395
    //
    // Without bloom the top loses a fifth of its share as the object rings
    // down, which is all a linear bank can do -- the high modes are damped
    // hardest, so their share can only fall.
    //
    // With bloom it **starts lower and ends three times higher**. Lower at
    // first because the coupling is conservative: the cascade is drawing on
    // the low modes' energy and has not yet delivered it. Higher later
    // because that is where it delivers. A tam-tam does exactly this and a
    // mode bank on its own cannot.
    //
    // **The strike amplitude is 0.30, and it used to be 0.9.** That mattered,
    // because through the plugin the control measured as completely inert:
    // late-window energy shares at 110/220/440 Hz read 0.1797/0.3642/0.4481
    // with Bloom off and 0.1917/0.3584/0.4434 at maximum, which is a knob that
    // moves the fourth decimal place. The von Karman term's rate goes as
    // amplitude squared and "large displacement" was referenced to 1.0, where
    // this test drives it; a Malleus voice at full velocity peaks at 0.046.
    //
    // `kBloomDrive` fixes the operating point, calibrated on a voice against
    // velocity (see `tezla-measure malleus`). 0.30 here is where this bar sits
    // in the same window.
    const auto fractions = [] (double bloom)
    {
        ModalResonator bank;
        bank.prepare (48000.0);
        buildBar (bank, 110.0, 2.0, 32);
        bank.setBloom (bloom);

        const auto rendered = strikeAndRing (bank, 0.30, 48000);

        const double early = highFraction (rendered, 960, 9600, 48000.0, 440.0);
        const double late = highFraction (rendered, 14400, 9600, 48000.0, 440.0);

        return std::pair { early, late };
    };

    const auto off = fractions (0.0);
    const auto on = fractions (ModalResonator::kMaxBloom);

    std::printf ("        [bloom] off: early %.4f late %.4f (%.3f)  "
                 "on: early %.4f late %.4f (%.3f)\n",
                 off.first, off.second, off.second / off.first,
                 on.first, on.second, on.second / on.first);

    // The high band holds its share far better with bloom than without.
    CHECK (on.second / on.first > off.second / off.first + 0.2);
}

TEZLA_TEST (bloom_is_amplitude_dependent_which_is_what_makes_it_physical)
{
    // A quadratic coupling's rate goes as amplitude squared, so a quiet
    // strike must bloom measurably less than a loud one. This is the
    // difference between a physical nonlinearity and a high shelf -- a shelf
    // would lift the top by the same proportion however hard the object was
    // hit, and would be the wrong instrument.
    //
    // Measured, late high-band fraction against strike amplitude, bloom at
    // its ceiling against bloom off. The amplitudes are the ones the
    // instrument reaches -- a full-velocity Malleus voice peaks at 0.046, and
    // a strike of 0.013 here drives the bank to 0.043:
    //
    //     strike   bank peak   bloom 1   bloom 0
    //       0.05      0.1046     0.1058    0.1187
    //       0.10      0.2092     0.0994    0.1187
    //       0.20      0.4185     0.3812    0.1187
    //       0.30      0.6277     0.3883    0.1187
    //       0.40      0.8370     0.3924    0.1187
    //       0.60      1.2554     0.4012    0.1187
    //       0.90      1.8831     0.0235    0.1187
    //       1.40      2.9293     0.0062    0.1187
    //
    // Three things in that table, and all three are the point.
    //
    // The linear column is **0.1187 at every amplitude**, to the last digit
    // printed: a linear bank does not care how hard it was hit, which is the
    // property bloom exists to remove.
    //
    // The bloom column is **threshold-like, not a smooth ramp**: dormant at a
    // soft hit, engaging through 0.20, holding to 0.60, and falling away again
    // as the injection swamps the state rather than perturbing it. That is
    // more physical than a proportional law would be, and it is why the
    // assertion below is written as "hard blooms, soft does not" rather than
    // as a monotone chain.
    //
    // And the **useful window is about 10 dB wide**, which is narrower than
    // the range a velocity control covers. That is a real limitation of a
    // coupling bounded by `q / (1 + |q|)`; widening it means changing the
    // saturation, which is a redesign of the bound rather than a constant.
    // `kBloomDrive` places the window, and it is calibrated **on a voice**
    // rather than here -- see `tezla-measure malleus`, which sweeps velocity
    // and is the table that decided the constant. This bar is not the object
    // the instrument plays and reaches a bank peak twenty times higher for the
    // same coupling response, which is exactly why the calibration cannot be
    // done from a unit test on a bare bank.
    const auto lateFraction = [] (double amplitude, double bloom)
    {
        ModalResonator bank;
        bank.prepare (48000.0);
        buildBar (bank, 110.0, 2.0, 32);
        bank.setBloom (bloom);

        return highFraction (strikeAndRing (bank, amplitude, 48000),
                             14400, 9600, 48000.0, 440.0);
    };

    const double quiet = lateFraction (0.10, ModalResonator::kMaxBloom);
    const double loud = lateFraction (0.30, ModalResonator::kMaxBloom);

    std::printf ("        [bloom] late high fraction, quiet %.4f loud %.4f\n",
                 quiet, loud);

    // A hard strike blooms; a soft one barely does.
    CHECK (loud > 3.0 * quiet);

    // And the linear bank is genuinely indifferent to level.
    const double quietOff = lateFraction (0.10, 0.0);
    const double loudOff = lateFraction (0.30, 0.0);

    CHECK (std::abs (loudOff - quietOff) < 1.0e-9);
}

TEZLA_TEST (every_bloom_setting_of_every_object_stays_bounded_and_dies)
{
    // CLAUDE.md section 7, and the gate this phase was ordered around: a
    // feedback loop around a nonlinearity needs a bound that cannot be
    // defeated **and a test that sweeps the whole parameter space rather than
    // sampling it**.
    //
    // The bound is the rational soft clip `q / (1 + |q|)`, whose magnitude is
    // below 1 for every finite input -- there is no threshold for a large
    // enough signal to step over -- together with the cap on the amount.
    //
    // 21 bloom settings (pushed to twice the ceiling, because a modulated
    // control can be driven there and the clamp is what has to hold) x 5
    // fundamentals over six octaves x 4 decay times x 3 mode counts = 1260
    // combinations, each struck at full amplitude and rung for a second.
    //
    // Measured: worst |sample| **4.3874** anywhere in the sweep, nothing
    // non-finite, and **every combination's energy still falls** over the
    // second half of the ring -- worst end/mid ratio 0.4068. That second
    // number is the assertion with teeth: a loop that sustained itself would
    // hold its energy flat rather than diverging visibly, and a peak test
    // alone would not notice.
    //
    // This test earned its place twice. The first formulation bounded the
    // coupling term and argued from that; the sweep found worst sample
    // **72.9** and energy *rising* 4.48x, because a bounded term is not a
    // bounded result -- a constant drive into a two-second resonator settles
    // at 28000 times it. The second scaled by mode bandwidth and got to 24.6
    // and 2.91, better and still wrong. Only making the coupling
    // **conservative** -- energy redistributed, never created -- made the
    // bound unconditional, and then it needed no constant at all.
    double worst = 0.0;
    double worstEnergyRatio = 0.0;
    int combinations = 0;

    for (int step = 0; step <= 20; ++step)
    {
        for (const double fundamental : { 30.0, 110.0, 440.0, 1200.0, 3000.0 })
        {
            for (const double t60 : { 0.05, 0.4, 2.0, 8.0 })
            {
                for (const int modes : { 4, 24, 64 })
                {
                    ModalResonator bank;
                    bank.prepare (48000.0);
                    buildBar (bank, fundamental, t60, modes);
                    bank.setBloom (2.0 * ModalResonator::kMaxBloom
                                     * static_cast<double> (step) / 20.0);

                    ++combinations;

                    for (int index = 0; index < modes; ++index)
                        bank.excite (index, 1.0);

                    double midEnergy = 0.0;

                    for (std::size_t n = 0; n < 48000; ++n)
                    {
                        const double sample = bank.process();

                        CHECK (std::isfinite (sample));
                        worst = std::max (worst, std::abs (sample));

                        if (n == 24000)
                            midEnergy = bank.energy();
                    }

                    // The ring must still be dying: energy at the end below
                    // energy at the midpoint, for every combination.
                    const double ratio = midEnergy > 0.0
                                       ? bank.energy() / midEnergy
                                       : 0.0;

                    worstEnergyRatio = std::max (worstEnergyRatio, ratio);
                }
            }
        }
    }

    std::printf ("        [bloom] %d combinations, worst |sample| %.4f, "
                 "worst end/mid energy %.4f\n",
                 combinations, worst, worstEnergyRatio);

    CHECK (worst < 8.0);
    CHECK (worstEnergyRatio < 1.0);
}
