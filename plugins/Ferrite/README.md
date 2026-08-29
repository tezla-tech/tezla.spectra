# Tezla Ferrite

A tape machine: the magnetic loop, the wavelength physics of the head against
the tape, and the motor that never quite holds speed — each modelled from the
mechanism rather than fitted to a curve.

**Code `Tzfe` · vendor `Tzla` · bundle `tech.tezla.Ferrite` · schema v1**

Build plan and phase history: [`PLAN.md`](PLAN.md).

---

## The thesis

**Tape is three machines in one box, and they are different kinds of machine.**

| stage | mechanism | character |
|---|---|---|
| the magnetisation | Jiles–Atherton hysteresis | compression, odd harmonics, memory |
| the head and the oxide | wavelength losses + contact bump | the tone: dark top, thick bottom |
| the transport | a motor's partials + a drifting wow | motion — texture, not vibrato |

The hysteresis is solved per sample from the Jiles–Atherton magnetisation
model (the DAFx-19 tape paper's formulation — RK4 with adaptive sub-stepping,
bounded by construction and tested to stay that way across the whole parameter
space). The losses are the closed-form spacing, thickness and gap terms
evaluated at the actual tape speed and turned into a **minimum-phase** FIR by
real-cepstrum construction, so they add no latency and no pre-ring. The
wobble is a fractional-delay read head whose flutter is a published fit of a
real transport (three partials, fixed phases) and whose wow drifts through a
mean-reverting random walk — because a motor that wobbled perfectly evenly
would sound like an LFO, and that is the one thing this must not do.

## The level is the drive

On a real machine there is no distortion knob: how hard you hit the tape *is*
the sound. Ferrite behaves the same way, measured (315 Hz, drive 0.7,
auto-trim on — the output level holds while the tone moves):

| level onto tape | THD | output level shift |
|---|---|---|
| −20 dBFS | −48.9 dB | 0.00 dB |
| −12 dBFS | −40.3 dB | +0.01 dB |
| −6 dBFS | −30.3 dB | −0.00 dB |
| 0 dBFS | −21.5 dB | −0.03 dB |

Roughly 9 dB more third harmonic per 8 dB of level: the loop's ceiling coming
in. At quiet levels the distortion floor (about −48 dB, third harmonic first)
is the **Rayleigh region** — hysteresis memory, nearly independent of the
Drive control, which is what tape actually does at low flux. Drive, Saturation
and Bias reshape *where and how* the ceiling arrives; the input level decides
how much of it you use.

The loop is odd-symmetric, as the physics is: tape warmth is third-first, not
second-first. (A valve gives you the evens — that is [Anvil](../Anvil/), and
they stack well.)

## The losses land where the speed says

Designed filter against the closed-form curve (spacing 5 µm, thickness 35 µm,
gap 2.5 µm; worst design error across every speed and rate measured at
0.42 dB, typically far less):

| speed | 2 kHz | 5 kHz | 10 kHz | 15 kHz | head bump at |
|---|---|---|---|---|---|
| 3.75 ips | −19.2 dB | −36.4 dB | −54.3 dB | −63.9 dB | 10.6 Hz |
| 7.5 ips | −11.0 dB | −22.5 dB | −35.7 dB | −46.8 dB | 21.2 Hz |
| 15 ips | −6.0 dB | −13.3 dB | −22.5 dB | −29.7 dB | 42.3 Hz |
| 30 ips | −3.1 dB | −7.3 dB | −13.3 dB | −18.3 dB | 84.7 Hz |

Halve the speed and the whole curve slides down an octave — the defining
property of wavelength losses, asserted in the tests. The head bump sits at
the contact wavelength (v / 9 mm) and lifts about 2.5 dB at its 100% setting.
For sub-bass work, 7.5 ips puts the bump at 21 Hz.

## Controls

**TAPE** — Input (±24 dB, the real drive), Drive (loop steepness), Saturation
(the ceiling), Bias (high = a narrow, clean, well-biased loop; low = wide,
under-biased and nasty), Speed (3.75/7.5/15/30 ips), Head Bump (0–200% of the
measured 2.5 dB), Auto Trim.

**MOTION** — Wow and Flutter depths (0 is bit-exact through the latency
contract), Hiss (calibrated: the number **is** the output noise floor in
dBFS, measured within a decibel; bottom of travel is off, and off is
bit-exact absence), and the expert rates.

**MACHINE** — Spacing, Thickness, Head Gap in microns: the three lengths the
loss physics runs on. A few microns of "dirt" on the spacing costs real
treble, exactly as the exponential says it should.

**Header** — Mix (the dry side is latency-matched, so 0% is bit-exact),
Output, Oversampling, bypass, A/B, tooltips.

Both channels ride one tape: the wobble is seeded identically per channel
(linked stereo), while the hiss generators are per-track and decorrelated —
each the §7-correct default for its mechanism.

## Auto Trim is measured, not guessed

Whenever drive, saturation, bias, input gain or the oversampling rate change,
a short probe (512 samples of sine through a scratch hysteresis stage at the
oversampled rate, makeup included) measures the small-signal gain of the
exact loop the audio is about to meet, and the compensator ramps to its
reciprocal. Probes are rate-limited to one per 2048 samples, so automation
costs a bounded slice of CPU. Held within ±1.5 dB across the whole
drive × saturation × input grid, asserted in the tests.

## Oversampling

The J-A recursion is quasi-static — dM scales with dH — so its *harmonic
ratios* are inherently rate-free; what changes with rate is where the
harmonics fold. Measured at maximum drive, worst of a swept probe set,
absolute dBFS of audible-band inharmonic energy at 48 kHz:

| factor | alias floor |
|---|---|
| ×1 | −43.6 dBFS |
| ×2 | −60.8 dBFS |
| ×4 | **−74.1 dBFS** |

The gate is −60 (CLAUDE.md §7): ×2 scrapes it, ×4 clears it with margin —
which is why Auto picks ×4 at 44.1/48 k, ×2 at 88.2/96 k, and off at 176.4 k
and above. The gate re-measured at 44.1, 96 and 192 kHz holds under Auto at
every rate.

**Latency**: the oversampler round trip (0/47/63/71 samples) plus the
wow/flutter head's 1 ms centre offset. Declared to the host; the mix's dry
path and the bypass are both delayed to match, so every A/B is honest.

**CPU** (this container, one stereo instance): ~10% of one core at ×4, ~19%
at ×8.

## Presets

A bit-exact mix-0 reference first, then: Drum bus glue · Sub weight (7.5 ips,
bump 160%, wobble nearly off — a sub wants pitch stability) · Reese thickener
(real wobble over hot, wide-loop drive) · Master glue (30 ips) · Clean
(30 ips, high bias — genuinely clean, priority 2) · Trashed (3.75 ips, full
wow, hiss up) · Warble (7.5 ips, wow at maximum).

## Verification

- 746 suite tests pass on x86-64, 15 of them Ferrite's engine gates: exact
  silence, DC drained after remanence, bit-exact mix-0 and bypass against the
  delayed input, the aliasing gates above, trim ±1.5 dB, block-size
  independence to exactly zero with the wobble and hiss running, channel
  linkage through automation, hiss calibration and decorrelation, latency
  values per factor, and CPU. Each mechanism was seen red before its test was
  trusted (the F1–F4 commit messages carry the numbers).
- `tezla-measure ferrite` renders every table above, plus the hysteresis
  loops themselves to CSV for plotting (`--out loop.csv`).
- The VST3 builds warning-clean and passes Steinberg's validator 47/47 on
  Linux. **Not yet loaded in a DAW on Windows from here** — the rig test is
  the acceptance test, and the qemu-aarch64 cross-check waits on CLAUDE.md
  §2.3's gate.

## Sources

The Jiles–Atherton formulation, the derivative recursion, the makeup law and
the TC-260 flutter fit follow **Jatin Chowdhury's DAFx-19 tape paper and the
AnalogTapeModel plugin** (GPL-3.0, compatible with this project's AGPL-3.0
via GPLv3 §13) — the paper's LaTeX ships inside that repository and was read
first-hand. Key files and the licence are vendored under
`technical references/ferrite/` with a provenance note separating what was
taken verbatim from what was derived; `docs/DSP-REFERENCES.md` carries the
full source table, including what was *not* read (Jiles & Atherton 1986,
Bertram, Camras — trusted through the paper's citations). The wavelength-loss
product is textbook magnetic-recording physics evaluated from first
principles; the wow's Ornstein–Uhlenbeck drift is this project's own design.
