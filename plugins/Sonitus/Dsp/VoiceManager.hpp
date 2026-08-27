#pragma once

// Who plays what: polyphony, stealing, and the mono modes.
//
// Framework-free, so the whole instrument can be played from a test without a
// DAW anywhere near it. The JUCE layer's entire job is to turn MIDI into
// `noteOn` and `noteOff` calls.
//
// ---------------------------------------------------------------------------
// Stealing, and why the policy is worth thinking about here
// ---------------------------------------------------------------------------
//
// Eight voices of seven-oscillator unison is a hundred and twelve oscillators
// before anything else in the chain. On a bass part that is rarely the
// constraint -- most of what this instrument plays is one note at a time -- but
// a rolled chord over a long release will run out, and what happens then is
// audible.
//
// The order is: a free voice, then the **quietest** released voice, then the
// oldest held one. Quietest rather than oldest among the released ones, because
// a release that has fallen to -40 dB is inaudible when it disappears and one
// that has just started is not. Oldest among the held ones because there is
// nothing better to go on, and because the player is still holding the others.
//
// ---------------------------------------------------------------------------
// Mono, legato and glide
// ---------------------------------------------------------------------------
//
// The classic reese is monophonic, and mono is not just "polyphony of one":
// releasing the upper of two held notes has to fall back to the lower one
// rather than stopping. So held notes go on a stack, and a note-off pops it and
// re-pitches the voice to whatever is underneath.
//
// **Legato differs from mono in one thing only**: whether a new note while
// another is held restarts the envelope. In mono it does; in legato it does
// not, and the voice simply glides. That is the difference between a bass line
// that articulates every note and one that slides between them, and it is worth
// a mode rather than a compromise.
//
// Glide is **constant-time**: an octave and a semitone take the same time to
// traverse. The alternative, constant-rate, makes a wide interval crawl and is
// almost never what anyone means by portamento. It is done in cents, because a
// glide is a pitch movement rather than a frequency one -- gliding linearly in
// hertz spends most of its time at the top of the interval.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <tezla/dsp/Tuning.hpp>

#include "SonitusVoice.hpp"

namespace tezla::sonitus {

using dsp::Tuning;

/// How the keyboard behaves.
///
/// **Append-only** -- a choice parameter stores an index. CLAUDE.md section 8.
enum class KeyboardMode
{
    poly = 0,
    mono,       ///< one voice, envelope retriggered on every note
    legato,     ///< one voice, envelope retriggered only from silence

    count
};

class VoiceManager
{
public:
    static constexpr int kMaxVoices = 8;

    /// How many notes can be held at once in mono mode before the oldest is
    /// forgotten. Ten fingers, and nobody is holding more than that on purpose.
    static constexpr int kHeldStackSize = 16;

    /// The widest glide the control reaches, in seconds.
    static constexpr double kMaximumGlideSeconds = 4.0;

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

        // Each voice gets its own seed, so the note-random source and the
        // unison phase scatter differ between them -- see Voice::prepare.
        for (int index = 0; index < kMaxVoices; ++index)
            voices_[static_cast<std::size_t> (index)]
              .prepare (sampleRate_, static_cast<std::uint64_t> (index) + 1ull);

        reset();
    }

    void reset() noexcept
    {
        for (auto& voice : voices_)
            voice.reset();

        heldCount_ = 0;
        sustaining_ = false;
        sustainedCount_ = 0;
        glideCents_ = 0.0;
        glideTargetCents_ = 0.0;
        monoNote_ = -1;
    }

    // -----------------------------------------------------------------------
    // Controls
    // -----------------------------------------------------------------------

    void setMode (KeyboardMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] KeyboardMode getMode() const noexcept { return mode_; }

    /// How many voices polyphony may use, 1 to 8.
    void setPolyphony (int voices) noexcept
    {
        polyphony_ = std::clamp (voices, 1, kMaxVoices);
    }

    [[nodiscard]] int getPolyphony() const noexcept { return polyphony_; }

    /// Portamento time. 0 is off and is bit-exactly off: the glide state is
    /// skipped rather than run with a coefficient of one.
    void setGlideSeconds (double seconds) noexcept
    {
        glideSeconds_ = std::clamp (seconds, 0.0, kMaximumGlideSeconds);
    }

    [[nodiscard]] double getGlideSeconds() const noexcept { return glideSeconds_; }

    /// Pitch bend, in semitones. The whole instrument bends, glide included.
    void setBendSemitones (double semitones) noexcept
    {
        bendSemitones_ = std::clamp (semitones, -48.0, 48.0);
    }

    Tuning& tuning() noexcept { return tuning_; }
    [[nodiscard]] const Tuning& tuning() const noexcept { return tuning_; }

    // -----------------------------------------------------------------------
    // Playing
    // -----------------------------------------------------------------------

    void noteOn (int note, double velocity)
    {
        if (note < 0 || note > 127)
            return;

        const double hz = tuning_.frequencyFor (note);

        // An unmapped key plays nothing. A keyboard map is allowed to leave
        // holes and this is what a hole sounds like.
        if (hz <= 0.0)
            return;

        pushHeld (note);

        if (mode_ == KeyboardMode::poly)
        {
            Voice& voice = claimVoice();

            voice.noteOn (note, hz, velocity, false);
            return;
        }

        Voice& voice = voices_[0];

        // Legato only retriggers from silence; mono always does.
        const bool alreadySounding = voice.isActive() && voice.isHeld();
        const bool retrigger = mode_ == KeyboardMode::legato && alreadySounding;

        monoNote_ = note;
        aimGlide (note, ! alreadySounding);

        voice.noteOn (note, currentGlideFrequency (note), velocity, retrigger);
    }

    void noteOff (int note)
    {
        if (note < 0 || note > 127)
            return;

        removeHeld (note);

        if (sustaining_)
        {
            rememberSustained (note);
            return;
        }

        releaseNote (note);
    }

    /// The sustain pedal. Notes released while it is down keep sounding until
    /// it comes up.
    void setSustain (bool down)
    {
        if (down == sustaining_)
            return;

        sustaining_ = down;

        if (down)
            return;

        for (int index = 0; index < sustainedCount_; ++index)
            releaseNote (sustained_[static_cast<std::size_t> (index)]);

        sustainedCount_ = 0;
    }

    /// Everything off, now. What a panic message and a transport stop both do.
    void allNotesOff() noexcept
    {
        for (auto& voice : voices_)
            voice.kill();

        heldCount_ = 0;
        sustainedCount_ = 0;
        monoNote_ = -1;
    }

    [[nodiscard]] int activeVoiceCount() const noexcept
    {
        int count = 0;

        for (const auto& voice : voices_)
            if (voice.isActive())
                ++count;

        return count;
    }

    /// The note the comb should track. The most recently started sounding
    /// voice, because a comb is a global stage and has to pick one.
    [[nodiscard]] double trackedFrequency() const noexcept
    {
        double frequency = 0.0;
        long long youngest = -1;

        for (const auto& voice : voices_)
            if (voice.isActive() && (youngest < 0 || voice.getAge() < youngest))
            {
                youngest = voice.getAge();
                frequency = voice.getFrequency();
            }

        return frequency;
    }

    // -----------------------------------------------------------------------
    // Running
    // -----------------------------------------------------------------------

    /// Advances the glide by one control chunk and re-pitches the mono voice.
    ///
    /// Counted in samples so the glide takes the same time however the host
    /// cuts the callback up -- the same rule as everything else here.
    void advanceGlide (int samples) noexcept
    {
        if (mode_ == KeyboardMode::poly || monoNote_ < 0)
            return;

        if (dsp::isExactlyZero (glideSeconds_) || glideCents_ == glideTargetCents_)
        {
            glideCents_ = glideTargetCents_;
        }
        else
        {
            // Constant time: the remaining distance is covered over what is
            // left of the glide, whatever that distance is.
            const double total = glideSeconds_ * sampleRate_;
            const double step = (glideTargetCents_ - glideStartCents_) * (samples / total);

            glideCents_ += step;

            if ((step > 0.0 && glideCents_ > glideTargetCents_)
                  || (step < 0.0 && glideCents_ < glideTargetCents_))
                glideCents_ = glideTargetCents_;
        }

        voices_[0].setFrequency (currentGlideFrequency (monoNote_));
    }

    /// Hands every sounding voice its controls for this chunk.
    ///
    /// Pitch bend is folded into the parameters rather than into the matrix,
    /// because it is not modulation: a bend applies to every voice equally and
    /// has no slot to occupy.
    void applyControls (const VoiceParameters& parameters, const GlobalSources& global)
    {
        VoiceParameters bent = parameters;

        bent.centsA += bendSemitones_ * 100.0;
        bent.centsB += bendSemitones_ * 100.0;

        for (auto& voice : voices_)
            if (voice.isActive())
                voice.applyControls (bent, global);
    }

    /// Sums every sounding voice into one stereo sample.
    void process (double& left, double& right) noexcept
    {
        for (auto& voice : voices_)
            voice.process (left, right);
    }

    [[nodiscard]] Voice& voice (int index) noexcept
    {
        return voices_[static_cast<std::size_t> (std::clamp (index, 0, kMaxVoices - 1))];
    }

private:
    /// The voice a new note should take, by the policy in the header.
    [[nodiscard]] Voice& claimVoice() noexcept
    {
        for (int index = 0; index < polyphony_; ++index)
            if (! voices_[static_cast<std::size_t> (index)].isActive())
                return voices_[static_cast<std::size_t> (index)];

        // Nothing free. Prefer the quietest released voice -- one that has
        // fallen to -40 dB is inaudible when it disappears and one that has
        // just started is not.
        int best = -1;
        double quietest = 1.0e9;

        for (int index = 0; index < polyphony_; ++index)
        {
            Voice& candidate = voices_[static_cast<std::size_t> (index)];

            if (candidate.isHeld())
                continue;

            if (candidate.getAmpLevel() < quietest)
            {
                quietest = candidate.getAmpLevel();
                best = index;
            }
        }

        if (best < 0)
        {
            // Every voice is still held. The oldest is the only thing left to
            // go on.
            long long oldest = -1;

            for (int index = 0; index < polyphony_; ++index)
                if (voices_[static_cast<std::size_t> (index)].getAge() > oldest)
                {
                    oldest = voices_[static_cast<std::size_t> (index)].getAge();
                    best = index;
                }
        }

        Voice& stolen = voices_[static_cast<std::size_t> (std::max (best, 0))];

        stolen.kill();

        return stolen;
    }

    void releaseNote (int note) noexcept
    {
        if (mode_ != KeyboardMode::poly)
        {
            // Mono: fall back to whatever is still held underneath.
            if (heldCount_ > 0)
            {
                const int previous = held_[static_cast<std::size_t> (heldCount_ - 1)];

                monoNote_ = previous;
                aimGlide (previous, false);

                // And **apply** it, rather than waiting for the next
                // advanceGlide. With the glide off, aiming and arriving are the
                // same thing, and a caller that has not rendered a chunk yet
                // would otherwise read the released note's pitch. The note-on
                // path is symmetric -- it passes currentGlideFrequency() to
                // Voice::noteOn -- and this is the release side of it.
                voices_[0].setFrequency (currentGlideFrequency (previous));
                return;
            }

            monoNote_ = -1;
            voices_[0].noteOff();
            return;
        }

        for (auto& voice : voices_)
            if (voice.isHeld() && voice.getNote() == note)
                voice.noteOff();
    }

    void pushHeld (int note) noexcept
    {
        removeHeld (note);

        if (heldCount_ >= kHeldStackSize)
        {
            // Drop the oldest rather than refusing the newest: the note being
            // played now is the one that matters.
            for (int index = 1; index < heldCount_; ++index)
                held_[static_cast<std::size_t> (index - 1)] = held_[static_cast<std::size_t> (index)];

            --heldCount_;
        }

        held_[static_cast<std::size_t> (heldCount_)] = note;
        ++heldCount_;
    }

    void removeHeld (int note) noexcept
    {
        int write = 0;

        for (int read = 0; read < heldCount_; ++read)
            if (held_[static_cast<std::size_t> (read)] != note)
                held_[static_cast<std::size_t> (write++)] = held_[static_cast<std::size_t> (read)];

        heldCount_ = write;
    }

    void rememberSustained (int note) noexcept
    {
        for (int index = 0; index < sustainedCount_; ++index)
            if (sustained_[static_cast<std::size_t> (index)] == note)
                return;

        if (sustainedCount_ >= kHeldStackSize)
            return;

        sustained_[static_cast<std::size_t> (sustainedCount_)] = note;
        ++sustainedCount_;
    }

    /// Points the glide at a new note. `snap` jumps rather than glides, which
    /// is what starting from silence must do -- otherwise the first note of a
    /// phrase slides in from wherever the last one ended.
    void aimGlide (int note, bool snap) noexcept
    {
        glideTargetCents_ = centsOf (note);

        if (snap || dsp::isExactlyZero (glideSeconds_))
        {
            glideCents_ = glideTargetCents_;
            glideStartCents_ = glideTargetCents_;
            return;
        }

        glideStartCents_ = glideCents_;
    }

    /// A note as cents above the tuning's reference, so the glide is a pitch
    /// movement rather than a frequency one.
    [[nodiscard]] double centsOf (int note) const noexcept
    {
        const double hz = tuning_.frequencyFor (note);

        return hz > 0.0 ? 1200.0 * std::log2 (hz / kGlideReferenceHz) : 0.0;
    }

    [[nodiscard]] double currentGlideFrequency (int note) const noexcept
    {
        if (mode_ == KeyboardMode::poly || dsp::isExactlyZero (glideSeconds_))
            return tuning_.frequencyFor (note);

        return kGlideReferenceHz * std::pow (2.0, glideCents_ / 1200.0);
    }

    /// The pitch the glide's cents are measured from. Any fixed value works;
    /// 440 keeps the numbers readable in a debugger.
    static constexpr double kGlideReferenceHz = 440.0;

    double sampleRate_ { 48000.0 };

    Tuning tuning_;
    std::array<Voice, kMaxVoices> voices_ {};

    KeyboardMode mode_ { KeyboardMode::poly };
    int polyphony_ { kMaxVoices };

    double glideSeconds_ { 0.0 };
    double glideCents_ { 0.0 };
    double glideStartCents_ { 0.0 };
    double glideTargetCents_ { 0.0 };
    int monoNote_ { -1 };

    double bendSemitones_ { 0.0 };

    std::array<int, kHeldStackSize> held_ {};
    int heldCount_ { 0 };

    bool sustaining_ { false };
    std::array<int, kHeldStackSize> sustained_ {};
    int sustainedCount_ { 0 };
};

} // namespace tezla::sonitus
