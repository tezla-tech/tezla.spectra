<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# Sonitus — phase 5: the horror phase

**`Tzso` · "Tezla Sonitus" · `tech.tezla.Sonitus` · instrument.**

The ask, in the user's words: *"i like to make 80s horror synth sounds and
drones, those crazy sounds / thriller/horror/action"*. Three things came out of
the survey of what the instrument already does, and the user picked all three.

Phase 4 finished Sonitus as a timbre machine. Phase 5 is about the three things
that palette still cannot reach:

| | what is missing | in one line |
|---|---|---|
| **Stack** | a chord that is *wrong* | the unison bank stops being a detune |
| **Tract** | a voice that is the wrong *size* | one knob resizes the throat |
| **Sag** | a machine that is *dying* | one slow instability shared by everything |

Each is a mode or a multiplier on machinery that already exists and is already
measured. None of them is a new engine. That is why all three fit in one phase.

---

## What was checked first, so nothing here is already possible

Sonitus is deep enough that the risk was proposing something it already does.
Read before this plan was written: `plugins/Sonitus/README.md` in full, all
123 parameter ids in `Source/PluginProcessor.h`, `Dsp/SonitusVoice.hpp`,
`Dsp/SonitusEngine.cpp`, `shared/tezla-dsp/include/tezla/dsp/UnisonBank.hpp`
and `Formant.hpp`.

Already there, and therefore **not** in this plan: noise as an oscillator shape
(with its own per-copy stream, so a noise stack is wide); ring modulation A × B;
the sine folder; a sub anywhere from two octaves below the note to two above;
per-copy analogue drift that survives a note-on; per-voice card drift on cutoff
and resonance; LFOs from 0 Hz ("held") to 160 Hz with sample-and-hold and smooth
random, key tracking, retrigger and fade-in; a sixteen-step sequencer and three
looping, snapping ADV envelopes; a key-tracked, scale-locked comb at up to 88 %
feedback; vowel morph, harmonic lock, an anti-formant notch, a triode and a
tilt; 44 built-in tunings and Scala import.

Deliberately **not** proposed, with the reason, so a later session does not
re-derive it: an arpeggiator (an ADV envelope on `level` with Loop and Snap is
already the pulse, and FL's piano roll is the rest); any reverb or space inside
the synth (that is a different plugin); a "horror" macro that is really an
effects rack.

---

## 1. Stack — the unison bank stops being a detune

### The observation

`UnisonBank` is seven oscillators with a per-copy pitch offset, a per-copy pan
and a per-copy drift. The offset is computed one way and one way only:

```cpp
const double offset = position (i) * detuneCents_ * 0.5;   // UnisonBank.hpp
const double ratio  = std::pow (2.0, offset / 1200.0);
increments_[index]  = frequency_ * ratio / sampleRate_;
```

Cents, symmetric, shaped by `kSpreadExponent`. That is a reese and it is the
right default. It is also one array away from being three other instruments.

### The API: one array, and the bank stays dumb

**`UnisonBank` gains exactly one thing** — a per-rank offset in cents and a
per-rank gain:

```cpp
/// Per-copy pitch offsets in cents and per-copy gains, on top of the detune.
/// Passing nullptr (the default) is the neutral state: 0 cents, gain 1.
void setRankOffsets (const double* cents, const double* gains, int count) noexcept;
```

Everything else — which intervals, which scale, where the Shepard phase is —
is computed by the caller in `plugins/Sonitus/`, because the tuning lives there
and the bank must stay framework-free and general. The bank's job stays "N
oscillators with offsets and gains".

Two consequences fall out for free:

- **The offset is additive with the detune**, so a cluster can still churn. The
  Detune knob never becomes a dead control in any mode.
- **The normalisation generalises exactly.** `1.0 / std::sqrt(N)` becomes
  `1.0 / std::sqrt(sum of g_i^2)`, and with every gain at 1 the sum of N ones is
  exactly N in IEEE arithmetic — so the neutral case is **bit-identical**, not
  merely equal. That is the claim the first test pins.

The setter **refuses a no-op by comparing the arrays**, in the callee, per
CLAUDE.md §7's fourth bite: every mode but Shepard pushes the same numbers every
control chunk, and `updateIncrements()` is seven `pow`s and fourteen trig calls.
One guard, in the object that knows what it is currently set to.

### The modes

One appended choice per oscillator, `stackA` / `stackB`, default `Detune`:

```
{ "Detune", "Octaves", "Fifths", "Tritones", "Cluster", "Diminished", "Scale", "Shepard" }
```

**Ranks are symmetric and rank 0 always exists**:
`rank(i) = i - floor((N - 1) / 2)`, so N = 5 gives −2..+2 and N = 4 gives
−1..+2. That matters: exactly one copy sits on the played pitch at 0 cents, so
**Stack never detunes the instrument** — turning Unison up adds notes around the
one you played rather than moving it. (Detune mode is deliberately not like
this; `UnisonBank::position(0)` is the bottom of the stack, which is exactly why
hard sync uses a dedicated master accumulator at A's nominal pitch. Sync is
unaffected by any of this.)

The five fixed modes are a generating interval in cents, `offset = rank × g`:

| mode | g | what it is |
|---|---|---|
| Octaves | 1200 | organ registration; the doubled pulse |
| Fifths | 700 | quintal stacking — hollow, wide, the action one |
| Tritones | 600 | symmetric, rootless, unresolvable |
| Cluster | 100 | the tone cluster |
| Diminished | 300 | the fully diminished stack — no root by construction |

**Scale** is the one that only this instrument can do, and it needs no new
tuning API at all: *copy at rank r plays the key `note + r × step`, through the
loaded tuning.*

```cpp
const double hz = tuning.frequencyFor (note + rank * step);
cents[i] = 1200.0 * std::log2 (hz / rootHz);
```

`Tuning::frequencyFor` already handles keyboard maps, non-octave repeats and
concert pitch, so one line gives correct behaviour in all 44 built-ins and any
`.scl` file. What a "step" means is then whatever the tuning says a key means:
a semitone in 12-EDO, a scale degree under a keyboard map, a Bohlen–Pierce
degree in a scale that repeats at 3/1. **The tooltip says exactly that**, because
it is the one place the control's meaning depends on another page.

`stackStepA` / `stackStepB`, 1–7 keys, default 1; greyed outside Scale mode.

A rank that falls off the keyboard (`frequencyFor` returns 0.0 outside
`kLowestNote..kHighestNote`) gets **gain exactly 0**, not a clamped note — two
copies on one pitch is a quiet lie, a silent copy is not. Pinned by a test at
note 120 with step 7.

### Shepard — the endless rise

The Shepard–Risset glissando: copies an octave apart all sliding at the same
rate, under a fixed window in log frequency, so a copy fades in at the bottom
exactly as another fades out at the top. It is the most recognisable device in
thriller scoring and Sonitus is four lines of arithmetic away from it.

With a shared phase `p` in [0, 1) and N copies:

```
u_k    = frac (p + k / N)
cents_k = 1200 * (N * u_k - N / 2)          // spans ±N/2 octaves
gain_k  = 0.5 * (1 - cos (2*pi * u_k))      // exactly 0 at the wrap
```

**The seam is silent by construction and it is provable.** The frequency jumps
from +N/2 octaves to −N/2 octaves precisely when `u_k` wraps through 0, where
`0.5(1 − cos 0)` is exactly 0.0. The oscillator's own phase is never reset — only
its increment changes — so there is no waveform discontinuity at all, at zero
amplitude.

**And the summed power is exactly flat for three copies or more.** Computed here
before it was written down, over 2048 phase steps:

| N | Σ gain² | ripple |
|---|---|---|
| 1 | 0 .. 1 | 1.000e+00 |
| 2 | 0.5 .. 1.0 | 5.000e-01 |
| **3** | **1.125** | **1.776e-15** |
| **4** | **1.500** | **1.110e-15** |
| **5** | **1.875** | **2.220e-15** |
| **6** | **2.250** | **3.109e-15** |
| **7** | **2.625** | **3.553e-15** |

Σ gain² = 0.375 N for N ≥ 3, at the arithmetic floor. The identity: Σ cos θ_k = 0
for N ≥ 2 and Σ cos 2θ_k = 0 for N ≥ 3, both being sums of roots of unity. So the
glissando has no tremolo in it at three copies or more, and audibly does at two —
which is the break-check, and the reason the tooltip says Shepard wants at least
three copies (seven is what it is for).

The generalised normalisation handles the level with no special case:
`1/sqrt(0.375 N)` is `1/sqrt(N)` times 1.633, so a Shepard stack comes out **4.26 dB**
louder than the raw arithmetic would leave it, and lands where the ear expects.

**The phase is one global accumulator**, stepped once per control chunk in
`SonitusEngine::advanceGlobalSources` and pushed into every bank of every voice.
That is not tidiness: a held chord must glide as one, and two voices with
independent phases would smear into a wash instead of a rise. Both oscillators
share it too — a "shear" with A rising while B falls is a roadmap item below,
not a v1 parameter.

Global controls, all at `kSchemaV8`:

- **`shepardRate`** — bipolar, ±4 octaves per second, skewed, **default 0.0**.
  Zero is a legitimate setting and is a *held* windowed octave stack, which is
  its own sound (an organ with the ends rolled off). The panel reads it as
  "held", the way LFO rate 0 already does.
- **`shepardSync`** + **`shepardDivision`** — one octave per division, sign from
  `shepardRate`. Exactly the pattern the LFOs already use, built from
  `dsp::divisions` so the two lists cannot drift.

The illusion repeats once per octave of travel, so the rate reads directly:
0.02 oct/s is a fifty-second dread riser, 4 oct/s is a siren.

### Cost, stated because the tooltip has to state it

Every mode but Shepard pushes constant arrays, the no-op guard fires, and the
per-chunk cost is one array comparison. **Shepard is the only mode with a
running cost**: seven `pow`s and seven `cos`es per bank per control chunk, ×2
banks ×32 voices ≈ 2.7 M of each per second at 48 kHz ×4. Estimated 1–3 % of a
core at full polyphony; measured before H2 closes, and the tooltip carries the
figure. The sample loop is **unchanged** — the window gains multiply into the
existing `gainL_`/`gainR_` pan gains, so Shepard costs nothing per sample.

---

## 2. Tract — one knob that resizes the throat

### The physics, which is why this is a knob and not an effect

The vowel filter puts its three resonances where Peterson & Barney measured
them in an adult male tract. A tube's resonances scale inversely with its
length: `F_n = (2n − 1)c / 4L`. So **one ratio applied to all three centres is
the same tract, a different size** — not a pitch shift, not a formant effect. The
played pitch does not move at all. Only the size of the thing making the sound
does, which is exactly why it reads as a creature rather than as a transposition.

- `tract` = 0.5 → a **35 cm** throat. Something large, and wrong.
- `tract` = 1.0 → the paper's adult male, ≈17.5 cm. Neutral.
- `tract` = 2.0 → **8.75 cm**. Something small, and wrong.

For scale: an adult female tract is about 1.2 on this control and a small child
about 1.6, so the range runs well past a human at both ends and passes through
every human on the way. The tooltip gives the centimetres, because that is what
makes the control legible.

### Where it goes

`Formant::updateCoefficients`, applied to the vowel frequency **before** the
harmonic lock:

```cpp
double frequency = a * std::pow (b / a, blend) * tract_;
...
if (lockAmount > 0.0)
    frequency *= std::pow (partial / frequency, lockAmount);
```

Before, not after, and it matters: the lock's geometric blend lands exactly on
`noteHz × (harmonic + index)` at full lock whatever `frequency` was, so **the
lock still wins**, Tract colours the approach to it, and overtone singing stays
in tune. A Tract applied after the lock would knock the resonance off the
partial and silently break the one thing the lock exists to guarantee.

**The bandwidths scale by the same ratio**, so Q is preserved:

```cpp
const double bandwidth = kBandwidths[index] * width * tract_
                           * std::pow (kLockedNarrowing, lockAmount);
```

A longer tube is the same tube, longer; its resonator quality is a property of
its shape, not its size. Holding the bandwidth fixed instead would make a big
throat sharper and a small one blurrier, which is a second effect nobody asked
for riding on the first. `formantQ(i)` unchanged under Tract is an assertion.

**The amplitudes are untouched**, and the paper argues the case itself: P&B's
relative amplitudes were averaged across men, women and children because the
measurements "did not show decided differences between classes of speakers" —
which is a statement that formant balance is size-independent, from the source
already cited in `Formant.hpp`.

### Neutral, and the clamps

`tract_ = 1.0` multiplies by exactly 1.0, which is exact in IEEE, so neutral is
bit-exact with **no branch**. A test feeds a signal and compares bit for bit
rather than trusting that.

No clamp is reachable at any legal setting: the highest formant is 3010 Hz (the
third of "ee"), which at Tract 2.0 is 6020 Hz against a ceiling of
`44100 × 0.45 = 19845 Hz`; the lowest is 270 Hz, which at 0.5 is 135 Hz against a
floor of 20. So Tract is safe at every supported rate, and the test says so at
44.1 k where it is tightest.

Parameter `tract` at `kSchemaV8`, geometric skew centred at 1.0, on the MANGLE
page beside Vowel and Sharpness, greyed when the vowel mix is 0 or the harmonic
lock is full. Appended to `globalDest` as **"Tract"**.

---

## 3. Sag — one slow instability shared by everything

### What is missing, precisely

Sonitus has two random walks and both are **uncorrelated on purpose**:
`UnisonBank`'s per-copy drift (that is what makes a reese churn) and the voice
card's per-voice drift (that is what makes eight voices sound like eight cards).
Uncorrelated drift makes a stack *thick*.

The opposite is missing: **one walk, common-mode across the whole instrument**.
That is not a thicker sound, it is a *failing* one — the tape slowing, the
generator browning out, the machine going wrong as one machine. It is what keeps
a three-minute drone from becoming wallpaper by bar eight, and it cannot be
faked with an LFO because an LFO repeats and the ear locks onto it within a bar.

### The walk

New, small, shared: **`dsp::SlowWalk`**. A near-duplicate of the two existing
walks was considered and rejected — it has a different shape, and the two
existing ones are pinned bit-exact by tests that must not be disturbed. Its
header says exactly that, so nobody "tidies" the three into one later without
knowing what it costs.

The shape is the interesting part. A "lurch" is normally got by making the fall
faster than the recovery, and that **biases the mean**: a walk that falls quickly
and crawls back spends more time below zero, so the instrument goes permanently
flat. Instead the asymmetry lives in the *target distribution*:

```cpp
target = random.bipolar() * std::abs (random.bipolar());
```

Two independent draws. Concentrated near zero with occasional excursions to ±1 —
so it mostly sits still and occasionally lurches — and **zero-mean by
construction**, since E[a·|b|] = E[a]·E[|b|] = 0 for independent a, b. The
one-pole towards it stays symmetric, the way the other two walks are. The test
runs ten simulated minutes and asserts the mean is inside three standard errors
of zero; the break-check is a deliberately skewed target, which fails it.

### What it moves, and how it reaches the voice

Two controls: **`sag`** (depth, 0–1, default 0) and **`sagRate`** (the period,
2–120 s, default 20). At full depth, from the one walk and in the same direction:

| | at full depth | why |
|---|---|---|
| pitch | ±40 cents | the capstan slipping — the audible half |
| cutoff | ±0.4 octaves | the sound dulling as it sags |
| level | ±1.5 dB | the amplifier drooping |

Fixed shares from one knob, following `voiceDrift`'s precedent exactly: three
things moving together from one cause is what reads as *one machine*, where
three separate knobs read as three effects.

It is carried in **`GlobalSources`**, whose macro field already argues the case
in its own comment — *"so the voice and the mangle read the identical figure
rather than two copies that could drift by a control chunk"*. That is the Sag
argument verbatim.

The pitch share is applied **where `ModDestination::pitch` is applied**
(`SonitusVoice.hpp:830`), not folded into `centsA`/`centsB` alongside the pitch
bend. That is a real distinction and it took reading `subIncrement` to find it:
the sub reads `frequency_ × pitchRatio` and ignores `centsA`, so sagging through
the cents field would leave the sub sitting perfectly in tune underneath a
sagging top. Half the instrument failing is not the effect.

**Exactly zero at depth 0**, `isExactlyZero`-guarded on all three shares, with
the walk still walking — again following `voiceDrift`. And because it is carried
in `GlobalSources` it is a **modulation source for free**, appended to both
`modSource` and `globalSource` as "Sag": point the machine's temperature at the
comb time and the flanger wanders with everything else. As a source it reads the
walk even at depth 0, which is correct — the depth knob is how much reaches the
voice *directly*, not whether the machine has a temperature.

Appended to `globalDest` as **"Sag"**, so an envelope can make the machine fail
on cue.

Both knobs wear `ui::spectralKnob` — the pastel rainbow ring the Drift knobs
already wear — so the family is legible at a glance. MOD page, its own small
plate.

---

## Parameters, and the lists that are frozen

Everything new is at **`kSchemaV8`** (current head is `kSchemaV7`), appended,
neutral at its default, so a project saved before this phase reopens sounding
identical. `kStateSchemaVersion` moves to `kSchemaV8`.

| id | type | range | default | page |
|---|---|---|---|---|
| `stackA`, `stackB` | choice | the 8 modes | Detune | OSC |
| `stackStepA`, `stackStepB` | int | 1–7 keys | 1 | OSC |
| `shepardRate` | float | ±4 oct/s, skewed | 0.0 | OSC |
| `shepardSync` | bool | — | off | OSC |
| `shepardDivision` | choice | `dsp::divisions` | 1 bar | OSC |
| `tract` | float | 0.5–2.0, geometric | 1.0 | MANGLE |
| `sag` | float | 0–1 | 0.0 | MOD |
| `sagRate` | float | 2–120 s, skewed | 20 s | MOD |

**Append-only lists touched** (CLAUDE.md §8 — a stored index, never a name):

- `choices::stack` — **new** list, so nothing to break; `static_assert`ed
  against `StackMode`.
- `choices::modSource` + `ModSource` — append `Sag`.
- `choices::globalSource` + `GlobalSource` — append `Sag`.
- `choices::globalDest` + `GlobalDestination` — append `Tract`, `Sag`,
  `Shepard rate`.
- `choices::modDest` / `ModDestination` — **untouched**. Stack mode and step are
  a choice and an integer, and the list holds continuous controls only by the
  rule already written there.

Depth table rows for the new global destinations, in the README's existing
shape: Tract ±2 octaves of scaling, Sag ±1 (the full depth range),
Shepard rate ±4 oct/s.

---

## Phases

One commit each; tests seen red before they are seen green; whole tree built
with no `--target` (`./scripts/build.sh NONE --test`, then the plugin and tool
targets); `tezla-measure selftest` before any number is quoted; Steinberg's
validator on the bundle; measured figures in the commit message. Every message
carries **"the qemu-aarch64 cross-check was not run (CLAUDE.md §2.3 gate)"**.

| phase | what |
|---|---|
| **H0** | this plan; `docs/ROADMAP.md` rows for the deferred items below |
| **H1** | **`UnisonBank::setRankOffsets`** + `dsp::SlowWalk`, shared, own commit: the neutral bit-exactness proof, the generalised normalisation, the Shepard window identity, the zero-mean walk |
| **H2** | **Stack** in Sonitus: `StackShapes.hpp`, the Scale path through `Tuning::frequencyFor`, the global Shepard phase and sync in the engine, parameters, OSC page, tooltips, measure table, CPU |
| **H3** | **Tract**: `Formant::setTract`, the bit-exact neutral, the lock-still-wins proof, MANGLE page, measure table |
| **H4** | **Sag**: the engine walk, the three shares, both source lists and the destination list, MOD page, measure table |
| **H5** | close-out: horror presets, README sections, ROADMAP, validator 47/47 on all thirteen, screenshots, the phase-5 CPU table |

**H1 is shared DSP and gets its own commit** per CLAUDE.md §11, because
`UnisonBank` and `Formant` are used by more than the plugin being changed.

### Tests, named

**Stack** — `stack_detune_mode_is_bit_exact_against_the_neutral_bank`;
`every_stack_mode_puts_one_copy_exactly_on_the_played_pitch`;
`interval_modes_land_on_the_intervals_they_name` (FFT partial ratios within a
cent); `scale_mode_copies_play_the_keys_the_step_names` (expectation from
`frequencyFor`, **not** from the same table — the Ictus break-check lesson);
`scale_mode_silences_a_copy_that_falls_off_the_keyboard`;
`shepard_summed_power_is_flat_for_three_copies_or_more` (pinning 0.375 N and the
2e-15 floor, red at N = 2); `shepard_has_no_seam` (max sample step never exceeds
the signal's own, over 20 s); `shepard_returns_to_the_same_spectrum_after_one_octave`
(the Risset claim, measured); `shepard_rate_zero_is_held`;
`shepard_is_the_same_at_44100_and_192000` (centroid trajectory within 0.5 %);
`stack_is_block_size_independent` (64 vs 512 bit-identical); CPU budget at 32
voices.

**Tract** — `tract_at_one_is_bit_exact`; `tract_scales_the_formants_and_holds_their_Q`;
`tract_peaks_land_where_the_ratio_says` (measured on the response, not on the
coefficients); `the_harmonic_lock_still_wins_under_tract`;
`tract_leaves_the_formant_amplitudes_alone`;
`tract_never_reaches_a_clamp_at_44100`.

**Sag** — `sag_depth_zero_is_bit_exact`; `sag_walk_is_zero_mean` (ten minutes,
three standard errors, red on a skewed target); `sag_is_bounded`;
`sag_moves_every_voice_by_the_same_cents` (against `voiceDrift`, which must
differ); `sag_does_not_restart_on_a_note`; `sag_period_matches_the_control`;
`sag_walks_the_same_trajectory_at_every_rate` (value at t = 10 s within 1e-3
across 44.1/48/96/192 k); `sag_reads_the_same_in_both_matrices`;
`sag_reaches_the_sub` — the one that would have caught the `centsA` mistake.

### `tezla-measure sonitus`, new table

Shepard window flatness against N; the Shepard centroid trajectory over one
octave at four host rates; interval-mode partial ratios per mode; the Tract
formant table (three centres and three Qs at 0.5 / 1.0 / 2.0); Sag walk mean,
bound and spectrum; ns per sample for Shepard on and off at 1/4/7 copies.

### Presets, at H5

House voice, no product or film names anywhere (§2.1): **Descent** (Shepard
falling into the comb), **Ascent** (the same, rising, into the filter),
**Cloister** (Scale cluster in Werckmeister III, slow swell), **Long Room**
(Tract 0.55, noise shape, vowel crawling), **Cellar** (fifths stack under deep
slow Sag), **Tritone Engine** (tritones, PM, sequencer on level — the action
pulse). Every preset's peak at full velocity is a measured number, per the
Sonitus level rule.

---

## Deferred, with the thing that would unpark each

Written down now rather than rediscovered, per CLAUDE.md §11 — an item nobody
can act on without asking a question first has not been written down properly.

- **`stackOrigin` (Centre / Up / Down).** Ranks are symmetric, so seven copies
  of Octaves puts one copy three octaves *below* the note — at A2 that is
  13.75 Hz, a rattle rather than a pitch. An organ registration is normally
  upward. **Unpark if** the octave stack sounds bottom-heavy on the rig; it is
  two more parameters and a one-line change to `rank(i)`.
- **Shepard shear** — A rising while B falls. **Unpark if** the single global
  rate feels like one gesture too few once it has been played.
- **Shepard panning by phase** rather than by rank, so the rising tone also
  sweeps the image. **Unpark** on the user's ear; it is a two-line change and a
  measurement of whether the mono sum still holds.
- **More vowels** stays blocked exactly as `README.md`'s roadmap says — on a
  source, and on the append-only decision about `formantMorph`. **Tract does not
  unblock it and does not touch it**: it is a new parameter with its own schema
  version, and the vowel list and the morph's meaning are untouched.
- **Self-oscillation** (Zavalishin's antisaturator) remains the next horror item
  after this phase, and remains scoped in `README.md`.

---

## Continuity — how any session resumes this work

Updated **in the same commit as each phase**, so whichever session picks this up
— after a context loss, a model change, or a fresh clone — needs nothing beyond
this file and CLAUDE.md.

**Phase status** (flip `pending` → `done` in the phase's own commit):

| phase | status |
|---|---|
| H0 plan | done |
| H1 shared DSP — `setRankOffsets`, `Shepard`, `SlowWalk` | done |
| H2 Stack | done |
| H3 Tract | done |
| H4 Sag | pending |
| H5 close-out | pending |

**What H1 actually landed, where it differs from the row above.** Three
headers rather than two, and one small move:

- `UnisonBank::setRankOffsets (cents, gains, count)` exactly as designed, plus
  `rankCentsOf` / `rankGainOf` for tests and a display. The normalisation is now
  `1/sqrt(sum of g^2)`.
- **`Shepard.hpp` came forward from H2 into the shared library.** The plan had
  every mode computed by the caller in `plugins/Sonitus/Dsp/StackShapes.hpp`,
  which is still right for the intervals and for Scale — those need the tuning.
  The Shepard arithmetic needs nothing but a phase and a count, and it is the
  piece carrying the theorem, so it belongs where it can be tested on its own.
  `shepardRanks` and `shepardWindowPower` are the API; H2 calls them.
- `SlowWalk.hpp` as designed, with the lurch in the target distribution.
- **`SmallRandom` moved to `SmallRandom.hpp`**, unchanged, because `SlowWalk`
  wanted it and "include the oscillator bank to get a random number" is not a
  dependency worth explaining. `UnisonBank.hpp` includes it, so every existing
  includer is untouched.

**H1's measured figures**, so H2 does not re-derive them:

| claim | figure |
|---|---|
| Shepard summed power, N = 3..7 | exactly 0.375 N, ripple 1.8e-15 .. 3.6e-15 |
| ...at N = 2 | ripple 5.0e-01 — the tremolo, and the break-check |
| Windowed stack against a flat one, through the bank | +0.003 dB (−4.263 dB without the generalised normalisation) |
| Level flatness sliding at 1 oct/s, 7 sines | 0.569 dB swing |
| Bit-exactness against the pre-change bank | 48 384 000 samples, identical FNV-1a; one ulp of frequency moves it |
| `SlowWalk` mean over 100 simulated minutes, six seeds | −0.0018 .. +0.0031 |
| ...with the lurch in the coefficients instead | −0.063, biased flat — why it is in the targets |

**Where this sits against the rest of the repository.** Sonitus phases 3 and 4
are complete (`PLAN-PHASE3.md`, `PLAN-PHASE4.md`); `PLAN-CPU.md` holds the
multicore and SIMD work **parked at the user's request**. Ictus is in flight at
`plugins/Ictus/PLAN.md`, complete through I4.2 plus the wires hold, with I5
(the punch chain) the first pending phase there. This phase and Ictus are
independent; whichever the user asks for next is the one to do.

**To resume:** read CLAUDE.md in full, then this file, then start at the first
`pending` row. The non-negotiables that bite hardest here:

- **Every new parameter at `kSchemaV8`, appended, neutral at its default.**
  Existing parameters keep the version they were born at, forever.
- **Every list in `choices::` is append-only**, and each is `static_assert`ed
  against the enum it indexes.
- **Bit-exact neutral is proved with a signal**, not assumed: Detune mode,
  Tract 1.0 and Sag 0 each get a bit-for-bit comparison. `Biquad::normalise`
  is the worked example of why (`a0 * (1/a0)` is not 1; `a0 / a0` is).
- **A passing test is worth nothing until it has been seen to fail.** Named
  break-checks above: N = 2 for the Shepard window, a skewed target for the
  walk, an expectation computed from the table under test for Scale mode.
- **§2.3 holds.** x86-64 Windows only: no ARM64 cross-build, no `qemu-aarch64`,
  no macOS CI, and every commit message says the cross-check was not run.
- **The acceptance test is the user's ears on the Windows rig**, not a green
  suite. Get Stack in front of them before H3 and H4 are polished — Shepard is
  the one that either lands or does not.
