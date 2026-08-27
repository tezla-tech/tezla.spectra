#pragma once

// Transpectus -- measurement only. It never changes the audio.
//
// Every other plugin in this suite has an opinion about the sound. This one has
// none: `process` copies its input to its output byte for byte and reports zero
// latency, and a test asserts both. A meter that coloured the signal would be
// worse than no meter, because you would be mixing into it.
//
// What it measures, and where each number comes from:
//
//   LUFS M / S / I     ITU-R BS.1770-5, via LoudnessMeter
//   dBTP               the same Recommendation's Annex 2, via TruePeakDetector
//   PLR / PSR          dBTP minus loudness. One subtraction, and the number
//                      nobody meters
//   correlation        full band and per band, via StereoAnalyser
//   spectrum           SpectrumCapture, folded by the editor's SpectrumAnalyser
//
// The spectrum is captured rather than analysed here: the FFT belongs on the
// message thread at frame rate, not on the audio thread at block rate, and
// SpectrumCapture is the lock-free hand-off the other two plugins already use.

#include <array>
#include <atomic>
#include <vector>

#include <tezla/dsp/Correlation.hpp>
#include <tezla/dsp/LoudnessMeter.hpp>
#include <tezla/dsp/SpectrumAnalyser.hpp>
#include <tezla/dsp/TruePeakDetector.hpp>

namespace tezla::transpectus
{

/// What a streaming platform does to a master.
///
/// Two facts per platform, and the second one is the half that gets forgotten:
/// several services only ever turn a loud master **down**. A quiet master on
/// YouTube simply plays quiet -- so "you are 6 dB under the target" is advice
/// there and a correction on Spotify.
struct LoudnessTarget
{
    const char* name;
    double lufs;
    bool   boostsQuietMaterial;
};

/// Verified 2026-08-27. **Stored as data with a date because these change**, and
/// a hardcoded number that silently goes stale is worse than no number at all.
/// The industry converged on this zone via AES TD1008.
inline constexpr LoudnessTarget kLoudnessTargets[]
{
    { "Spotify",      -14.0, true  },
    { "Apple Music",  -16.0, true  },
    { "YouTube",      -14.0, false },
    { "Tidal",        -14.0, false },
    { "Amazon Music", -14.0, false },
    { "SoundCloud",   -14.0, true  },
    { "Deezer",       -15.0, true  },
    { "EBU R128",     -23.0, true  },
};

inline constexpr int kNumLoudnessTargets = static_cast<int> (std::size (kLoudnessTargets));

/// The ceiling every one of them expects, and the one Capstone defaults near.
inline constexpr double kDeliveryTruePeakDb = -1.0;

struct Parameters
{
    /// Which target the readout is computed against. An index into the table
    /// above, which is **append-only** for the same reason every choice list in
    /// this project is -- see CLAUDE.md section 8.
    int targetIndex { 0 };

    /// 1 = the ITU filter, 16 = strict. Same control as Capstone's, and the
    /// same cost.
    dsp::TruePeakMode truePeak { dsp::TruePeakMode::Standard };

    /// Where the mono check happens, in Hz.
    double monoCheckHz { dsp::StereoAnalyser::kDefaultLowCrossoverHz };

    [[nodiscard]] bool operator== (const Parameters&) const = default;
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    /// How many display bins the spectrum is folded onto.
    static constexpr int kNumBins = 96;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setParameters (const Parameters& parameters);

    /// Measures. Does not touch `channels` -- see the header.
    void process (const double* const* channels, int numChannels, int numSamples) noexcept;

    // ---- readings, all safe to call from the message thread ------------------

    [[nodiscard]] double getMomentaryLufs()  const noexcept { return loudness_.getMomentaryLufs(); }
    [[nodiscard]] double getShortTermLufs()  const noexcept { return loudness_.getShortTermLufs(); }
    [[nodiscard]] double getIntegratedLufs() const noexcept { return loudness_.getIntegratedLufs(); }

    /// The highest true peak since the last reset, in dBTP.
    [[nodiscard]] double getTruePeakDb() const noexcept
    {
        return truePeakDb_.load (std::memory_order_relaxed);
    }

    /// Peak to loudness: how much transient survived the whole programme.
    /// Below about 5 dB the transients are gone.
    [[nodiscard]] double getPlr() const noexcept;

    /// The same against short-term loudness, which moves bar to bar. The one
    /// you watch while working.
    [[nodiscard]] double getPsr() const noexcept;

    [[nodiscard]] double getCorrelation() const noexcept { return stereo_.getCorrelation(); }

    [[nodiscard]] double getBandCorrelation (dsp::StereoAnalyser::Band band) const noexcept
    {
        return stereo_.getBandCorrelation (band);
    }

    [[nodiscard]] bool isLowBandMonoSafe() const noexcept { return stereo_.isLowBandMonoSafe(); }

    /// What the selected platform will do to this master, in dB. Positive means
    /// it will be turned **down** by that much; negative means it will be turned
    /// up, and zero means nothing happens -- which is also the answer for a
    /// quiet master on a platform that does not boost.
    [[nodiscard]] double getTargetDeltaDb() const noexcept;

    [[nodiscard]] const LoudnessTarget& getTarget() const noexcept;

    /// Clears the integration, the true-peak hold and the loudness range --
    /// the "restart measurement" button, which does not disturb the filters.
    void resetMeasurement() noexcept;

    /// For the editor's FFT. Mono sum, which is what a balance is read from.
    [[nodiscard]] const dsp::SpectrumCapture& getSpectrumCapture() const noexcept
    {
        return capture_;
    }

    /// For the editor's goniometer. The sample pairs the correlation number
    /// summarises -- a wide mix and a hard-panned pair can read the same r,
    /// and only the picture says which one you have.
    [[nodiscard]] const dsp::StereoScope& getStereoScope() const noexcept
    {
        return scope_;
    }

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }

private:
    double sampleRate_  { 48000.0 };
    int    numChannels_ { 2 };

    Parameters parameters_;

    dsp::LoudnessMeter  loudness_;
    dsp::StereoAnalyser stereo_;
    dsp::SpectrumCapture capture_;
    dsp::StereoScope     scope_;

    std::array<dsp::TruePeakDetector, kMaxChannels> detectors_;

    /// Written by the audio thread, read by the editor.
    std::atomic<double> truePeakDb_ { -200.0 };

    /// The short-window true peak behind PSR, held with a slow fall so it reads
    /// as a bar rather than a flicker.
    std::atomic<double> shortTruePeakDb_ { -200.0 };

    /// Mono sum, so the spectrum shows a balance rather than one channel.
    std::vector<double> monoScratch_;
};

} // namespace tezla::transpectus
