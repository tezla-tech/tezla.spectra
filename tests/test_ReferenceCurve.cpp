#include "TestFramework.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <tezla/dsp/ReferenceCurve.hpp>

using namespace tezla::dsp;

namespace
{
constexpr std::size_t kBins = 96;
constexpr double kFrameRate = 30.0;

/// A tilted spectrum, in dB, as a display would hand it over.
std::vector<float> tilted (double slopeDbPerBin, double offsetDb = 0.0)
{
    std::vector<float> bins (kBins);

    for (std::size_t i = 0; i < kBins; ++i)
        bins[i] = static_cast<float> (offsetDb + slopeDbPerBin * static_cast<double> (i));

    return bins;
}

/// Captures a whole curve from one repeated frame.
void capture (ReferenceCurve& curve, const std::vector<float>& frame, double seconds = 10.0)
{
    curve.beginCapture (seconds, kFrameRate);

    while (curve.isCapturing())
        curve.push (frame.data(), frame.size());
}
} // namespace

TEZLA_TEST (reference_curve_is_about_shape_not_level)
{
    // The property that decides whether the feature is usable. A quiet
    // reference and a loud one with the same balance must give the same curve,
    // or comparing a mix against a reference reads as "you need more of
    // everything" -- true, and useless.
    ReferenceCurve quiet, loud;
    quiet.prepare (kBins, 48000.0);
    loud.prepare (kBins, 48000.0);

    capture (quiet, tilted (-0.2, -60.0));
    capture (loud,  tilted (-0.2,  -6.0));

    CHECK (quiet.hasCurve());
    CHECK (loud.hasCurve());

    double worst = 0.0;

    for (std::size_t i = 0; i < kBins; ++i)
        worst = std::max (worst, std::abs (quiet.getCurveDb()[i] - loud.getCurveDb()[i]));

    // 54 dB apart in level, identical in shape. Measured at 1.9e-06 dB, which
    // is the precision of the float the display hands over rather than any
    // difference in the curves -- a tolerance of 1e-9 would be asserting
    // something float cannot carry.
    CHECK (worst < 1.0e-4);
}

TEZLA_TEST (reference_curve_keeps_the_slope_it_was_given)
{
    // Normalising and smoothing must not flatten the thing being measured.
    // Away from the edges, where the smoothing window runs out of neighbours,
    // the captured slope has to be the slope that went in.
    ReferenceCurve curve;
    curve.prepare (kBins, 48000.0);

    constexpr double slope = -0.25;
    capture (curve, tilted (slope));

    const auto& c = curve.getCurveDb();

    double worst = 0.0;

    for (std::size_t i = 20; i + 20 < kBins; ++i)
        worst = std::max (worst, std::abs ((c[i + 1] - c[i]) - slope));

    CHECK (worst < 1.0e-6);

    // And the mean is zero, which is what "normalised" means here.
    double sum = 0.0;
    for (const double v : c) sum += v;
    CHECK (std::abs (sum / static_cast<double> (kBins)) < 1.0e-6);
}

TEZLA_TEST (reference_curve_compares_a_signal_against_itself_as_flat)
{
    // The headline check: capture a spectrum, then hand the same spectrum back
    // as the live one. The difference must be zero everywhere -- if it is not,
    // the two sides are being treated differently and every reading off this
    // display would be wrong by that amount.
    ReferenceCurve curve;
    curve.prepare (kBins, 48000.0);

    const auto frame = tilted (-0.18, -30.0);
    capture (curve, frame);

    std::vector<double> difference;
    curve.computeDifference (frame.data(), frame.size(), difference);

    double worst = 0.0;

    for (std::size_t i = 20; i + 20 < kBins; ++i)
        worst = std::max (worst, std::abs (difference[i]));

    CHECK (worst < 0.5);
}

TEZLA_TEST (reference_curve_reports_a_real_difference_with_the_right_sign)
{
    // Positive means the mix has more there than the reference. A display that
    // had the sign backwards would send every user the wrong way, and would
    // look perfectly plausible doing it.
    ReferenceCurve curve;
    curve.prepare (kBins, 48000.0);

    capture (curve, tilted (0.0, -30.0));            // flat reference

    const auto brighter = tilted (0.2, -30.0);       // rises with frequency
    std::vector<double> difference;
    curve.computeDifference (brighter.data(), brighter.size(), difference);

    // Low bins below the reference, high bins above it.
    CHECK (difference[10] < -2.0);
    CHECK (difference[kBins - 10] > 2.0);
}

TEZLA_TEST (reference_curve_averages_power_rather_than_decibels)
{
    // Averaging decibels averages logarithms, which weights a quiet moment as
    // heavily as a loud one and pulls the curve towards whatever the track does
    // least. On most material the two are close; this is a signal where they
    // disagree completely, which is what makes it a test rather than a hope.
    //
    // Two frames alternating. One falls with frequency and is loud at the
    // bottom (-10 dB down to -48); the other rises and is loud at the top
    // (-40 dB up to -2). At every bin one frame is roughly 30 dB above the
    // other, so:
    //
    //   averaging power  ->  the louder frame wins at each end, and the curve
    //                        rises about 8 dB from bottom to top
    //   averaging dB     ->  the two slopes cancel exactly, and the curve is
    //                        **flat**
    //
    // A flat result here would mean the averaging is wrong.
    ReferenceCurve curve;
    curve.prepare (kBins, 48000.0);
    curve.beginCapture (10.0, kFrameRate);

    const auto loudAtBottom = tilted (-0.4, -10.0);
    const auto loudAtTop    = tilted ( 0.4, -40.0);

    bool alternate = false;

    while (curve.isCapturing())
    {
        curve.push (alternate ? loudAtTop.data() : loudAtBottom.data(), kBins);
        alternate = ! alternate;
    }

    const auto& c = curve.getCurveDb();

    // Measured: +0.61 at bin 10, +8.58 at bin 85.
    CHECK (c[kBins - 10] > c[10] + 5.0);
}

TEZLA_TEST (reference_curve_refuses_a_capture_too_short_to_mean_anything)
{
    // A two-second capture is a snapshot of one chord. The floor is enforced
    // rather than documented, because the failure is a curve that looks like a
    // measurement and is not.
    ReferenceCurve curve;
    curve.prepare (kBins, 48000.0);

    curve.beginCapture (0.5, kFrameRate);

    // Clamped up to the minimum rather than accepted.
    const int expected = static_cast<int> (std::lround (ReferenceCurve::kMinimumSeconds * kFrameRate));

    int frames = 0;
    const auto frame = tilted (-0.1);

    while (curve.isCapturing() && frames < expected * 4)
    {
        curve.push (frame.data(), kBins);
        ++frames;
    }

    CHECK (frames == expected);
}

TEZLA_TEST (reference_curve_survives_a_round_trip_through_text)
{
    ReferenceCurve saved, loaded;
    saved.prepare (kBins, 48000.0);
    loaded.prepare (kBins, 48000.0);

    capture (saved, tilted (-0.22, -25.0));

    CHECK (loaded.fromText (saved.toText()));
    CHECK (loaded.hasCurve());

    double worst = 0.0;

    for (std::size_t i = 0; i < kBins; ++i)
        worst = std::max (worst, std::abs (saved.getCurveDb()[i] - loaded.getCurveDb()[i]));

    // Written to three decimals, so this is the format's resolution rather than
    // a tolerance -- and a thousandth of a dB is far below anything visible.
    CHECK (worst < 0.001);
}

TEZLA_TEST (reference_curve_refuses_text_it_cannot_trust)
{
    // A half-loaded reference is worse than none, because it still looks like a
    // measurement. Every one of these must leave the object unchanged.
    ReferenceCurve curve;
    curve.prepare (kBins, 48000.0);

    CHECK (! curve.fromText (""));
    CHECK (! curve.fromText ("nonsense"));
    CHECK (! curve.fromText ("tzref 2 96\n"));                 // future version
    CHECK (! curve.fromText ("tzref 1 32\n"));                 // wrong bin count
    CHECK (! curve.fromText ("tzref 1 96\n1.0\n2.0\n"));       // truncated

    CHECK (! curve.hasCurve());
}

TEZLA_TEST (reference_curve_reports_progress_while_capturing)
{
    ReferenceCurve curve;
    curve.prepare (kBins, 48000.0);

    CHECK (curve.getProgress() == 0.0);

    curve.beginCapture (10.0, kFrameRate);

    const auto frame = tilted (-0.1);
    const int total = static_cast<int> (std::lround (10.0 * kFrameRate));

    for (int i = 0; i < total / 2; ++i)
        curve.push (frame.data(), kBins);

    CHECK (curve.getProgress() > 0.45);
    CHECK (curve.getProgress() < 0.55);

    // Cancelling leaves any stored curve alone -- there is none here, so it
    // simply stops.
    curve.cancelCapture();
    CHECK (! curve.isCapturing());
    CHECK (! curve.hasCurve());
}
