#pragma once

// A small passive RC network, solved properly rather than approximated by an
// EQ that happens to have three knobs.
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
// It is linear. Every element is a resistor or a capacitor, so no amount of
// signal changes the network. That is correct for a tone stack and wrong for
// anything with a valve in it -- the nonlinearities live in TriodeStage and in
// the power amplifier, on either side of this.
//
// It also assumes an ideal voltage source driving it. A real stack is fed from
// a plate through an output impedance, and loaded by the next grid; both are
// just more resistors in the netlist, so a topology that cares can include them.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace tezla::dsp {

/// A linear RC network with one input node and one output node.
///
/// Node 0 is ground and node 1 is the input; everything from 2 up is an unknown
/// solved each sample.
template <std::size_t MaxNodes = 8, std::size_t MaxElements = 16>
class RcNetwork
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
        capacitorVoltage_.fill (0.0);
        capacitorCurrent_.fill (0.0);
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
        numCapacitors_ = 0;
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
        if (numCapacitors_ >= MaxElements)
            return;

        capacitors_[numCapacitors_++] = { a, b, std::max (farads, 1.0e-15) };
    }

    [[nodiscard]] std::size_t getResistorCount() const noexcept { return numResistors_; }
    [[nodiscard]] std::size_t getCapacitorCount() const noexcept { return numCapacitors_; }

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

        for (std::size_t i = 0; i < numCapacitors_; ++i)
        {
            capacitorConductance_[i] = 2.0 * capacitors_[i].farads * sampleRate_;
            stamp (capacitors_[i].a, capacitors_[i].b, capacitorConductance_[i]);
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

        for (std::size_t i = 0; i < numCapacitors_; ++i)
        {
            const double equivalent = -capacitorConductance_[i] * capacitorVoltage_[i]
                                    - capacitorCurrent_[i];

            const std::size_t a = capacitors_[i].a;
            const std::size_t b = capacitors_[i].b;

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

        // And carry the capacitors forward.
        for (std::size_t i = 0; i < numCapacitors_; ++i)
        {
            const double v = nodeVoltage_[capacitors_[i].a] - nodeVoltage_[capacitors_[i].b];
            const double equivalent = -capacitorConductance_[i] * capacitorVoltage_[i]
                                    - capacitorCurrent_[i];

            capacitorCurrent_[i] = capacitorConductance_[i] * v + equivalent;
            capacitorVoltage_[i] = v;
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
    struct Capacitor { std::size_t a, b; double farads; };

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

                if (factor == 0.0)
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
    std::array<Capacitor, MaxElements> capacitors_ {};
    std::size_t numResistors_ { 0 };
    std::size_t numCapacitors_ { 0 };

    std::array<double, MaxElements> capacitorConductance_ {};
    std::array<double, MaxElements> capacitorVoltage_ {};
    std::array<double, MaxElements> capacitorCurrent_ {};

    std::array<std::array<double, MaxNodes>, MaxNodes> matrix_ {};
    std::array<std::array<double, MaxNodes>, MaxNodes> inverse_ {};
    std::array<double, MaxNodes> inputColumn_ {};
    std::array<double, MaxNodes> nodeVoltage_ {};
};

} // namespace tezla::dsp
