#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Decibels.hpp>
#include <tezla/dsp/TruePeakDetector.hpp>

using namespace tezla::dsp;

namespace
{
/// The worst under-read the detector shows across every alignment of a sine to
/// the sample grid, in dB.
///
/// A quarter of the sample rate on purpose. The ITU's bound assumes the peak
/// can sit exactly half an oversampled period from its nearest neighbour, and
/// whether that is reachable depends on the frequency: at 0.45 the sample grid
/// repeats every twenty samples and covers phase so densely that a plain sample
/// meter under-reads by only 0.11 dB, nowhere near the 16 dB the formula allows.
/// At 0.25 the grid is four points a cycle and the worst case is exactly
/// attainable, so the measurement and the formula are comparing the same thing.
double worstUnderReadDb (int factor, double normalisedFrequency, int offsets = 512)
{
    constexpr double amplitude = 0.5;
    double worst = 0.0;

    for (int o = 0; o < offsets; ++o)
    {
        TruePeakDetector detector;
        detector.prepare (TruePeakDetector::kMaxFactor);
        detector.setFactor (factor);
        detector.reset();

        const double startPhase = 2.0 * std::numbers::pi * o / offsets;
        double highest = 0.0;

        for (int i = 0; i < 2000; ++i)
        {
            const double v = amplitude
                * std::sin (2.0 * std::numbers::pi * normalisedFrequency * i + startPhase);

            const double reading = detector.process (v);

            if (i > 200)                       // past the filter's fill time
                highest = std::max (highest, reading);
        }

        worst = std::max (worst, gainToDb (amplitude, -200.0) - gainToDb (highest, -200.0));
    }

    return worst;
}
} // namespace

TEZLA_TEST (itu_coefficients_are_transcribed_exactly)
{
    // The single thing in Capstone that is copied rather than derived, so the
    // single thing a typo could ruin silently -- a wrong digit would shift the
    // meter by a fraction of a dB and nothing else would notice.
    //
    // Two properties of the published table make that checkable. Every
    // coefficient is an exact multiple of 1/8192, because the filter was
    // designed for integer arithmetic; and the 48-tap prototype is symmetric,
    // so phase 3 reversed is phase 0 and phase 2 reversed is phase 1. The first
    // catches a mistyped digit, the second catches a swapped or misordered
    // column, which the first would not.
    using Detector = TruePeakDetector;

    bool allExact = true;

    for (int phase = 0; phase < Detector::kItuPhases; ++phase)
        for (int tap = 0; tap < Detector::kItuTaps; ++tap)
        {
            const double scaled = Detector::kItuCoefficients[static_cast<std::size_t> (phase)]
                                                            [static_cast<std::size_t> (tap)] * 8192.0;

            if (std::abs (scaled - std::round (scaled)) > 1.0e-9)
                allExact = false;
        }

    CHECK (allExact);

    bool symmetric = true;

    for (int tap = 0; tap < Detector::kItuTaps; ++tap)
    {
        const int mirrored = Detector::kItuTaps - 1 - tap;

        if (Detector::kItuCoefficients[0][static_cast<std::size_t> (tap)]
            != Detector::kItuCoefficients[3][static_cast<std::size_t> (mirrored)])
            symmetric = false;

        if (Detector::kItuCoefficients[1][static_cast<std::size_t> (tap)]
            != Detector::kItuCoefficients[2][static_cast<std::size_t> (mirrored)])
            symmetric = false;
    }

    CHECK (symmetric);
}

TEZLA_TEST (true_peak_detector_meets_the_itu_under_read_bound)
{
    // Attachment 1 to Annex 2 gives the worst-case under-read for a ratio as
    // -20log10(cos(pi.f/n)). This checks the filters actually achieve it, which
    // the formula alone does not promise: it assumes the reconstruction is
    // sampled exactly, and ours is a finite FIR approximating it.
    for (const int factor : { 1, 4, 8, 16 })
    {
        const double bound = TruePeakDetector::worstCaseUnderReadDb (factor, 0.25);
        const double measured = worstUnderReadDb (factor, 0.25);

        // A hundredth of a decibel of slack. The designed filters land within
        // 0.0001 dB of the bound, so this is not generous, it is arithmetic.
        CHECK (measured <= bound + 0.01);
    }

    // And each step genuinely buys accuracy, which a filter that had quietly
    // stopped interpolating would not show.
    const double one     = worstUnderReadDb (1, 0.25);
    const double four    = worstUnderReadDb (4, 0.25);
    const double eight   = worstUnderReadDb (8, 0.25);
    const double sixteen = worstUnderReadDb (16, 0.25);

    CHECK (one > four);
    CHECK (four > eight);
    CHECK (eight > sixteen);

    // The headline number for the panel: a sample-peak meter is this formula at
    // a factor of one, and at a quarter of the sample rate it reads 3 dB low.
    CHECK (std::abs (one - 3.0103) < 0.01);
    CHECK (sixteen < 0.02);
}

TEZLA_TEST (true_peak_detector_never_under_reads_the_sample_peak)
{
    // Two of the ITU's four phases sum to 0.973 rather than 1, so a detector
    // that took the maximum over phases alone would read 2.4% low on anything
    // slow -- and would let the *sample* ceiling through while claiming to
    // guard something stricter. The aligned sample is included for exactly
    // this, and DC is the case that exposes it.
    for (const int factor : { 1, 4, 8, 16 })
    {
        TruePeakDetector detector;
        detector.prepare (TruePeakDetector::kMaxFactor);
        detector.setFactor (factor);
        detector.reset();

        double lowest = 1.0e9;

        for (int i = 0; i < 400; ++i)
        {
            const double reading = detector.process (0.8);

            if (i > 64)
                lowest = std::min (lowest, reading);
        }

        CHECK (lowest >= 0.8 - 1.0e-12);
    }
}

TEZLA_TEST (true_peak_detector_finds_what_a_sample_meter_misses)
{
    // The reason the class exists, on the signal that shows it worst: a sine at
    // a quarter of the sample rate, offset so every sample lands 45 degrees off
    // the peak. The samples read 0.707 of the amplitude and the waveform
    // between them reaches all of it.
    constexpr double amplitude = 0.5;

    const auto readingAt = [] (int factor)
    {
        TruePeakDetector detector;
        detector.prepare (TruePeakDetector::kMaxFactor);
        detector.setFactor (factor);
        detector.reset();

        double highest = 0.0;

        for (int i = 0; i < 1000; ++i)
        {
            const double v = amplitude
                * std::sin (2.0 * std::numbers::pi * 0.25 * i + std::numbers::pi * 0.25);

            const double reading = detector.process (v);

            if (i > 200)
                highest = std::max (highest, reading);
        }

        return highest;
    };

    const double samplePeak = readingAt (1);
    const double truePeak   = readingAt (16);

    // 1/sqrt(2) of the amplitude, which is what a peak meter would show.
    CHECK (std::abs (samplePeak - amplitude / std::numbers::sqrt2) < 1.0e-9);

    // And the real thing, within the ratio's own limit either side. The upper
    // bound is not 1e-9: an interpolating FIR has passband ripple and reads a
    // little high in the middle of the band -- 0.002 dB for this one, against
    // 0.22 dB for the ITU's shorter filter. Over-reading is the safe direction
    // for a limiter and the wrong one for a meter, so it is bounded rather than
    // ignored.
    CHECK (truePeak > amplitude * 0.998);
    CHECK (truePeak <= amplitude * 1.001);

    // Which is 3 dB of difference on an ordinary tone.
    CHECK (gainToDb (truePeak, -200.0) - gainToDb (samplePeak, -200.0) > 2.9);
}

TEZLA_TEST (true_peak_detector_is_silent_on_silence)
{
    for (const int factor : { 1, 4, 8, 16 })
    {
        TruePeakDetector detector;
        detector.prepare (TruePeakDetector::kMaxFactor);
        detector.setFactor (factor);
        detector.reset();

        double highest = 0.0;

        for (int i = 0; i < 500; ++i)
            highest = std::max (highest, detector.process (0.0));

        CHECK (highest == 0.0);
    }
}

TEZLA_TEST (true_peak_detector_reports_a_usable_latency)
{
    // The caller has to delay the audio by at least this, or the gain arrives
    // after the peak it was computed for. Zero at a factor of one, because
    // there is no filter to have a group delay.
    TruePeakDetector detector;
    detector.prepare (TruePeakDetector::kMaxFactor);

    detector.setFactor (1);
    CHECK (detector.getLatencySamples() == 0);

    detector.setFactor (4);
    CHECK (detector.getLatencySamples() == 6);

    detector.setFactor (16);
    CHECK (detector.getLatencySamples() == TruePeakDetector::kDesignedTaps / 2);
}
