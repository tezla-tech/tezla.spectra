// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The minimum of the last N samples, in amortised constant time.
//
// This is the stage that turns a look-ahead limiter's ceiling from a hope into
// a theorem. On its own it says nothing interesting; combined with a smoothing
// kernel supported on the same window it gives the property the whole plugin
// rests on -- the smoothed gain is provably never above the gain the sample
// being processed requires, so the ceiling cannot be exceeded at all.
//
// The argument, in one line: the minimum taken at n-k covers a window that
// contains n whenever k is inside the kernel's support, so every term of the
// weighted average is already below the gain at n, and a convex combination of
// things below g[n] is below g[n]. Measured, that construction overshoots by
// one ULP; smoothing without this stage overshoots by 0.86 against a 0.5
// ceiling, and a one-pole attack/release by 1.91.
//
// Implemented as a monotonic deque rather than by scanning the window. A value
// can never again be the minimum once a later, smaller value has arrived, so
// the deque holds only the values that could still win, in increasing order.
// Each sample is pushed once and popped at most once: amortised O(1), against
// the O(log N) that Hamalainen cites from Pitas and the O(N) of the obvious
// implementation.
//
// A ring of the raw history is kept alongside it, which the deque alone does
// not need -- it is there so the window length can *grow* mid-stream. Growing
// brings samples back into range that the deque had already discarded from its
// front, and without the history there would be no way to recover them. That
// matters because the window length is set from the attack and hold controls,
// which a user turns while audio is running, and a window that is briefly
// narrower than it claims is exactly the case that breaks the guarantee.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tezla::dsp {

class RunningMinimum
{
public:
    /// Allocates for the longest window that will ever be asked for.
    ///
    /// The only call here that touches memory. setLength() works inside this
    /// capacity, so the audio thread can move the window without allocating.
    void prepare (int maximumLength)
    {
        capacity_ = std::max (1, maximumLength);

        history_.assign (static_cast<std::size_t> (capacity_), 0.0);

        // One more than the window: a push happens before the sample that has
        // just fallen out of the far end is evicted, so the deque is briefly
        // one longer than the window it represents.
        dequeCapacity_ = capacity_ + 1;
        deque_.assign (static_cast<std::size_t> (dequeCapacity_), Entry {});

        length_ = capacity_;
        reset();
    }

    /// Empties the window and treats everything before now as `value`.
    ///
    /// Unity rather than zero by default: a limiter's gain starts at 1, and a
    /// window primed with zeros would mute the first N samples of every
    /// transport start.
    void reset (double value = 1.0) noexcept
    {
        std::fill (history_.begin(), history_.end(), value);

        position_ = 0;
        head_     = 0;
        count_    = 0;

        // One entry standing for the whole primed history.
        if (capacity_ > 0)
        {
            deque_[0] = Entry { 0, value };
            count_    = 1;
        }
    }

    /// The window, in samples. Clamped to what prepare() allocated.
    ///
    /// Rebuilds from the retained history, so growing the window is exact
    /// immediately rather than after it has refilled. Costs O(length) once per
    /// change and nothing per sample.
    void setLength (int length) noexcept
    {
        const int wanted = std::clamp (length, 1, capacity_);

        if (wanted == length_)
            return;

        length_ = wanted;
        rebuild();
    }

    [[nodiscard]] int getLength() const noexcept { return length_; }
    [[nodiscard]] int getCapacity() const noexcept { return capacity_; }

    /// Adds a sample and returns the minimum of the window ending at it.
    [[nodiscard]] double process (double x) noexcept
    {
        ++position_;

        history_[static_cast<std::size_t> (position_ % capacity_)] = x;

        // Anything at least as large as the new sample can never be the
        // minimum again: the new one is smaller and outlives it.
        while (count_ > 0 && back().value >= x)
            --count_;

        pushBack (Entry { position_, x });

        // And anything older than the window has fallen out of it.
        while (count_ > 0 && front().index <= position_ - length_)
            popFront();

        return front().value;
    }

    /// The current minimum without adding anything.
    [[nodiscard]] double get() const noexcept
    {
        return count_ > 0 ? front().value : 1.0;
    }

private:
    struct Entry
    {
        std::int64_t index {};
        double       value {};
    };

    [[nodiscard]] const Entry& front() const noexcept
    {
        return deque_[static_cast<std::size_t> (head_)];
    }

    [[nodiscard]] const Entry& back() const noexcept
    {
        const int index = (head_ + count_ - 1) % dequeCapacity_;
        return deque_[static_cast<std::size_t> (index)];
    }

    void pushBack (Entry entry) noexcept
    {
        const int index = (head_ + count_) % dequeCapacity_;
        deque_[static_cast<std::size_t> (index)] = entry;
        ++count_;
    }

    void popFront() noexcept
    {
        head_ = (head_ + 1) % dequeCapacity_;
        --count_;
    }

    /// Rebuilds the deque from the retained history over the current window.
    void rebuild() noexcept
    {
        head_  = 0;
        count_ = 0;

        const std::int64_t oldest = std::max<std::int64_t> (position_ - length_ + 1, 0);

        for (std::int64_t i = oldest; i <= position_; ++i)
        {
            const double value = history_[static_cast<std::size_t> (i % capacity_)];

            while (count_ > 0 && back().value >= value)
                --count_;

            pushBack (Entry { i, value });
        }

        if (count_ == 0)
            pushBack (Entry { position_, 1.0 });
    }

    std::vector<double> history_;
    std::vector<Entry>  deque_;

    int capacity_      { 1 };
    int dequeCapacity_ { 1 };
    int length_        { 1 };

    /// Absolute sample position, so the window test is a comparison rather
    /// than a modular subtraction that has to worry about wrapping.
    std::int64_t position_ { 0 };

    int head_  { 0 };
    int count_ { 0 };
};

} // namespace tezla::dsp
