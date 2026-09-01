// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

#include <tezla/dsp/MinimumPhaseFir.hpp>

#include <TapeLoss.hpp>

using tezla::dsp::MinimumPhaseFir;

// -- The regression that made the lift safe ---------------------------------
//
// MinimumPhaseFir was TapeLoss's private machinery before Membrana needed
// it. The pins below are FNV-1a hashes over the RAW BIT PATTERNS of every
// designed tap, plus three spot taps as exact 64-bit patterns, captured from
// the pre-lift TapeLoss at four sample rates and two tape speeds. Equality
// here is bit-for-bit: if any reordering, "simplification" or optimisation
// of the shared designer changes one bit of one Ferrite tap, this goes red
// -- and it must, because projects already render through these exact
// coefficients.
//
// (A physics aside the table shows: 96 kHz at 15 ips shares its spot taps
// with 48 kHz at 7.5 ips. The loss curve is a function of f/v alone, so
// doubling both the rate and the speed samples the same normalised curve --
// the wavelength story of the header, visible in the numbers.)

TEZLA_TEST (minimum_phase_lift_left_ferrite_taps_where_they_were)
{
    struct Pin
    {
        double fs, ips;
        int taps;
        double t0, t1, t64;
    };

    // Captured 2026-09-01 from the design as it lived inside TapeLoss, to
    // prove the lift into shared/ changed nothing.
    //
    // **These were bit patterns, compared with ==, and that was wrong.** It
    // held on GCC, held on clang, held under qemu-aarch64 -- and failed 26 of
    // its 40 comparisons on MSVC, because these taps come out of `std::log`,
    // `std::exp` and an FFT's `sin`/`cos`, and the UCRT's transcendentals are
    // not bit-identical to glibc's. No implementation promises they would be;
    // IEEE-754 does not require correctly-rounded `exp`.
    //
    // The claim the pins exist to make is "the lift moved this code without
    // changing what it computes", and a tolerance of 1e-12 relative makes it
    // just as well: a real regression -- a changed window, a dropped fold, a
    // different cepstrum -- moves a tap by percent, not by an ulp. What the
    // bit form added over that was not extra rigour, only a false claim about
    // portability. The bit-exactness CLAUDE.md section 7 does require is a
    // different thing entirely: identity through a neutral setting *within one
    // build*, which no cross-toolchain comparison can speak to.
    //
    // The tap count stays exact, because it is an integer and a change in it
    // is a design change.
    static constexpr Pin pins[] = {
        { 44100, 15, 128, 0.074751097186262619, 0.12567188446794414, 0.0005022240609759784 },
        { 44100, 7.5, 128, 0.015526518996231361, 0.040051026182702801, 0.0011688317003482156 },
        { 48000, 15, 128, 0.063904479483842314, 0.11297327297565427, 0.00055535237350941031 },
        { 48000, 7.5, 128, 0.012425104548114786, 0.033285037125768795, 0.0012998733633568923 },
        { 96000, 15, 256, 0.012425104548114786, 0.033285037125768795, 0.0012998733633568923 },
        { 96000, 7.5, 256, 0.0035250415764499117, 0.0076221711516535401, 0.0030159718283206275 },
        { 192000, 15, 512, 0.0035250415764499117, 0.0076221711516535401, 0.0030159718283206275 },
        { 192000, 7.5, 512, 0.0018781686314332168, 0.002278060239467217, 0.0055641488387866925 },
    };

    // Relative, because the taps span 0.126 down to 0.0005 and one absolute
    // tolerance cannot be tight on both.
    const auto matches = [] (double actual, double pinned)
    {
        return std::abs (actual - pinned) <= 1.0e-12 * std::abs (pinned);
    };

    for (const auto& pin : pins)
    {
        tezla::ferrite::TapeLoss loss;
        loss.prepare (pin.fs);
        loss.setSpeedIps (pin.ips);
        loss.prepare (pin.fs);   // land the design in the active state

        CHECK (loss.activeTapCount() == pin.taps);
        CHECK (matches (loss.activeTapData()[0], pin.t0));
        CHECK (matches (loss.activeTapData()[1], pin.t1));
        CHECK (matches (loss.activeTapData()[64], pin.t64));
    }
}

// -- The designer on its own terms ------------------------------------------

TEZLA_TEST (minimum_phase_flat_target_designs_a_unit_impulse)
{
    // log |H| = 0 everywhere is the identity: the cepstrum is zero, the fold
    // is zero, exp gives 1 at every bin, and the impulse is delta[0]. The
    // residue is FFT round-off only.
    double halfLog[513] {};
    double taps[64] {};
    MinimumPhaseFir<1024>::design (halfLog, taps, 64);

    CHECK_NEAR (taps[0], 1.0, 1.0e-12);

    for (int n = 1; n < 64; ++n)
        CHECK_NEAR (taps[n], 0.0, 1.0e-12);
}

TEZLA_TEST (minimum_phase_matches_a_one_pole_target_and_front_loads)
{
    // A 6 dB/oct rolloff at 2 kHz, sampled at the design bins for 48 kHz
    // with the usual -60 dB floor. The designed FIR must sit on the analytic
    // curve through the band, and its energy must sit at the front -- the
    // minimum-phase claim, measured rather than asserted.
    constexpr double fs = 48000.0;
    constexpr double fc = 2000.0;

    double halfLog[513] {};

    for (int bin = 0; bin <= 512; ++bin)
    {
        const double hz = bin * fs / 1024.0;
        double magnitude = 1.0 / std::sqrt (1.0 + (hz / fc) * (hz / fc));

        if (magnitude < 1.0e-3)
            magnitude = 1.0e-3;

        halfLog[bin] = std::log (magnitude);
    }

    double taps[128] {};
    MinimumPhaseFir<1024>::design (halfLog, taps, 128);

    // DFT of the taps against the analytic curve: within 0.1 dB from
    // 100 Hz to 20 kHz (measured worst 0.03 dB mid-band; the check leaves
    // room for the truncation to 128 taps).
    for (double hz : { 100.0, 300.0, 1000.0, 2000.0, 6000.0, 20000.0 })
    {
        double re = 0.0, im = 0.0;

        for (int n = 0; n < 128; ++n)
        {
            const double angle = -2.0 * 3.141592653589793 * hz * n / fs;
            re += taps[n] * std::cos (angle);
            im += taps[n] * std::sin (angle);
        }

        const double designed = 20.0 * std::log10 (std::sqrt (re * re + im * im));
        const double target = 20.0 * std::log10 (1.0 / std::sqrt (1.0 + (hz / fc) * (hz / fc)));
        CHECK_NEAR (designed, target, 0.1);
    }

    // Front-loading: a linear-phase realisation of the same magnitude has
    // its energy centred at tap 64; the minimum-phase one must carry over
    // 95% inside the first 16 taps.
    double head = 0.0, total = 0.0;

    for (int n = 0; n < 128; ++n)
    {
        total += taps[n] * taps[n];

        if (n < 16)
            head += taps[n] * taps[n];
    }

    CHECK (head / total > 0.95);
}
