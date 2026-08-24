#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <tezla/dsp/Biquad.hpp>

using namespace tezla::dsp;

namespace {

/// Measures a filter's actual gain at one frequency by running a sine through
/// it and reading the steady-state amplitude. This checks the *implementation*,
/// not just the coefficient formula -- a transfer function that only agrees
/// with itself proves nothing.
///
/// Amplitude is taken as RMS * sqrt(2), never as the largest sample seen. Peak
/// picking silently under-reads any tone with few samples per cycle: a 16 kHz
/// sine at 48 kHz has three samples per cycle and peak-picks at 0.866, which
/// looks exactly like a filter that is 1.2 dB down. RMS is exact for a sine at
/// any sample density.
double measuredGainAt (Biquad<double>& filter, double frequencyHz, double sampleRate)
{
    filter.reset();

    const int settleSamples  = 40000;
    const int measureSamples = 40000;
    const double omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;

    for (int i = 0; i < settleSamples; ++i)
        (void) filter.process (std::sin (omega * static_cast<double> (i)));

    double sumOfSquares = 0.0;
    for (int i = settleSamples; i < settleSamples + measureSamples; ++i)
    {
        const double y = filter.process (std::sin (omega * static_cast<double> (i)));
        sumOfSquares += y * y;
    }

    return std::sqrt (2.0 * sumOfSquares / static_cast<double> (measureSamples));
}

} // namespace

TEZLA_TEST (biquad_lowpass_passes_dc_and_stops_nyquist)
{
    constexpr double fs = 48000.0;
    Biquad<double> filter;
    filter.setCoefficients (design::lowpass (1000.0, 0.707, fs));

    CHECK_NEAR (measuredGainAt (filter, 20.0,    fs), 1.0, 0.02);
    CHECK_NEAR (measuredGainAt (filter, 1000.0,  fs), 0.7071, 0.02);   // -3 dB at the corner
    CHECK      (measuredGainAt (filter, 16000.0, fs) < 0.01);
}

TEZLA_TEST (biquad_highpass_is_the_mirror_of_lowpass)
{
    constexpr double fs = 48000.0;
    Biquad<double> filter;
    filter.setCoefficients (design::highpass (1000.0, 0.707, fs));

    CHECK      (measuredGainAt (filter, 20.0,   fs) < 0.01);
    CHECK_NEAR (measuredGainAt (filter, 1000.0, fs), 0.7071, 0.02);
    CHECK_NEAR (measuredGainAt (filter, 16000.0, fs), 1.0, 0.02);
}

TEZLA_TEST (biquad_peak_hits_its_stated_gain)
{
    constexpr double fs = 48000.0;

    for (const double gainDb : { -12.0, -6.0, 6.0, 12.0 })
    {
        Biquad<double> filter;
        filter.setCoefficients (design::peak (1000.0, 1.0, gainDb, fs));

        const double expected = std::pow (10.0, gainDb / 20.0);
        CHECK_NEAR (measuredGainAt (filter, 1000.0, fs), expected, expected * 0.03);

        // Well away from the centre it must be transparent.
        CHECK_NEAR (measuredGainAt (filter, 40.0, fs), 1.0, 0.05);
    }
}

TEZLA_TEST (biquad_shelves_reach_their_plateaus)
{
    constexpr double fs = 48000.0;
    constexpr double gainDb = 9.0;
    const double expected = std::pow (10.0, gainDb / 20.0);

    Biquad<double> low;
    low.setCoefficients (design::lowShelf (300.0, 0.707, gainDb, fs));
    CHECK_NEAR (measuredGainAt (low, 20.0,    fs), expected, expected * 0.05);
    CHECK_NEAR (measuredGainAt (low, 15000.0, fs), 1.0, 0.05);

    Biquad<double> high;
    high.setCoefficients (design::highShelf (3000.0, 0.707, gainDb, fs));
    CHECK_NEAR (measuredGainAt (high, 20.0,    fs), 1.0, 0.05);
    CHECK_NEAR (measuredGainAt (high, 18000.0, fs), expected, expected * 0.05);
}

TEZLA_TEST (biquad_allpass_is_flat)
{
    constexpr double fs = 48000.0;
    Biquad<double> filter;
    filter.setCoefficients (design::allpass (1000.0, 0.707, fs));

    for (const double f : { 30.0, 200.0, 1000.0, 5000.0, 18000.0 })
        CHECK_NEAR (measuredGainAt (filter, f, fs), 1.0, 0.02);
}

TEZLA_TEST (biquad_analytic_response_matches_measured_response)
{
    constexpr double fs = 96000.0;
    const auto coefficients = design::lowpass (2000.0, 1.2, fs);

    Biquad<double> filter;
    filter.setCoefficients (coefficients);

    for (const double f : { 50.0, 500.0, 2000.0, 8000.0 })
        CHECK_NEAR (measuredGainAt (filter, f, fs), coefficients.magnitudeAt (f, fs), 0.02);
}

TEZLA_TEST (biquad_response_agrees_across_sample_rates_below_an_eighth_of_nyquist)
{
    // The rule that matters most on this rig -- but stated honestly.
    //
    // Computing coefficients from the actual sample rate is necessary and not
    // sufficient. The bilinear transform warps the frequency axis, so an RBJ
    // biquad designed at 48 kHz and the same design at 192 kHz agree closely
    // well below Nyquist and diverge as they approach it. Measured, for a
    // 4 kHz lowpass at Q 0.707:
    //
    //        2 kHz    48k -0.247 dB   192k -0.263 dB   (0.02 dB apart)
    //        6 kHz    48k -8.269 dB   192k -7.853 dB   (0.42 dB apart)
    //       15 kHz    48k -29.89 dB   192k -23.31 dB   (6.6  dB apart)
    //
    // The 192 kHz curve is the one tracking the analogue prototype; the 48 kHz
    // curve is the outlier. So: trust a plain biquad to be rate-independent
    // only below about Fs/8, and put anything whose high-frequency shape
    // actually matters -- a cabinet response, a tape head bump, the tone
    // control inside a saturation stage -- above an oversampled section, or
    // design it with a method that matches at the frequencies of interest.
    constexpr double lowestRate = 48000.0;
    const double trustedLimitHz = lowestRate / 8.0;

    for (const double f : { 100.0, 1000.0, 2000.0, 4000.0, 6000.0 })
    {
        CHECK (f <= trustedLimitHz);

        const double at48  = design::lowpass (4000.0, 0.707, 48000.0) .magnitudeAt (f, 48000.0);
        const double at96  = design::lowpass (4000.0, 0.707, 96000.0) .magnitudeAt (f, 96000.0);
        const double at192 = design::lowpass (4000.0, 0.707, 192000.0).magnitudeAt (f, 192000.0);

        CHECK_NEAR (20.0 * std::log10 (at96),  20.0 * std::log10 (at48), 0.5);
        CHECK_NEAR (20.0 * std::log10 (at192), 20.0 * std::log10 (at48), 0.5);
    }

    // And pin the divergence itself, so that if a future change to the filter
    // design alters it, this test says so rather than quietly passing.
    const double near48  = 20.0 * std::log10 (design::lowpass (4000.0, 0.707, 48000.0) .magnitudeAt (15000.0, 48000.0));
    const double near192 = 20.0 * std::log10 (design::lowpass (4000.0, 0.707, 192000.0).magnitudeAt (15000.0, 192000.0));

    CHECK_NEAR (near48,  -29.89, 0.1);
    CHECK_NEAR (near192, -23.31, 0.1);
    CHECK (near48 < near192 - 5.0);
}

TEZLA_TEST (biquad_is_stable_with_a_corner_above_nyquist)
{
    // Automation can push a cutoff past Nyquist. That must clamp, not explode.
    constexpr double fs = 48000.0;
    Biquad<double> filter;
    filter.setCoefficients (design::lowpass (80000.0, 0.707, fs));

    double value = 1.0;
    for (int i = 0; i < 10000; ++i)
        value = filter.process (i == 0 ? 1.0 : 0.0);

    CHECK (std::isfinite (value));
    CHECK (std::abs (value) < 10.0);
}
