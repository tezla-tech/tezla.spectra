// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// Real-time spectrum capture and analysis.
//
// Split in two along the thread boundary, because that is the only split that
// matters here:
//
//   SpectrumCapture   audio thread. Copies samples into a preallocated ring and
//                     publishes a write position. No allocation, no locks, no
//                     transcendentals -- a memcpy and one atomic store.
//
//   SpectrumAnalyser  message thread. Takes the most recent window, applies a
//                     Hann window, runs the FFT, and folds the result onto
//                     log-spaced display bins with temporal smoothing and peak
//                     hold.
//
// Framework-free on purpose. The display that draws it is JUCE; this is not,
// so it can be tested offline and reused by a standalone analyser later without
// dragging a GUI framework behind it.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "Decibels.hpp"
#include "Fft.hpp"

namespace tezla::dsp {

/// Audio-thread side. One producer, one consumer.
///
/// A torn read is possible in principle -- the consumer can be copying a region
/// the producer is overwriting -- and is accepted deliberately. The cost is one
/// slightly wrong frame in a display that redraws thirty times a second; the
/// alternative is a lock on the audio thread, which is never worth it.
class SpectrumCapture
{
public:
    /// Allocates. Never call from the audio thread.
    void prepare (int capacity)
    {
        capacity_ = static_cast<std::size_t> (std::max (capacity, 2));

        // Round up to a power of two so the wrap is a mask rather than a modulo.
        std::size_t size = 1;
        while (size < capacity_)
            size <<= 1;

        buffer_.assign (size, 0.0);
        mask_ = size - 1;
        write_.store (0, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), 0.0);
        write_.store (0, std::memory_order_relaxed);
    }

    /// Audio thread. Real-time safe.
    void push (const double* samples, int numSamples) noexcept
    {
        if (buffer_.empty() || samples == nullptr || numSamples <= 0)
            return;

        std::size_t write = write_.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            buffer_[write] = samples[i];
            write = (write + 1) & mask_;
        }

        // Release, so a consumer that sees this index also sees the samples.
        write_.store (write, std::memory_order_release);
    }

    /// Message thread. Copies the most recent `numSamples` in chronological
    /// order. Returns false if the capture is not prepared or is too short.
    [[nodiscard]] bool readLatest (double* destination, int numSamples) const noexcept
    {
        if (buffer_.empty() || destination == nullptr || numSamples <= 0)
            return false;

        if (static_cast<std::size_t> (numSamples) > buffer_.size())
            return false;

        const std::size_t write = write_.load (std::memory_order_acquire);
        std::size_t read = (write + buffer_.size() - static_cast<std::size_t> (numSamples)) & mask_;

        for (int i = 0; i < numSamples; ++i)
        {
            destination[i] = buffer_[read];
            read = (read + 1) & mask_;
        }

        return true;
    }

    [[nodiscard]] std::size_t getSize() const noexcept { return buffer_.size(); }

private:
    std::vector<double> buffer_;
    std::size_t capacity_ { 0 };
    std::size_t mask_     { 0 };
    std::atomic<std::size_t> write_ { 0 };
};

/// Message-thread side: window, transform, and fold onto log-spaced bins.
class SpectrumAnalyser
{
public:
    /// Nothing below this reads as anything but the floor.
    static constexpr float kFloorDb = -96.0f;

    /// Allocates. Never call from the audio thread.
    ///
    /// `fftOrder` 11 is 2048 points: about 23 Hz of resolution at 48 kHz, which
    /// separates the harmonics of anything above a bass note and still redraws
    /// comfortably at 30 frames a second.
    void prepare (double sampleRate, int fftOrder, int numBins,
                  double lowHz = 20.0, double highHz = 20000.0)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        fftSize_    = static_cast<std::size_t> (1) << static_cast<std::size_t> (std::clamp (fftOrder, 6, 15));
        numBins_    = static_cast<std::size_t> (std::max (numBins, 2));

        // The top of the display cannot exceed Nyquist, and at 44.1 kHz it does.
        highHz_ = std::min (highHz, sampleRate_ * 0.5 * 0.98);
        lowHz_  = std::clamp (lowHz, 1.0, highHz_ * 0.5);

        window_.assign (fftSize_, 0.0);
        scratch_.assign (fftSize_, 0.0);
        magnitudes_.assign (numBins_, kFloorDb);
        peaks_.assign (numBins_, kFloorDb);
        edges_.assign (numBins_ + 1, 0.0);

        // Hann. Its coherent gain is exactly 0.5, which is why the scaling below
        // is 4/N and not 2/N -- get that wrong and every reading is 6 dB out,
        // which looks plausible enough to ship.
        double sum = 0.0;
        double sumOfSquares = 0.0;

        for (std::size_t i = 0; i < fftSize_; ++i)
        {
            window_[i] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i)
                                               / static_cast<double> (fftSize_));
            sum += window_[i];
            sumOfSquares += window_[i] * window_[i];
        }

        // Noise power bandwidth: mean(w^2) / mean(w)^2, which is exactly 1.5 for
        // Hann. Summing power across a display bin collects the whole main lobe,
        // and a Hann main lobe carries half again as much power as its peak bin
        // -- so without this every reading is 1.76 dB high. Measured before the
        // correction: a full-scale sine read +1.76 dBFS, which looks close
        // enough to right to go unnoticed.
        //
        // Computed from the window rather than written as 1.5, so it stays
        // correct if the window ever changes.
        const double mean = sum / static_cast<double> (fftSize_);
        const double meanSquare = sumOfSquares / static_cast<double> (fftSize_);
        noisePowerBandwidth_ = mean > 0.0 ? meanSquare / (mean * mean) : 1.0;

        // Log-spaced edges: an octave takes the same width wherever it sits,
        // which is the only way a spectrum reads like music rather than like a
        // graph with everything crammed into the right-hand third.
        const double ratio = std::log (highHz_ / lowHz_);
        for (std::size_t i = 0; i <= numBins_; ++i)
            edges_[i] = lowHz_ * std::exp (ratio * static_cast<double> (i)
                                           / static_cast<double> (numBins_));

        reset();
    }

    void reset() noexcept
    {
        std::fill (magnitudes_.begin(), magnitudes_.end(), kFloorDb);
        std::fill (peaks_.begin(), peaks_.end(), kFloorDb);
    }

    /// How fast a falling bin falls, and how fast the peak hold decays, in dB
    /// per frame. Rises are instant: a spectrum that ramps up to a transient
    /// hides the transient.
    void setBallistics (float fallDbPerFrame, float peakFallDbPerFrame) noexcept
    {
        fallDb_ = std::max (fallDbPerFrame, 0.0f);
        peakFallDb_ = std::max (peakFallDbPerFrame, 0.0f);
    }

    [[nodiscard]] std::size_t getFftSize() const noexcept { return fftSize_; }
    [[nodiscard]] std::size_t getNumBins() const noexcept { return numBins_; }

    /// Centre frequency of a display bin, for drawing the axis.
    [[nodiscard]] double getBinFrequency (std::size_t index) const noexcept
    {
        if (index + 1 >= edges_.size())
            return highHz_;
        return std::sqrt (edges_[index] * edges_[index + 1]);
    }

    [[nodiscard]] const std::vector<float>& getMagnitudesDb() const noexcept { return magnitudes_; }
    [[nodiscard]] const std::vector<float>& getPeaksDb()      const noexcept { return peaks_; }

    /// Pulls the latest window out of a capture and folds it onto the bins.
    /// Returns false when there was nothing to read.
    bool update (const SpectrumCapture& capture)
    {
        if (fftSize_ == 0 || ! capture.readLatest (scratch_.data(), static_cast<int> (fftSize_)))
            return false;

        Spectrum spectrum (fftSize_);
        for (std::size_t i = 0; i < fftSize_; ++i)
            spectrum[i] = Complex { scratch_[i] * window_[i], 0.0 };

        fft (spectrum);

        // 2/N turns a one-sided bin magnitude into the amplitude of the sine
        // that made it; the further 1/0.5 undoes the Hann window's coherent
        // gain. A full-scale sine then reads 0 dBFS, which is what a user
        // expects a full-scale sine to read.
        const double scale = 4.0 / static_cast<double> (fftSize_);
        const double binWidth = sampleRate_ / static_cast<double> (fftSize_);
        const std::size_t usable = fftSize_ / 2;

        for (std::size_t bin = 0; bin < numBins_; ++bin)
        {
            const double lower = edges_[bin];
            const double upper = edges_[bin + 1];

            auto first = static_cast<std::size_t> (std::ceil (lower / binWidth));
            auto last  = static_cast<std::size_t> (std::floor (upper / binWidth));

            first = std::max<std::size_t> (first, 1);
            last  = std::min (last, usable - 1);

            // When the display bin is narrower than one FFT bin -- which it is
            // for the bottom two octaves, because log-spaced bins are a few Hz
            // wide at 40 Hz while a 2048-point transform at 48 kHz resolves 23
            // -- widen to the three bins around its centre. That is a real
            // resolution limit, not a bug to code around: several display bins
            // necessarily share one transform bin down there, and a tone reads
            // as a short plateau rather than a spike.
            //
            // Widening rather than picking one bin is what keeps the *level*
            // right. Snapping to the nearest bin read a 100 Hz tone 2.0 dB low,
            // and interpolating between neighbours cannot do better than the
            // neighbours themselves -- both endpoints sit below a peak that
            // falls between them, so it still read 1.8 dB low.
            if (first > last)
            {
                const auto centre = static_cast<std::ptrdiff_t> (
                    std::llround (std::sqrt (lower * upper) / binWidth));

                first = static_cast<std::size_t> (std::clamp<std::ptrdiff_t> (
                    centre - 1, 1, static_cast<std::ptrdiff_t> (usable) - 1));
                last = static_cast<std::size_t> (std::clamp<std::ptrdiff_t> (
                    centre + 1, 1, static_cast<std::ptrdiff_t> (usable) - 1));
            }

            // Overlap each display bin into its neighbours by one transform bin.
            //
            // Without it a tone whose main lobe straddles a display-bin edge has
            // its power split between the two, and both read low. Sweeping
            // 900 Hz to 1100 Hz showed the error was periodic and landed exactly
            // where the bin index increments: mostly within 0.2 dB, dropping to
            // -2.8 dB at the edges. A Hann lobe is only about two bins wide, so
            // one bin of overlap is enough to keep it whole, and the cost is
            // that broadband content is counted twice at the seams -- invisible
            // on a display, unlike a 3 dB notch that moves with the note.
            first = first > 1 ? first - 1 : 1;
            last  = std::min (last + 1, usable - 1);

            // Summed as power, not taken as a maximum. A tone rarely lands on a
            // bin centre, and summing recovers the energy the window spread
            // either side of it instead of reading up to 1.4 dB low depending on
            // where the tone happened to fall.
            double power = 0.0;

            for (std::size_t k = first; k <= last; ++k)
            {
                const double magnitude = std::abs (spectrum[k]) * scale;
                power += magnitude * magnitude;
            }

            // A Hann main lobe carries half again as much power as its peak bin,
            // so the sum has to be divided by the window's noise power bandwidth
            // or every reading is 1.76 dB high.
            power /= noisePowerBandwidth_;

            const auto db = static_cast<float> (gainToDb (std::sqrt (power),
                                                          static_cast<double> (kFloorDb)));

            // Instant rise, damped fall.
            magnitudes_[bin] = db > magnitudes_[bin] ? db
                                                     : std::max (db, magnitudes_[bin] - fallDb_);

            peaks_[bin] = magnitudes_[bin] > peaks_[bin]
                        ? magnitudes_[bin]
                        : std::max (magnitudes_[bin], peaks_[bin] - peakFallDb_);
        }

        return true;
    }

private:
    double sampleRate_ { 44100.0 };
    double lowHz_      { 20.0 };
    double highHz_     { 20000.0 };

    std::size_t fftSize_ { 0 };
    std::size_t numBins_ { 0 };

    std::vector<double> window_;
    std::vector<double> scratch_;
    std::vector<double> edges_;

    std::vector<float> magnitudes_;
    std::vector<float> peaks_;

    double noisePowerBandwidth_ { 1.0 };

    float fallDb_     { 1.6f };
    float peakFallDb_ { 0.35f };
};

} // namespace tezla::dsp
