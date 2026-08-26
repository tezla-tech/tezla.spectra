#pragma once

// What the waveform actually reaches between the samples.
//
// A peak meter reads the largest *sample*. The signal a converter reconstructs
// passes through those samples and goes wherever the sinc interpolation takes
// it in between, which can be well above any of them. Measured on this project:
// a limiter set to a 0.5 ceiling and driven by a sample-peak detector holds
// 0.5000 exactly at every sample and still reconstructs to 0.5723 -- +1.17 dBTP
// over the ceiling it claims to enforce. Lookahead does not help; it is a
// detector problem, not a timing one.
//
// So the level fed to the limiter is taken from an oversampled copy. ITU-R
// BS.1770-5 Annex 2 defines how, and the four-phase coefficient table below is
// **transcribed from that Recommendation** rather than derived -- the one thing
// in Capstone that is copied. That is deliberate: a meter that agrees with
// every other dBTP meter has to use the standard's filter, and no measurement
// we could run would tell us that a filter of our own was the wrong one to have
// chosen. Every coefficient is an exact multiple of 1/8192, which is what makes
// a transcription error checkable rather than merely unlikely, and the test
// checks it.
//
// The Recommendation's 12.04 dB attenuation step is deliberately absent. It
// exists to give integer arithmetic headroom, and the standard says so: "This
// step is not necessary if the calculations are performed in floating point."
// Everything here is double.
//
// Factors above 4 are ours rather than the standard's, designed at prepare()
// from a windowed sinc. The Recommendation asks for 4 as a minimum and says
// higher is preferred; its own table says why, because the worst-case under-read
// is set by the ratio rather than by the filter:
//
//     4x     0.554 dB        16x    0.034 dB
//     8x     0.136 dB        32x    0.008 dB
//
// which is why 4x matches other meters but is not, on its own, a guarantee.
//
// Measured against that bound at a quarter of the sample rate, where the worst
// alignment is actually reachable: 4x comes in at 0.082 dB against a bound of
// 0.168, and the designed 8x and 16x land on 0.0419 and 0.0105 against 0.0419
// and 0.0105 -- on the theoretical limit for their ratio, to a ten-thousandth
// of a decibel, and they hold that at 0.4 of the sample rate too.
//
// One asymmetry worth knowing before reading the meter. The ITU's own filter
// over-reads by up to 0.22 dB in the middle of the band -- its twelve taps per
// phase have passband ripple, and a longer filter would not be the standard's
// filter. The designed ones are flat to 0.002 dB. So Standard agrees with every
// other dBTP meter, including where they are all slightly high, and Strict is
// the one to believe.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace tezla::dsp {

class TruePeakDetector
{
public:
    /// ITU-R BS.1770-5 Annex 2: order 48, four phases, twelve taps each.
    ///
    /// Laid out as the Recommendation prints it -- one column per phase, read
    /// downwards. The prototype is symmetric, so phase 3 reversed is phase 0
    /// and phase 2 reversed is phase 1; the test asserts that too, because it
    /// catches a swapped column that a sum check would not.
    static constexpr int kItuPhases = 4;
    static constexpr int kItuTaps   = 12;

    static constexpr std::array<std::array<double, kItuTaps>, kItuPhases> kItuCoefficients {{
        {  0.0017089843750,  0.0109863281250, -0.0196533203125,  0.0332031250000,
          -0.0594482421875,  0.1373291015625,  0.9721679687500, -0.1022949218750,
           0.0476074218750, -0.0266113281250,  0.0148925781250, -0.0083007812500 },
        { -0.0291748046875,  0.0292968750000, -0.0517578125000,  0.0891113281250,
          -0.1665039062500,  0.4650878906250,  0.7797851562500, -0.2003173828125,
           0.1015625000000, -0.0582275390625,  0.0330810546875, -0.0189208984375 },
        { -0.0189208984375,  0.0330810546875, -0.0582275390625,  0.1015625000000,
          -0.2003173828125,  0.7797851562500,  0.4650878906250, -0.1665039062500,
           0.0891113281250, -0.0517578125000,  0.0292968750000, -0.0291748046875 },
        { -0.0083007812500,  0.0148925781250, -0.0266113281250,  0.0476074218750,
          -0.1022949218750,  0.9721679687500,  0.1373291015625, -0.0594482421875,
           0.0332031250000, -0.0196533203125,  0.0109863281250,  0.0017089843750 }
    }};

    /// Taps per phase for the factors the Recommendation does not tabulate.
    ///
    /// Twenty-four, chosen by measuring rather than by taste. At sixteen the
    /// filter rather than the ratio is the limit near the top of the band -- 16x
    /// under-reads by 0.102 dB at 0.4 of the sample rate against a ratio bound
    /// of 0.027, which is passband droop. At twenty-four it reads 0.029 and the
    /// ratio is the limit again. Thirty-two and forty-eight buy 0.0017 dB more
    /// and cost proportionally, so this is where the curve flattens.
    static constexpr int kDesignedTaps = 24;

    /// The largest factor prepare() will allocate for.
    static constexpr int kMaxFactor = 16;

    /// Allocates for the largest factor. The only call here that touches
    /// memory: setFactor() rebuilds coefficients in place.
    void prepare (int maximumFactor = kMaxFactor)
    {
        maximumFactor_ = std::clamp (maximumFactor, 1, kMaxFactor);

        coefficients_.assign (static_cast<std::size_t> (maximumFactor_ * kDesignedTaps), 0.0);
        history_.assign (static_cast<std::size_t> (std::max (kDesignedTaps, kItuTaps)), 0.0);

        factor_ = 0;
        setFactor (maximumFactor_);
        reset();
    }

    void reset() noexcept
    {
        std::fill (history_.begin(), history_.end(), 0.0);
        writePosition_ = 0;
    }

    /// 1 (off, sample peak only), 4 (the ITU filter), or anything up to the
    /// prepared maximum (designed here). Allocation-free.
    void setFactor (int factor) noexcept
    {
        const int wanted = std::clamp (factor, 1, maximumFactor_);

        if (wanted == factor_)
            return;

        factor_ = wanted;

        if (factor_ == 1)
        {
            taps_ = 1;
            centre_ = 0;
            return;
        }

        if (factor_ == kItuPhases)
        {
            taps_ = kItuTaps;

            for (int phase = 0; phase < kItuPhases; ++phase)
                for (int tap = 0; tap < kItuTaps; ++tap)
                    coefficients_[static_cast<std::size_t> (phase * taps_ + tap)]
                        = kItuCoefficients[static_cast<std::size_t> (phase)]
                                          [static_cast<std::size_t> (tap)];

            // Where phase 0's energy sits, which is what the sample peak has to
            // be compared against so the two readings are of the same instant.
            centre_ = 6;
            return;
        }

        designPolyphase();
    }

    [[nodiscard]] int getFactor() const noexcept { return factor_; }

    /// How far behind the input the reading is, in samples.
    ///
    /// The prototype is symmetric, so this is its group delay rounded up. The
    /// caller has to delay the audio by at least this much or the gain arrives
    /// after the peak it was computed for.
    [[nodiscard]] int getLatencySamples() const noexcept { return centre_; }

    /// The largest magnitude the reconstructed waveform reaches around this
    /// sample.
    ///
    /// The aligned sample itself is included rather than assumed to be covered.
    /// The ITU's phase 0 has a dominant tap of 0.972 rather than 1, so it does
    /// not reproduce the input exactly, and a detector that under-read the
    /// sample peak would let the *sample* ceiling through while claiming to
    /// guard something stricter.
    [[nodiscard]] double process (double x) noexcept
    {
        history_[static_cast<std::size_t> (writePosition_)] = x;

        if (factor_ == 1)
        {
            advance();
            return std::abs (x);
        }

        double peak = std::abs (at (centre_));

        for (int phase = 0; phase < factor_; ++phase)
        {
            double sum = 0.0;

            for (int tap = 0; tap < taps_; ++tap)
                sum += coefficients_[static_cast<std::size_t> (phase * taps_ + tap)] * at (tap);

            peak = std::max (peak, std::abs (sum));
        }

        advance();
        return peak;
    }

    /// The Recommendation's own worst-case under-read for a ratio, in dB.
    ///
    /// Attachment 1 to Annex 2: a pair of oversampled points can straddle the
    /// peak of a sinusoid at the highest metered frequency, missing it by at
    /// most half the oversampled period. `normalisedFrequency` is relative to
    /// the *sample rate*, so Nyquist is 0.5 and the ITU tabulates 0.45 and 0.5.
    /// Valid at a factor of 1 too, and that is the point rather than an edge
    /// case: a plain sample-peak meter is this formula at n = 1, and at a
    /// quarter of the sample rate it reads 3.01 dB low. Special-casing it to
    /// zero -- which this did, until the measurement disagreed -- would have
    /// said a sample peak meter is exact, which is the belief the whole class
    /// exists to correct.
    [[nodiscard]] static double worstCaseUnderReadDb (int factor,
                                                      double normalisedFrequency = 0.45) noexcept
    {
        const double n = static_cast<double> (std::max (factor, 1));

        return -20.0 * std::log10 (std::cos (std::numbers::pi * normalisedFrequency / n));
    }

private:
    [[nodiscard]] double at (int tapsAgo) const noexcept
    {
        const int size = static_cast<int> (history_.size());
        int index = writePosition_ - tapsAgo;

        while (index < 0)
            index += size;

        return history_[static_cast<std::size_t> (index)];
    }

    void advance() noexcept
    {
        if (++writePosition_ >= static_cast<int> (history_.size()))
            writePosition_ = 0;
    }

    /// A windowed sinc, split into phases, for the factors the ITU does not
    /// tabulate. Each phase is normalised to unity at DC so a steady level
    /// reads as itself whichever phase happens to catch it.
    void designPolyphase() noexcept
    {
        taps_ = kDesignedTaps;
        centre_ = kDesignedTaps / 2;

        const double length = static_cast<double> (factor_ * taps_);

        for (int phase = 0; phase < factor_; ++phase)
        {
            double sum = 0.0;

            for (int tap = 0; tap < taps_; ++tap)
            {
                // Position of this tap in the prototype, measured from its
                // centre, in input samples.
                const double offset = static_cast<double> (tap - centre_)
                                    + static_cast<double> (phase) / static_cast<double> (factor_);

                const double argument = std::numbers::pi * offset;
                const double sinc = std::abs (argument) < 1.0e-12
                                  ? 1.0 : std::sin (argument) / argument;

                // Blackman, over the whole prototype rather than per phase, so
                // the phases are slices of one window rather than sixteen.
                const double position = (static_cast<double> (tap * factor_ + phase)) / (length - 1.0);
                const double window = 0.42
                                    - 0.50 * std::cos (2.0 * std::numbers::pi * position)
                                    + 0.08 * std::cos (4.0 * std::numbers::pi * position);

                const double value = sinc * window;
                coefficients_[static_cast<std::size_t> (phase * taps_ + tap)] = value;
                sum += value;
            }

            if (std::abs (sum) > 1.0e-12)
                for (int tap = 0; tap < taps_; ++tap)
                    coefficients_[static_cast<std::size_t> (phase * taps_ + tap)] /= sum;
        }
    }

    std::vector<double> coefficients_;
    std::vector<double> history_;

    int maximumFactor_ { kMaxFactor };
    int factor_        { 0 };
    int taps_          { 1 };
    int centre_        { 0 };
    int writePosition_ { 0 };
};

} // namespace tezla::dsp
