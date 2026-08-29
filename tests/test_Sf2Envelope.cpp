// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <cstdint>

#include <Sf2Envelope.hpp>

using namespace tezla::svarayantra;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kInstant = -32768.0;   // what a file says for "as fast as possible"

[[nodiscard]] EnvelopeSpec instantSpec()
{
    EnvelopeSpec spec;
    spec.delayTimecents = kInstant;
    spec.attackTimecents = kInstant;
    spec.holdTimecents = kInstant;
    spec.decayTimecents = kInstant;
    spec.releaseTimecents = kInstant;
    spec.sustainLevel = 0.0;
    return spec;
}

[[nodiscard]] double runFor (Sf2Envelope& envelope, std::int64_t samples)
{
    double last = 0.0;

    for (std::int64_t i = 0; i < samples; ++i)
        last = envelope.next();

    return last;
}

[[nodiscard]] double dB (double gain)
{
    return 20.0 * std::log10 (gain);
}
} // namespace

// ---------------------------------------------------------------------------
// Timecents
// ---------------------------------------------------------------------------

TEZLA_TEST (timecents_convert_to_seconds_per_the_spec)
{
    // 0 timecents is one second; each 1200 halves. The defaults (-12000) sit
    // under the -11950 pin, so a generator left unsaid is instant.
    EnvelopeSpec spec = instantSpec();
    spec.attackTimecents = 0.0;

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);

    // Half way up the one-second attack after half a second.
    const double half = runFor (envelope, 24000);
    CHECK (std::abs (half - 0.5) < 1e-3);

    // At the top after the full second, and sustaining there (sustain 0 cB).
    const double top = runFor (envelope, 24000);
    CHECK (std::abs (top - 1.0) < 1e-9);
    CHECK (envelope.phase() == Sf2Envelope::Phase::sustain);

    // -1200 timecents is half a second.
    spec.attackTimecents = -1200.0;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);
    const double quarter = runFor (envelope, 12000);
    CHECK (std::abs (quarter - 0.5) < 1e-3);

    // Instant attack: full level on the very first sample.
    spec.attackTimecents = kInstant;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);
    CHECK (envelope.next() == 1.0);
}

TEZLA_TEST (the_delay_phase_holds_silence_first)
{
    EnvelopeSpec spec = instantSpec();
    spec.delayTimecents = -6000.0;   // 2^-5 s = 31.25 ms = 1500 samples
    spec.attackTimecents = kInstant;

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);

    // Exactly 1500 samples of silence, then the instant attack's top.
    bool allSilent = true;

    for (int i = 0; i < 1500; ++i)
        allSilent = allSilent && envelope.next() == 0.0;

    CHECK (allSilent);
    CHECK (envelope.next() == 1.0);
}

// ---------------------------------------------------------------------------
// The volume decay and sustain, measured in dB
// ---------------------------------------------------------------------------

TEZLA_TEST (volume_decay_falls_at_100_dB_per_stated_time_and_stops_at_the_sustain)
{
    // Decay 0 tc = 1 second, defined as the time for a full 100 dB traverse;
    // sustain 200 cB = -20 dB, so the fall lands after 0.2 s and holds.
    EnvelopeSpec spec = instantSpec();
    spec.decayTimecents = 0.0;
    spec.sustainLevel = 200.0;

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);

    const double atTenth = runFor (envelope, 4800);      // 0.1 s
    CHECK (std::abs (dB (atTenth) - -10.0) < 0.1);

    const double atQuarter = runFor (envelope, 7200);    // 0.25 s
    CHECK (std::abs (dB (atQuarter) - -20.0) < 0.01);
    CHECK (envelope.phase() == Sf2Envelope::Phase::sustain);

    // And it stays put.
    const double later = runFor (envelope, 48000);
    CHECK (later == atQuarter);
}

TEZLA_TEST (sustain_units_are_centibels_for_volume_and_permille_for_modulation)
{
    EnvelopeSpec spec = instantSpec();
    spec.decayTimecents = -3600.0;   // 125 ms, fast but not instant
    spec.sustainLevel = 60.0;        // -6 dB

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);
    const double volumeSustain = runFor (envelope, 48000);
    CHECK (std::abs (volumeSustain - std::pow (10.0, -60.0 / 200.0)) < 1e-12);

    spec.sustainLevel = 250.0;       // modulation: 25% below peak
    envelope.start (spec, Sf2Envelope::Kind::modulation, 60, kRate);
    const double modSustain = runFor (envelope, 48000);
    CHECK (std::abs (modSustain - 0.75) < 1e-12);
}

TEZLA_TEST (full_sustain_attenuation_finishes_the_voice_rather_than_sustaining_silence)
{
    // Sustain at 1000 cB is the format's conventional silence. A voice
    // parked there can never be heard again, so the envelope must declare
    // itself finished -- the CPU-zombie lesson: a silent immortal voice
    // passes every silence test while eating a core.
    EnvelopeSpec spec = instantSpec();
    spec.sustainLevel = 1000.0;

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);

    CHECK (envelope.isFinished());
    CHECK (envelope.isEffectivelySilent());
    CHECK (envelope.next() == 0.0);

    // The same parked-forever trap through a running decay: sustain zero is
    // below the -100 dB floor, so the decay ends there, finished.
    spec.decayTimecents = -3600.0;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);
    (void) runFor (envelope, 48000);
    CHECK (envelope.isFinished());
}

// ---------------------------------------------------------------------------
// Release
// ---------------------------------------------------------------------------

TEZLA_TEST (release_falls_from_the_level_actually_reached)
{
    // Released a quarter of the way up a one-second attack, the level falls
    // from 0.25 (-12.04 dB) at 100 dB per release-second: silence (-100 dB)
    // arrives after (100 - 12.04) / 100 = 0.88 of the stated release.
    EnvelopeSpec spec = instantSpec();
    spec.attackTimecents = 0.0;
    spec.releaseTimecents = 0.0;

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);

    const double reached = runFor (envelope, 12000);
    CHECK (std::abs (reached - 0.25) < 1e-3);

    envelope.release();
    envelope.release();   // idempotent

    (void) runFor (envelope, 40000);          // 0.833 s: not silent yet
    CHECK (! envelope.isFinished());

    (void) runFor (envelope, 3500);           // past 0.88 s: done
    CHECK (envelope.isFinished());
    CHECK (envelope.next() == 0.0);
}

TEZLA_TEST (a_release_during_the_delay_finishes_immediately)
{
    EnvelopeSpec spec = instantSpec();
    spec.delayTimecents = 0.0;   // a second of silence

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);
    (void) runFor (envelope, 100);

    envelope.release();
    CHECK (envelope.isFinished());
}

TEZLA_TEST (quick_release_reaches_silence_in_ten_milliseconds_for_stealing)
{
    EnvelopeSpec spec = instantSpec();   // sustaining at full level
    spec.releaseTimecents = 8000.0;      // a deliberately vast normal release

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::volume, 60, kRate);
    (void) runFor (envelope, 10);
    CHECK (envelope.phase() == Sf2Envelope::Phase::sustain);

    envelope.quickRelease();

    // 100 dB at 100 dB-per-0.01s: done within 480 samples plus rounding.
    (void) runFor (envelope, 500);
    CHECK (envelope.isFinished());
}

// ---------------------------------------------------------------------------
// Key scaling
// ---------------------------------------------------------------------------

TEZLA_TEST (keynum_scaling_stretches_hold_below_middle_c_and_shrinks_it_above)
{
    // keynumToVolEnvHold 100 adds 100 timecents per key below 60: an octave
    // down doubles the hold, an octave up halves it.
    EnvelopeSpec spec = instantSpec();
    spec.holdTimecents = -1200.0;        // 0.5 s at key 60
    spec.keynumToHold = 100.0;
    spec.decayTimecents = -3600.0;
    spec.sustainLevel = 600.0;           // well below peak, so leaving hold shows

    auto holdLength = [&] (int key)
    {
        Sf2Envelope envelope;
        envelope.start (spec, Sf2Envelope::Kind::volume, key, kRate);

        std::int64_t samples = 0;

        while (envelope.next() >= 1.0 && samples < 1000000)
            ++samples;

        return samples;
    };

    const auto at60 = holdLength (60);
    const auto at48 = holdLength (48);
    const auto at72 = holdLength (72);

    CHECK (std::abs (at60 - 24000) <= 1);
    CHECK (std::abs (at48 - 48000) <= 1);
    CHECK (std::abs (at72 - 12000) <= 1);
}

// ---------------------------------------------------------------------------
// The modulation kind
// ---------------------------------------------------------------------------

TEZLA_TEST (the_modulation_envelope_decays_and_releases_linearly)
{
    EnvelopeSpec spec = instantSpec();
    spec.decayTimecents = 0.0;           // full range in one second
    spec.sustainLevel = 400.0;           // sustain at 0.6
    spec.releaseTimecents = 0.0;

    Sf2Envelope envelope;
    envelope.start (spec, Sf2Envelope::Kind::modulation, 60, kRate);

    // Linear: down 0.2 of the range after 0.2 s.
    const double early = runFor (envelope, 9600);
    CHECK (std::abs (early - 0.8) < 1e-3);

    // At the sustain by 0.4 s and holding.
    const double held = runFor (envelope, 14400);
    CHECK (std::abs (held - 0.6) < 1e-3);
    CHECK (envelope.phase() == Sf2Envelope::Phase::sustain);

    // Release: linear at full-range-per-second from 0.6 -- gone by 0.6 s.
    envelope.release();
    (void) runFor (envelope, 27000);
    CHECK (! envelope.isFinished());
    (void) runFor (envelope, 2500);
    CHECK (envelope.isFinished());
}
