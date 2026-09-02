<!--
Copyright (c) 2026 The Tezla <thetezla@proton.me>
Created by The Tezla -- https://github.com/wingit33/tezla.tech
Music: https://soundcloud.com/thetezla | https://thetezla.bandcamp.com
Built with development assistance from Claude (Anthropic).
SPDX-License-Identifier: AGPL-3.0-only
GNU AGPLv3 (see LICENSE), plus NOTICE.md's attribution term. Keep intact.
-->

# Roadmap — the backburner

**Nothing in this file is in flight.** Work that *is* in flight lives in the
active plugin's `PLAN.md`, and [`CLAUDE.md`](../CLAUDE.md) §11 names which one.
This is the other list: things deliberately parked, each with the reason it was
parked and the specific thing that would unpark it.

It is written to be read cold, by a session that was not there when the decision
was made. So each item says what it *would change* — not "improve Membrana", but
which constants move and what measurement decides them. An item nobody can act
on without asking a question first is an item that has not been written down
properly.

---

## 1. Membrana — recalibrate the thresholds against real recorded vocals

**Status: parked, waiting on one file from the user.**

Membrana v0.1.0 ships with thresholds chosen from first principles and verified
against synthetic signals — sine bursts, filtered noise, a synthesised vowel.
Every mechanism is proven correct against those. What has *never* been checked
is whether the defaults sit in the right place for material the user actually
records, because no such material has been through the plugin here.

Two parameters and six internal constants depend on that, and only on that:

| what | current | why it is a guess |
|---|---|---|
| `presThresh` default | **−28 dBFS** | The level at which the presence ride starts leaning in. A voice tracked hot at −6 dBFS peaks never reaches it; one tracked at −20 dBFS sits under it permanently and the ride is a static shelf. |
| `detFloor` default | **−55 dBFS** | Everything at or below it is lifted exactly 0.000 dB. Set above the room tone and breath is lost with the hiss; set below it and the hiss comes up with the breath. |
| `PresenceTracker::kKneeDb` | 12 dB | How far below the threshold the ride reaches full lift. |
| `PresenceTracker::kAttackSeconds` / `kReleaseSeconds` | 0.120 / 0.400 | The engineer's hand. Too fast reads as a compressor. |
| `DetailLift::kWindowDb` | 20 dB | The band above the floor that gets lifted — i.e. what counts as "detail". |
| `DetailLift::kKneeDb` / `kFloorEaseDb` | 15 / 6 dB | The two shoulders of the window. |
| `DetailLift::kMeanSeconds` | 0.005 | The detector's magnitude mean, added because an instantaneous magnitude through a non-monotone window curve settled 2 dB low. |

### What would settle it

**A dry, unnormalised WAV**, at whatever level the user's chain actually
records at, with **nothing** done to it — no compression, no normalising, no
noise reduction, no gain staging after the interface. Twenty seconds is plenty
if it contains all four of: a sustained vowel, a quiet spoken line, a shouted or
belted line, and at least a second of the room with nobody singing.

Every one of those four does a specific job:

- the **sustained vowel** says whether the detail detector is genuinely blind to
  the body of the voice on real material, as it is on a synthesised vowel
  (measured: exactly zero lift);
- the **quiet line** and the **loud line** together are the ride's working
  range, and their difference is what `presThresh` and `kKneeDb` have to
  straddle;
- the **room tone** is the only honest way to place `detFloor`. It is currently
  a guess at a number that is a property of the user's room and preamp, not of
  the plugin.

### What it is not for

**This is measurement, not listening.** Nothing here needs anyone's ears, and
nothing here is a judgement about whether the plugin sounds good — that is the
user's call and always has been. The file gets run through
`tezla-measure membrana` extended with a file-input mode, and the output is a
histogram of where the material sits against each threshold. Defaults follow
from the histogram.

The corollary matters: **a sample cannot make the mechanisms better, only the
defaults.** The bounded lifts, the absolute floor and the bit-exact neutral path
are structural and are already measured. If the histogram says the defaults are
fine, the correct outcome is to change nothing and record that they were
checked.

### The IP line, restated because this is exactly where it would blur

A vocal recording is **material to measure levels on**. It is not a source of
curves. Nothing about a supplied file may become a fitted response, a captured
impulse, or a preset named after whatever it was recorded on. CLAUDE.md §2.1
holds unchanged: Membrana's model is physics, and a WAV moves default numbers
around inside that model without touching it.

---

## 2. Prism (`Tzpr`) — the multiband enhancer

**Status: parked at the user's request.** The name and code are reserved in
[`plugins/README.md`](../plugins/README.md) so they cannot be spent on something
else.

Four bands of harmonic generation plus per-band stereo width. It is deliberately
its own plugin rather than a mode of Halo: it costs considerably more CPU and
signal complexity than belongs in a tool used on a single channel.

Unparking it means writing `plugins/Prism/PLAN.md` first, in the shape the
finished plans use — `Malleus/PLAN-PHASE2.md` and `Membrana/PLAN.md` are the
worked examples.

---

## 3. macOS and ARM64 — the §2.3 gate

**Status: gated, and the gate is doing its job.** CLAUDE.md §2.3 defers all
ARM64 and macOS work until the x86-64 Windows build is bug-squashed and
feature-complete, because the loop that matters is build → load → play → say
what is wrong, and that loop runs on Windows 11 and FL Studio.

The gate has been lifted **once**, deliberately and narrowly: on 2026-09-01 the
user asked for a macOS/Apple-Silicon build of Sonitus and a full-suite release
through CI. That produced the `v0.88.8-sonitus` and `v0.88.8` runs. It did not
change the standing rule, and the next ARM64 or macOS run needs the same kind of
explicit ask — or the gate lifting properly, which is the user saying the
Windows features are finalised and the bugs are gone.

What still has not happened, and what CLAUDE.md §5 insists on saying plainly:
**nobody has loaded any of these bundles in a DAW on either platform.** CI green
means the code compiles and the DSP measures correctly. It does not mean FL
Studio scans it, Logic loads the AU, or Gatekeeper lets a downloaded build run.

---

## 4. Validators in CI

**Status: not started; a natural next step, left out to keep runs quick.**

Steinberg's `validator` and Apple's `auval` both catch real problems, and both
are run by hand today — all twelve bundles currently pass 47/47 on Linux. Wiring
them into the `build` job would make that automatic. The build instructions for
`validator` are in CLAUDE.md §10; `auval` needs a macOS runner and therefore
sits behind item 3.

## 5. Code signing and notarisation

**Status: blocked on a paid Apple Developer account**, which is a purchase, not
a task.

Until then every macOS download is quarantined and the DAW reports it as
damaged. Every document and release note that offers a macOS download must say
how to clear the attribute — the workflow writes the `xattr -dr
com.apple.quarantine` lines into the release notes automatically, but *only when
a macOS build was actually in the run*. See
[`docs/CI.md`](CI.md#downloaded-builds-and-macos-quarantine) and
[`docs/BUILD-MACOS.md`](BUILD-MACOS.md) §4.

## 6. CLAP

**Status: cheap, and deliberately not done.** CLAUDE.md §2.2: adding CLAP later
is fine, and it must not be added without being asked. JUCE 9.0.1 has no native
CLAP output, so it means pulling in `clap-juce-extensions`. Because §4 keeps all
DSP framework-free, the cost is a wrapper rather than a rewrite — which is
precisely why there is no hurry.

---

## 7. Sonitus CPU — the optimisations that would change the sound, parked

**Status: parked at the user's request, deliberately.** The brief for the
Sonitus CPU work of 2026-09-01 was that *the sound and its harmonics must not
change in any way*, so only bit-exact changes were made (guards on per-chunk
setters, a shared filter coefficient — see the "×8 stress case" section of
`plugins/Sonitus/README.md` for the measurements). Everything below would
change the output, however slightly, and so waits.

The measured picture it waits against: ×8 with 224 oscillators costs 1973 ms
per second of audio here, exactly linear in the factor; oscillators are ~60 %
of it, the filter's drive rail ~21 %, fold ~11 %, tube ~7 %, the sine sub ~5 %;
LTO is not a lever (−6 % at best, and GCC's is slower).

### What is parked, largest first

1. **Oscillators and sub at the base rate, nonlinear path oversampled per
   voice.** A polyBLEP saw is already band-limited; oversampling it removes
   aliasing that is not there. Rendering the oscillator mix at the host rate and
   upsampling per voice into the fold/filter/tube would take roughly half of
   the ×8 cost off and turn "×8 = 8× everything" into "×8 = 8× the nonlinear
   stages". It changes the signal path — a halfband per voice, so the §6
   rate-independence null and the §7 aliasing sweep must be re-measured at
   every factor — and PM, feedback and hard sync alias on their own, so those
   oscillators must stay at the internal rate when engaged.
2. **Cheaper transcendentals**: the sub's `std::sin` (phase rotator or table),
   the SVF rail's `std::tanh` (rational approximation), the tube's `std::pow`
   (integer-power or polynomial). About −20 % together. Each differs from libm
   by a few units in the last place: inaudible, and not bit-identical.
3. **Filter coefficients per control chunk instead of per sample.** The
   cutoff smoother is a one-pole that never quite settles, so a modulated
   cutoff recomputes a `tan` per filter per sample; updating every 32 samples
   would remove that, and would quantise the sweep at 12 kHz — a different
   sweep, if an indistinguishable one.
4. **Skip the formant bank at mix 0** (0.5 %). Only worth listing because it
   looks free and is not: skipping leaves the bank's state cold at the moment
   the knob is engaged.

### What would unpark each

**The user saying so, per item, after seeing the evidence** — a null test
between the current output and the candidate, rendered on the golden patch
set, with the residual's peak in dBFS. Items 2–4 are contained and could be
offered that way in an afternoon each; item 1 is a design change with its own
plan and measurement pass. None of them is worth doing quietly, and none will
be.

## 8. Sonitus CPU — multicore voices and a lane-parallel unison bank, parked

**Parked 2026-09-02 at the user's request**, with the plan written and kept:
`plugins/Sonitus/PLAN-CPU.md`. Both items are bit-exact by design — voices
rendered on worker threads and summed in voice order; the polynomial oscillator
shapes (saw, pulse, triangle, double saw) advanced in SIMD lanes against the
scalar `Oscillator` as the reference — and neither has a line of code yet.

**What would unpark it:** the user saying go, item by item, multicore first.
The plan names the phases, the tests that must be seen red, and the
measurements (`tezla-measure sonitus-stress`, the 32 goldens) that decide
whether each phase lands.

## 9. Ictus — ideas parked at the plan stage

**Parked 2026-09-02 while the plan was written**, so that the first build is
the plan and not the plan plus its afterthoughts. Each is a contained addition
to a plugin whose engines are described in `plugins/Ictus/PLAN.md`; none is a
change to anything already decided.

1. **A per-session "unit tolerance" offset.** The TR-808 bass-drum paper
   (Werner, Abel, Smith, DAFx-14) attributes unit-to-unit differences to part
   tolerances. Ictus's *Humanise* is per hit; a small static offset drawn once
   at `prepare` — the Sonitus voice-temperature idea — would make two instances
   of the same kit differ the way two machines do. Would change: a second,
   smaller deviation table applied once per instance, behind the same
   Humanise knob. Decided by: the user hearing two instances side by side.
2. **Kick retrigger mode, *restart / add*.** The 808 adds its retrigger pulse
   to the still-ringing resonator rather than restarting it (the paper's §8),
   which is why it has no machine-gun effect. Ictus restarts with a 1 ms
   crossfade. An *add* mode would excite the running body instead. Would
   change: one append-only choice on the kick pad and a second `noteOn` path.
   Decided by: a fast roll on the rig.
3. **A "shifted series" shell for the kick.** Sound On Sound's bass-drum
   analysis shows a real kick's partials as a harmonic series shifted by a
   constant (43 Hz + 7 Hz → 50, 93, 136, 179 Hz). The kick's *Harmonics* are
   true harmonics of the body; a small modal shell at shifted partials would
   add the acoustic-kick inharmonicity. Would change: a three-mode
   `ModalResonator` on the kick pad with a *Shell* level, exact at 0.
   Decided by: whether a DnB kick ever wants it — likely not, hence parked.
4. **A "Both" output option** (a pad on its own bus *and* in Main). Rejected
   from v1 because it duplicates audio; trivial to append to the `output`
   choice list if a workflow needs it.
5. **Svarayantra adopting `SincInterpolator`.** Its 4-point Hermite has the
   same top-end droop (−4.4 dB at 20 kHz for a 44.1 k source) and first image
   (−8 dB) that were measured when Ictus's sample layer was designed. Would
   change: the player's read path and its measured interpolation figures;
   bit-exactness with existing renders is lost by design. Decided by: the
   user asking, after hearing the Ictus layer.

**What would unpark each:** the user saying so, per item.
