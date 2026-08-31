<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# Malleus — phase 2

**`Tzml` · "Tezla Malleus" · `tech.tezla.Malleus` · instrument.**

Phase 1 built the object and four ways to hit it. What it does not yet do is
the thing that separates a *modal bank* from an *instrument*: everything in it
is **linear**, **undamped by the player**, and **heard from one point**.

Phase 2 fixes those three, each from the same physics the mode tables came
from, plus the playability gap that a single exciter leaves.

## What phase 2 adds, and why each one

### 1. Bloom — the modes talk to each other

This is the flagship, and it is the biggest single thing missing.

Every mode in Malleus today is independent: struck, it rings down, and nothing
it does affects any other mode. Real objects are not like that. Hit a tam-tam
hard and the sound **builds after the strike** — a shimmer that was not there
at the moment of contact climbs out of the low modes over the next second.
That is not reverb and it is not a filter sweep. It is energy migrating
*upward* through the mode bank, and it is why a gong sample never sounds like
a gong when you play it quietly.

The physics is the geometric (von Kármán) nonlinearity: when a plate's
displacement is comparable to its thickness, the restoring force stops being
linear in displacement, and the quadratic term couples mode *triads* —
f₁ + f₂ → f₃. The audible consequence is a cascade from low modes to high
ones whose rate depends on amplitude, which is exactly the "only when you hit
it hard" behaviour.

Cheaply and honestly: square the bank's own output (a quadratic term produces
precisely the sum and difference tones a quadratic coupling does), bound it,
and inject it into the modes **above** the strike's centre of energy. Bloom
sets the coupling strength; zero is bit-exact off.

**This is a feedback loop around a nonlinearity**, so it carries CLAUDE.md
§7's full kit — a bound that cannot be defeated, and a test that sweeps the
whole parameter space rather than sampling it. The sympathetic bank's Drone
already establishes the pattern in this plugin.

### 2. Damp — a hand on the object

Percussion is *played* with damping. A palm on a drum head, fingers on a
cymbal's edge, the heel of a hand stopping a gong. Malleus has no way to do
it: Decay sets the ring once, and the only way to shorten a note is to have
set it shorter before you played it.

Damp adds loss, per mode, **proportional to frequency** — because that is what
a hand does. Soft tissue absorbs high partials far faster than low ones, which
is why a damped cymbal goes dull before it goes quiet rather than just fading.
A flat decay multiplier would be a volume pedal.

Continuous and modulatable, so aftertouch or a pedal can play it. Zero is
bit-exact off.

### 3. Two exciters, and velocity that chooses between them

Today the exciter is a **choice**, so an object can be struck or plucked or
bowed but not struck *with anything else*. Two things follow from that, and
both are playability rather than physics:

- **Layering.** A real strike is a contact *and* a scrape. Two exciters with a
  blend gives a mallet with a fingernail on it, or a bow with a pluck to start
  it — which is how a bowed string is actually begun.
- **Velocity picks the hardness.** On a real drum a soft hit is felt and a
  hard hit is stick, because the same mallet compresses differently. One
  Hardness control with a velocity amount does that in one gesture, and it is
  the single biggest thing standing between this plugin and a playable drum.

### 4. Two listening positions

The strike already weights each mode by sin(kπp) — where you hit it decides
which modes you excite. **Where you listen from decides which modes you
hear**, by exactly the same expression, and today there is only one listening
point and it is implicit.

So: two output taps at two positions on the object, left and right. That is
genuine stereo from the geometry rather than a widener — and the mono sum is
honest, because both taps are real listening points and summing them is what
standing in front of the object does.

Worth measuring rather than assuming: the two taps share modes, so the sum
can *cancel* at particular position pairs. That number goes in the tooltip.

### 5–6. JUCE layer, then close-out

Parameters at schema v2, appended destinations, presets that use the new
controls, `tezla-measure malleus` extended with a bloom-stability sweep and a
damping-slope table, README, validator on all eleven.

---

## Phases

One phase, one commit. Tests written **and run** in that commit, every
mechanism seen red or break-checked with the numbers pinned, the whole tree
built, and "the qemu-aarch64 cross-check was not run" stated per §2.3.

- **M2-0 — this file.**
- **M2-1 — Bloom.** Nonlinear mode coupling in `ModalResonator`. Tests: zero
  bloom is **bit-exact** against today's bank; a swept parameter space stays
  bounded and finite rather than a sample of it; energy genuinely moves
  *upward* (the high-mode fraction rises after the strike, measured, and does
  not at bloom 0); the effect is **amplitude-dependent** (a quiet strike
  blooms measurably less than a loud one, which is what makes it physical
  rather than a shelf).
- **M2-2 — Damp.** Frequency-proportional loss. Tests: zero is bit-exact; the
  decay of a high mode shortens more than a low one, with the slope measured
  and pinned; the object still rings rather than being muted; retuning damping
  mid-note does not click (a no-op guard, and a state-preserving change).
- **M2-3 — Two exciters and velocity hardness.** Tests: the blend at either
  end is bit-exact the single exciter it came from; velocity-to-hardness at
  zero amount is bit-exact; the measured strike centroid rises with velocity
  by the amount asked for.
- **M2-4 — Two listening positions.** Tests: both positions equal is bit-exact
  mono (both channels identical); a mode on a node at one position is exactly
  absent there; the mono-sum cancellation measured across the position pair
  grid, not assumed.
- **M2-5 — JUCE layer and editor.** Schema v2 parameters, appended
  destinations with `static_assert`s updated, controls on the panel, presets.
- **M2-6 — close-out.** Measure command, README, validator 47/47 on all
  eleven, screenshots.

## Risks

- **Bloom is a feedback loop and can be driven to divergence.** The reason it
  is first. If it cannot be made unconditionally bounded it does not ship, and
  the sweep is the gate — before the JUCE layer knows it exists.
- **`ModalResonator` is shared.** Malleus is its only user today, but it lives
  in `shared/`, so the new paths are off by default and the old ones stay
  bit-exact with a test pinning them.
- **Two listening positions can cancel in mono.** That is physically honest
  and musically a trap. Measured across the grid, and the tooltip names the
  pairs to avoid rather than the panel silently doing something safe.
- **Four new controls on an already dense panel.** Malleus's editor has room;
  if it does not, the mode-stack visualiser earns its space more than a fourth
  row of knobs does and the close-out says so.

## Cut lines, in order, if scope presses

1. **Two listening positions** (the object is already stereo through the
   sympathetic bank's spread).
2. **Exciter layering**, keeping velocity-to-hardness — which is the half that
   makes it playable.
3. **Damp's** modulation routing, shipping it as a plain control.

Never cut: the bloom sweep, the bit-exact neutral defaults, the append-only
discipline, the drop-don't-fold partial rule.

---

## Continuity — how any session resumes this work

Updated **in the same commit as each phase**.

**Phase status** (flip `pending` -> `done` in the phase's commit):

| phase | status |
|---|---|
| M2-0 plan | done |
| M2-1 Bloom | pending |
| M2-2 Damp | pending |
| M2-3 two exciters + velocity hardness | pending |
| M2-4 two listening positions | pending |
| M2-5 JUCE layer and editor | pending |
| M2-6 close-out | pending |

**To resume**: read CLAUDE.md in full, then `plugins/Malleus/PLAN.md` (phase
1, complete) and this file; take the first `pending` phase. The
non-negotiables are the ones listed at the end of phase 1's plan, unchanged,
plus:

- **`ModalResonator` lives in `shared/`.** Anything added to it is off by
  default and the existing path stays bit-exact, pinned by a test that feeds
  signal through and compares bit for bit.
- **A partial above 0.45 x the sample rate is dropped, not folded.** That rule
  is why this instrument oversamples nowhere, and anything phase 2 injects
  into the bank has to respect it or the aliasing figures stop being true.
- Voices must measurably **die**: assert activity, not silence.
