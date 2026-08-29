# Malleus — the impossible-object percussion synthesiser

**Code `Tzml` · "Tezla Malleus" · `tech.tezla.Malleus` · instrument.**

The malleus is the hammer: the middle-ear bone that strikes the incus — and
*incus* is Latin for anvil. The suite already ships Anvil; this is the bone
that hits it, and the bone you hear with.

## The thesis

Every synth on this rig makes waves and filters them. Malleus makes
**objects** and excites them: modal physical modelling, where the spectrum
comes from geometry and material rather than from a waveform. And because
this suite's soul is the microtuning engine, the flagship goes one step
further than any physical modeller: **the object's own overtones are
tunable** — stretched past what physics allows, or quantised onto the loaded
scale, so a bell's partials *agree with* Bohlen–Pierce, or a gong's with
slendro. The tuning engine finally meets instruments whose overtones it can
own.

What that is for, on this rig: physical 808s (struck membrane + per-hit
tension drop — the tabla gliss, weaponised), neuro stabs (inharmonic bar/bell
hits with locked overtones), gamelan that agrees with itself, bowed drones
(stick-slip on a bowl, or impossibly on a membrane), and sitar clouds from a
sympathetic bank tuned to the scale.

## The model

One voice = an excitation feeding a **modal resonator bank** (up to 64
two-pole modes, each with its own frequency, T60 and amplitude), through a
**vactrol low-pass gate**, alongside a **sympathetic bank** of tuned strings
coupled to the object's output.

- **Mode ratio tables are derived, not copied** (CLAUDE.md §2.1): the
  free-free bar from the roots of cos x · cosh x = 1 (ratios 1 : 2.756 :
  5.404 : 8.933…), the circular membrane from Bessel zeros computed in-house
  (2.405 / 3.832 / 5.136 / 5.520…), the stiff string from fₙ = n·√(1+Bn²),
  the plate from the (m²+n²) family. The church bell's minor-third series
  (hum ½, prime 1, tierce 1.2, quint 1.5, nominal 2 …) is empirical — bell
  founders shaped it, no closed form exists — so it is taken from the
  standard organology literature and recorded as such in DSP-REFERENCES.
- **Material** morphs continuously across those tables in log-frequency;
  **Stretch** applies a power-law exponent on top; **Overtone Lock**
  quantises the result onto `dsp::Tuning`'s current scale, 0–100%.
- **Exciters**: Mallet (hardness → raised-cosine contact width → spectral
  rolloff; strike Position weights modes by sin(nπx), so hitting the middle
  of a bar physically silences its even modes), Pluck (1/n² + position comb),
  Roll (a bouncing-ball interval series — accelerating mallet rolls — seeded
  and deterministic), and Bow (the classic hyperbolic stick-slip friction
  curve, self-oscillating, **bounded per §7**: cap below unity, soft clip in
  the loop, a guard with an excess accessor, and a swept test).
- **Drop**: a per-hit tension envelope glides every mode's frequency through
  the resonator's state-preserving retune — membrane physics behind the 808
  drop.
- **LPG**: the vactrol modelled from its mechanism — LED fast-on, LDR slow
  nonlinear dark-decay, cutoff and gain coupled — which is what makes struck
  notes "ping" the west-coast way.
- **No oversampling by default**: linear resonators cannot alias and the
  strike is band-limited by construction. The bow is measured first; ×2 on
  its interface only if the sweep demands it — the factor chosen by
  measurement, as Ferrite's was.

## Phases

Each phase is one commit: tests written and run in the same commit, every
mechanism seen red (or break-checked), numbers quoted, whole tree built,
"the qemu-aarch64 cross-check was not run" noted per §2.3.

- **M1 — `shared/tezla-dsp/ModalResonator.hpp`**: the bank. Per-mode
  freq/T60/amp, impulse-weighted excitation, **state-preserving retune**
  (the Ferrite `DcBlocker::retune` lesson — Drop depends on it), energy
  readout for voice retirement. Tests: rings at the asked frequency, T60
  within ±5% of asked, mid-ring retune click-free and landing where stated,
  silence exact, no-op guards bit-exact, block-size independence.
- **M2 — `ModeShapes.hpp`**: root-finders and tables pinned against the
  classic figures; morph (endpoints bit-exact against raw tables); Stretch;
  Overtone Lock against `dsp::Tuning` (lock = 1 → every mode within half a
  cent of a scale degree; tested on Bohlen–Pierce and slendro).
- **M3 — exciters**: mallet, pluck, roll, noise. Tests: spectral centroid
  rises with hardness (pinned); midpoint strike suppresses even modes by
  more than 40 dB; roll intervals follow the ratio; seeds replay exactly.
- **M4 — bow**: onset of self-oscillation measured against pressure and
  speed; the §7 bound with teeth; the full parameter sweep stays bounded;
  the bound break-checked.
- **M5 — `LowpassGate.hpp`**: vactrol dynamics. Tests: the ping's decay is
  measurably non-exponential (the vactrol signature, pinned); closed gate is
  exact silence; retriggering is click-free against steady-state steps.
- **M6 — Drop + `SympatheticBank`**: the glide lands where stated; the
  sympathetic strings' spectrum peaks sit on Tuning degrees; Drone feedback
  bounded across the sweep.
- **M7 — Voice + manager + `MalleusEngine`**: polyphony 16 with stealing;
  retirement on modal energy with **activity asserted** — voices measurably
  die and CPU returns to baseline (the Sonitus zombie-voice lesson, #118);
  per-hit seeded variation; CPU measured per voice; aliasing swept at
  maximum hardness; silence exact; block-size independence with a roll
  running.
- **M8 — JUCE layer**: schema-v1 parameters (choice lists append-only from
  birth), `ui::TuningHost` implemented as Svarayantra does, state, presets:
  Slendro Gongs · BP Bell Choir · Physical 808 · Neuro Stab · Bowed Bowl
  Drone · Sitar Cloud · Glass Marimba · Tabla Drop · a silence reference.
- **M9 — editor**: house panel with a bone-ivory accent, tabs
  OBJECT / EXCITE / RESONANCE / TUNING, and the **mode-stack visualiser** —
  the object's partials drawn as lines with the scale's degrees ghosted
  behind them as Overtone Lock rises. The identity picture.
- **M10 — close-out**: `tezla-measure malleus` (mode tables to CSV, T60
  accuracy, strike spectra vs hardness, bow onset map, aliasing, CPU),
  README, registry flip, validator 47/47 on all nine, docs current.

## Risks

- **The bow** is the one genuinely hard piece of DSP. Mitigation: an
  energy-bounded friction interface plus the §7 sweep gate; the fallback is
  shipping M7–M10 with mallet/pluck/roll and landing the bow as its own
  later phase — the instrument stands without it.
- **`std::cyl_bessel_j` on MSVC**: C++17 special math, shipped since VS2017.
  If it misbehaves there, an in-house series with the same pinned tests
  replaces it. Design-time only, never the audio thread.
- **CPU**: 64 modes × 16 voices estimates at 10–15% of a core worst-case.
  Measured at M1 and again at M7 *before* the default mode count and
  polyphony are chosen.

Sonitus P4 and Prism remain parked. This plan does not touch them.

## Continuity — how any session resumes this work

This section is the handoff. It is updated **in the same commit as each
phase**, so whichever assistant session picks the work up — after a context
loss, a model change, or a fresh clone — needs nothing beyond this file and
CLAUDE.md.

**Phase status** (flip `pending` → `done <short-hash>` in the phase's commit):

| phase | status |
|---|---|
| M0 plan + references + registry | done (this commit) |
| M1 ModalResonator | done |
| M2 ModeShapes + Overtone Lock | done |
| M3 exciters | done |
| M4 bow | done |
| M5 LowpassGate | done |
| M6 Drop + SympatheticBank | done |
| M7 voice + engine | pending |
| M8 JUCE layer + presets | pending |
| M9 editor | pending |
| M10 close-out | pending |

**To resume**: read CLAUDE.md in full, then this file; take the first
`pending` phase. The non-negotiables that every phase here has honoured, in
one place:

- One phase = one commit. Tests written and RUN in that commit; every
  mechanism seen red first or break-checked (edit → red → revert), with the
  measured numbers pinned in the test comments and quoted in the commit
  message.
- Build the whole tree before pushing (`./scripts/build.sh NONE --test` or
  the cmake equivalent with no `--target`), run all tests, and run
  Steinberg's validator on any plugin whose bundle changed.
- The qemu-aarch64 cross-check is **deliberately not run** (CLAUDE.md §2.3
  gate); say so in every commit message rather than implying coverage.
- New first-party files carry the six-line licence header copied from a
  neighbour. No model identifiers in anything pushed. The commit footer
  comes from the harness — use whatever the current session mandates.
- Derive DSP from the physics and measure it; anything taken from a source
  is attributed at the point of use AND in `docs/DSP-REFERENCES.md`
  (CLAUDE.md §9). Setters that clear or re-aim state carry no-op guards
  (`dsp::isExactly`). Continuous parameters are smoothed; discrete switches
  crossfade. Silence in → exact zeros out. Voices must measurably die.
- The prior art to copy patterns from: Ferrite (`plugins/Ferrite/`) for the
  phase discipline and engine shape, Sonitus (`plugins/Sonitus/Dsp/
  VoiceManager.hpp`) for polyphony, Svarayantra for TuningHost/TuningPanel
  wiring, Capstone/Ferrite editors for the panel grid.
