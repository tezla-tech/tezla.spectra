// Copyright (c) 2026 The Tezla <thetezla@proton.me>
// Created by The Tezla -- https://github.com/wingit33/tezla.tech
// Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
// Built with development assistance from Claude (Anthropic).
// SPDX-License-Identifier: AGPL-3.0-only
// GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.

#pragma once

// The exact acoustic response of a rigid sphere to a point source at finite
// range. This is the mic body: what the capsule actually receives is the
// pressure on the sphere's surface, and the sphere rewrites the top octaves
// before any electronics exist -- the presence rise on axis, the shadow off
// axis, and the trade (audible, and the reason range is modelled at all)
// where moving in close swaps high-frequency rise for low.
//
//     H(rho, mu, theta) = pressure at the surface point
//                         / free-field pressure at the sphere's centre
//       rho   = r / a          source distance over sphere radius,  rho > 1
//       mu    = w a / c        frequency, scaled by the radius
//       theta = incidence angle from the surface point's outward normal
//
//     H = -(rho/mu) e^(-i mu rho)
//           * sum_{m=0}^inf (2m+1) P_m(cos theta) h_m(mu rho) / h'_m(mu)
//
// == Attribution (CLAUDE.md section 9: taken, not derived) =================
// Formula and evaluation algorithm: Richard O. Duda & William L. Martens,
// "Range dependence of the response of a spherical head model",
// JASA 104(5):3048-3058, November 1998, doi 10.1121/1.423886 -- Eqs (7)-(8)
// for the series, Appendix A for the Q_m-polynomial substitution and
// Appendix B for the evaluation loop and its stopping rule. The algorithm's
// own provenance, per that paper, is Bauck & Cooper (1980), extended by Duda
// and Martens to finite range. Read first-hand from a user-supplied PDF
// (2026-09-01); recorded with its access route in docs/DSP-REFERENCES.md,
// "Microphone physics and presence -- Membrana".
//
// This is the copy-what-measurement-cannot-check case: a subtly wrong
// reimplementation of a convergent series still converges, still draws a
// plausible curve, and differs only in ways a listening test cannot pin.
// So the evaluation below follows their appendices term for term, and the
// tests assert the paper's own printed limits and values against it.
// ==========================================================================
//
// Why not evaluate the spherical Hankel functions directly: h_m explodes
// factorially with m, and the textbook recursion overflows double precision
// long before the series converges. Their Appendix A substitutes
//
//     h_m(x) = Q_m(1/(ix)) (-i)^m e^(ix)                              (A3)
//     Q_m(z) = -(2m-1) z Q_{m-1}(z) + Q_{m-2}(z)                      (A5)
//     Q_0(z) = z,  Q_1(z) = z - z^2,  Q_{-1}(z) == z
//     h'_m(x) = [Q_{m-1}(1/(ix)) - ((m+1)/(ix)) Q_m(1/(ix))]
//               * (-i)^(m-1) e^(ix)                                   (A7)
//
// so every exponential cancels analytically. With z_r = 1/(i mu rho) and
// z_a = 1/(i mu), each term of the sum becomes (their A10)
//
//     term_m = (2m+1) P_m(cos theta) Q_m(z_r)
//              / [ (m+1) z_a Q_m(z_a) - Q_{m-1}(z_a) ]
//
// and the whole response is
//
//     H = rho e^(-i mu) / (i mu) * sum_m term_m
//
// -- the e^(i mu rho) inside h_m(mu rho) has cancelled the e^(-i mu rho) of
// the prefactor, which is why the exponent is e^(-i mu) and not e^(-i mu rho).
// Legendre by its own recursion (their A8-A9):
// P_m(x) = ((2m-1) x P_{m-1} - (m-1) P_{m-2}) / m, P_0 = 1, P_1 = x.
//
// Stopping rule, theirs verbatim: after each term compute the fractional
// change |term| / |sum so far|, and stop only when it has been below the
// threshold for TWO successive terms -- the terms oscillate, and one small
// term proves nothing.
//
// One addition of ours, beyond the printed pseudocode: at small mu the Q_m
// values themselves overflow. |z_a| = 1/mu exceeds 1 below mu = 1, and
// Q_m grows like (2m-1)!! z^(m+1); at mu ~ 0.01 with the slow convergence of
// rho near 1 (the fractional change decays only like rho^-m, so rho = 1.2
// legitimately needs ~120 terms) the running values pass 1e308 around
// m = 95. What happens then is worse than a crash: the denominator (built
// from the larger z_a) reaches inf first, each term collapses to exactly
// zero, the stopping rule reads two "converged" terms and the series exits
// early with a silently truncated tail -- verified by break-check, which is
// why the tests freeze the term counts rather than only the values. (With
// both recursions overflowed it is NaN instead.) The recursion is linear in
// Q, and term_m is a ratio of values at the same m -- so all four carried
// values are rescaled by an exact power of two whenever they grow past
// 2^512. Powers of two are exact in binary floating point: the ratio, and
// therefore every term and the response, is bit-for-bit unchanged. The test
// sweep covers the corner that overflows without this (rho = 1.2 at 20 Hz)
// and the limits pin that the rescaled series still lands on the paper's
// values.
//
// Their time convention is e^(i(kr - wt)) -- the conjugate of Kuhn's and
// Rabinowitz's (their footnote 1). Membrana fits magnitude only, so the
// convention cannot matter here; the one test that asserts a complex value
// (the low-mu limit, their Eq 11: H -> 1 - i (3/2) mu cos theta) asserts it
// in THEIR convention, which is the convention this file implements.
//
// Everything here runs at design time -- building an EQ target on a
// frequency grid -- never in processBlock. Cost is irrelevant; exactness and
// convergence are asserted instead.

#include <cmath>
#include <complex>

namespace tezla::membrana {

struct SphereDiffraction
{
    /// Their suggested order of magnitude; two successive fractional changes
    /// below this end the series.
    static constexpr double kThreshold = 1.0e-10;

    /// Hard cap on terms. The physics needs roughly max(mu, the rho-decay
    /// tail) terms; the worst corner of the parameter space measured 197
    /// terms (rho = 1.2, mu = 105.5 -- 192 kHz on a 60 mm body, an octave
    /// past any design grid -- at theta = 180). The cap sits well above
    /// that, and the test asserts it is never the thing that stopped the
    /// loop.
    static constexpr int kMaxTerms = 300;

    /// Rescale trigger for the carried Q values (see the header comment).
    /// 2^512: far enough from overflow to be safe, exact to scale by.
    static constexpr double kRescaleAbove = 1.3407807929942597e154;  // 2^512
    static constexpr double kRescaleBy    = 7.4583407312002070e-155; // 2^-512

    /// H(rho, mu, cosTheta) in the paper's convention. rho > 1 (the source
    /// is outside the sphere); mu >= 0. mu == 0.0 returns exactly 1.0 by
    /// predicate -- the far-field static limit (their Eq 11) -- because
    /// z_a = 1/(i mu) does not exist there. Note what that predicate is NOT:
    /// at close range the true static value is the geometric near field
    /// (+18.05 dB at rho = 1.25, reached smoothly by mu = 0.1 and pinned by
    /// test), which is a LEVEL, not a shape. A consumer whose frequency grid
    /// includes DC pins that bin by its own policy -- CapsuleEq's target is
    /// flat below its LF corner by construction -- rather than reading it
    /// from here.
    ///
    /// termsUsed, when given, receives the number of series terms summed
    /// (m = 0 and 1 included), so a test can assert the cap was never hit.
    static std::complex<double> response (double rho, double mu, double cosTheta,
                                          int* termsUsed = nullptr)
    {
        using cd = std::complex<double>;

        if (mu == 0.0)
        {
            if (termsUsed != nullptr)
                *termsUsed = 0;
            return { 1.0, 0.0 };
        }

        const cd i  { 0.0, 1.0 };
        const cd za = 1.0 / (i * mu);         // z at the sphere surface
        const cd zr = 1.0 / (i * mu * rho);   // z at the source range

        // m = 0 and m = 1 explicitly, as in their Appendix B pseudocode.
        // Q_{-1}(z) == z makes the m = 0 denominator need no special case.
        cd qr2 = zr;              // Q_0(z_r)
        cd qa2 = za;              // Q_0(z_a)
        cd qr1 = zr - zr * zr;    // Q_1(z_r)
        cd qa1 = za - za * za;    // Q_1(z_a)
        double p2 = 1.0;          // P_0
        double p1 = cosTheta;     // P_1

        cd sum = qr2 / (za * qa2 - za);                        // m = 0
        sum += 3.0 * p1 * qr1 / (2.0 * za * qa1 - qa2);        // m = 1

        int terms = 2;
        int successivelyBelow = 0;

        for (int m = 2; m < kMaxTerms; ++m)
        {
            const double twoMm1 = 2.0 * m - 1.0;

            const cd qr = -twoMm1 * zr * qr1 + qr2;            // Q_m(z_r), (A5)
            const cd qa = -twoMm1 * za * qa1 + qa2;            // Q_m(z_a)
            const double p = (twoMm1 * cosTheta * p1 - (m - 1.0) * p2) / m;

            const cd term = (2.0 * m + 1.0) * p * qr
                            / ((m + 1.0) * za * qa - qa1);     // (A10)
            sum += term;
            ++terms;

            const double frac = std::abs (term) / std::abs (sum);
            successivelyBelow = frac < kThreshold ? successivelyBelow + 1 : 0;

            qr2 = qr1; qr1 = qr;
            qa2 = qa1; qa1 = qa;
            p2 = p1;   p1 = p;

            if (successivelyBelow >= 2)
                break;

            // The exact rescale (ours; see header). qa1 always has the
            // largest magnitude of the four -- |z_a| >= |z_r| since rho > 1.
            if (std::abs (qa1.real()) > kRescaleAbove
                || std::abs (qa1.imag()) > kRescaleAbove)
            {
                qr1 *= kRescaleBy; qr2 *= kRescaleBy;
                qa1 *= kRescaleBy; qa2 *= kRescaleBy;
            }
        }

        if (termsUsed != nullptr)
            *termsUsed = terms;

        return rho * std::exp (-i * mu) / (i * mu) * sum;
    }

    /// |H| in dB -- the form the EQ target uses, and convention-proof.
    static double magnitudeDb (double rho, double mu, double cosTheta)
    {
        return 20.0 * std::log10 (std::abs (response (rho, mu, cosTheta)));
    }

    /// mu for a physical frequency and sphere radius: 2 pi f a / c.
    static double muFor (double fHz, double radiusMetres, double speedOfSound = 343.0) noexcept
    {
        return 2.0 * std::acos (-1.0) * fHz * radiusMetres / speedOfSound;
    }
};

} // namespace tezla::membrana
