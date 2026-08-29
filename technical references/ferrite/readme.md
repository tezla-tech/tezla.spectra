# Ferrite — reference material and its provenance

Third-party material consulted for the Ferrite tape machine. Everything in
this folder stays as its authors wrote it (no tezla headers — CLAUDE.md §9),
and every file's licence is recorded here and in `docs/DSP-REFERENCES.md`.

## Source

All files come from one repository, cloned in full and read directly:

**AnalogTapeModel** (CHOW Tape Model) by Jatin Chowdhury
https://github.com/jatinchowdhury18/AnalogTapeModel
Licence: **GPL-3.0** (`LICENSE-AnalogTapeModel` here) — compatible with this
project's AGPL-3.0-only (GPLv3 §13 permits the combination).

## The paper — READ FIRST-HAND

`chowdhury-dafx19-tape-paper.tex` + `chowdhury-dafx19-references.bib` are the
LaTeX source of:

> Jatin Chowdhury, "Real-time Physical Modelling for Analog Tape Machines",
> Proc. DAFx-19, Birmingham, 2019.

The `dafx.de` and `arxiv.org` copies are blocked by this container's proxy,
but the author ships the paper's source in the GPLv3 repository, so it has
been read in full, first-hand — the "read the paper first" instruction in
CLAUDE.md §9 is satisfied literally, and no claim below rests on a search
snippet.

What the paper settles: the Karlqvist record-head field and its collapse to
H(t) = NEI(t)/g; the Jiles–Atherton dM/dt form (their eq. 18) with the
Langevin function, its small-argument guards (|x| < 1e-4 → x/3, 1/3), δ_S
and δ_M; the trapezoidal Ḣ recursion and why low-order implicit solvers
struggle at Nyquist; the tanh continued-fraction approximation; the physical
tape constants (Ms 3.5e5 A/m, k 27 kA/m, a 22 kA/m, c 0.17, α 1.6e-3 for
γFe₂O₃); the playback loss product e^(−kd)·(1−e^(−kδ))/(kδ)·sinc(kg/2) with
k = 2πf/v; TC-260 bias practice (55 kHz at ~5× input, 24 kHz recovery LPF);
and the pulse-train method behind the wow/flutter model.

## The implementation files, and what each contributed

| File | What was learned / taken |
|---|---|
| `HysteresisOps.h` | The production J-A state: **normalized units (Ms ≈ 1)**, α = 1.6e-3, k = 0.47875, the cached-constant factoring of dM/dt, and `hysteresisFuncPrime` — the analytic ∂(dM/dt)/∂M needed for Newton–Raphson. Also the leaky derivative estimate (`dAlpha = 0.75`). |
| `HysteresisProcessing.h/.cpp` | Solver menu (RK2/RK4/NR4/NR8), the NR update with Talpha = T/1.9, per-solver input clamps (±8/±10/±12.5), the NaN / M-overflow guard, and **`cook()` — the musical parameter mapping**: drive → a = Ms/(0.01+6·drive), saturation → Ms = 0.5+1.5(1−sat), bias/width → c = √(1−width)−0.01. |
| `HysteresisProcessor.cpp` | The architectural decision that matters most: the shipped plugin's default mode has **no explicit bias carrier** — bias is the c-reshaping above; the 55 kHz carrier survives only in a legacy "V1" mode. Also the makeup law (1+0.6·width)/(0.5+1.5(1−sat)) and the ×2 minimum-phase default oversampling. |
| `DCBlocker.h` | Confirms a DC blocker follows the hysteresis stage. |
| `LossFilter.h/.cpp` | Frequency-sampled **linear-phase** FIR over the analytic loss product (order scaled by fs, latency order/2), 20 Hz wavenumber floor, and the head bump as an ad-hoc **musical** peak biquad: f = v·0.0254/(gap·500), gain ≤ ~1.5×, Q 2 — the bump is not in the loss formula. Full-filter crossfade on parameter change. |
| `FlutterProcess.h/.cpp` | The TC-260-fitted flutter: three sine partials at f, 2f, 3f with fixed phase offsets (0, 13π/4, −π/10), amplitudes ≈ −230/−80/−99 µs and a +350 µs centre delay, depth slewed over 50 ms. |
| `WowProcess.h/.cpp`, `OHProcess.h` | Wow: one sine whose frequency drifts by rand^1.25·drift per block, amplitude up to ~1000·1000/fs samples, amplitude-modulated by an Ornstein–Uhlenbeck-style noise process (normal noise, mean-reverting damping ~20·amt+1). |
| `WowFlutterProcessor.cpp` | How wow + flutter + centre offset sum into one modulated delay-line read. |

## What Ferrite takes verbatim vs derives

Taken with attribution (knowledge measurement cannot check, or fitted values):
the J-A equation itself and its Langevin guards (published maths), the tanh
continued fraction, the physical tape constants, the normalized musical
mapping's *structure* (drive→a, sat→Ms, bias→c), the TC-260 flutter partial
ratios/phases, and the head-bump frequency rule.

Derived and measured here rather than copied: the solver (our own NR
implementation, stability re-established by sweep), all parameter *ranges*
(retuned against our own THD and response measurements), the loss filter
(same analytic magnitude but a **minimum-phase** design per CLAUDE.md §6,
zero latency, magnitude-matched by test), the wow/flutter generators
(seedable, block-size independent per §7), hiss, and the whole engine
plumbing (house Oversampler, BypassMixer, DC blocker, VU).

## Not fetched

- The DAFx-19 PDF itself (dafx.de, arxiv.org — proxy-blocked). Irrelevant in
  practice: the LaTeX source above is the same text from the author.
- Bertram, *Theory of Magnetic Recording* and Jiles's textbook — the deep
  sources behind the paper's constants. The paper's citations are trusted
  for those numbers; rows in DSP-REFERENCES say so.
