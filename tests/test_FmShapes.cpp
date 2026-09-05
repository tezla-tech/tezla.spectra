// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include <tezla/dsp/Fft.hpp>
#include <tezla/dsp/FmBandwidth.hpp>
#include <tezla/dsp/FmOperator.hpp>
#include <tezla/dsp/FmShapes.hpp>

using namespace tezla::dsp;

namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;
}

TEZLA_TEST (the_sine_shape_is_bit_exactly_the_operator_that_shipped_before_shapes)
{
    // ---------------------------------------------------------------------
    // **CLAUDE.md section 7, and the reason sine is index 0**
    // ---------------------------------------------------------------------
    //
    // "Almost identity" means every existing project changes the day the plugin
    // updates. Sine must not merely round-trip through the table to something
    // close -- it must not touch the table at all. Two claims, both exact:
    // the table read returns std::sin, and an operator set to Sine produces
    // what the operator produced before shapes existed.
    const auto& tables = FmShapeTables::instance();

    for (int i = 0; i < 4096; ++i)
    {
        const double phase = static_cast<double> (i) / 4096.0 * 3.0 - 1.0;   // negatives too
        CHECK (tables.read (FmShape::sine, phase) == std::sin (kTwoPi * phase));
    }

    FmOperator shaped;
    FmOperator reference;
    shaped.prepare (48000.0);
    reference.prepare (48000.0);
    shaped.setFrequency (220.0);
    reference.setFrequency (220.0);
    shaped.setShape (FmShape::sine);
    shaped.setFeedback (0.3);
    reference.setFeedback (0.3);
    shaped.setQuadratureNeeded (true);
    reference.setQuadratureNeeded (true);

    std::size_t differing = 0;

    for (int i = 0; i < 48000; ++i)
    {
        const double pm = 0.8 * std::sin (kTwoPi * 3.0 * static_cast<double> (i) / 48000.0);

        if (! (shaped.advance (pm) == reference.advance (pm)))
            ++differing;
    }

    std::printf ("        [shape] Sine vs the pre-shape operator: %zu of 48000 samples differ\n",
                 differing);

    CHECK (differing == 0);
}

TEZLA_TEST (every_shape_stops_where_it_promises_to)
{
    // ---------------------------------------------------------------------
    // **The promise the index cap is built on**
    // ---------------------------------------------------------------------
    //
    // `fmShapeHarmonics` is not a description of these tables, it is a promise
    // to `FmBandwidth`: a modulator carrying n harmonics puts its sideband
    // ladder n times further out, and the cap multiplies by exactly that
    // number. A table with a partial above its stated count under-protects,
    // silently, in the one place aliasing is a defect rather than the
    // instrument.
    //
    // So: render one cycle of each shape into a power-of-two window -- which
    // makes every partial land exactly on a bin, no leakage, no window -- and
    // assert nothing above the promise.
    constexpr std::size_t kFrames = 4096;
    const auto& tables = FmShapeTables::instance();

    for (int s = 0; s < static_cast<int> (FmShape::count); ++s)
    {
        const auto shape = static_cast<FmShape> (s);
        const int promised = fmShapeHarmonics (shape);

        // Exactly 8 cycles in the window, so harmonic h lands on bin 8h.
        constexpr int kCycles = 8;
        std::vector<double> rendered (kFrames, 0.0);

        for (std::size_t i = 0; i < kFrames; ++i)
            rendered[i] = tables.read (shape,
                static_cast<double> (kCycles) * static_cast<double> (i)
                  / static_cast<double> (kFrames));

        const auto spectrum = fftOfReal (rendered);

        // **Against the STRONGEST partial, not the first one.** Half sine is
        // built from `|sin|`'s series, which has only *even* harmonics -- it
        // has no first harmonic at all. Referencing harmonic 1 divided by the
        // FFT's own floor and duly reported 255 harmonics and +11 dB of leak
        // for a wave that stops at 8: the test was broken, not the table.
        double strongest = 0.0;

        for (std::size_t bin = kCycles; bin < kFrames / 2; bin += kCycles)
            strongest = std::max (strongest, std::norm (spectrum[bin]));

        double above = 0.0;
        int highest = 0;

        for (std::size_t bin = kCycles; bin < kFrames / 2; bin += kCycles)
        {
            const double power = std::norm (spectrum[bin]);
            const int harmonic = static_cast<int> (bin) / kCycles;

            if (power > 0.0 && 10.0 * std::log10 (power / strongest) > -80.0)
                highest = std::max (highest, harmonic);

            if (harmonic > promised)
                above += power;
        }

        const double leakDb = 10.0 * std::log10 (
            std::max (above / std::max (strongest, 1.0e-30), 1.0e-30));

        std::printf ("        [shape] %-10s promises %2d harmonics, highest seen %2d, "
                     "energy above it %+7.1f dB\n",
                     fmShapeNames[s], promised, highest, leakDb);

        CHECK (highest <= promised);

        // -80 dB is the same floor the oversampling filters are designed to,
        // and here it is the interpolation error rather than the series.
        CHECK (leakDb < -80.0);
    }
}

TEZLA_TEST (every_shape_is_normalised_and_centred)
{
    // Peak 1, because an operator's output IS a phase deviation in cycles for
    // whatever it modulates: a shape peaking at 1.27 would silently deliver
    // 27 % more index than the matrix cell says, and the cap would be capping a
    // number that is not the one doing the work.
    //
    // Mean 0 for the same reason from the other side: a DC offset on a
    // modulator is a constant phase shift on whatever it drives -- inaudible,
    // and therefore misleading.
    const auto& tables = FmShapeTables::instance();

    for (int s = 0; s < static_cast<int> (FmShape::count); ++s)
    {
        const auto shape = static_cast<FmShape> (s);

        double peak = 0.0;
        double mean = 0.0;
        constexpr int kPoints = 8192;

        for (int i = 0; i < kPoints; ++i)
        {
            const double value = tables.read (shape, static_cast<double> (i) / kPoints);

            peak = std::max (peak, std::abs (value));
            mean += value;
        }

        mean /= kPoints;

        std::printf ("        [shape] %-10s peak %.6f, mean %+.2e\n",
                     fmShapeNames[s], peak, mean);

        CHECK (std::abs (peak - 1.0) < 1.0e-3);
        CHECK (std::abs (mean) < 1.0e-3);
    }
}

TEZLA_TEST (a_shaped_modulator_widens_the_prediction_by_its_harmonic_count)
{
    // ---------------------------------------------------------------------
    // **This is what makes a non-sine operator safe rather than lucky**
    // ---------------------------------------------------------------------
    //
    // Leaving `setHarmonics` at 1 for a saw modulator under-predicts the top by
    // sixteen, and the index cap then lets through exactly the aliasing it
    // exists to stop. The prediction has to move with the shape.
    const auto predict = [] (int harmonics)
    {
        FmBandwidth bandwidth;
        bandwidth.setOperatorCount (2);
        bandwidth.setOperatorFrequency (0, 220.0);
        bandwidth.setOperatorFrequency (1, 660.0);
        bandwidth.setHarmonics (0, 1);
        bandwidth.setHarmonics (1, harmonics);
        bandwidth.setIndex (1, 0, 2.0);   // op 2 -> op 1, index 2 cycles

        return bandwidth.topSidebandHz();
    };

    const double sine = predict (1);
    const double saw = predict (fmShapeHarmonics (FmShape::saw));

    std::printf ("        [shape] predicted top with a sine modulator %.0f Hz, "
                 "with a 16-harmonic saw %.0f Hz (%.1fx)\n",
                 sine, saw, saw / sine);

    // The modulator's own top moves by exactly its harmonic count, and the
    // sidebands ride on it, so the prediction has to grow by close to 16.
    CHECK (saw > sine * 12.0);

    // And the cap has to answer it: the same matrix that needs no capping with
    // a sine modulator must be capped with a saw one.
    const auto scaleFor = [] (int harmonics)
    {
        FmBandwidth bandwidth;
        bandwidth.setOperatorCount (2);
        bandwidth.setOperatorFrequency (0, 220.0);
        bandwidth.setOperatorFrequency (1, 660.0);
        bandwidth.setHarmonics (0, 1);
        bandwidth.setHarmonics (1, harmonics);
        bandwidth.setIndex (1, 0, 2.0);

        return bandwidth.indexScaleFor (0.9 * 0.5 * 192000.0);
    };

    const double sineScale = scaleFor (1);
    const double sawScale = scaleFor (fmShapeHarmonics (FmShape::saw));

    std::printf ("        [shape] cap scale: sine modulator %.6f, saw modulator %.6f\n",
                 sineScale, sawScale);

    CHECK (sineScale == 1.0);   // exactly inert, not nearly
    CHECK (sawScale < 1.0);
}

TEZLA_TEST (a_saw_modulator_at_high_index_still_meets_the_aliasing_floor)
{
    // ---------------------------------------------------------------------
    // **CLAUDE.md section 7: aliasing is a defect everywhere except where it
    // is the instrument, and a waveform choice is not that place**
    // ---------------------------------------------------------------------
    //
    // The measurement is the whole point of the additive design: run a saw
    // modulator hard enough that a naive implementation would smear, at the
    // oversampled internal rate the engine actually runs at, and look at what
    // folds back into the audible band.
    constexpr double kRate = 192000.0;      // the internal rate at x4 from 48 k
    constexpr std::size_t kFrames = 1u << 16;

    // **Exactly on a bin, and that is the whole measurement.** 220 Hz at this
    // rate and length is bin 75.09: every partial then smears across
    // neighbouring bins and lands in the "inharmonic" pile, which reported
    // +13.3 dB of aliasing for a pair that has none at all. Bin 75 is
    // 219.7265625 Hz, and a 3:1 modulator on it is bin 225, so every sideband
    // -- at carrier +/- n * modulator, all multiples of the carrier for a 3:1
    // ratio -- lands exactly on a multiple of bin 75.
    constexpr std::size_t kCarrierBin = 75;
    constexpr double kBinWidth = kRate / static_cast<double> (kFrames);
    constexpr double kCarrier = static_cast<double> (kCarrierBin) * kBinWidth;
    constexpr double kModulator = 3.0 * kCarrier;
    const auto render = [] (double index, FmShape shape)
    {
        FmOperator carrier;
        FmOperator modulator;
        carrier.prepare (kRate);
        modulator.prepare (kRate);
        carrier.setFrequency (kCarrier);
        modulator.setFrequency (kModulator);
        modulator.setShape (shape);

        std::vector<double> out (kFrames, 0.0);

        for (std::size_t i = 0; i < kFrames; ++i)
            out[i] = carrier.advance (index * modulator.advance (0.0));

        return out;
    };

    // Everything at a multiple of 220 Hz is a real partial; anything else is
    // folded. Bin width is 192000 / 65536 = 2.93 Hz, and 220 is 75 bins, so
    // every partial lands exactly on a bin.
    const auto inharmonicDb = [] (const std::vector<double>& rendered)
    {
        const auto spectrum = fftOfReal (rendered);

        double harmonic = 0.0;
        double inharmonic = 0.0;

        // The audible band only -- above 20 kHz nobody is listening and the
        // decimator removes it anyway.
        const auto top = static_cast<std::size_t> (20000.0 / kBinWidth);

        for (std::size_t bin = 1; bin < top; ++bin)
        {
            const double power = std::norm (spectrum[bin]);

            if (bin % kCarrierBin == 0)
                harmonic += power;
            else
                inharmonic += power;
        }

        return 10.0 * std::log10 (
            std::max (inharmonic / std::max (harmonic, 1.0e-30), 1.0e-30));
    };

    struct Case { FmShape shape; double index; };

    const Case cases[] {
        { FmShape::sine, 4.0 },
        { FmShape::triangle, 2.0 },
        { FmShape::square, 1.0 },
        { FmShape::saw, 1.0 },
        { FmShape::halfSine, 2.0 }
    };

    for (const auto& item : cases)
    {
        const double floorDb = inharmonicDb (render (item.index, item.shape));

        std::printf ("        [alias] %-10s modulator at index %.1f : %+7.1f dB inharmonic\n",
                     fmShapeNames[static_cast<int> (item.shape)], item.index, floorDb);

        // CLAUDE.md section 7's gate: no inharmonic component above -60 dBFS in
        // the audible band. Stated as total inharmonic energy against total
        // harmonic energy, which is the stricter reading of the two.
        CHECK (floorDb < -60.0);
    }
}
