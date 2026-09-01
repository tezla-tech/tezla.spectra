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
// the wavelength story of the header, visible in the bits.)

namespace
{
    std::uint64_t fnv1a (const double* taps, int count)
    {
        std::uint64_t hash = 1469598103934665603ULL;

        for (int n = 0; n < count; ++n)
        {
            const auto bits = std::bit_cast<std::uint64_t> (taps[n]);

            for (int b = 0; b < 8; ++b)
            {
                hash ^= (bits >> (8 * b)) & 0xff;
                hash *= 1099511628211ULL;
            }
        }

        return hash;
    }

    std::uint64_t bitsOf (double value)
    {
        return std::bit_cast<std::uint64_t> (value);
    }
}

TEZLA_TEST (minimum_phase_lift_left_ferrite_taps_byte_identical)
{
    struct Pin
    {
        double fs, ips;
        int taps;
        std::uint64_t hash, t0, t1, t64;
    };

    // Captured 2026-09-01 from the design as it lived inside TapeLoss.
    static constexpr Pin pins[] = {
        { 44100.0, 15.0, 128, 0xb3b4f15469c13404ULL, 0x3fb322e34dc14f41ULL, 0x3fc016042ce8836aULL, 0x3f4074f5f5655f4fULL },
        { 44100.0, 7.5, 128, 0x4d944c4e1030e478ULL, 0x3f8fcc5e1a79ee3eULL, 0x3fa481916f407c4aULL, 0x3f53266f7b5c8c30ULL },
        { 48000.0, 15.0, 128, 0x6e330a632707268eULL, 0x3fb05c0b4173751cULL, 0x3fbcebd100c0a4deULL, 0x3f4232a22417e268ULL },
        { 48000.0, 7.5, 128, 0x52de614dcfe6617bULL, 0x3f8972554d77e09aULL, 0x3fa10abc83cd7a2fULL, 0x3f554c1065696bbcULL },
        { 96000.0, 15.0, 256, 0x146713225e9a7e43ULL, 0x3f8972554d77e09aULL, 0x3fa10abc83cd7a2fULL, 0x3f554c1065696bbcULL },
        { 96000.0, 7.5, 256, 0x06b34eb810884977ULL, 0x3f6ce08c49366aa8ULL, 0x3f7f386cfd224507ULL, 0x3f68b4f38bc91162ULL },
        { 192000.0, 15.0, 512, 0x2d60d62ed8bcc947ULL, 0x3f6ce08c49366aa8ULL, 0x3f7f386cfd224507ULL, 0x3f68b4f38bc91162ULL },
        { 192000.0, 7.5, 512, 0x44f2aa63db43035fULL, 0x3f5ec59c364bd65cULL, 0x3f62a97047423034ULL, 0x3f76ca6ed4aec1baULL },
    };

    for (const auto& pin : pins)
    {
        tezla::ferrite::TapeLoss loss;
        loss.prepare (pin.fs);
        loss.setSpeedIps (pin.ips);
        loss.prepare (pin.fs);   // land the design in the active state

        CHECK (loss.activeTapCount() == pin.taps);
        CHECK (fnv1a (loss.activeTapData(), loss.activeTapCount()) == pin.hash);
        CHECK (bitsOf (loss.activeTapData()[0]) == pin.t0);
        CHECK (bitsOf (loss.activeTapData()[1]) == pin.t1);
        CHECK (bitsOf (loss.activeTapData()[64]) == pin.t64);
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
