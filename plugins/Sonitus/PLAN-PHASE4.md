<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# Sonitus — phase 4

**`Tzso` · "Tezla Sonitus" · `tech.tezla.Sonitus` · instrument.**

Phase 3 finished the synthesiser as a *sound designer's* instrument: seven
oscillator shapes with morph, three ADV envelopes, tempo sync everywhere, two
modulation matrices. Phase 4 is about the two things that are still hard to
reach — **richer timbre from the oscillators themselves**, and **fewer knobs
between an idea and the sound**.

Parked at the user's request after phase 3; resumed on their word.

## What phase 4 adds, and why each one

### 1. Operator feedback, and B → A cross PM

Today phase modulation runs one way: A modulates B. That is half an FM pair.
Two paths are missing and both are cheap:

- **Feedback** — an oscillator modulating *its own* phase. This is the classic
  DX-series operator feedback, and it is the single most useful FM control
  there is: a sine with rising feedback walks continuously from sine to saw to
  noise. On a reese it is exactly the "more teeth without more oscillators"
  control the patch always wants.
- **Cross PM** — B modulating A while A modulates B. That is a genuine
  feedback loop through two nonlinearities, and it is where the growl lives.

**This is the phase with the safety argument**, and CLAUDE.md §7 states it:
*a feedback loop around a nonlinearity needs a bound that cannot be defeated —
a soft clip inside the loop, plus a cap below unity on the amount — and a test
that sweeps the whole parameter space rather than sampling it.* Emberdrive's
Feedback stage is the worked example in this repository and the pattern to
follow. A cross-PM pair that can be driven to divergence is not shippable,
whatever it sounds like at the settings anyone tried.

The loop is broken by one sample in each direction, which is what makes it
computable at all. That delay is the design, not an artifact, and it gets said
out loud where it happens.

### 2. FM ratio readout

Two oscillators tuned by cents and semitones is right for detuning and wrong
for FM: what an FM patch cares about is the **ratio**, and whether it is
simple. 2:1 and 3:2 are harmonic and sound like one instrument; 2.03:1 is a
beating mess; √2:1 is a bell.

So the OSC page states the ratio Sonitus is *actually* running from the two
pitch controls, in lowest terms when it is close to a simple one, and flags
how far off it is. No new parameter — a readout, computed from what is already
there. It is the difference between guessing at a ratio and setting one.

### 3. Filter Morph

The filter has four modes as a **choice**: lowpass, bandpass, highpass, notch.
A choice cannot be modulated (CLAUDE.md §8: destinations hold continuous
controls only, by construction), so the filter's character is the one thing
in the voice a envelope cannot sweep.

Morph adds a continuous control that crossfades **LP → BP → HP**, and being
continuous it becomes a modulation destination. Notch stays on the choice: it
is not on the line between the other three and does not belong on a slider
between them.

At Morph 0 the filter must be **bit-exact** against the existing lowpass, and
that is not a nicety — the mode choice is frozen and every saved project uses
it. The default is 0.

### 4. Scale-locked comb

The comb already key-tracks. But its delay is a continuous frequency, so on a
microtuned patch — which is half of why Sonitus exists — the comb's resonance
sits between the scale's notes and fights the tuning it is supposed to serve.

Scale lock snaps the comb's tuned frequency onto the **loaded scale**, so the
comb resonates at a pitch that is in the tuning. On 12-TET it is a small
convenience; on Partch's 43-tone or a Persian scale it is the difference
between a comb that belongs and one that does not.

Off by default, and off must be bit-exact against today's comb.

### 5. Four macros

Twenty-seven destinations and thirteen sources is a lot of matrix. A macro is
one knob wired to several things at once — the control a *performance* needs
and a matrix cannot give, because a matrix has one source per row.

Four macro knobs, each a source in **both** matrices, appended to both source
lists. Neutral at their default so nothing changes until one is assigned.

### 6. Close-out

`tezla-measure sonitus` gains a feedback-stability sweep and a morph-response
table; README; validator on all eleven; showcase presets that use the new
controls rather than merely tolerating them.

---

## Phases

One phase, one commit. Tests written **and run** in that commit, every
mechanism seen red or break-checked with the numbers pinned, the whole tree
built, and "the qemu-aarch64 cross-check was not run" stated per §2.3.

- **P4-0 — this file.** The plan and its Continuity section.
- **P4-1 — operator feedback and cross PM.** `Oscillator` gains self-phase
  feedback; the voice gains a B → A path. **Both bounded so they cannot
  diverge**, with a soft clip inside the loop and a cap below unity, per §7.
  Tests: zero feedback is bit-exact against today's output; a full sweep of
  the whole (feedbackA, feedbackB, pmAB, pmBA) space stays bounded and finite,
  not a sample of it; the one-sample delay is measured rather than assumed;
  a sine with rising feedback shows the sine → saw progression in its harmonic
  series.
- **P4-2 — FM ratio readout.** Framework-free ratio-in-lowest-terms helper
  with a tolerance, tested against a table; the OSC page shows it.
- **P4-3 — filter Morph.** `SvfFilter` gains a continuous LP → BP → HP morph.
  Tests: morph 0 is **bit-exact** against `SvfMode::lowpass`; 0.5 and 1.0
  match bandpass and highpass to within the crossfade's own arithmetic; the
  response at the three corners measured, not asserted; the new destination
  appended and its `static_assert` updated.
- **P4-4 — scale-locked comb.** Snap the comb's tuned frequency onto the
  loaded scale. Tests: off is bit-exact; on, the comb's frequency is a scale
  degree for every note across several tunings; the snap is stable (no
  flicker at the boundary between two degrees).
- **P4-5 — four macros.** Appended to `ModSource` and `GlobalSource` and to
  both choice lists, with both `static_assert`s updated. Tests: a macro at its
  default changes nothing, bit-exact; a macro assigned in both matrices moves
  both; state round-trips.
- **P4-6 — close-out.** Measure command, README, presets, validator 47/47 on
  all eleven, screenshots.

## Risks

- **Cross PM is a feedback loop and can be driven to divergence.** This is the
  real risk of the phase and the reason P4-1 comes first: if it cannot be made
  unconditionally bounded, it ships as feedback-only (which is not a loop) and
  cross PM is cut. The sweep test is the gate, and it runs before the JUCE
  layer knows the feature exists.
- **Changing `SvfFilter` touches every plugin that uses it.** Mitigated the
  same way `GainComputer` was for Phonoss: the new path is off by default and
  the old one has to stay bit-exact, with a test pinning it.
- **Two more source-list entries in each matrix.** Append-only, `static_assert`
  updated, and a saved project must reopen sounding the same — the test that
  already exists for the phase-3 appends is the pattern.

## Cut lines, in order, if scope presses

1. **Cross PM** (keep operator feedback, which is not a loop and carries most
   of the value).
2. **Scale-locked comb** (the tuning engine is already the differentiator;
   this is a refinement of it).
3. **Two of the four macros** (two is still useful; four is comfortable).

Never cut: the bounded-feedback sweep, the bit-exact neutral defaults, the
append-only discipline, the aliasing measurements.

---

## Continuity — how any session resumes this work

Updated **in the same commit as each phase**, so whichever session picks this
up — after a context loss, a model change, or a fresh clone — needs nothing
beyond this file and CLAUDE.md.

**Phase status** (flip `pending` -> `done` in the phase's commit):

| phase | status |
|---|---|
| P4-0 plan | done |
| P4-1 operator feedback + cross PM | done |
| P4-2 FM ratio readout | done |
| P4-3 filter Morph | done |
| P4-4 scale-locked comb | done |
| P4-5 four macros | done |
| P4-6 close-out | done |

**Work done on Sonitus outside this list**, so a resuming session is not
surprised by it: the DICEROLL tab (a seventh page -- RANDOMIZE, roll history,
per-section locks with solo, and the AMOUNT/SPREAD strengths), the ADV
envelopes' point ceiling raised from 8 to 16, and the synced envelopes' tempo
ruler. Both were asked for
directly and neither belongs to a phase; both are finished and pushed. The
point ceiling matters to anything here that touches the ADV parameters --
points 1..8 carry schema V2 and 9..16 carry V4.

**Phase 4 is complete.** Nothing here is pending. What a later phase would
pick up, in the order it was cut rather than in any order of merit:

- The macros are four knobs with no *page* of their own -- they sit on MOD.
  Eight, or a macro that could be renamed, would want somewhere to live.
- Morph covers lowpass -> bandpass -> highpass. Notch is deliberately off the
  axis (see `SvfFilter::setMorph`), and a second axis for it is a design
  question rather than a missing feature.
- The scale lock snaps the comb. The formant's harmonic lock and the phaser's
  centre are the two other continuous frequencies in the instrument that a
  tuning could own.

**To resume** (a later phase, or the parked work elsewhere): read CLAUDE.md in
full, then this file. The non-negotiables:

- One phase = one commit. Tests written and RUN in that commit; every
  mechanism seen red first or break-checked (edit -> red -> revert), with the
  measured numbers pinned in the test comments and quoted in the commit.
- Build the whole tree before pushing (no `--target`), run all tests, and run
  Steinberg's validator on any plugin whose bundle changed. `SvfFilter` and
  the shared UI are used by more than Sonitus.
- The qemu-aarch64 cross-check is **deliberately not run** (CLAUDE.md §2.3);
  say so in every commit rather than implying coverage.
- New first-party files carry the six-line licence header. No model
  identifiers in anything pushed.
- **The frozen things**: `ModSource`, `GlobalSource`, `ModDestination`,
  `dest::`, `division::` and every `StringArray` behind a choice parameter are
  **append-only**, and each has a `static_assert` pairing it with its list.
  A stored slot is an index. Adding in the middle silently repoints every
  saved patch.
- Neutral settings are **bit-exact**, not merely transparent, and there is a
  test that feeds signal through and compares bit for bit.

**Prior art in this repository**: Emberdrive's Feedback stage for the bounded
loop (`plugins/Emberdrive/Dsp/`), Phonoss for the "static predicate + bit-exact
neutral" pattern, `plugins/Malleus/PLAN.md` for the phase shape.
