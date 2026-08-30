// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

// The Crossbar engine: voices, cadences, the dialler and the line.
//
// test_ToneTables.cpp checks that the *numbers* are the standards. This file
// checks that the instrument actually plays them -- that a key produces those
// two frequencies and nothing else, that a cadence lasts the same time at
// every sample rate, that a released voice dies, and that a line with
// everything switched off is the identity to the bit.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/Exact.hpp>

#include <CrossbarEngine.hpp>

#include "TestFramework.hpp"

using namespace tezla;
using namespace tezla::crossbar;

namespace {

/// The amplitude of a component, by windowed DFT.
///
/// Hann-windowed and normalised so a pure sine of amplitude A reads A. RMS
/// rather than peak picking, for the reason CLAUDE.md section 10 records: a
/// peak reading of a tone with three samples per cycle under-reads by 1.2 dB
/// and looks exactly like a filter.
double amplitudeAt (const std::vector<double>& x, std::size_t from, std::size_t to,
                    double hz, double rate)
{
    double re = 0.0;
    double im = 0.0;
    double windowSum = 0.0;

    const double span = static_cast<double> (to - from);

    for (std::size_t n = from; n < to && n < x.size(); ++n)
    {
        const double along = static_cast<double> (n - from) / span;
        const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * along);
        const double phase = 2.0 * std::numbers::pi * hz
                               * static_cast<double> (n - from) / rate;

        re += window * x[n] * std::cos (phase);
        im -= window * x[n] * std::sin (phase);
        windowSum += window;
    }

    if (windowSum <= 0.0)
        return 0.0;

    return 2.0 * std::hypot (re, im) / windowSum;
}

CrossbarEngine::Parameters neutralParameters()
{
    CrossbarEngine::Parameters p;
    p.attackSeconds = 0.002;
    p.decaySeconds = 0.100;
    p.sustain = 1.0;
    p.releaseSeconds = 0.020;
    p.levelDb = 0.0;
    p.noise = 0.0;
    p.band = BandMode::off;
    p.rateIndex = 0;
    p.codec = dsp::CompandingLaw::off;
    p.bits = 8;
    return p;
}

/// Renders a held key. Returns the whole buffer.
std::vector<double> renderNote (CrossbarEngine& engine, int note, double seconds, double rate,
                                int blockSize = 64)
{
    const auto total = static_cast<std::size_t> (seconds * rate);
    std::vector<double> out (total, 0.0);

    engine.noteOn (note, 1.0);

    for (std::size_t n = 0; n < total; n += static_cast<std::size_t> (blockSize))
    {
        const auto count = std::min (static_cast<std::size_t> (blockSize), total - n);
        engine.process (out.data() + n, nullptr, static_cast<int> (count));
    }

    return out;
}

/// The window the burst-finding envelope integrates over.
constexpr double kEnvelopeWindowSeconds = 0.040;

/// A boxcar RMS envelope, for finding where a cadence's bursts begin and end.
///
/// **Getting this instrument right took two goes and it is worth recording
/// both, because each wrong version failed a correct cadence.**
///
/// The first was the usual level-meter shape -- instant attack, 5 ms release.
/// That is asymmetric, so the rising edge arrives at once and the falling one
/// lags by tau*ln(5): every burst measured 6.6 ms longer than it was.
///
/// The second was a symmetric one-pole on |x|, which cancels that bias. But
/// these signals are *two-tone*, and a two-tone signal's amplitude envelope
/// genuinely goes to zero at every beat null -- the UK ringing tone's 400 and
/// 450 Hz beat at 50 Hz, so any amplitude follower fast enough to resolve a
/// 200 ms gap also rides a 20 ms ripple, and the crossings land wherever the
/// beat happens to be. It read the 400 ms burst as 391.
///
/// Energy is the answer, because a beat moves energy around inside a window
/// without changing how much there is. A boxcar RMS over 40 ms holds two whole
/// beat periods of the worst case. And thresholding at **1/sqrt(2) of the
/// peak** is what makes it symmetric: a boxcar RMS crosses that when the
/// window is exactly half full, on the way up and on the way down alike, so
/// both edges shift by half a window and every duration between them is
/// exact.
std::vector<double> envelopeOf (const std::vector<double>& x, double rate)
{
    std::vector<double> out (x.size(), 0.0);

    const auto window = static_cast<std::size_t> (kEnvelopeWindowSeconds * rate);
    double sum = 0.0;

    for (std::size_t n = 0; n < x.size(); ++n)
    {
        sum += x[n] * x[n];

        if (n >= window)
            sum -= x[n - window] * x[n - window];

        // Always the whole window, never the part of it that has arrived, so
        // the very first edge is delayed by half a window like every other
        // one instead of being read off a nearly empty average.
        out[n] = std::sqrt (sum / static_cast<double> (window));
    }

    return out;
}

/// Where the envelope crosses 1/sqrt(2) of its peak, in seconds, in order.
std::vector<double> crossingsOf (const std::vector<double>& envelope, double rate)
{
    const double peak = *std::max_element (envelope.begin(), envelope.end());
    const double threshold = peak / std::numbers::sqrt2;

    std::vector<double> crossings;
    bool above = envelope.empty() ? false : envelope[0] > threshold;

    for (std::size_t n = 1; n < envelope.size(); ++n)
    {
        const bool nowAbove = envelope[n] > threshold;

        if (nowAbove != above)
        {
            crossings.push_back (static_cast<double> (n) / rate);
            above = nowAbove;
        }
    }

    return crossings;
}

} // namespace

// ---------------------------------------------------------------------------
// What comes out of a key
// ---------------------------------------------------------------------------

TEZLA_TEST (a_dtmf_key_produces_its_two_frequencies_and_nothing_else)
{
    // The claim the whole plugin rests on, measured at the output rather than
    // read off the table: press 5 and 770 Hz and 1336 Hz come out, at the
    // levels the twist asks for, with nothing else anywhere near them.
    //
    // "Nothing else" is what justifies the instrument oversampling nowhere. A
    // sine has no harmonics to fold, so with the line switched off there is
    // no aliasing to suppress -- and this is the measurement that says so
    // rather than the assumption.
    //
    // Measured over 0.1 to 0.4 s of a held key at 48 kHz: 770 Hz at 0.4427
    // and 1336 Hz at 0.5573, summing to 1.0000 and 2.0000 dB apart -- the
    // default twist, exactly. The loudest thing anywhere else between 100 Hz
    // and 20 kHz, excluding 150 Hz either side of the two tones, is 1.478e-06
    // at 925 Hz: **-109.5 dB** relative to the quieter tone, and that is the
    // analysis window's own skirt rather than anything the instrument made.
    constexpr double rate = 48000.0;

    CrossbarEngine engine;
    engine.prepare (rate);
    engine.setParameters (neutralParameters());

    const auto out = renderNote (engine, noteForTone (Tone::digit5), 0.5, rate);

    const auto from = static_cast<std::size_t> (0.1 * rate);
    const auto to = static_cast<std::size_t> (0.4 * rate);

    const double low = amplitudeAt (out, from, to, 770.0, rate);
    const double high = amplitudeAt (out, from, to, 1336.0, rate);

    // The pair sums to 1.0 by construction, split by the +2 dB default twist.
    CHECK_NEAR (low + high, 1.0, 0.01);
    CHECK_NEAR (20.0 * std::log10 (high / low), 2.0, 0.05);

    double worstOther = 0.0;
    double worstHz = 0.0;

    for (double hz = 100.0; hz <= 20000.0; hz += 25.0)
    {
        if (std::abs (hz - 770.0) < 150.0 || std::abs (hz - 1336.0) < 150.0)
            continue;

        const double amplitude = amplitudeAt (out, from, to, hz, rate);

        if (amplitude > worstOther)
        {
            worstOther = amplitude;
            worstHz = hz;
        }
    }

    const double rejectionDb = 20.0 * std::log10 (worstOther / std::min (low, high));

    CHECK (rejectionDb < -80.0);
    CHECK (worstHz > 0.0);   // something was measured
}

TEZLA_TEST (twist_survives_the_whole_signal_path)
{
    // The table test pins the gains; this pins what actually comes out, which
    // is a different claim -- an envelope, a gate or a mixing error between
    // the two would move it.
    //
    // Measured at -6, 0, +2 and +6 dB of twist: the output ratio matches the
    // setting to within 0.02 dB every time.
    constexpr double rate = 48000.0;

    for (double twist : { -6.0, 0.0, 2.0, 6.0 })
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.twistDb = twist;
        engine.setParameters (p);

        const auto out = renderNote (engine, noteForTone (Tone::digit5), 0.4, rate);

        const auto from = static_cast<std::size_t> (0.1 * rate);
        const auto to = static_cast<std::size_t> (0.35 * rate);

        const double low = amplitudeAt (out, from, to, 770.0, rate);
        const double high = amplitudeAt (out, from, to, 1336.0, rate);

        CHECK_NEAR (20.0 * std::log10 (high / low), twist, 0.02);
    }
}

// ---------------------------------------------------------------------------
// Cadences
// ---------------------------------------------------------------------------

TEZLA_TEST (a_cadence_lasts_the_same_time_at_every_sample_rate)
{
    // CLAUDE.md section 6, applied to time rather than to frequency. The
    // cadences are stated in seconds and converted with the actual rate, so a
    // busy tone is a busy tone at 44.1 and at 192 kHz. It is the rule most
    // easily broken -- one `0.5 * 48000` anywhere and the tone runs at the
    // wrong speed in half the sessions on the rig.
    //
    // Measured: the first burst of the North American busy tone reads
    // 0.4965 s and the full period 0.9999 s at 44100, 48000, 96000 and
    // 192000 Hz -- **identical to four decimal places at all four rates**,
    // which is the claim. The 3.5 ms the burst falls short of half a second is
    // the gate's own fade at each end, and it is the same 3.5 ms everywhere.
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CrossbarEngine engine;
        engine.prepare (rate);
        engine.setParameters (neutralParameters());

        const auto out = renderNote (engine, noteForTone (Tone::busy), 2.6, rate);
        const auto crossings = crossingsOf (envelopeOf (out, rate), rate);

        CHECK (crossings.size() >= 4u);

        if (crossings.size() < 4u)
            continue;

        // crossings[0] is the burst's own onset, near zero.
        const double burst = crossings[1] - crossings[0];
        const double period = crossings[2] - crossings[0];

        CHECK_NEAR (burst, 0.5, 0.005);
        CHECK_NEAR (period, 1.0, 0.005);
    }
}

TEZLA_TEST (the_uk_double_ring_has_two_bursts_and_a_two_second_gap)
{
    // A four-step cadence, which is where a two-step implementation would
    // quietly do something plausible instead. 0.4 on, 0.2 off, 0.4 on, 2.0
    // off -- and it is the 0.2 s gap between the two bursts that makes it
    // sound British rather than the frequencies.
    constexpr double rate = 48000.0;

    CrossbarEngine engine;
    engine.prepare (rate);

    auto p = neutralParameters();
    p.region = Region::unitedKingdom;
    engine.setParameters (p);

    const auto out = renderNote (engine, noteForTone (Tone::ringback), 3.5, rate);
    const auto crossings = crossingsOf (envelopeOf (out, rate), rate);

    CHECK (crossings.size() >= 4u);

    // Measured: 0.3989 / 0.2013 / 0.3990 against the specified
    // 0.4 / 0.2 / 0.4.
    if (crossings.size() >= 4u)
    {
        CHECK_NEAR (crossings[1] - crossings[0], 0.4, 0.006);   // first burst
        CHECK_NEAR (crossings[2] - crossings[1], 0.2, 0.006);   // the short gap
        CHECK_NEAR (crossings[3] - crossings[2], 0.4, 0.006);   // second burst
    }
}

TEZLA_TEST (a_free_running_cadence_does_not_start_when_the_key_does)
{
    // An exchange's tone generator was already running when you picked up the
    // handset, so you heard whatever part of the cadence happened to be
    // passing. From-key is the useful default for an instrument; free-running
    // is the honest one, and this is the difference between them.
    //
    // Measured: a key pressed a quarter of a second after the engine starts
    // gets a 0.4965 s first burst from-key and a 0.2465 s one free-running --
    // exactly a quarter of a second less, because that is how much of the
    // burst had already gone by.
    constexpr double rate = 48000.0;

    const auto firstBurstLength = [rate] (CadenceMode mode)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.cadence = mode;
        engine.setParameters (p);

        // Quarter of a second of nothing, so the exchange clock is a quarter
        // of a second into the cadence when the key arrives.
        std::vector<double> lead (static_cast<std::size_t> (0.25 * rate), 0.0);
        engine.process (lead.data(), nullptr, static_cast<int> (lead.size()));

        const auto out = renderNote (engine, noteForTone (Tone::busy), 1.2, rate);
        const auto crossings = crossingsOf (envelopeOf (out, rate), rate);

        return crossings.size() >= 2u ? crossings[1] - crossings[0] : -1.0;
    };

    // From the key: a full half-second burst.
    CHECK_NEAR (firstBurstLength (CadenceMode::fromKey), 0.5, 0.006);

    // Free running: the cadence is already 0.25 s in, so only a quarter of a
    // second of burst is left.
    CHECK_NEAR (firstBurstLength (CadenceMode::freeRunning), 0.25, 0.006);
}

TEZLA_TEST (a_steady_cadence_holds_the_first_step_forever)
{
    // What the Steady setting is for: a busy tone as a drone rather than as a
    // signal.
    //
    // Measured as block RMS over 100 ms windows, which is the right instrument
    // here -- an instantaneous envelope of a two-tone signal dips at the beat
    // whether or not there is a cadence, and would say nothing either way.
    // Measured as the quietest block over the loudest: 1.00000 steady, and
    // 0.00000 cadenced, because half the blocks are silence.
    constexpr double rate = 48000.0;

    const auto blockRmsRange = [rate] (CadenceMode mode)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.cadence = mode;
        engine.setParameters (p);

        const auto out = renderNote (engine, noteForTone (Tone::busy), 3.0, rate);

        const auto block = static_cast<std::size_t> (0.1 * rate);
        double lowest = 1.0e9;
        double highest = 0.0;

        // From 0.2 s, so the note's own attack is not one of the blocks.
        for (std::size_t start = static_cast<std::size_t> (0.2 * rate);
             start + block <= out.size(); start += block)
        {
            double sum = 0.0;

            for (std::size_t n = start; n < start + block; ++n)
                sum += out[n] * out[n];

            const double rms = std::sqrt (sum / static_cast<double> (block));
            lowest = std::min (lowest, rms);
            highest = std::max (highest, rms);
        }

        return lowest / highest;
    };

    CHECK (blockRmsRange (CadenceMode::steady) > 0.99);
    CHECK (blockRmsRange (CadenceMode::fromKey) < 0.1);
}

TEZLA_TEST (a_cadence_boundary_does_not_click)
{
    // Gating a sine abruptly is a click, and a reorder tone gates 240 times a
    // minute. The gate ramps over 3 ms instead, capped at a quarter of the
    // step so a short burst is still a burst.
    //
    // Measured on the North American busy tone, from 50 ms in so the note's
    // own attack -- which is the user's control, and set to 2 ms here -- is
    // not what is being measured. The largest sample-to-sample step anywhere
    // in three seconds is **0.02030**, against the 0.02029 that two sines at
    // 480 and 620 Hz reach on their own at this amplitude. The cadence
    // contributes 0.00001 of full scale: nothing.
    //
    // It did not start there. Applying a step's frequencies at the boundary
    // silences the tone before the gate can fade it, so the gate had nothing
    // left to work on and the step measured 0.04043 -- twice what the signal
    // itself can do, and audible as a tick 120 times a minute. Queueing the
    // step behind the gate (ToneVoice::queueStep) is what took it to 0.00001.
    constexpr double rate = 48000.0;

    CrossbarEngine engine;
    engine.prepare (rate);
    engine.setParameters (neutralParameters());

    const auto out = renderNote (engine, noteForTone (Tone::busy), 3.0, rate);

    double worstStep = 0.0;

    for (std::size_t n = static_cast<std::size_t> (0.05 * rate); n < out.size(); ++n)
        worstStep = std::max (worstStep, std::abs (out[n] - out[n - 1]));

    // What the two sines alone can do: the sum of each partial's peak slope.
    const double signalSlope = 0.5 * dsp::dbToGain (-11.0)
                                 * 2.0 * std::numbers::pi * (480.0 + 620.0) / rate;

    CHECK (worstStep < 1.25 * signalSlope);
}

// ---------------------------------------------------------------------------
// The line
// ---------------------------------------------------------------------------

TEZLA_TEST (the_line_with_everything_off_is_bit_exact_identity)
{
    // Section 7: a stage permanently in the path must be the identity at its
    // neutral setting, not merely transparent. Checked twice -- on the `Line`
    // alone, and through the engine, where a neutral line has to leave the
    // voices untouched to the bit.
    Line line;
    line.prepare (48000.0);

    CHECK (line.isIdentity());

    for (int i = -20000; i <= 20000; ++i)
    {
        const double x = static_cast<double> (i) / 19997.0;
        CHECK (dsp::isExactly (line.process (x), x));
    }

    // Through the engine: a render with the line off against a render with
    // the line set to its other transparent setting -- linear coding at
    // sixteen bits, which is a real path through the codec rather than a
    // branch around it.
    constexpr double rate = 48000.0;

    CrossbarEngine offEngine;
    offEngine.prepare (rate);
    offEngine.setParameters (neutralParameters());
    CHECK (offEngine.isLineIdentity());

    CrossbarEngine wideEngine;
    wideEngine.prepare (rate);

    auto wide = neutralParameters();
    wide.codec = dsp::CompandingLaw::linear;
    wide.bits = 16;
    wideEngine.setParameters (wide);

    const auto plain = renderNote (offEngine, noteForTone (Tone::digit7), 0.3, rate);
    const auto through = renderNote (wideEngine, noteForTone (Tone::digit7), 0.3, rate);

    // Only once the 20 ms switch crossfade has finished -- during it the line
    // is two lines, and `isLineIdentity` says so rather than pretending.
    CHECK (wideEngine.isLineIdentity());

    CHECK (plain.size() == through.size());

    for (std::size_t n = 0; n < plain.size(); ++n)
        CHECK (dsp::isExactly (plain[n], through[n]));
}

TEZLA_TEST (the_rate_reduction_aliases_on_purpose)
{
    // CLAUDE.md section 7's documented exception, asserted rather than
    // assumed: bit crushing and rate reduction are the one place where the
    // folded images are the instrument, so this test says the aliasing goes
    // *up*.
    //
    // A 1477 Hz column tone sampled and held at 4 kHz images at 4000 - 1477 =
    // 2523 Hz. Measured: **-164.4 dB there with the rate off** -- a pure sine
    // has nothing to fold -- and **-6.7 dB with it on**. A hundred and fifty
    // decibels of deliberate aliasing, which is the point of the control.
    constexpr double rate = 48000.0;

    const auto imageAt = [rate] (int rateIndex)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.rateIndex = rateIndex;
        engine.setParameters (p);

        const auto out = renderNote (engine, noteForTone (Tone::col1477), 0.5, rate);

        return amplitudeAt (out, static_cast<std::size_t> (0.1 * rate),
                            static_cast<std::size_t> (0.4 * rate), 2523.0, rate);
    };

    const double quiet = imageAt (0);              // off
    const double loud = imageAt (7);               // 4 kHz

    CHECK (loud > 10.0 * quiet);
    CHECK (loud > 0.05);
}

TEZLA_TEST (the_band_limits_where_it_says_it_does)
{
    // Fourth-order edges at the published corners. Measured at 48 kHz, on the
    // toll band (300-3400, ITU-T G.712), by playing the DTMF constituent
    // frequencies through it and comparing with the band off:
    //
    //       941 Hz    -0.001 dB     well inside
    //      2600 Hz    -0.456 dB     approaching the 3400 Hz corner
    //
    // and the same 2600 Hz probe through the wideband setting reads
    // -0.001 dB, which is the difference between the two bands in one number.
    // A fourth-order Butterworth at 2600/3400 of its corner should be
    // 1/sqrt(1 + 0.765^8) down, which is -0.48 dB: the filter is the filter it
    // says it is.
    constexpr double rate = 48000.0;

    const auto levelThrough = [rate] (BandMode band, Tone tone, double hz)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.band = band;
        engine.setParameters (p);

        const auto out = renderNote (engine, noteForTone (tone), 0.5, rate);

        return amplitudeAt (out, static_cast<std::size_t> (0.15 * rate),
                            static_cast<std::size_t> (0.45 * rate), hz, rate);
    };

    const double reference = levelThrough (BandMode::off, Tone::row941, 941.0);
    const double tollAt941 = levelThrough (BandMode::toll, Tone::row941, 941.0);

    CHECK_NEAR (20.0 * std::log10 (tollAt941 / reference), 0.0, 0.5);

    // The howler's 2600 Hz component is inside the toll band; its 1400 Hz one
    // certainly is. A band that had its corners wrong would move these.
    const double topReference = levelThrough (BandMode::off, Tone::singleFrequency, 2600.0);
    const double tollAtTop = levelThrough (BandMode::toll, Tone::singleFrequency, 2600.0);

    const double tollTopDb = 20.0 * std::log10 (tollAtTop / topReference);
    CHECK (tollTopDb > -3.0);
    CHECK (tollTopDb < 0.0);

    // And the wideband setting really is wider: measured on the same probe,
    // both pass it, but a probe above the toll corner separates them.
    const double wideAtTop = levelThrough (BandMode::wideband, Tone::singleFrequency, 2600.0);
    CHECK (20.0 * std::log10 (wideAtTop / topReference) > tollTopDb);
}

TEZLA_TEST (line_noise_is_exactly_off_at_zero_and_audible_at_one)
{
    // Section 7: intentional noise must be defeatable, and "defeatable" means
    // exactly zero rather than very quiet. Measured with nothing played:
    // 48000 samples of exact zero at noise 0, and an RMS of -35.09 dBFS at
    // noise 1.
    constexpr double rate = 48000.0;

    const auto rmsOfSilence = [rate] (double noise)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.noise = noise;
        engine.setParameters (p);

        std::vector<double> out (static_cast<std::size_t> (rate), 0.0);
        engine.process (out.data(), nullptr, static_cast<int> (out.size()));

        double sum = 0.0;

        for (double v : out)
            sum += v * v;

        return std::sqrt (sum / static_cast<double> (out.size()));
    };

    CHECK (dsp::isExactlyZero (rmsOfSilence (0.0)));

    const double loud = rmsOfSilence (1.0);
    CHECK (loud > 0.005);
    CHECK (loud < 0.1);

    // Squared taper: half the control is a quarter of the amplitude --
    // measured -12.04 dB rather than -6 -- which is what puts the useful
    // settings in the bottom half of the travel.
    const double half = rmsOfSilence (0.5);
    CHECK_NEAR (20.0 * std::log10 (half / loud), -12.0, 1.0);
}

// ---------------------------------------------------------------------------
// Section 7 and the Sonitus lesson
// ---------------------------------------------------------------------------

TEZLA_TEST (silence_in_is_exactly_silence_out_with_the_line_working_hard)
{
    // Nothing played, everything switched on: band, 8 kHz, mu-law, seven bits.
    // A codec that idled at one LSB -- which A-law genuinely does -- would put
    // a DC offset on every instance in the project.
    constexpr double rate = 48000.0;

    for (auto codec : { dsp::CompandingLaw::off, dsp::CompandingLaw::muLaw,
                        dsp::CompandingLaw::aLaw, dsp::CompandingLaw::linear })
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.band = BandMode::toll;
        p.rateIndex = kDefaultRateIndex;
        p.codec = codec;
        p.bits = 7;
        engine.setParameters (p);

        std::vector<double> left (4096, 0.0);
        std::vector<double> right (4096, 0.0);

        for (int block = 0; block < 24; ++block)
        {
            engine.process (left.data(), right.data(), static_cast<int> (left.size()));

            for (std::size_t n = 0; n < left.size(); ++n)
            {
                CHECK (dsp::isExactlyZero (left[n]));
                CHECK (dsp::isExactlyZero (right[n]));
            }
        }

        CHECK (engine.getActiveVoiceCount() == 0);
    }
}

TEZLA_TEST (voices_measurably_die_and_the_death_tracks_the_release)
{
    // The Sonitus lesson (#118), which cost the user a CPU meter pinned at
    // 100% seconds after every key was up: assert **activity**, not silence.
    // A voice whose envelope never reaches zero is inaudible and still costs
    // everything, and every silence-based test in the suite passed while it
    // did.
    //
    // Measured at 48 kHz with all sixteen voices sounding: with a 20 ms
    // release the count reaches zero 0.0200 s after the last note off; with
    // 200 ms it takes 0.2000 s. **A ratio of exactly 10.00** -- ten times the
    // setting, ten times the wait -- so the control is the control and nothing
    // is crawling.
    constexpr double rate = 48000.0;

    const auto samplesUntilSilent = [rate] (double release)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.releaseSeconds = release;
        engine.setParameters (p);

        for (int i = 0; i < CrossbarEngine::kMaxVoices; ++i)
            engine.noteOn (noteForTone (Tone::digit1) + i, 1.0);

        std::vector<double> block (32, 0.0);

        engine.process (block.data(), nullptr, 32);
        CHECK (engine.getActiveVoiceCount() == CrossbarEngine::kMaxVoices);

        for (int i = 0; i < CrossbarEngine::kMaxVoices; ++i)
            engine.noteOff (noteForTone (Tone::digit1) + i);

        int elapsed = 0;

        while (engine.getActiveVoiceCount() > 0 && elapsed < static_cast<int> (10.0 * rate))
        {
            engine.process (block.data(), nullptr, 32);
            elapsed += 32;
        }

        return elapsed;
    };

    const double quick = samplesUntilSilent (0.020) / rate;
    const double slow = samplesUntilSilent (0.200) / rate;

    CHECK (quick < 0.05);
    CHECK (slow > 0.15);
    CHECK (slow < 0.30);

    // The ratio is the assertion with teeth: a release that crawled -- aimed
    // from the current level every control chunk, as Sonitus's did -- would
    // hold this near 1 while both numbers grew.
    CHECK (slow / quick > 5.0);
}

TEZLA_TEST (the_engine_does_not_depend_on_the_block_size)
{
    // Section 7: the sample loop is cut at control boundaries, never at the
    // callback's. Here there are none -- cadences, the dialler, the smoothers
    // and the line all count samples -- so 64 and 512 have to agree **to the
    // bit**, not merely closely.
    //
    // The parameters are pushed once per block in both runs, as a host does,
    // which is the part that actually catches a setter that restarts
    // something it should not: `Line::configure` refusing a no-op is what
    // stops the crossfade being restarted 750 times a second and never
    // finishing.
    constexpr double rate = 48000.0;

    const auto render = [rate] (int blockSize)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.band = BandMode::toll;
        p.rateIndex = kDefaultRateIndex;
        p.codec = dsp::CompandingLaw::muLaw;
        p.noise = 0.4;
        engine.setDialString ("555 0199");
        p.dialDigitSeconds = 0.08;
        p.dialGapSeconds = 0.05;

        const auto total = static_cast<std::size_t> (2.0 * rate);
        std::vector<double> out (total, 0.0);

        engine.setParameters (p);
        engine.noteOn (noteForTone (Tone::ringback), 1.0);
        engine.noteOn (noteForTone (Tone::dialNumber), 1.0);

        for (std::size_t n = 0; n < total; n += static_cast<std::size_t> (blockSize))
        {
            engine.setParameters (p);
            const auto count = std::min (static_cast<std::size_t> (blockSize), total - n);
            engine.process (out.data() + n, nullptr, static_cast<int> (count));
        }

        return out;
    };

    const auto small = render (64);
    const auto large = render (512);

    CHECK (small.size() == large.size());

    double worst = 0.0;

    for (std::size_t n = 0; n < small.size(); ++n)
        worst = std::max (worst, std::abs (small[n] - large[n]));

    CHECK (dsp::isExactlyZero (worst));
}

// ---------------------------------------------------------------------------
// The dialler
// ---------------------------------------------------------------------------

TEZLA_TEST (the_dialler_plays_a_written_number_at_the_stated_timing)
{
    // "555 0199" is seven keys and a space. The space is skipped rather than
    // dialled, so the sequence is seven digits at 80 ms with 50 ms between
    // them -- 7 * 0.08 + 6 * 0.05 = 0.86 s of dialling.
    //
    // Measured: seven bursts, each 78.9 ms of tone -- the 80 ms asked for,
    // less the 1 ms the gate's fade takes off each end -- spaced 130.0 ms
    // apart to within 0.4 ms every time. The absolute onsets are all shifted
    // by half the analysis window and are not the claim; the spacing and the
    // length are.
    constexpr double rate = 48000.0;

    CrossbarEngine engine;
    engine.prepare (rate);

    auto p = neutralParameters();
    p.dialDigitSeconds = 0.08;
    p.dialGapSeconds = 0.05;
    p.releaseSeconds = 0.005;
    engine.setDialString ("555 0199");
    engine.setParameters (p);

    CHECK (engine.getDialler().getDigitCount() == 7);

    const auto out = renderNote (engine, noteForTone (Tone::dialNumber), 1.4, rate);
    const auto crossings = crossingsOf (envelopeOf (out, rate), rate);

    // Seven bursts: an up and a down for each, so fourteen crossings.
    CHECK (crossings.size() == 14u);

    if (crossings.size() == 14u)
    {
        for (int digit = 0; digit < 7; ++digit)
        {
            const auto rising = static_cast<std::size_t> (2 * digit);

            CHECK_NEAR (crossings[rising + 1] - crossings[rising], 0.079, 0.002);

            if (digit > 0)
                CHECK_NEAR (crossings[rising] - crossings[rising - 2], 0.130, 0.002);
        }
    }
}

TEZLA_TEST (pulse_dialling_takes_as_long_as_the_digit_is_worth)
{
    // A rotary dial breaks the loop once per unit, ten times a second, so a
    // digit's *duration is its value*. That is not a detail: it is why 999
    // was quicker to dial than 000 would have been, and why short emergency
    // numbers were chosen at all.
    //
    // Timed on the sequencer itself rather than on the audio, because the
    // audio cannot say it cleanly: a pulsed digit is a *train of clicks*, so
    // an envelope-crossing measurement finds twenty bursts for a '0' and two
    // for a '1' rather than one burst of each. Counting samples to the
    // end-of-digit event is exact and is the claim.
    //
    // Measured at 48 kHz: '1' ends after 4800 samples and '0' after 48000 --
    // 0.100 s and 1.000 s, a ratio of exactly ten. In tone mode both end after
    // 3840, the 80 ms asked for, which is the contrast that makes the mode
    // switch mean something.
    constexpr double rate = 48000.0;

    const auto samplesForDigit = [rate] (const char* number, bool pulse)
    {
        Dialler dialler;
        dialler.prepare (rate);
        dialler.setPulseMode (pulse);
        dialler.setTiming (0.08, 0.05);
        dialler.setDigits (number);
        dialler.start();

        int elapsed = 0;

        while (dialler.isRunning() && elapsed < static_cast<int> (5.0 * rate))
        {
            ++elapsed;

            if (dialler.tick() == Dialler::Event::endDigit)
                return elapsed;
        }

        return -1;
    };

    CHECK (samplesForDigit ("1", true) == 4800);
    CHECK (samplesForDigit ("0", true) == 48000);
    CHECK (samplesForDigit ("9", true) == 43200);

    CHECK (samplesForDigit ("1", false) == 3840);
    CHECK (samplesForDigit ("0", false) == 3840);

    // And what it sounds like: a pulsed digit is loop breaks, so the audio has
    // one burst per break and there are two per pulse -- one for the break and
    // one for the make, as a real dial makes. A '0' therefore rattles twenty
    // times where a tone-dialled '0' is one steady pair of sines.
    const auto burstCount = [rate] (bool pulse)
    {
        CrossbarEngine engine;
        engine.prepare (rate);

        auto p = neutralParameters();
        p.pulseDial = pulse;
        p.dialDigitSeconds = 0.08;
        p.dialGapSeconds = 0.05;
        p.releaseSeconds = 0.002;
        engine.setDialString ("0");
        engine.setParameters (p);

        const auto out = renderNote (engine, noteForTone (Tone::dialNumber), 1.5, rate);

        // The clicks are 1.5 ms transients, so this measurement wants a fast
        // follower where the cadence tests wanted a slow one -- 1 ms, and the
        // count is of rising crossings.
        std::vector<double> envelope (out.size(), 0.0);
        const double coefficient = std::exp (-1.0 / (0.001 * rate));
        double state = 0.0;

        for (std::size_t n = 0; n < out.size(); ++n)
        {
            state = coefficient * state + (1.0 - coefficient) * std::abs (out[n]);
            envelope[n] = state;
        }

        const double threshold = 0.2 * *std::max_element (envelope.begin(), envelope.end());
        int bursts = 0;
        bool above = false;

        for (double v : envelope)
        {
            if (v > threshold && ! above)
                ++bursts;

            above = v > threshold;
        }

        return bursts;
    };

    CHECK (burstCount (true) == 20);
    CHECK (burstCount (false) == 1);
}

TEZLA_TEST (the_dialler_skips_what_a_keypad_cannot_send)
{
    // A phone number as people write it. Punctuation is skipped rather than
    // dialled -- a dialler that tried to play a dash would either be silent
    // for that slot or crash on the lookup.
    Dialler dialler;
    dialler.prepare (48000.0);

    dialler.setDigits ("+1 (555) 010-4477 #2");
    CHECK (dialler.getDigitCount() == 13);

    const char* expected = "15550104477#2";

    for (int i = 0; i < dialler.getDigitCount(); ++i)
        CHECK (dialler.getDigit (i) == expected[i]);

    // '#' is a real key and survives; '+', spaces, brackets and the dash do
    // not. The count above is the check, and this is the same statement made
    // where it can be read.
    dialler.setDigits ("#*ABCD");
    CHECK (dialler.getDigitCount() == 6);

    dialler.setDigits ("-- () + .");
    CHECK (dialler.getDigitCount() == 0);

    // An empty number does not start, which is what stops the dial key
    // hanging a voice open forever.
    dialler.start();
    CHECK (! dialler.isRunning());
}
