# Ferrite — the tape machine

<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

The suite's first priority has always been "tube/**tape**/transformer
saturation" and the sonic reference is PSP VintageWarmer — yet six plugins
in, there is no tape. Ferrite is the tape machine proper: magnetic
hysteresis, speed-dependent playback losses with the head bump, wow and
flutter, hiss. Registered `Tzfe`, bundle `tech.tezla.Ferrite`, product
"Tezla Ferrite".

**The job on this rig:** weight and glue. Sub-bass that comes back rounder,
drum busses that sit together, reeses that stop fizzing — at 20 instances if
need be, so every stage pays for itself.

---

## Sources, settled up front (see `technical references/ferrite/`)

The model is Chowdhury's DAFx-19 tape machine. The paper itself has been
read first-hand (its LaTeX ships inside the GPLv3 AnalogTapeModel repo — the
blocked-domain problem does not arise), and the production source was read
alongside it. Licences recorded in `docs/DSP-REFERENCES.md` **before** any
of it influenced code. What is taken verbatim is confined to what
measurement cannot check: the Jiles–Atherton equation and its guards, the
fitted constants, the flutter partial set, the musical mapping's structure.
Everything with observable behaviour is derived here and pinned by our own
measurements.

## The model, and two deliberate departures

```
in → trim → [oversample ×N:  J-A HYSTERESIS (drive, sat, bias) ] → ↓N
   → DC blocker → LOSS (speed: spacing·thickness·gap, min-phase) + HEAD BUMP
   → WOW/FLUTTER (modulated fractional delay)
   → HISS (defeatable) → auto-compensation → trim → out
                                (dry path latency-matched through BypassMixer)
```

1. **Bias is a parameter, not a carrier.** The paper simulates the 55 kHz
   bias current at ×16 oversampling; the shipped CHOW plugin retired that to
   a legacy mode and reshapes the loop instead (bias → c, the
   reversible-magnetization ratio). We follow the production decision: high
   bias linearizes (correct physical intuition), low bias widens the loop
   into grit, and the hysteresis stage runs at the house Auto oversampling
   rate instead of ×16. The carrier's one genuine loss — the underbias
   "deadzone" crossover — is noted as a possible later mode, not smuggled in.

2. **The loss filter is minimum-phase.** CHOW frequency-samples the analytic
   loss magnitude into a linear-phase FIR (order/2 latency, pre-ringing).
   CLAUDE.md §6 makes tone shaping minimum-phase by default, so we take the
   same analytic magnitude — e^(−kd) · (1−e^(−kδ))/(kδ) · sinc(kg/2),
   k = 2πf/v — and design a minimum-phase FIR from it by real cepstrum, for
   zero latency and no pre-ring. The test gate is a magnitude match against
   the analytic curve at 44.1/48/96/192 kHz. If the cepstral route proves
   numerically fragile, the documented fallback is CHOW's linear-phase form
   with declared latency.

The head bump is not in the loss formula and never was: it is a musical
resonance. Bump frequency follows tape speed (the v/(gap·500) rule, taken
with attribution), implemented as a low peak biquad — safely below Fs/8 at
base rate — with an amount control, tuned in F2 against plotted responses.

## Phases

Each phase is one commit: tests written and run in the same commit, every
mechanism seen red (or break-checked), numbers quoted, whole tree built.

**All seven phases are done and committed** (F1 `135f9dc`, F2 `5aead51`,
F3 `ce388bc`, F4 `3209bd9`, F5 `63c1b6a`, F6 `0e33879`, F7 is the commit
carrying this line). Departures from the letter of the plan, argued in the
commits: F1's solver ended as RK4 with adaptive sub-stepping after
Newton–Raphson diverged at high slew (the full story is at the solve site in
`Hysteresis.hpp`); F4's trim probes the exact loop rather than using a fitted
law, and holds ±1.5 dB per the engine test; the cross-rate THD test planned
for F4 was replaced by a cross-rate aliasing gate, because the quasi-static
J-A recursion measures rate-identical THD even unoversampled and a test that
cannot go red is a decoration. What remains open is the rig test: nothing
here has been loaded into FL Studio from this container.

- **F1 — `Hysteresis.hpp`**: the J-A core. dM/dt with Langevin guards and
  the tanh continued fraction (accuracy measured against std::tanh);
  Newton–Raphson solver with analytic derivative; normalized units;
  the musical mapping (drive → a, saturation → Ms, bias → c) with ranges
  retuned by measurement; input clamp + blow-up guard (clamp, never a hard
  reset to zero mid-stream). Tests: loop encloses area and shows remanence;
  odd-only harmonics for a symmetric input; THD-vs-drive measured and
  pinned; a full parameter × frequency × amplitude sweep stays finite;
  solver residual bounded; silence in (after a DC blocker) → exact zero out.
- **F2 — `TapeLoss.hpp`**: analytic loss magnitude → minimum-phase FIR via
  real cepstrum (small in-house radix-2 FFT for design time only, never the
  audio thread); head bump biquad; speed choice 3.75/7.5/15/30 ips with a
  crossfaded switch. Tests: magnitude vs analytic within tolerance at four
  rates; bump centre tracks speed; the switch is click-free; impulse is
  causal and front-loaded (the minimum-phase claim, measured).
- **F3 — `WowFlutter.hpp`**: one modulated fractional-delay line; flutter =
  the TC-260 three-partial stack (attributed) + depth; wow = drifting sine ×
  Ornstein–Uhlenbeck amplitude (derived, seedable). Tests: measured pitch
  deviation matches the configured depth (the Svarayantra vibrato method);
  deterministic for a fixed seed; block-size independent bit-for-bit; depth
  zero is bit-exact passthrough.
- **F4 — `FerriteEngine`**: the whole chain with the house Oversampler
  (Auto), DC blocker, hiss (seeded, defeatable, exact-zero when off),
  drive-tracking auto-compensation (fitted from measurement, verified ±1 dB
  loudness across the drive range), latency-matched BypassMixer, VU-ballistic
  metering taps (§7: analogue claims get honest meters). Measurements that
  gate the phase: aliasing < −60 dBFS audible-band at maximum drive with the
  chosen Auto factor (measured, not assumed — and the factor chosen BY that
  measurement); silence in → exact zeros with hiss off; null vs dry at
  bypass; block-size independence with wow running; CPU per instance.
- **F5 — JUCE layer**: parameters (drive, saturation, bias, speed, bump,
  wow, flutter, hiss, mix, trims, auto-comp, oversampling; expert: spacing,
  thickness, gap, wow/flutter rates), state, presets aimed at the rig: Drum
  Bus Glue, Sub Weight, Reese Thickener, Master Glue, Clean 30 ips, Trashed.
- **F6 — Editor**: house dark panel with Ferrite's own accent, VU meter
  front and centre, main/expert pages, tooltips that state costs.
- **F7 — Close-out**: `tezla-measure ferrite` (hysteresis loop CSV, THD
  table, loss response per speed, aliasing, CPU), README, registry flip,
  validator 47/47, docs current.

## Risks

- **Solver stability at Nyquist drive.** The paper is explicit that
  low-order implicit methods struggle; production answer is clamps + NR +
  Talpha fudge. Our F1 sweep is the gate; if NR misbehaves at ×2, the Auto
  floor for this plugin rises and the tooltip says why.
- **Cepstral minimum-phase.** New ground for this repo. Contained: one
  design-time function with a measured gate and a documented fallback.
- **CPU.** NR-4 with two tanh per iteration at ×4 is roughly 8 tanh-equivalents
  per sample. Budgeted, then measured in F4; if 20 instances is not sane,
  the default polyphony of solutions is: cheaper solver at low drive, or ×2
  Auto floor. Measured before chosen.

Sonitus P4 and B1 remain parked. This plan does not touch them.
