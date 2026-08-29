// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/SpectrumAnalyser.hpp>

using namespace tezla::dsp;

namespace {

/// Feeds a steady tone through the capture and analyses it, letting the
/// ballistics settle so the reading is the tone and not the attack.
SpectrumAnalyser analyseTone (double frequency, double amplitude, double sampleRate,
                              int fftOrder = 11, int numBins = 128)
{
    SpectrumCapture capture;
    capture.prepare (1 << (fftOrder + 1));

    SpectrumAnalyser analyser;
    analyser.prepare (sampleRate, fftOrder, numBins);

    const int blockSize = 256;
    std::vector<double> block (static_cast<std::size_t> (blockSize));
    std::size_t sampleIndex = 0;

    for (int frame = 0; frame < 40; ++frame)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            block[static_cast<std::size_t> (i)] =
                amplitude * std::sin (2.0 * std::numbers::pi * frequency
                                      * static_cast<double> (sampleIndex) / sampleRate);
            ++sampleIndex;
        }

        capture.push (block.data(), blockSize);
        analyser.update (capture);
    }

    return analyser;
}

/// The loudest display bin, and its level.
std::pair<std::size_t, float> loudestBin (const SpectrumAnalyser& analyser)
{
    const auto& bins = analyser.getMagnitudesDb();
    std::size_t best = 0;

    for (std::size_t i = 1; i < bins.size(); ++i)
        if (bins[i] > bins[best])
            best = i;

    return { best, bins[best] };
}

} // namespace

TEZLA_TEST (spectrum_capture_returns_the_most_recent_samples_in_order)
{
    // A ring buffer that hands back the window reversed, rotated or stale looks
    // like a plausible spectrum and is not one.
    SpectrumCapture capture;
    capture.prepare (256);

    std::vector<double> input (1000);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<double> (i);

    // Pushed in blocks that do not divide the ring evenly.
    for (std::size_t offset = 0; offset < input.size(); offset += 37)
    {
        const auto count = std::min<std::size_t> (37, input.size() - offset);
        capture.push (input.data() + offset, static_cast<int> (count));
    }

    std::vector<double> window (128);
    CHECK (capture.readLatest (window.data(), 128));

    for (std::size_t i = 0; i < window.size(); ++i)
        CHECK (window[i] == input[input.size() - 128 + i]);
}

TEZLA_TEST (spectrum_capture_refuses_a_window_longer_than_it_holds)
{
    SpectrumCapture capture;
    capture.prepare (128);

    std::vector<double> window (4096);
    CHECK (! capture.readLatest (window.data(), 4096));
}

TEZLA_TEST (spectrum_reads_a_full_scale_sine_as_zero_dbfs)
{
    // The reading that everything else is judged against, and the one most
    // easily 6 dB out: a Hann window has a coherent gain of exactly 0.5, so the
    // scaling is 4/N and not the 2/N a rectangular window would want. Being
    // wrong there looks entirely plausible on screen.
    //
    // Deliberately not bin-exact frequencies -- a display has no say in where
    // the music puts its partials.
    for (const double frequency : { 100.0, 440.0, 997.0, 3163.0, 9871.0 })
    {
        const auto analyser = analyseTone (frequency, 1.0, 48000.0);
        const auto [bin, level] = loudestBin (analyser);

        // Measured across a fine sweep of 900 to 1100 Hz, the worst reading is
        // 0.07 dB low. Before the display bins were overlapped it was 2.8 dB,
        // and the error moved with the note.
        CHECK_NEAR (level, 0.0f, 0.25f);

        // And where the display can resolve it, it has to be in the right place.
        //
        // "Where it can" is a condition, not a hunch: a display bin at frequency
        // f is f * (ratio - 1) Hz wide, and the transform resolves fs / N. Below
        // the crossing point -- around 400 Hz here -- several display bins share
        // one transform bin, the peak reads as a plateau, and which bin of that
        // plateau happens to be highest is arbitrary. The level is still right;
        // the position is not meaningful, so it is not asserted.
        const double centre = analyser.getBinFrequency (bin);
        const double binRatio = analyser.getBinFrequency (1) / analyser.getBinFrequency (0);

        const double displayBinHz = frequency * (binRatio - 1.0);
        const double transformBinHz = 48000.0 / static_cast<double> (analyser.getFftSize());

        if (displayBinHz > transformBinHz)
            CHECK (std::abs (std::log (centre / frequency)) < 1.5 * std::log (binRatio));
    }
}

TEZLA_TEST (spectrum_is_smeared_at_the_bottom_and_the_level_survives_it)
{
    // The bottom two octaves are genuinely smeared, and that is arithmetic
    // rather than a bug: a log display bin at 40 Hz is about 2 Hz wide while a
    // 2048-point transform at 48 kHz resolves 23, so several display bins
    // necessarily share one transform bin and a tone reads as a short plateau
    // rather than a spike.
    //
    // What must survive it is the *level*. A display that reads bass 2 dB low
    // is worse than one that draws it wide.
    const auto coarse = analyseTone (60.0, 1.0, 48000.0, 11);
    const auto fine   = analyseTone (60.0, 1.0, 48000.0, 13);

    CHECK_NEAR (loudestBin (coarse).second, 0.0f, 0.5f);
    CHECK_NEAR (loudestBin (fine).second,   0.0f, 0.5f);

    // And the plateau is real: neighbouring bins around the peak read close to
    // it, because they are looking at the same transform bins.
    const auto peak = loudestBin (coarse);
    CHECK (coarse.getMagnitudesDb()[peak.first > 0 ? peak.first - 1 : 0] > peak.second - 6.0f);
}

TEZLA_TEST (spectrum_tracks_level_down_the_scale)
{
    for (const double amplitudeDb : { 0.0, -6.0, -20.0, -40.0, -60.0 })
    {
        const auto analyser = analyseTone (997.0, dbToGain (amplitudeDb), 48000.0);
        const auto [bin, level] = loudestBin (analyser);

        (void) bin;
        CHECK_NEAR (level, static_cast<float> (amplitudeDb), 0.8f);
    }
}

TEZLA_TEST (spectrum_puts_a_tone_in_the_same_place_at_every_sample_rate)
{
    // The display is a picture of frequency, not of bin index. A partial at
    // 3 kHz has to sit at 3 kHz whether the session is at 44.1 or 192.
    double reference = 0.0;
    bool first = true;

    for (const double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto analyser = analyseTone (3163.0, 0.7, sampleRate);
        const auto [bin, level] = loudestBin (analyser);

        CHECK_NEAR (level, -3.1f, 0.8f);   // 0.7 amplitude

        const double centre = analyser.getBinFrequency (bin);

        if (first)
        {
            reference = centre;
            first = false;
        }
        else
        {
            // Within a fifth of an octave of each other.
            CHECK (centre / reference > 0.87);
            CHECK (centre / reference < 1.15);
        }
    }
}

TEZLA_TEST (spectrum_bins_are_log_spaced_and_ordered)
{
    // Log spacing is what makes a spectrum read like music: an octave takes the
    // same width wherever it sits. Linear spacing crams everything below 2 kHz
    // into the left tenth of the display, which is where all the music is.
    SpectrumAnalyser analyser;
    analyser.prepare (48000.0, 11, 96);

    const auto count = analyser.getNumBins();
    double previousRatio = 0.0;

    for (std::size_t i = 1; i < count; ++i)
    {
        const double lower = analyser.getBinFrequency (i - 1);
        const double upper = analyser.getBinFrequency (i);

        CHECK (upper > lower);

        const double ratio = upper / lower;
        if (i > 1)
            CHECK_NEAR (ratio, previousRatio, 1.0e-6);   // constant ratio == log spacing
        previousRatio = ratio;
    }

    CHECK (analyser.getBinFrequency (0) < 40.0);
    CHECK (analyser.getBinFrequency (count - 1) > 15000.0);
}

TEZLA_TEST (spectrum_top_bin_never_exceeds_nyquist)
{
    // At 44.1 kHz a 20 kHz display top is above the usable band. Asking the FFT
    // for a bin that does not exist is how an analyser reads garbage at the
    // right-hand edge, or crashes.
    for (const double sampleRate : { 44100.0, 48000.0, 192000.0 })
    {
        SpectrumAnalyser analyser;
        analyser.prepare (sampleRate, 11, 128);

        CHECK (analyser.getBinFrequency (analyser.getNumBins() - 1) < sampleRate * 0.5);
    }
}

TEZLA_TEST (spectrum_of_silence_is_the_floor)
{
    SpectrumCapture capture;
    capture.prepare (4096);

    SpectrumAnalyser analyser;
    analyser.prepare (48000.0, 11, 128);

    const std::vector<double> silence (512, 0.0);
    for (int frame = 0; frame < 200; ++frame)
    {
        capture.push (silence.data(), 512);
        analyser.update (capture);
    }

    for (const float bin : analyser.getMagnitudesDb())
        CHECK (bin <= SpectrumAnalyser::kFloorDb + 0.01f);
}

TEZLA_TEST (spectrum_update_without_a_capture_does_nothing)
{
    // An analyser prepared for a bigger window than the capture holds must say
    // so rather than read past the end of it.
    SpectrumCapture capture;
    capture.prepare (64);

    SpectrumAnalyser analyser;
    analyser.prepare (48000.0, 11, 128);

    CHECK (! analyser.update (capture));
}

TEZLA_TEST (spectrum_falls_gradually_and_rises_at_once)
{
    // Ballistics: a display that eases into a transient hides the transient,
    // and one that drops instantly flickers too much to read.
    SpectrumCapture capture;
    capture.prepare (4096);

    SpectrumAnalyser analyser;
    analyser.prepare (48000.0, 11, 128);
    analyser.setBallistics (2.0f, 0.5f);

    const int blockSize = 512;
    std::vector<double> block (static_cast<std::size_t> (blockSize));
    std::size_t sampleIndex = 0;

    const auto pushTone = [&] (double amplitude)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            block[static_cast<std::size_t> (i)] =
                amplitude * std::sin (2.0 * std::numbers::pi * 997.0
                                      * static_cast<double> (sampleIndex) / 48000.0);
            ++sampleIndex;
        }
        capture.push (block.data(), blockSize);
        analyser.update (capture);
    };

    for (int i = 0; i < 40; ++i)
        pushTone (1.0);

    const auto loud = loudestBin (analyser);

    // Now silence. One frame later it must have fallen by the stated step and
    // no more -- not dropped straight to the floor.
    const std::vector<double> silence (static_cast<std::size_t> (blockSize), 0.0);
    for (int i = 0; i < 8; ++i)
    {
        capture.push (silence.data(), blockSize);
        analyser.update (capture);
    }

    const float afterEight = analyser.getMagnitudesDb()[loud.first];
    CHECK (afterEight < loud.second);
    CHECK (afterEight > loud.second - 8.0f * 2.0f - 0.01f);

    // The peak hold falls more slowly, so it is still above the trace.
    CHECK (analyser.getPeaksDb()[loud.first] >= afterEight);
}

// ---------------------------------------------------------------------------
// The bass transform (B1): two resolutions, each where it is good
// ---------------------------------------------------------------------------

namespace {

/// Two steady tones through a capture large enough for the long window,
/// with or without the bass transform configured.
SpectrumAnalyser analyseTwoTones (double hzA, double hzB, double rate, bool bass)
{
    SpectrumCapture capture;
    capture.prepare (1 << 15);

    SpectrumAnalyser analyser;
    analyser.prepare (rate, 11, 128);

    if (bass)
        analyser.setBassTransform (14, 500.0);

    const int blockSize = 1024;
    std::vector<double> block (static_cast<std::size_t> (blockSize));
    std::size_t n = 0;

    for (int frame = 0; frame < 48; ++frame)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto t = static_cast<double> (n++) / rate;
            block[static_cast<std::size_t> (i)] =
                0.5 * std::sin (2.0 * std::numbers::pi * hzA * t)
              + 0.5 * std::sin (2.0 * std::numbers::pi * hzB * t);
        }

        capture.push (block.data(), blockSize);
        analyser.update (capture);
    }

    return analyser;
}

/// How deeply the display separates two tones: the lower of the two local
/// peaks minus the deepest bin between them. A smear reads ~0.
float toneSeparation (const SpectrumAnalyser& analyser, double hzA, double hzB)
{
    const auto& bins = analyser.getMagnitudesDb();

    const auto binNear = [&] (double hz)
    {
        std::size_t best = 0;
        double bestError = 1.0e9;

        for (std::size_t i = 0; i < bins.size(); ++i)
        {
            const double error = std::abs (std::log (analyser.getBinFrequency (i) / hz));
            if (error < bestError)
            {
                bestError = error;
                best = i;
            }
        }

        return best;
    };

    // The peak may sit a bin or two off the nominal spot; search locally.
    const auto peakAround = [&] (std::size_t centre)
    {
        std::size_t best = centre;

        for (std::size_t i = centre >= 2 ? centre - 2 : 0;
             i <= centre + 2 && i < bins.size(); ++i)
            if (bins[i] > bins[best])
                best = i;

        return best;
    };

    const std::size_t peakA = peakAround (binNear (hzA));
    const std::size_t peakB = peakAround (binNear (hzB));

    if (peakA >= peakB)
        return 0.0f;

    float valley = 1.0e9f;

    for (std::size_t i = peakA + 1; i < peakB; ++i)
        valley = std::min (valley, bins[i]);

    return std::min (bins[peakA], bins[peakB]) - valley;
}

} // namespace

TEZLA_TEST (the_bass_transform_resolves_what_the_short_one_smears)
{
    // 45 and 60 Hz -- a bass line moving a fourth at the bottom of the sub
    // octave, the interval that carries most of this music. The short
    // window's 23.4 Hz bins smear the whole region into one plateau (the
    // probe read a flat -0.18 dB from 31 Hz to 98); the 16384-point bass
    // transform resolves 2.9 Hz and pulls them apart.
    //
    // The limit is the window, not wishfulness: a Hann mainlobe is four
    // transform bins wide, 11.7 Hz here, so a whole tone at A1 (10 Hz of
    // separation) can never valley deeply at this order -- and doubling the
    // order again would mean two thirds of a second of audio, half a bar of
    // jungle. A fourth resolves; state the capability honestly.
    const auto smeared = analyseTwoTones (45.0, 60.0, 48000.0, false);
    const auto resolved = analyseTwoTones (45.0, 60.0, 48000.0, true);

    CHECK (resolved.wasBassUsed());

    const float without = toneSeparation (smeared, 45.0, 60.0);
    const float with = toneSeparation (resolved, 45.0, 60.0);

    // Measured: 0.0 dB of separation without the bass transform; 15.0 dB
    // with it (peaks -6.1, the valley between them -21.2).
    CHECK (without < 1.0f);
    CHECK (with > 10.0f);
}

TEZLA_TEST (the_seam_keeps_its_level)
{
    // A full-scale sine must read 0 dBFS on both sides of the split AND
    // anywhere inside the crossfade octave -- the blend is done as power on
    // two identically calibrated transforms, so any blend of agreeing
    // readings has to agree. A weight error (anything where the two halves
    // do not sum to one) dips exactly here and nowhere else.
    for (const double frequency : { 260.0, 355.0, 430.0, 500.0, 580.0, 700.0, 900.0 })
    {
        SpectrumCapture capture;
        capture.prepare (1 << 15);

        SpectrumAnalyser analyser;
        analyser.prepare (48000.0, 11, 128);
        analyser.setBassTransform (14, 500.0);

        const int blockSize = 1024;
        std::vector<double> block (static_cast<std::size_t> (blockSize));
        std::size_t n = 0;

        for (int frame = 0; frame < 48; ++frame)
        {
            for (int i = 0; i < blockSize; ++i)
                block[static_cast<std::size_t> (i)] =
                    std::sin (2.0 * std::numbers::pi * frequency
                              * static_cast<double> (n++) / 48000.0);

            capture.push (block.data(), blockSize);
            analyser.update (capture);
        }

        CHECK_NEAR (loudestBin (analyser).second, 0.0f, 0.3f);
    }
}

TEZLA_TEST (the_top_is_untouched_by_the_bass_transform)
{
    // Above the crossfade the long transform has no say at all: identical
    // input with and without it must produce bit-identical readings there.
    const auto plain = analyseTwoTones (2000.0, 5000.0, 48000.0, false);
    const auto twoBand = analyseTwoTones (2000.0, 5000.0, 48000.0, true);

    const auto& a = plain.getMagnitudesDb();
    const auto& b = twoBand.getMagnitudesDb();

    bool identical = true;

    for (std::size_t i = 0; i < a.size(); ++i)
        if (plain.getBinFrequency (i) > 500.0 * std::numbers::sqrt2 * 1.05)
            identical = identical && a[i] == b[i];

    CHECK (identical);
}

TEZLA_TEST (a_capture_too_short_for_the_long_window_degrades_visibly)
{
    // The long transform needs 16384 samples; a capture holding 4096 cannot
    // feed it. The short path must carry on -- and wasBassUsed() must say
    // what happened, because a silent fallback is an invisible lie about
    // the resolution on screen.
    SpectrumCapture capture;
    capture.prepare (4096);

    SpectrumAnalyser analyser;
    analyser.prepare (48000.0, 11, 128);
    analyser.setBassTransform (14, 500.0);

    std::vector<double> block (4096, 0.25);
    capture.push (block.data(), 4096);

    CHECK (analyser.update (capture));
    CHECK (! analyser.wasBassUsed());
}
