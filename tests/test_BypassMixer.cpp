// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <cmath>
#include <vector>

#include <tezla/dsp/BypassMixer.hpp>

using namespace tezla::dsp;

namespace {

/// A signal with no repeats, so an off-by-one delay cannot hide.
std::vector<double> ramp (std::size_t length, double start = 1.0)
{
    std::vector<double> signal (length);
    for (std::size_t i = 0; i < length; ++i)
        signal[i] = start + static_cast<double> (i);
    return signal;
}

/// Runs a whole signal through, in blocks whose size does not divide it evenly.
std::vector<double> run (BypassMixer& mixer, const std::vector<double>& dry,
                         const std::vector<double>& processed, int blockSize = 137)
{
    std::vector<double> output = processed;

    for (std::size_t offset = 0; offset < output.size(); offset += static_cast<std::size_t> (blockSize))
    {
        const int numSamples = static_cast<int> (std::min (static_cast<std::size_t> (blockSize),
                                                           output.size() - offset));

        double* processedPointer = output.data() + offset;
        const double* dryPointer = dry.data() + offset;

        mixer.process (&processedPointer, &dryPointer, 1, numSamples);
    }

    return output;
}

} // namespace

TEZLA_TEST (bypass_delays_the_dry_path_by_exactly_the_reported_latency)
{
    // The bug this class exists for. The hand-rolled version in the JUCE layer
    // delayed by zero, so engaging bypass jumped the signal by the whole
    // reported latency and the crossfade swept a comb filter across it.
    //
    // A ramp is used rather than a sine so that being wrong by one sample is as
    // visible as being wrong by seventy.
    for (const int latency : { 0, 1, 47, 63, 71, 512 })
    {
        BypassMixer mixer;
        mixer.prepare (48000.0, latency, 1);
        mixer.reset (true);              // fully bypassed from the first sample

        const auto dry = ramp (4096);
        const std::vector<double> processed (dry.size(), -999.0);   // must not appear

        const auto output = run (mixer, dry, processed);

        CHECK (mixer.getLatencySamples() == latency);

        for (std::size_t i = static_cast<std::size_t> (latency); i < dry.size(); ++i)
            CHECK (output[i] == dry[i - static_cast<std::size_t> (latency)]);
    }
}

TEZLA_TEST (bypass_off_leaves_the_processed_signal_bit_exact)
{
    // The other end has to be exact too. A mixer that leaves a thousandth of the
    // dry path in the sound is colouring every plugin that uses it.
    BypassMixer mixer;
    mixer.prepare (48000.0, 71, 1);
    mixer.reset (false);

    const auto dry = ramp (4096);
    std::vector<double> processed (dry.size());
    for (std::size_t i = 0; i < processed.size(); ++i)
        processed[i] = std::sin (0.017 * static_cast<double> (i));

    const auto output = run (mixer, dry, processed);

    for (std::size_t i = 0; i < processed.size(); ++i)
        CHECK (output[i] == processed[i]);
}

TEZLA_TEST (bypass_crossfade_takes_the_stated_time_and_does_not_click)
{
    // Switching has to be a fade, not a jump, and the fade has to be the length
    // it claims at every sample rate.
    for (const double sampleRate : { 44100.0, 48000.0, 192000.0 })
    {
        BypassMixer mixer;
        mixer.prepare (sampleRate, 0, 1);
        mixer.reset (false);

        // Dry is a constant 1, processed a constant 0, so the output *is* the
        // crossfade and can be read directly.
        const std::size_t length = static_cast<std::size_t> (sampleRate * 0.1);
        const std::vector<double> dry (length, 1.0);
        const std::vector<double> processed (length, 0.0);

        mixer.setBypassed (true);
        const auto output = run (mixer, dry, processed);

        const auto expected = static_cast<std::size_t> (BypassMixer::kFadeSeconds * sampleRate);

        // Complete when it should be, and not before.
        CHECK (output[expected + 4] == 1.0);
        CHECK (output[expected / 2] > 0.3);
        CHECK (output[expected / 2] < 0.7);

        // Monotone, and no step larger than one fade increment: that is what
        // "does not click" means as a number rather than as a hope.
        const double largestStep = 2.0 / (BypassMixer::kFadeSeconds * sampleRate);
        for (std::size_t i = 1; i < expected + 8; ++i)
        {
            CHECK (output[i] >= output[i - 1]);
            CHECK (output[i] - output[i - 1] <= largestStep);
        }
    }
}

TEZLA_TEST (bypass_fades_both_channels_at_the_same_rate)
{
    // The fade is advanced once per sample, not once per channel. Advancing it
    // per channel makes a stereo plugin fade at twice the rate of a mono one,
    // and pulls the image apart while it does.
    BypassMixer mixer;
    mixer.prepare (48000.0, 0, 2);
    mixer.reset (false);
    mixer.setBypassed (true);

    constexpr int length = 2048;
    std::vector<double> leftDry (length, 1.0), rightDry (length, 1.0);
    std::vector<double> leftOut (length, 0.0), rightOut (length, 0.0);

    double* processed[2] { leftOut.data(), rightOut.data() };
    const double* dry[2] { leftDry.data(), rightDry.data() };

    mixer.process (processed, dry, 2, length);

    for (int i = 0; i < length; ++i)
        CHECK (leftOut[static_cast<std::size_t> (i)] == rightOut[static_cast<std::size_t> (i)]);
}

TEZLA_TEST (bypass_survives_a_block_longer_than_its_latency)
{
    // The original bug was hidden by block size: the ring buffer was only as
    // long as the latency, so any block bigger than that overwrote samples it
    // still owed the output. Hosts choose the block size, not us.
    BypassMixer mixer;
    mixer.prepare (48000.0, 4, 1);
    mixer.reset (true);

    const auto dry = ramp (1024);
    const std::vector<double> processed (dry.size(), -999.0);

    const auto output = run (mixer, dry, processed, 512);

    for (std::size_t i = 4; i < dry.size(); ++i)
        CHECK (output[i] == dry[i - 4]);
}

TEZLA_TEST (bypass_reset_clears_the_delay_and_the_fade)
{
    // Two runs from a fresh reset must match. A delay line still holding the
    // last take, or a crossfade caught mid-move, both make a plugin sound
    // slightly different depending on where the transport was.
    const auto dry = ramp (2048);
    std::vector<double> processed (dry.size());
    for (std::size_t i = 0; i < processed.size(); ++i)
        processed[i] = std::cos (0.03 * static_cast<double> (i));

    BypassMixer mixer;
    mixer.prepare (48000.0, 47, 1);

    mixer.reset (false);
    mixer.setBypassed (true);
    const auto first = run (mixer, dry, processed);

    mixer.reset (false);
    mixer.setBypassed (true);
    const auto second = run (mixer, dry, processed);

    for (std::size_t i = 0; i < first.size(); ++i)
        CHECK (first[i] == second[i]);
}

TEZLA_TEST (bypass_setting_the_latency_it_already_has_disturbs_nothing)
{
    // The shipped bug, and the reason the guard is in setLatency rather than in
    // its callers. Changing the length makes the ring's contents meaningless, so
    // a real change clears it -- but the signature says "safe from the audio
    // thread", which invites a caller to push the current latency once per
    // block. Capstone did, and every callback wiped the dry path: bypassed at
    // 64-sample blocks with 53 samples of latency, 83% of its output samples
    // were exactly zero and the rest jumped 0.4985 between neighbours where the
    // signal itself steps 0.0196.
    constexpr int latency = 53;
    constexpr std::size_t length = 2048;

    const auto dry = ramp (length);
    const std::vector<double> processed (length, -1.0);

    // Two mixers, identical except that one is told its latency every block.
    BypassMixer quiet;
    quiet.prepare (48000.0, 256, 1);
    quiet.setLatency (latency);
    quiet.setBypassed (true);
    quiet.reset (true);

    BypassMixer pushed;
    pushed.prepare (48000.0, 256, 1);
    pushed.setLatency (latency);
    pushed.setBypassed (true);
    pushed.reset (true);

    std::vector<double> quietOut = processed;
    std::vector<double> pushedOut = processed;

    constexpr int blockSize = 64;

    for (std::size_t offset = 0; offset < length; offset += blockSize)
    {
        const int numSamples = static_cast<int> (std::min (static_cast<std::size_t> (blockSize),
                                                           length - offset));
        const double* dryPointer = dry.data() + offset;

        double* a = quietOut.data() + offset;
        quiet.process (&a, &dryPointer, 1, numSamples);

        // The only difference: the same value, pushed again before every block.
        pushed.setLatency (latency);

        double* b = pushedOut.data() + offset;
        pushed.process (&b, &dryPointer, 1, numSamples);
    }

    bool identical = true;

    for (std::size_t i = 0; i < length; ++i)
        if (quietOut[i] != pushedOut[i])
            identical = false;

    CHECK (identical);

    // And the shared answer is right in the first place: past the fill, the
    // bypassed output is the input delayed by exactly the latency.
    bool delayedExactly = true;

    for (std::size_t i = static_cast<std::size_t> (latency); i < length; ++i)
        if (quietOut[i] != dry[i - static_cast<std::size_t> (latency)])
            delayedExactly = false;

    CHECK (delayedExactly);
}

TEZLA_TEST (bypass_changing_the_latency_still_takes_effect)
{
    // The other half of the guard. Refusing a no-op must not turn into refusing
    // a real change -- an early-out that compared the wrong thing would pass the
    // test above and silently pin the delay at whatever it was first given.
    const auto dry = ramp (1024);
    const std::vector<double> processed (1024, -1.0);

    BypassMixer mixer;
    mixer.prepare (48000.0, 256, 1);
    mixer.setBypassed (true);

    for (const int latency : { 0, 7, 200, 41 })
    {
        mixer.setLatency (latency);
        CHECK (mixer.getLatency() == latency);

        mixer.reset (true);
        const auto output = run (mixer, dry, processed, 137);

        bool delayedExactly = true;

        for (std::size_t i = static_cast<std::size_t> (latency); i < dry.size(); ++i)
            if (output[i] != dry[i - static_cast<std::size_t> (latency)])
                delayedExactly = false;

        CHECK (delayedExactly);
    }
}
