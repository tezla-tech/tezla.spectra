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

6. **The plate's cascade — the build-up an open hat has and this one does
   not.** Rossing measured a cymbal's 2–10 kHz band building by 10 dB or more
   in the first 50 ms after the strike (Fletcher & Rossing §20.3), energy
   climbing out of the low modes through the quadratic coupling Chaigne, Touzé
   & Thomas describe; Harrison & Hill saw it on 14-inch hi-hats. The shared
   bank's Bloom is that coupling and it was tried on the plate at I4.3 and
   removed with the numbers (`plugins/Ictus/PLAN.md`): on 64 modes tens of
   hertz apart the coupling term's difference tones drive every mode's forced
   response at about 1/(2 sin(ω/2)) times the drive — 150× at 205 Hz — and the
   first 80 ms collapsed to a centroid of 128 Hz. **What would work:** a
   coherent per-mode drive, not an impulse spread in time (a slow unipolar push
   cannot excite a fast mode). The cheap route is a per-mode output-gain ramp
   over ~50 ms on the modes above ~1 kHz, interpolated per sample so it puts no
   sidebands at the control rate — about 64 multiplies a sample for 50 ms —
   with the equations of F&R §20.6 (20.5–20.6) giving each mode's rise time.
   Would change: the open pad only; the closed pair does not cascade. Decided
   by: the user hearing the open hat and wanting the sizzle to *grow*.
7. **Split doublets on the plate.** Every m > 0 mode of a real cymbal is a
   pair split by a fraction of a percent to a few percent (Perrin et al.: four
   peaks within 15 Hz at 340 Hz), and the beating between partners is part of
   a long ride's shimmer. The plate spends its 64 modes on distinct
   frequencies with a deterministic jitter instead. Would change: the lowest
   16 modes as pairs (32 slots) plus 32 singles above — the splitting is only
   audible where modes are resolvable. Decided by: the open hat on the rig.
8. **Grit at the host rate.** The hat's Grit runs inside the oversampled
   section, so it is quantisation-as-saturation and the folded images CLAUDE.md
   §7 reserves for a crusher are removed. A per-pad host-rate stage needs the
   per-pad buses of I7. Would change: Grit moves after the pad's own
   decimation, gaining the imaging of a low-rate sample path. Decided by: I7
   landing, and the user preferring it.
9. **Fletcher & Rossing against the kick, the snares and Malleus.** The user
   has supplied the complete book (2026-09-05) and only §3.6 and ch. 20 have
   been read against an engine. The chapter map in `plugins/Ictus/PLAN.md`
   ("What else the book bears on") names what each remaining chapter would
   settle: 18.9–18.13 for the kick's shell and the snare's head modes and wires,
   19 and 21 for Malleus's bar and bell tables (the bell row in
   `docs/DSP-REFERENCES.md` is still marked second-hand), 20.5–20.9 for
   Malleus's Bloom against measured gongs. Would change: numbers, checked
   against measurements, never models. Decided by: the next round on each of
   those engines.

10. **A per-pad presets gate for the drum machine.** `tezla-render presets`
   plays one note at full velocity — note 36, the kick — which is the right
   gate for a synthesiser and blind for a drum machine: a hat-only preset reads
   as the default kick (found at I4.3, where *Lush Hats* and *Fat Hats* both
   read −2.0 dBFS, identical to *Init Kit*). At I4.4 the same gate read the
   untouched kit presets *DnB Tight* at **+0.7 dBFS** and *Jungle Snap* at
   **+4.2 dBFS** over the kick alone — pre-existing, since the kick path at
   neutral is bit-identical (golden render), and left as they are because the
   kit presets are the user's to trim (I4.2). Would change: the gate strikes
   every pad a preset touches, or all eight, and reports the loudest. Decided
   by: the next preset round on Ictus.

11. **The velvet-noise reverberation papers, read first-hand.** `dsp::EarlyReflections`
   (Ictus I4.5's Room) has the *shape* of velvet noise -- sparse random-sign taps at
   random positions within equal cells -- known second-hand: the proxy refuses
   dafx.de and aes.org, so neither paper was read (CLAUDE.md §9; the row in
   `docs/DSP-REFERENCES.md` says so). Nothing numeric was taken; the tap count, the
   30 dB fall and the normalisation are this project's. Would change: the row's
   status, and possibly the tap density or the decay law if the papers argue for
   different ones. Decided by: the user fetching M. Karjalainen & H. Järveläinen,
   "Reverberation Modeling Using Velvet Noise", AES 30th International Conference
   (2007), and V. Välimäki, H.-M. Lehtonen & M. Takanen, "A Perceptual Study on
   Velvet Noise and Its Variants at Different Pulse Densities", DAFx-13 -- both
   into `technical references/drumsynth/`.
12. **Kick 2 and Perc have no pages, so they have no side.** Kick 2 plays the kick
   engine's defaults and Perc the tom defaults; neither is pulled from a parameter
   set of its own, so neither can spread or have a room, and their Width and Mono
   below on the MIX page stay greyed by construction. Would change: a KICK 2 page
   (a second `k2*` parameter set at the next schema) and a PERC page, each with a
   room; the MIX page's greying then lights up on its own. Decided by: the user
   wanting a second kick or a tom that is not the defaults -- I4's plan always
   meant them to arrive with I9's per-pad pages.
13. **The clap layer through its own room, and a layer under the ghost.** The clap
   under Snare 1 goes through the snare's room, at the snare's pan, which is what a
   layered hit should do; a second clap under the ghost, or the layer with the CLAP
   page's own room instead, are one parameter each. Would change: `g1Clap` /
   `g1ClapOffset`, and a Layer room choice. Decided by: the user hearing Clap Snare
   and asking.

**What would unpark each:** the user saying so, per item.

## 10. Sonitus declares twice its latency

**Found 2026-09-02 on Ictus's first kick**, and left alone in Sonitus until
the user decides, because fixing it moves its output by half a sample and
its declared PDC by tens of samples.

`Oversampler::getLatencySamples()` is the **round trip** — upsample and
downsample, what an effect incurs — and its tap counts (95/65/65) are chosen
so that sum is a whole number of host samples: 47 / 63 / 71 at ×2 / ×4 / ×8.
An instrument writes straight into `internalBuffers()` and runs only the
decimation half, so its real delay is **half of that, and a half-sample**:
23.5 / 31.5 / 35.5. Sonitus is the one other instrument on that path (the
others do not oversample their generators), and it declares the round-trip
figure, so FL Studio's PDC advances it by 23.5 / 31.5 / 35.5 host samples too
much (0.66 ms at 48 kHz ×4). Ictus measured it (`tests/test_Ictus.cpp`, the latency test), delays
its internal signal by `factor / 2` samples before decimating and declares
24 / 32 / 36 — exact to a residual of 1e-6 against the undecimated render.

**Would change**: the same `factor / 2` alignment delay in Sonitus's
process loop, `getLatencySamples()` returning `(roundTrip + 1) / 2`, and its
"new latency" tests updated. The output shifts by half a host sample, so
existing renders no longer null against new ones (the 32 goldens included),
and every saved project's PDC moves.

**What would unpark it:** the user saying so.

## 11. Sonitus phase 5 — ideas parked at the plan stage

**Parked 2026-09-03 while `plugins/Sonitus/PLAN-PHASE5.md` was written**, so the
first build is the plan and not the plan plus its afterthoughts. **Phase 5 is
complete**: **Stack**, **Tract** and **Sag** all shipped. Four items were cut
from it deliberately. **Phase 6 then shipped three of them**, on the same day
and at the user's ask, so only the fourth is still parked.

1. ~~**`stackOrigin` — Centre / Up / Down.**~~ **Shipped in phase 6** as
   `stackOriginA`/`stackOriginB` at schema V9. Exactly one copy stays on the
   played note at every count and every origin, so it moves the stack's weight
   and never its tuning; greyed in Detune mode, which has no side to build on.
2. ~~**Shepard shear — A rising while B falls.**~~ **Shipped in phase 6** as
   `shepardShear`. A second accumulator advanced by `(1 − 2·shear)` of the
   first's step, so 0 is the shared phase that shipped (bit-identical), 0.5
   holds B still, 1 makes B fall exactly as fast as A rises.
3. ~~**Shepard panning by phase rather than by rank.**~~ **Shipped in phase 6**
   as `shepardPanA`/`shepardPanB` — and the measurement turned the reasoning
   here on its head, which is worth recording. This entry predicted panning by
   phase would "sweep it across the field as it climbs". Per copy it does; the
   *ensemble* does the opposite, because phase sets a copy's pitch, its window
   gain and its position together, so one copy always arrives where another
   leaves. Measured over five seconds, panning by rank swings a band's balance
   from −0.159 to −0.524 to +0.497 while panning by phase reads −0.182 three
   times: it is rank panning that churns, and phase panning that produces a
   **stationary fan**, low at one side and high at the other. Numbers in
   `StackShapes.hpp` and `tests/test_Stack.cpp`.
4. **More vowels stays blocked exactly as it was**, on a source and on the
   append-only decision about `formantMorph` (`plugins/Sonitus/README.md`,
   Roadmap). **Tract does not unblock it and does not touch it**: it is a new
   parameter at its own schema version, and the vowel list and the meaning of a
   stored morph position are unchanged. **This is the one still parked.**

**Self-oscillation also shipped in phase 6**, and not by the route scoped here.
The plan was Zavalishin's antisaturator; the measurement rejected two designs
before the third. Bounding the loop with the existing rail is rate-dependent by
construction — growth per cycle is rate-independent but the rail compresses once
per *sample*, so the limit cycle ran 1.17 to 1.69 across four rates and sang 45
cents flat. Level-dependent damping fixes that, but only once the amplitude it
reads has no ripple: `|s1|` swings within every cycle and pulled the pitch 2.34%
sharp at 6 kHz / 44.1 kHz. The two integrator states are in exact quadrature and
of exactly equal magnitude, so `sqrt((s1² + s2²)/(1 + g²))` is the bandpass
envelope exactly. Amplitude then reads **0.800000** and frequency error
**0.0000%** at every rate, cutoff and resonance. Derivation in `SvfFilter.hpp`.

**What would unpark item 4:** the user saying so.

---

## 12. Stryda — ideas parked at the plan stage

**Parked 2026-09-04 while the plan was written**, so that the first build is the
plan and not the plan plus its afterthoughts. Each is a contained addition to
the FM synthesiser described in `plugins/Stryda/PLAN.md`; none is a change to
anything already decided.

1. **Higher-order FM as a seventh character.** Lazzarini et al., "Theory and
   practice of higher-order frequency modulation synthesis" (JNMR 2024) was
   **read first-hand** on 2026-09-04 — it defines true frequency (not phase)
   modulation at second order and above, with an operator formulation that
   composes into arbitrary topologies including feedback, and it ships a
   reference implementation. It is parked because **Character already spans the
   two flavours the brief asked for** (classic PM and ModFM), and a third axis
   on every operator is six more parameters and a third bandwidth criterion for
   a sound nobody has yet asked to hear. Would change: `dsp::FmOperator` gains
   an order control and `dsp::FmBandwidth` a fourth criterion.
   **What would unpark it:** the user wanting a spectrum a single operator
   cannot currently reach — or F1's CPU table showing that one higher-order
   operator is cheaper than the two it would replace.

2. **A resample / freeze lane.** Every neurofunk source read (MusicRadar ×2,
   BassGorilla) says the same thing: the genre's basses are made by *resampling
   and reprocessing*, not in one synth pass. A lane that captures a bar of the
   current patch and plays it back as an oscillator would build that in. It is
   parked because it is large — a capture buffer, a loop editor, a player and a
   state story for the audio — and because **Ictus's Render-pad-to-WAV (I8b)
   should land first**: it solves the same file-and-ownership problem, and doing
   it twice differently would be the wrong order. **What would unpark it:**
   Ictus I8 shipping, or the user asking for it directly.

3. **Per-operator ADV envelopes.** Stryda ships **two** shared sixteen-point
   `MultiEnvelope`s that any operator can select as its envelope source. Six
   private ones would be 288 parameters for a feature used on two operators at a
   time, and the parameter list is already ≈425. **What would unpark it:** the
   user finding in practice that two are not enough — which is a real
   possibility on a patch where three operators each want their own pattern, and
   is exactly the kind of thing only playing it reveals.

4. **An eighth and ninth operator.** Two more than the era ever had, and more
   stacking room for layered patches. Parked because the matrix becomes 8×8 = 64
   cells to display and modulate and the per-voice cost rises about a third,
   which is better spent on the mangle chain. **What would unpark it:** F3's rig
   test showing headroom to spare, plus a patch the user wanted and could not
   build with six.

5. **Exponential FM as a per-cell switch.** Deferred out of Stryda F4. The hard
   half is already done and tested: `fm::exponentialBandwidthHz` implements
   Timoney & Lazzarini's DAFx-11 Eq (16), so the bound that makes it safe to
   ship exists. What is missing is the operator mode and the switch. It is the
   analogue cross-modulation sound — clangorous, and its carrier frequency
   moves with the modulation depth, which linear FM's does not — and it is in
   none of the reference instruments. Parked because it is a novelty flavour
   rather than anything the brief asked for, and F4 was already the largest
   parameter addition in the plugin. **What would unpark it:** the user wanting
   that specific sound, or a spare six parameters at a later schema.

6. **Sonitus and Svarayantra adopting `FmBandwidth`'s readout.** Sonitus has PM
   between two oscillators, operator feedback and a reverse path, and no way to
   see where its spectrum ends; Svarayantra pitches samples up with no warning
   about where the images land. The predictor built for Stryda answers both.
   Parked because it is a change to two shipped plugins for a display, and
   §7's bit-exactness rules mean touching them is never free. **What would
   unpark it:** F1 confirming the predictor's accuracy, and the user wanting it
   there.

7. **Skip the decimator over provable silence.** Stryda's engine now skips the
   per-sample voice loop when no voice is sounding and writes the zeros
   directly, which took idle from 0.0785 s to 0.0431 s per four seconds of
   audio — 1.96 % of a core to 1.08 %. The remaining cost is almost entirely
   `Oversampler::downsample` running its halfband FIRs over silence, which it
   must, because a FIR's output is only provably zero once its delay line has
   flushed. Making that skippable means the oversampler counting consecutive
   silent input samples against its own support and saying when it is clean.
   Parked because it is a **shared** change — every plugin in the suite decimates
   through that object, and §7's bit-exactness rule means each one's neutral path
   has to be re-proved — so it belongs in its own commit rather than in a
   plugin's close-out. It would roughly halve the idle cost of every instrument
   here, which matters at CLAUDE.md §1's twenty instances in a project.
   **What would unpark it:** the user asking, or an idle CPU figure becoming a
   problem on the rig.
