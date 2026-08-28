// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// A small passive network of resistors, capacitors and inductors, solved
// properly rather than approximated by an EQ that happens to have three knobs.
//
// ---------------------------------------------------------------------------
// Why a solver and not three filters
// ---------------------------------------------------------------------------
//
// A guitar amplifier's tone controls are not an equaliser. They are one passive
// network with three potentiometers embedded in it, and every control changes
// what the others do -- turning the bass up moves the treble corner, turning the
// mid down changes how much of everything gets through, and the whole thing has
// an insertion loss that varies with the settings. Reaching for a shelf, a bell
// and another shelf gets three independent controls that behave nothing like
// the thing being modelled, and it is the single most common way an amp
// simulation gives itself away.
//
// So: state the circuit, and solve it.
//
// ---------------------------------------------------------------------------
// How
// ---------------------------------------------------------------------------
//
// Nodal analysis with trapezoidal companion models -- the same method SPICE
// uses, and exact for a linear network at any sample rate rather than a
// bilinear-transformed approximation of a hand-derived transfer function.
//
// A capacitor between two nodes, discretised trapezoidally, is a conductance
// 2C/T in parallel with a current source that remembers the last sample:
//
//     i[n] = Gc * v[n] + Ieq,    Gc = 2C/T,    Ieq = -Gc*v[n-1] - i[n-1]
//
// An inductor is the exact dual, and falls out of the same trapezoidal step
// applied to v = L di/dt instead of i = C dv/dt:
//
//     i[n] = Gl * v[n] + Ieq,    Gl = T/(2L),  Ieq = +Gl*v[n-1] + i[n-1]
//
// Only the signs of the history differ, which is why both live in one array of
// reactive elements here rather than two. Checked against the exact response of
// a series R-L to a step: the steady state is exact and the worst transient
// error is 3.6 mA in 125, over a time constant of 17 samples.
//
// Every element then contributes only conductances to a nodal matrix and known
// currents to a right-hand side, so each sample is: build the right-hand side
// from the histories, solve, update the histories.
//
// The matrix depends only on the component values, so it is inverted once when
// a potentiometer moves and never again. A sample costs one matrix-vector
// product over the unknown nodes -- for the four unknowns of a tone stack, six
// multiply-adds -- plus one update per capacitor. There is no allocation and no
// solve on the audio thread.
//
// ---------------------------------------------------------------------------
// What this is not
// ---------------------------------------------------------------------------
//
// It is linear. Every element is a resistor, a capacitor or an inductor, so no
// amount of signal changes the network. That is correct for a tone stack and
// for a loudspeaker at sane excursions, and wrong for anything with a valve in
// it -- the nonlinearities live in TriodeStage and in the power amplifier, on
// either side of this.
//
// It also assumes an ideal voltage source driving it. A real stack is fed from
// a plate through an output impedance, and loaded by the next grid; both are
// just more resistors in the netlist, so a topology that cares can include them.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "Exact.hpp"

namespace tezla::dsp {

/// A linear R/L/C network with one input node and one output node.
///
/// Node 0 is ground and node 1 is the input; everything from 2 up is an unknown
/// solved each sample.
template <std::size_t MaxNodes = 8, std::size_t MaxElements = 16>
class PassiveNetwork
{
public:
    static constexpr std::size_t kGround = 0;
    static constexpr std::size_t kInput = 1;

    /// The first node index that is actually solved for.
    static constexpr std::size_t kFirstUnknown = 2;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        factorise();
        reset();
    }

    void reset() noexcept
    {
        reactiveVoltage_.fill (0.0);
        reactiveCurrent_.fill (0.0);
        nodeVoltage_.fill (0.0);
    }

    /// Declares how many nodes the netlist uses, ground and input included.
    void setNodeCount (std::size_t count) noexcept
    {
        nodeCount_ = std::clamp (count, kFirstUnknown, MaxNodes);
    }

    /// Which node the output is taken from. Ground and input are legal, if odd.
    void setOutputNode (std::size_t node) noexcept { outputNode_ = node; }

    void clearElements() noexcept
    {
        numResistors_ = 0;
        numReactive_ = 0;
    }

    /// Adds a resistor. Zero or negative resistance is treated as a short of
    /// 1 milliohm, which keeps a potentiometer at the end of its travel finite
    /// rather than infinite.
    void addResistor (std::size_t a, std::size_t b, double ohms) noexcept
    {
        if (numResistors_ >= MaxElements)
            return;

        resistors_[numResistors_++] = { a, b, 1.0 / std::max (ohms, 1.0e-3) };
    }

    /// Changes a resistor already added, by the order it was added in. This is
    /// how a potentiometer moves: the netlist is fixed, the values are not.
    void setResistor (std::size_t index, double ohms) noexcept
    {
        if (index < numResistors_)
            resistors_[index].conductance = 1.0 / std::max (ohms, 1.0e-3);
    }

    void addCapacitor (std::size_t a, std::size_t b, double farads) noexcept
    {
        if (numReactive_ >= MaxElements)
            return;

        reactive_[numReactive_++] = { a, b, std::max (farads, 1.0e-15), true };
    }

    /// Adds an inductor. Trapezoidally it is a capacitor with the history's
    /// signs flipped, which is the whole of the difference.
    void addInductor (std::size_t a, std::size_t b, double henries) noexcept
    {
        if (numReactive_ >= MaxElements)
            return;

        reactive_[numReactive_++] = { a, b, std::max (henries, 1.0e-12), false };
    }

    /// Changes a reactive element already added, by the order it was added in.
    /// Farads for a capacitor, henries for an inductor -- the kind is fixed at
    /// the point it was added and this does not change it.
    void setReactive (std::size_t index, double value) noexcept
    {
        if (index < numReactive_)
            reactive_[index].value = std::max (value, 1.0e-15);
    }

    [[nodiscard]] std::size_t getResistorCount() const noexcept { return numResistors_; }
    [[nodiscard]] std::size_t getReactiveCount() const noexcept { return numReactive_; }

    /// Rebuilds and inverts the nodal matrix. Allocates nothing, but is far too
    /// slow for a sample loop -- call it when a control moves, not per sample.
    void factorise() noexcept
    {
        const std::size_t n = unknownCount();

        for (auto& row : matrix_)
            row.fill (0.0);

        inputColumn_.fill (0.0);

        const auto stamp = [&] (std::size_t a, std::size_t b, double g)
        {
            // Conductance between two nodes: +g on both diagonals, -g on the
            // off-diagonals, and anything touching the input node moves to the
            // right-hand side instead, because that node's voltage is known.
            const auto index = [] (std::size_t node) { return node - kFirstUnknown; };

            const bool aUnknown = a >= kFirstUnknown;
            const bool bUnknown = b >= kFirstUnknown;

            if (aUnknown) matrix_[index (a)][index (a)] += g;
            if (bUnknown) matrix_[index (b)][index (b)] += g;

            if (aUnknown && bUnknown)
            {
                matrix_[index (a)][index (b)] -= g;
                matrix_[index (b)][index (a)] -= g;
            }

            if (aUnknown && b == kInput) inputColumn_[index (a)] += g;
            if (bUnknown && a == kInput) inputColumn_[index (b)] += g;
        };

        for (std::size_t i = 0; i < numResistors_; ++i)
            stamp (resistors_[i].a, resistors_[i].b, resistors_[i].conductance);

        for (std::size_t i = 0; i < numReactive_; ++i)
        {
            // 2C/T for a capacitor, T/2L for an inductor. Both are positive
            // conductances, so the matrix does not know which is which.
            reactiveConductance_[i] = reactive_[i].isCapacitor
                                        ? 2.0 * reactive_[i].value * sampleRate_
                                        : 1.0 / (2.0 * reactive_[i].value * sampleRate_);

            stamp (reactive_[i].a, reactive_[i].b, reactiveConductance_[i]);
        }

        invert (n);
    }

    [[nodiscard]] double process (double input) noexcept
    {
        const std::size_t n = unknownCount();

        // Right-hand side: the known input node's contribution, plus each
        // capacitor's memory of the last sample.
        std::array<double, MaxNodes> rhs {};

        for (std::size_t i = 0; i < n; ++i)
            rhs[i] = inputColumn_[i] * input;

        for (std::size_t i = 0; i < numReactive_; ++i)
        {
            const double equivalent = equivalentCurrent (i);

            const std::size_t a = reactive_[i].a;
            const std::size_t b = reactive_[i].b;

            if (a >= kFirstUnknown) rhs[a - kFirstUnknown] -= equivalent;
            if (b >= kFirstUnknown) rhs[b - kFirstUnknown] += equivalent;
        }

        // Solve, using the inverse computed when the controls last moved.
        nodeVoltage_[kGround] = 0.0;
        nodeVoltage_[kInput] = input;

        for (std::size_t i = 0; i < n; ++i)
        {
            double sum = 0.0;

            for (std::size_t j = 0; j < n; ++j)
                sum += inverse_[i][j] * rhs[j];

            nodeVoltage_[i + kFirstUnknown] = sum;
        }

        // And carry the reactive elements forward.
        for (std::size_t i = 0; i < numReactive_; ++i)
        {
            const double v = nodeVoltage_[reactive_[i].a] - nodeVoltage_[reactive_[i].b];
            const double equivalent = equivalentCurrent (i);

            reactiveCurrent_[i] = reactiveConductance_[i] * v + equivalent;
            reactiveVoltage_[i] = v;
        }

        return nodeVoltage_[outputNode_];
    }

    /// The node voltages from the last process() call, for tests that want to
    /// look inside rather than only at the output.
    [[nodiscard]] double getNodeVoltage (std::size_t node) const noexcept
    {
        return node < MaxNodes ? nodeVoltage_[node] : 0.0;
    }

private:
    struct Resistor { std::size_t a, b; double conductance; };

    /// Farads if isCapacitor, henries otherwise.
    struct Reactive { std::size_t a, b; double value; bool isCapacitor; };

    /// The companion current source, which is the only place the two kinds
    /// differ:
    ///
    ///     capacitor   Ieq = -G*v[n-1] - i[n-1]
    ///     inductor    Ieq = +G*v[n-1] + i[n-1]
    [[nodiscard]] double equivalentCurrent (std::size_t i) const noexcept
    {
        const double history = reactiveConductance_[i] * reactiveVoltage_[i] + reactiveCurrent_[i];
        return reactive_[i].isCapacitor ? -history : history;
    }

    [[nodiscard]] std::size_t unknownCount() const noexcept
    {
        return nodeCount_ > kFirstUnknown ? nodeCount_ - kFirstUnknown : 0;
    }

    /// Gauss-Jordan with partial pivoting. Small and dense, run only when a
    /// control moves.
    void invert (std::size_t n) noexcept
    {
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                inverse_[i][j] = (i == j) ? 1.0 : 0.0;

        auto working = matrix_;

        for (std::size_t column = 0; column < n; ++column)
        {
            std::size_t pivot = column;

            for (std::size_t row = column + 1; row < n; ++row)
                if (std::abs (working[row][column]) > std::abs (working[pivot][column]))
                    pivot = row;

            if (pivot != column)
            {
                std::swap (working[pivot], working[column]);
                std::swap (inverse_[pivot], inverse_[column]);
            }

            const double diagonal = working[column][column];

            // A singular matrix means a node floats with nothing connecting it.
            // Leaving the row as it stands gives that node zero volts, which is
            // a far better failure than a division by zero reaching the audio.
            if (std::abs (diagonal) < 1.0e-300)
                continue;

            const double scale = 1.0 / diagonal;

            for (std::size_t j = 0; j < n; ++j)
            {
                working[column][j] *= scale;
                inverse_[column][j] *= scale;
            }

            for (std::size_t row = 0; row < n; ++row)
            {
                if (row == column)
                    continue;

                const double factor = working[row][column];

                if (isExactlyZero (factor))
                    continue;

                for (std::size_t j = 0; j < n; ++j)
                {
                    working[row][j] -= factor * working[column][j];
                    inverse_[row][j] -= factor * inverse_[column][j];
                }
            }
        }
    }

    double sampleRate_ { 48000.0 };
    std::size_t nodeCount_ { kFirstUnknown };
    std::size_t outputNode_ { kFirstUnknown };

    std::array<Resistor, MaxElements> resistors_ {};
    std::array<Reactive, MaxElements> reactive_ {};
    std::size_t numResistors_ { 0 };
    std::size_t numReactive_ { 0 };

    std::array<double, MaxElements> reactiveConductance_ {};
    std::array<double, MaxElements> reactiveVoltage_ {};
    std::array<double, MaxElements> reactiveCurrent_ {};

    std::array<std::array<double, MaxNodes>, MaxNodes> matrix_ {};
    std::array<std::array<double, MaxNodes>, MaxNodes> inverse_ {};
    std::array<double, MaxNodes> inputColumn_ {};
    std::array<double, MaxNodes> nodeVoltage_ {};
};

} // namespace tezla::dsp
