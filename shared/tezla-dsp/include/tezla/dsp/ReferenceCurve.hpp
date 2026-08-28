// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A reference spectrum: the tonal balance of a track you already like, held
// still so a mix can be compared against it.
//
// The honest alternative to a shipped "target curve". Genre target curves are
// folklore -- they vary by track, by era and by who drew them -- and baking one
// into a tool somebody trusts is worse than shipping nothing. This ships no
// curves at all. It measures the ones you point it at.
//
// Three properties decide whether the feature is useful rather than decorative,
// and all three are easy to leave out:
//
//   long integration    A two-second FFT is a snapshot of one moment: one chord,
//                       one drum hit. Thirty seconds of music is what converges
//                       on a *balance*. The default is 30 s and the floor is 5,
//                       below which capture refuses rather than returning
//                       something that looks like an answer.
//
//   level normalised    Subtracting the mean across bins makes it a curve about
//                       shape. Without it, comparing a quiet reference to a loud
//                       mix reads as "you need more of everything", which is
//                       true and useless.
//
//   smoothed            Raw bins are noisy enough that the eye cannot see the
//                       trend. Smoothing to a fraction of an octave is what
//                       turns a hairy graph into a readable line.
//
// What it is not: a matching EQ. It draws the difference and leaves the moves
// to you, because the difference between two mixes is often a arrangement
// decision rather than an EQ one.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace tezla::dsp {

class ReferenceCurve
{
public:
    /// How long a capture runs by default, and the shortest it may be.
    static constexpr double kDefaultSeconds = 30.0;
    static constexpr double kMinimumSeconds = 5.0;

    /// Smoothing width. A sixth of an octave keeps the shape of a resonance
    /// while removing the noise between bins.
    static constexpr double kSmoothingOctaves = 1.0 / 6.0;

    /// Nothing below this contributes to the average. A bin that is silent for
    /// the whole capture should not drag the curve down as though it were
    /// quiet material -- it is *absent* material, which is a different thing.
    static constexpr double kFloorDb = -90.0;

    /// Sized to a display's bin layout. Never call from the audio thread.
    void prepare (std::size_t numBins, double sampleRate)
    {
        numBins_ = std::max<std::size_t> (numBins, 2);
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        accumulator_.assign (numBins_, 0.0);
        counts_.assign (numBins_, 0);
        curve_.assign (numBins_, 0.0);
        scratch_.assign (numBins_, 0.0);

        clear();
    }

    /// Throws away both the capture in progress and the stored curve.
    void clear() noexcept
    {
        std::fill (accumulator_.begin(), accumulator_.end(), 0.0);
        std::fill (counts_.begin(), counts_.end(), 0);
        std::fill (curve_.begin(), curve_.end(), 0.0);

        capturing_ = false;
        hasCurve_ = false;
        frames_ = 0;
        framesWanted_ = 0;
    }

    /// Begins a capture. `frameRate` is how often push() will be called, so the
    /// class can count seconds rather than frames -- the number a user set.
    void beginCapture (double seconds, double frameRate)
    {
        const double wanted = std::max (seconds, kMinimumSeconds);
        const double rate = frameRate > 0.0 ? frameRate : 30.0;

        std::fill (accumulator_.begin(), accumulator_.end(), 0.0);
        std::fill (counts_.begin(), counts_.end(), 0);

        frames_ = 0;
        framesWanted_ = std::max (1, static_cast<int> (std::lround (wanted * rate)));
        capturing_ = true;
    }

    /// Abandons a capture in progress, leaving any stored curve alone.
    void cancelCapture() noexcept
    {
        capturing_ = false;
        frames_ = 0;
    }

    [[nodiscard]] bool isCapturing() const noexcept { return capturing_; }
    [[nodiscard]] bool hasCurve()    const noexcept { return hasCurve_; }

    /// 0 to 1 while capturing, for a progress ring.
    [[nodiscard]] double getProgress() const noexcept
    {
        return framesWanted_ > 0
             ? std::clamp (static_cast<double> (frames_) / framesWanted_, 0.0, 1.0) : 0.0;
    }

    /// Feeds one frame of the display's bins, in dB. Returns true on the frame
    /// that completes the capture.
    ///
    /// Averaged as **power**, not as decibels. Averaging dB is averaging
    /// logarithms, which weights a quiet moment as heavily as a loud one and
    /// pulls the curve towards whatever the track does least.
    bool push (const float* magnitudesDb, std::size_t count)
    {
        if (! capturing_ || magnitudesDb == nullptr)
            return false;

        const std::size_t span = std::min (count, numBins_);

        for (std::size_t i = 0; i < span; ++i)
        {
            const double db = static_cast<double> (magnitudesDb[i]);

            if (db <= kFloorDb)
                continue;

            accumulator_[i] += std::pow (10.0, db * 0.1);
            ++counts_[i];
        }

        if (++frames_ < framesWanted_)
            return false;

        finish();
        return true;
    }

    /// The stored curve, in dB, normalised so its mean is zero and smoothed.
    /// Empty until a capture completes or one is loaded.
    [[nodiscard]] const std::vector<double>& getCurveDb() const noexcept { return curve_; }

    /// Live minus reference, per bin, both normalised to their own mean -- so
    /// this is the shape difference and not a level difference. Positive means
    /// the mix has more there than the reference does.
    ///
    /// Writes into `out`, which the caller owns, so a display can call this on
    /// its timer without allocating.
    void computeDifference (const float* magnitudesDb, std::size_t count,
                            std::vector<double>& out) const
    {
        out.assign (numBins_, 0.0);

        if (! hasCurve_ || magnitudesDb == nullptr)
            return;

        const std::size_t span = std::min (count, numBins_);

        // The live spectrum gets the same treatment the reference had, or the
        // two are not comparable: same floor, same normalisation.
        double sum = 0.0;
        std::size_t counted = 0;

        for (std::size_t i = 0; i < span; ++i)
            if (magnitudesDb[i] > kFloorDb)
            {
                sum += static_cast<double> (magnitudesDb[i]);
                ++counted;
            }

        if (counted == 0)
            return;

        const double mean = sum / static_cast<double> (counted);

        for (std::size_t i = 0; i < span; ++i)
            out[i] = magnitudesDb[i] > kFloorDb
                   ? (static_cast<double> (magnitudesDb[i]) - mean) - curve_[i] : 0.0;
    }

    // ---- storage ------------------------------------------------------------

    /// Serialises to plain text: a version line, then one value per bin.
    ///
    /// Text rather than base64 so a curve is diffable, readable and repairable.
    /// A reference somebody spent thirty seconds capturing should not be an
    /// opaque blob.
    [[nodiscard]] std::string toText() const
    {
        std::string text = "tzref 1 " + std::to_string (numBins_) + "\n";

        if (! hasCurve_)
            return text;

        for (const double value : curve_)
        {
            char buffer[32];
            std::snprintf (buffer, sizeof (buffer), "%.3f\n", value);
            text += buffer;
        }

        return text;
    }

    /// Reads back what toText() wrote. Returns false and changes nothing if the
    /// text is not a curve of the right size -- a half-loaded reference would
    /// be worse than none, because it would look like a measurement.
    bool fromText (const std::string& text)
    {
        std::size_t position = 0;
        const auto line = [&text, &position] () -> std::string
        {
            if (position >= text.size())
                return {};

            const auto end = text.find ('\n', position);
            auto result = text.substr (position, end == std::string::npos
                                               ? std::string::npos : end - position);
            position = end == std::string::npos ? text.size() : end + 1;

            // A file written on Windows arrives with CRLF, and these travel
            // between machines by design -- that is the whole reason they are
            // files rather than only project state. strtod happens to stop at
            // the carriage return, so this works either way today; relying on
            // that is not the same as handling it.
            if (! result.empty() && result.back() == '\r')
                result.pop_back();

            return result;
        };

        std::string tag;
        int version = 0;
        std::size_t bins = 0;

        {
            const auto header = line();
            char tagBuffer[16] {};

            if (std::sscanf (header.c_str(), "%15s %d %zu", tagBuffer, &version, &bins) != 3)
                return false;

            tag = tagBuffer;
        }

        if (tag != "tzref" || version != 1 || bins != numBins_)
            return false;

        std::vector<double> loaded;
        loaded.reserve (bins);

        for (std::size_t i = 0; i < bins; ++i)
        {
            const auto value = line();

            if (value.empty())
                return false;

            // The whole line has to be a number, not merely start with one.
            // strtod returns 0.0 for text it cannot read at all, so accepting
            // whatever it hands back turns a corrupt file into a curve of
            // zeros -- which is a flat reference, which is a plausible-looking
            // measurement. That is the exact failure this function exists to
            // refuse.
            char* end = nullptr;
            const double parsed = std::strtod (value.c_str(), &end);

            if (end == value.c_str() || end == nullptr || *end != '\0')
                return false;

            loaded.push_back (parsed);
        }

        curve_ = std::move (loaded);
        hasCurve_ = true;
        capturing_ = false;
        return true;
    }

private:
    /// Turns the accumulated power into the stored curve: mean, then dB, then
    /// normalise to zero mean, then smooth.
    void finish()
    {
        for (std::size_t i = 0; i < numBins_; ++i)
            scratch_[i] = counts_[i] > 0
                        ? 10.0 * std::log10 (accumulator_[i] / static_cast<double> (counts_[i]))
                        : kFloorDb;

        double sum = 0.0;
        std::size_t counted = 0;

        for (std::size_t i = 0; i < numBins_; ++i)
            if (counts_[i] > 0)
            {
                sum += scratch_[i];
                ++counted;
            }

        const double mean = counted > 0 ? sum / static_cast<double> (counted) : 0.0;

        for (std::size_t i = 0; i < numBins_; ++i)
            scratch_[i] = counts_[i] > 0 ? scratch_[i] - mean : 0.0;

        smooth (scratch_, curve_);

        capturing_ = false;
        hasCurve_ = true;
    }

    /// Moving average over a fixed number of bins.
    ///
    /// The display's bins are log-spaced by construction, so a fixed *count* of
    /// them is a fixed fraction of an octave -- which is why this is a plain box
    /// and not a frequency-dependent kernel. That only holds because the bins
    /// are log-spaced; on a linear axis it would smear the bass and leave the
    /// top untouched.
    void smooth (const std::vector<double>& in, std::vector<double>& out) const
    {
        out.assign (numBins_, 0.0);

        if (numBins_ < 2)
            return;

        // How many bins span the smoothing width.
        const double binsPerOctave = static_cast<double> (numBins_)
                                   / std::log2 (20000.0 / 20.0);
        const int half = std::max (1, static_cast<int> (std::lround (
                                       kSmoothingOctaves * binsPerOctave * 0.5)));

        for (std::size_t i = 0; i < numBins_; ++i)
        {
            const auto first = static_cast<std::size_t> (std::max (0, static_cast<int> (i) - half));
            const auto last  = std::min (numBins_ - 1, i + static_cast<std::size_t> (half));

            double sum = 0.0;

            for (std::size_t j = first; j <= last; ++j)
                sum += in[j];

            out[i] = sum / static_cast<double> (last - first + 1);
        }
    }

    std::vector<double> accumulator_;
    std::vector<int>    counts_;
    std::vector<double> curve_;
    mutable std::vector<double> scratch_;

    std::size_t numBins_ { 2 };
    double sampleRate_   { 48000.0 };

    bool capturing_ { false };
    bool hasCurve_  { false };
    int  frames_    { 0 };
    int  framesWanted_ { 0 };
};

} // namespace tezla::dsp
