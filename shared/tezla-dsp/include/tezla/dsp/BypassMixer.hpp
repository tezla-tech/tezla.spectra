#pragma once

// Latency-matched, click-free bypass.
//
// A plugin that reports latency to the host must delay its bypassed signal by
// exactly that latency. Otherwise the host's delay compensation shifts the
// processed path but not the bypassed one, the bypassed signal arrives early,
// and every A/B comparison is a lie: the bypassed side sounds tighter for
// reasons that have nothing to do with the plugin.
//
// Getting it wrong is worse than not compensating at all, and this is not
// hypothetical -- it shipped in both plugins. Each hand-rolled a ring buffer in
// its JUCE layer that wrote the whole block and then read back starting from
// the same index it had begun writing at, which returns the samples just
// written. The delay was zero. Switching bypass therefore jumped the signal by
// the full reported latency, and a 10 ms crossfade between two copies of the
// same audio 69 samples apart is a comb filter sweeping past. It sounded like
// an effect, because it was one.
//
// It lives here rather than in either plugin's JUCE layer for two reasons: so
// every plugin gets the same behaviour, and so it can be tested at all. There
// is no JUCE in the test runner.

#include <algorithm>
#include <cmath>
#include <vector>

namespace tezla::dsp {

/// Crossfades between a processed signal and a latency-matched copy of the dry
/// input.
class BypassMixer
{
public:
    /// How long the crossfade takes. Long enough not to click, short enough to
    /// still feel like a switch rather than a fade.
    static constexpr double kFadeSeconds = 0.010;

    /// Allocates. Never call from the audio thread.
    ///
    /// `latencySamples` is what the host has been told, and is the delay the dry
    /// path is given.
    void prepare (double sampleRate, int latencySamples, int numChannels)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        latency_    = std::max (latencySamples, 0);
        channels_   = std::max (numChannels, 1);

        // One position per sample of delay, plus one to write into. Any longer
        // and the arithmetic below would have to carry a second pointer; any
        // shorter and the write would clobber a sample still owed to the output.
        length_ = static_cast<std::size_t> (latency_) + 1;

        lines_.assign (static_cast<std::size_t> (channels_), std::vector<double> (length_, 0.0));

        fadeStep_ = 1.0 / std::max (1.0, kFadeSeconds * sampleRate_);

        reset (bypassed_);
    }

    /// Clears the delay and jumps the crossfade to its end state, so a transport
    /// restart does not fade in from wherever the fade happened to be.
    void reset (bool bypassed) noexcept
    {
        for (auto& line : lines_)
            std::fill (line.begin(), line.end(), 0.0);

        write_    = 0;
        bypassed_ = bypassed;
        mix_      = bypassed ? 1.0 : 0.0;
    }

    void setBypassed (bool shouldBypass) noexcept { bypassed_ = shouldBypass; }

    [[nodiscard]] bool isBypassed() const noexcept { return bypassed_; }

    [[nodiscard]] int getLatencySamples() const noexcept { return latency_; }

    /// `processed` holds the plugin's output and is overwritten with the mix.
    /// `dry` holds the original input -- the same samples that went in, before
    /// anything touched them.
    void process (double* const* processed, const double* const* dry,
                  int numChannels, int numSamples) noexcept
    {
        if (numSamples <= 0 || lines_.empty())
            return;

        const int active = std::min (numChannels, channels_);
        const double target = bypassed_ ? 1.0 : 0.0;

        // Samples outer, channels inner: one crossfade shared by every channel.
        // Advancing it per channel would fade at twice the rate in stereo.
        std::size_t write = write_;
        double mix = mix_;

        for (int i = 0; i < numSamples; ++i)
        {
            mix = target > mix ? std::min (target, mix + fadeStep_)
                               : std::max (target, mix - fadeStep_);

            // Write, then read `latency` positions back. In that order it is
            // correct for a latency of zero as well, where the read lands on the
            // sample just written -- which is what no delay means.
            const std::size_t read = write + length_ - static_cast<std::size_t> (latency_) >= length_
                                   ? write - static_cast<std::size_t> (latency_)
                                   : write + length_ - static_cast<std::size_t> (latency_);

            for (int channel = 0; channel < active; ++channel)
            {
                auto& line = lines_[static_cast<std::size_t> (channel)];

                line[write] = dry[channel][i];
                const double delayed = line[read];

                // At either end this is exact, not merely close. A bypass that
                // leaks a thousandth of the processed signal is not a bypass,
                // and neither is one that leaks the dry path into the sound.
                if (mix >= 1.0)
                    processed[channel][i] = delayed;
                else if (mix > 0.0)
                    processed[channel][i] = processed[channel][i] * (1.0 - mix) + delayed * mix;
            }

            write = write + 1 < length_ ? write + 1 : 0;
        }

        write_ = write;
        mix_   = mix;
    }

private:
    double sampleRate_ { 44100.0 };
    int    latency_    { 0 };
    int    channels_   { 1 };

    std::vector<std::vector<double>> lines_;
    std::size_t length_ { 1 };
    std::size_t write_  { 0 };

    bool   bypassed_ { false };
    double mix_      { 0.0 };
    double fadeStep_ { 1.0 };
};

} // namespace tezla::dsp
