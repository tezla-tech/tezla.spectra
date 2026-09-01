# Membrana — the microphone stage (presence, from physics)

**Code `Tzmb` · "Tezla Membrana" · `tech.tezla.Membrana` · effect.**

**Membrana** is Latin for the membrane — the mic diaphragm itself, the
artificial tympanum that feeds the suite's malleus and incus. It sits **before
Phonoss** in the vocal chain: mic character first, then the strip.

## The thesis

Every mechanism the suite owns for "more presence" *invents* signal:
Emberdrive, Ferrite and Anvil distort, Halo synthesises harmonics that were
never there. What an engineer usually means by presence is different in kind —
it lives in what the microphone already captured: how close the singer stood
(proximity), what the capsule body did to the top octaves (diffraction, the
presence peak), and whether the quiet detail — consonants, breath — sits up
against the vowels (dynamics, not tone). None of that is Halo's job, and
building it into Halo would blur the one clean boundary the suite has.

Three mechanisms, three stages, all working on what is already in the signal:

1. **Capsule** — body diffraction (exact rigid-sphere scattering, source at
   finite range) plus a grille resonance, realised as a minimum-phase FIR.
2. **Position** — distance and off-axis angle through the exact first-order
   pressure-gradient model: proximity effect, pattern level, and their
   coupling.
3. **Presence & Detail** — dynamics: a level-tracking presence shelf that
   leans in when the singer backs off, and a bounded upward expander that
   lifts high-band detail but never the noise floor.

Decisions made with the user before this plan was approved: name **Membrana**;
**exact sphere physics** (not parametric curves); presence threshold
**explicit** (a knob, not self-calibrating).

---

## Research material — status

Per CLAUDE.md §9 the container can search but mostly cannot fetch; the user
supplies papers on request. Recorded honestly here and in
`docs/DSP-REFERENCES.md`:

| Source | Status | What it settles |
|---|---|---|
| Duda & Martens, "Range-Dependence of the HRTF for a Spherical Head" (conference version; eScholarship item 0kb7r9m9) | **READ first-hand — user-supplied PDF, 2026-09-01** (escholarship.org is egress-blocked from this container) | The transfer-function definition H(ρ, μ, θ) = surface pressure ÷ free-field pressure **at the sphere's centre**, with ρ = r/a, μ = ωa/c, θ = incidence angle. **The sphere response is minimum-phase** (their numerical rceps evidence) — which makes the minimum-phase FIR a faithful realisation of the physics, not a convenience. Range dependence matters for ρ < 5. On-axis HF limit ≈ +6 dB; flat near θ = 100°; ripples and a bright spot toward θ = 180°. μ = 1 ↔ 624 Hz at a = 8.75 cm (scales to ≈ 2.18 kHz at a = 25 mm). Their measured bowling-ball HRTFs confirm the series for ρ ≥ 2; below ρ = 2 their *source* stopped being a point — the series itself is exact. |
| Duda & Martens, "Range dependence of the response of a spherical head model", **JASA 104(5):3048–3058, 1998**, DOI 10.1121/1.423886 | **READ first-hand — user-supplied PDF, 2026-09-01** | Everything MB1 needs, verbatim: Eqs (7)/(8) define the series; **Appendix A gives the overflow-proof evaluation** via the polynomial Q_m; **Appendix B gives pseudocode** with the m = 0, 1 terms explicit and the stopping rule (two successive terms below threshold). Provenance: Bauck & Cooper (1980), extended to finite range. Footnote 1: their time convention e^(i(kr−ωt)) makes their formulas conjugates of Kuhn's and Rabinowitz's — magnitude-only use is convention-proof. Pinned values: \|H(∞,∞,0)\| = 2 (Eq 12); H(∞,μ,θ) ≈ 1 − i(3/2)μcosθ at low μ (Eq 11); on-axis ≈ **+3 dB at μ = 1**; θ = 150° down ≈ **13 dB at μ = 30**; θ ≈ 100° roughly flat; bright spot flat to μ ≈ 20; range matters for ρ < 5; at ρ = 1.25 the frontal HF rise is ≈ **+2 dB instead of +6** — close range trades HF rise for LF, the audible "closer = warmer" fact. Minimum-phase **for all ranges and incidence angles** (Sec II.D). |
| Rabinowitz, Maxwell, Shao & Wei, "Sound localization cues for a magnified head", *Presence* 2:125–129 (1993) | not read — **paywalled, and NOT needed**: it merely introduces the series the read papers use, the series is the standard exterior-scattering solution, and MB1's limit tests pin the structure. Skip unless a free copy appears. | Origin of the series formula per the read paper. |
| Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 60(6):399–408, 2012 | **READ first-hand — user-supplied PDF, 2026-09-01** (www.eecs.qmul.ac.uk is egress-blocked from this container; upgrades the older cited-not-read DSP-REFERENCES row) | Eq (4) is the quadratic soft-knee curve `GainComputer` has named since it was written — now verified against the source. Eq (7): α = e^(−1/(τ·fs)), step-invariant, coefficient can move without clicks — the house EnvelopeFollower approach, confirmed. Eqs (14)–(17): decoupled and branching peak detectors and their **smooth** variants. §3.2 + conclusion, the recommendation MB3/MB4 adopt: **feed-forward, smoothing in the log domain AFTER the gain computer** — no attack lag, no fixed detector threshold, guaranteed-smooth return to 0 dB, variable knee for free; their Fig 9 shows this also minimises gain-modulation distortion, the sideband claim MB5 measures. **The paper does not treat upward expansion** — MB4's lift curve is our own design, built with the paper's validated *methodology*. |
| Beranek & Mellow *Acoustics: Sound Fields and Transducers*; Eargle *The Microphone Book*; Wiener JASA 19:444–451 (1947) | books/paper, not read — background only | Proximity is derived below from the spherical-wave gradient; these would only re-derive it. |

**Nothing is outstanding.** All three load-bearing papers are read first-hand.
Should a later phase hit another blocked source, the standing rule is
CLAUDE.md §9: stop, give the user the URLs with one line each on what they
change, and carry on with what does not depend on them.

**IP guard (CLAUDE.md §2.1)**: physics only. No measured curves of named
commercial microphones, no brand names in presets or docs. Published *facts*
(a capsule diameter, "large-diaphragm bodies run ~50 mm") are public
documentation and fine.

---

## The model, in full

### 1. Directivity and proximity — exact, analytic, first order

A point source radiates p(r) = (A/r)·e^(−jkr), k = ω/c. A pressure (omni)
capsule reads p. A gradient capsule reads ∂p/∂r ∝ (1/r + jk)·p; gradient mics
are equalised flat for the far field (kr ≫ 1), so the residual near-field
factor is (1 + 1/(jkr)) — the proximity effect. A first-order mic with pattern
parameter **a** (a = 1 omni, 0.5 cardioid, 0 figure-8) at off-axis angle θ:

    H_mic(s) = D + G·ω_p / s        (one real first-order section)
      D   = a + (1−a)·cosθ          (pattern level at θ)
      G   = (1−a)·cosθ              (gradient weight at θ)
      ω_p = c / r                   (c = 343 m/s, r = distance in metres)

Everything couples correctly by construction: on-axis every pattern has D = 1
and proximity weight (1−a); at θ = 90° a cardioid is −6.02 dB with **zero**
proximity; omni never has any. The pole at DC is the ideal-gradient blow-up;
it is bounded by the mic's own LF limit, a fixed second-order highpass at
`lowLimitHz` (Q 0.707) — physically the diaphragm resonance.

Normalisation: the whole position block is computed **relative to the
reference condition (r = 1 m, θ = 0)** and the neutral path is taken by
predicate, so defaults are bit-exact identity (see the −0.0 note under
Detail).

Pinned numbers for tests (from the same closed form the code uses — the test
catches discretisation and wiring, and the *structure* is pinned by the
zeros): boost_dB(f) = 10·log10(1 + (G·c/(2π·f·r·D))²).

- Cardioid, r = 5 cm, on-axis: +3.01 dB at 546 Hz; +14.8 dB at 100 Hz.
- Figure-8 doubles G: corner 1092 Hz; +20.8 dB at 100 Hz.
- Doubling r moves the +3 dB point down exactly one octave.
- Omni: 0.000 dB at every f, every r. Cardioid at θ = 90°: level −6.02 dB,
  boost 0.000 dB.

Discretisation: all corners ≪ Fs/8, so plain bilinear first/second-order
sections are rate-independent per CLAUDE.md §6; coefficients from the actual
Fs; retuned at control-chunk boundaries state-preservingly (the
`DcBlocker::retune` rule — never via `prepare()`).

### 2. Body diffraction — the exact rigid-sphere series

Definition fixed by the read papers: **H(ρ, μ, θ)** = pressure on the sphere
surface ÷ free-field pressure at the sphere centre, ρ = r/a (a = body radius =
`capsuleMm`/2000 m), μ = ωa/c, θ = incidence angle (our `axisDeg`).

Series, exactly as the JASA paper prints it (Eqs 7–8; their convention
e^(i(kr−ωt)), the conjugate of Kuhn's/Rabinowitz's — immaterial here because
the fit is **magnitude only**):

    H(ρ, μ, θ) = −(ρ/μ) · e^(−iμρ) · Σ_{m=0}^{∞} (2m+1) · P_m(cosθ) · h_m(μρ) / h′_m(μ),   ρ > 1

**Implement it the way their Appendices A–B do** (Bauck & Cooper's algorithm
extended to finite range), not by raw Hankel recursion — the h_m explode with
m and the Q_m substitution keeps every intermediate a polynomial value:

- Substitution (A3): h_m(x) = Q_m(1/(ix)) · (−i)^m · e^(ix).
- Q recursion (A4–A5): Q_m(z) = −(2m−1)·z·Q_{m−1}(z) + Q_{m−2}(z);
  Q_0(z) = z, Q_1(z) = z − z², and Q_{−1}(z) ≡ z so the m = 1 derivative case
  needs no special branch.
- Derivative (A7): h′_m(x) = [Q_{m−1}(1/(ix)) − ((m+1)/(ix))·Q_m(1/(ix))] ·
  (−i)^(m−1) · e^(ix).
- Legendre (A8–A9): P_m(x) = ((2m−1)/m)·x·P_{m−1}(x) − ((m−1)/m)·P_{m−2}(x);
  P_0 = 1, P_1 = x.
- Assembled (A10): with zr = 1/(iμρ) and za = 1/(iμ), each term is
  (2m+1)·P_m(cosθ)·Q_m(zr) / [((m+1)/(iμ))·Q_m(za) − Q_{m−1}(za)], the m = 0
  and m = 1 terms computed explicitly as in their Appendix B pseudocode, the
  loop from m = 2, and the prefactor ρ·e^(−iμ)/(iμ) (the e^(iμρ) inside h_m
  cancels the e^(−iμρ) of Eq 7 — the pseudocode's `exp(-i*mu)` is correct and
  self-consistent).
- **Stopping rule, theirs verbatim**: iterate while either of the last two
  fractional changes \|term\|/\|sum\| exceeds the threshold — i.e. stop only
  when two successive terms are both below it. Threshold 1e−10; hard cap 300
  terms (the worst corner of the swept space measures 197: ρ = 1.2 at
  μ = 105.5, an octave past any design grid); a test asserts the cap is never
  reached over the whole parameter space (ρ ∈ [1.2, 80], μ up to 192 kHz ×
  30 mm sphere). Convergence is governed by (1/ρ)^m beyond the arguments, so
  ρ → 1 is the slow corner — and design-time only, never in `processBlock`.
- ρ is clamped ≥ 1.2 for this block (the mouth cannot occupy the mic body);
  the proximity block above keeps the true r.

Pinned tests, every number first-hand from the read papers: \|H\| → 1 (0 dB)
as μ → 0 at every ρ, θ, approaching as 1 − i(3/2)μcosθ (Eq 11 — assert the
complex value at μ = 0.01, not just the magnitude); on-axis +6.02 ± 0.3 dB for
μ ≫ 1 at ρ = 100 (Eq 12) and **+3 ± 0.5 dB at exactly μ = 1**; θ = 150° down
≈ 13 dB at μ = 30; θ ≈ 100° flat within ±1.5 dB to μ = 5; θ = 180° bright
spot — flat to μ ≈ 20 and non-monotonic beyond; range dependence visible for
ρ < 5 and gone by ρ = 100; **at ρ = 1.25 the frontal HF rise is ≈ +2 dB where
ρ = ∞ gives +6** — the close-range LF/HF trade that is the audible point of
modelling range at all.

What the EQ actually applies: the **character curve**
C_dB(f) = `character`% × [ \|H(ρ(r), μ(f), θ)\|_dB − \|H(ρ(1 m), μ(f), 0)\|_dB ].
At the 1 m on-axis reference this is exactly 0 dB at every f whatever
`character` is — so defaults stay identity. `character` is the honest knob:
how much of the raw diffraction the imaginary manufacturer left unequalised.

### 3. Grille resonance

One parametric peak added to the magnitude target in dB: centre `grilleHz`,
gain `grille`% × 6 dB, Q fixed at 2.5 (documented; a Q parameter can append
later under §8 rules). Physically a Helmholtz cavity — f_H =
(c/2π)·√(S/(V·L_eff)) — but exposing S, V, L would be knob theatre; the
frequency is the control that matters. Default depth 0 = neutral.

### 4. Realisation — one minimum-phase FIR plus the analytic LF sections

The composed HF magnitude target (diffraction × character + grille peak, in
dB, exactly 0 dB below ~600 Hz because proximity is handled analytically) is
fitted by the **real-cepstrum minimum-phase FIR** method that already exists
in `plugins/Ferrite/Dsp/TapeLoss.hpp` (log-magnitude → IFFT → fold anticausal
onto causal → exp → window; including its guard against unit-circle zeros).

- **MB2 lifts that machinery into
  `shared/tezla-dsp/include/tezla/dsp/MinimumPhaseFir.hpp`** — its own commit
  per §11, with a bit-exactness regression: TapeLoss's designed coefficients
  byte-identical before and after the lift for its own targets at
  44.1/48/96/192.
- Justified as *faithful*, not approximate: the sphere response itself is
  minimum-phase (both read papers) and CLAUDE.md §6 makes tone shaping
  minimum-phase by default.
- 96 taps at 48 kHz, scaled with Fs (cap 256), rebuilt at parameter-change
  chunk boundaries into a spare buffer and swapped (the Oversampler
  allocation-free precedent); rate-independence is asserted by measuring the
  rendered curve at 44.1/48/96/192 and comparing to the same analogue target
  within ±0.2 dB (§6's actual requirement).
- Fit error assert: < 0.25 dB, 700 Hz–20 kHz, worst case over a parameter
  sweep.
- Latency: minimum-phase FIR ⇒ report **0**; `BypassMixer` stays latency 0;
  bypass remains click-free and honest.

### 5. Presence — dynamics, not tone

A high shelf at `presHz` whose gain **leans in when the singer backs off**:

- Topology, per the read Reiss paper's recommendation (§3.2 and conclusion):
  **feed-forward, with the smoothing in the log domain placed AFTER the
  static curve** — the smoother acts on the computed lift itself, which gives
  no attack lag, a guaranteed smooth return to 0 dB with no fixed detector
  threshold, and lets the knee vary freely. Level detection: instantaneous
  \|x\| → dB, then the curve, then the smoother.
- Smoother: one-pole in dB, α = e^(−1/(τ·fs)) (their Eq 7, the step-invariant
  form whose coefficient can move without clicks), branching attack/release:
  τ_attack 120 ms when the lift is falling (singer got louder — back off
  quickly-ish), τ_release 400 ms when the lift is rising (lean in slowly).
  Both internal constants pinned by test.
- Curve, with x = clamp01((`presThresh` − L_dB)/W), W = 12 dB knee, and
  s = x²(3−2x) (Hermite):
  lift_dB = `presence` × ((1 − `track`) + `track`·s).
  `track` = 0 is a static shelf (exactly `presence` dB always); 1 is fully
  adaptive; both ends asserted exactly.
- Realisation: y = x + g·HP(x) with the **TPT `SvfFilter` highpass** — the
  ZDF structure takes per-chunk coefficient/gain updates without state
  stepping; g additionally smoothed 30 ms. Control chunk = the engine's
  single chunk constant (same pattern as `PhonossEngine`); the sample loop is
  cut at the chunk boundary, never the callback's (§7 — Emberdrive's 0.296
  lesson).
- Bound: lift ≤ `presence` ≤ +9 dB by construction; asserted over a full
  parameter sweep, not sampled (§7).

### 6. Detail — a bounded upward expander with a floor

The thing a 5 kHz shelf cannot do: bring consonants and breath up relative to
the vowel without brightening the vowel.

- Split: H = x − OnePole_LP(x) at `detHz` — complementary **by construction**
  (the DeEsser precedent): the recombination is x + g·H, so g = 0 is the
  identity candidate.
- **The −0.0 trap, written down so no successor re-trips it**: x + 0.0·H is
  NOT bit-exact — when x = −0.0, −0.0 + 0.0 = +0.0 flips the sign bit; the
  subtractive arrangement fails the same way when 0.0·H produces −0.0.
  **Neutral is a branch on a static predicate** (`detail` = 0 ⇒ out = in
  verbatim), the exact "static predicate + bit-exact identity" pattern
  Phonoss's stages use. Same rule for the presence stage.
- Same topology as Presence, per the read paper: instantaneous \|H\| → dB →
  the curve → a **smooth branching** one-pole on the lift in dB (their Eq 16
  adapted to the log domain: attack coefficient while the lift falls, release
  while it rises, and the smooth variant so a plateau uses the full time
  constant), attack 2 ms, release 80 ms.
- Curve, T_d = `detFloor` + 20 dB, W_d = 15 dB:
  lift_dB = `detail` × s(clamp01((T_d − L_H)/W_d)) × s(clamp01((L_H − `detFloor`)/6))
  — quieter detail is lifted more (first factor), but **nothing at or below
  the floor is lifted at all** (second factor): consonants up, hiss not.
- Bounds: lift ≤ `detail` ≤ +12 dB everywhere, asserted over the swept space.
- Silence in → silence out exactly (the floor guarantees it; asserted).

### 7. Auto level

With `autoLevel` on (default), the composed position/capsule EQ's broadband
gain (measured at design time at 1 kHz on the actual coefficients) is divided
out, so `distanceCm` reads as *tone*, not loudness — §7's auto-trim rule.
Off, the physical 20·log10(1 m / r) is applied, clamped to +24 dB.

### Engine order and the identity claim

in → capsule+position EQ (LF analytic + HF FIR) → presence → detail →
auto-level/output trim. Every stage neutral ⇒ **bit-identical** output
(Phonoss's `isIdentity` pattern, 40001-sample test including a
denormal-hostile tail); silence → silence exactly; denormals off per §2.2; no
oversampling — the path is linear except smoothed gain modulation, and the §7
aliasing sweep is still *measured* at maximum dynamics settings (inharmonic
< −60 dBFS asserted, not assumed).

---

## Parameters

String IDs frozen at birth, kSchemaV1, all units via `stringFromValue` — the
Phonoss lesson: `withLabel` alone never reaches the panel.

| id | name | range (default) | notes |
|---|---|---|---|
| `micOn` | Mic model | bool (on) | stage enable; neutral defaults make on == identity anyway |
| `pattern` | Pattern | 0–1 (0.5) | 0 omni · 0.5 cardioid · 1 fig-8; text stops at landmarks |
| `capsuleMm` | Body | 20–60 mm (50) | diffracting sphere diameter |
| `character` | Character | 0–100 % (35) | how much raw diffraction survives; 0 dB at 1 m regardless |
| `grille` | Grille | 0–100 % (0) | resonance depth, ×6 dB |
| `grilleHz` | Grille freq | 3–12 kHz (7 kHz) | log skew |
| `distanceCm` | Distance | 2–100 cm (100) | log skew centred ~15 cm; 100 = reference = neutral |
| `axisDeg` | Off-axis | 0–90° (0) | couples level, proximity and shadow truthfully |
| `autoLevel` | Auto level | bool (on) | judge tone, not loudness |
| `lowLimitHz` | LF limit | 20–120 Hz (40) | the mic's physical LF corner; bounds the proximity pole |
| `presenceOn` | Presence | bool (on) | |
| `presence` | Presence | 0–9 dB (0) | the bound is the range |
| `presHz` | Pres freq | 2–8 kHz (4.5 kHz) | |
| `presThresh` | Pres thresh | −60–0 dBFS (−28) | explicit, per user decision |
| `track` | Track | 0–100 % (65) | 0 = static shelf, 100 = fully adaptive |
| `detailOn` | Detail | bool (on) | |
| `detail` | Detail | 0–12 dB (0) | |
| `detHz` | Detail split | 1.5–8 kHz (3 kHz) | |
| `detFloor` | Floor | −90–−30 dBFS (−55) | below it nothing lifts — hiss stays down |
| `output` | Output | −24–+24 dB (0) | |
| `bypass` | — | house pattern | latency-matched, click-free |

Leads for the size hierarchy: `distanceCm`, `presence`, `detail`, `pattern`.
Trims: `lowLimitHz`, `grilleHz`, `detFloor`.

Presets (no brand names): *Neutral* (bit-exact) · *Close & Warm* (cardioid,
8 cm) · *Radio Chest* (7 cm, LF limit 60, character 50) · *De-Boom* (pattern
0.25, 20 cm) · *Backed-Off Detail* (45 cm, detail +6) · *Quiet-Verse Lift*
(presence +6, track 100) · *Crisp Small Capsule* (body 25 mm, character 70,
grille 35 @ 9 kHz) · *Podcast Presence* (presence +4 @ 5 kHz, detail +3).

---

## What reuses what

| piece | path | used for |
|---|---|---|
| real-cepstrum min-phase FIR design | `plugins/Ferrite/Dsp/TapeLoss.hpp` → **lift to** `shared/tezla-dsp/include/tezla/dsp/MinimumPhaseFir.hpp` | the capsule EQ; Ferrite regression bit-exact |
| `Fft` | `shared/tezla-dsp/include/tezla/dsp/Fft.hpp` | cepstrum |
| `SvfFilter` (TPT) | shared | the dynamic presence shelf (modulation-safe) |
| `EnvelopeFollower`, `LevelFollower` | shared | presence + detail sidechains |
| `SmoothedValue`, `DcBlocker`, `Exact`, `Denormals`, `Decibels` | shared | the usual |
| `BypassMixer` | shared | latency-0 honest bypass |
| **not** `GainComputer` | — | it clamps ratio ≥ 1 and documents upward expansion as "a different curve"; the DetailLift curve is its own class, and Capstone is never touched |
| house UI | `shared/tezla-ui`: KnobLookAndFeel, HouseControls, LampButton, PanelDesign, ScrollWheel | the whole editor |
| displays | `plugins/Phonoss/Source/PluginEditor.*` (SibilanceDisplay / ChainReductionDisplay patterns) | copy-adapt for the curve pane + activity lanes; not shared yet, note it |
| engine skeleton | `plugins/Phonoss/Dsp/PhonossEngine.hpp` | chunked control, isIdentity, meter atomics |

New plugin-local DSP (framework-free, CLAUDE.md §4): `Dsp/MicPattern.hpp`,
`Dsp/SphereDiffraction.hpp`, `Dsp/CapsuleEq.hpp`, `Dsp/PresenceTracker.hpp`,
`Dsp/DetailLift.hpp`, `Dsp/MembranaEngine.hpp`.

---

## Phases

One commit each; tests written, run and **seen red or break-checked** in the
same commit; numbers pinned in comments and quoted in commit messages; whole
tree built (never `--target` alone); "the qemu-aarch64 cross-check was not run
(CLAUDE.md 2.3 gate)" in every message.

- **MB0** — this file, registry row `Tzmb` in `plugins/README.md`,
  DSP-REFERENCES rows with the access statuses above (three papers READ
  first-hand, upgrading the old cited-not-read Reiss row), CLAUDE.md §11
  in-flight note. Both requested papers were supplied before approval —
  nothing is outstanding.
- **MB1** — `MicPattern` + `SphereDiffraction` (math cores, no filters yet).
  `SphereDiffraction` is a direct implementation of Duda–Martens Appendix B
  (the Q-polynomial form, their seeds and stopping rule), attributed twice per
  §9: at the point of use and in DSP-REFERENCES. Tests: every pinned number in
  the model sections above, including the three that came from the journal
  version — +3 dB at μ = 1, −13 dB at θ = 150°/μ = 30, and the ρ = 1.25
  frontal rise of +2 dB against +6 at ρ = ∞; the low-μ complex form
  1 − i(3/2)μcosθ; convergence cap never hit; the (1/ρ)^m decay measured;
  omni-zero, θ = 90° proximity zero, octave shift, ±6 dB and 0 dB limits,
  θ = 180° ripple, ρ < 5 range sensitivity. Break-checks: flip the
  Q-recursion sign → limits fail; drop the second convergence confirmation →
  assert catches the early exit.
- **MB2** — lift `MinimumPhaseFir` to shared (own commit, Ferrite designed
  coefficients byte-identical at 4 rates), then `CapsuleEq`: compose target,
  fit, analytic LF sections, state-preserving retune, spare-buffer swap.
  Tests: fit < 0.25 dB (700 Hz–20 kHz, swept); rendered curve
  rate-independent ±0.2 dB across 44.1/48/96/192; neutral bit-exact by
  predicate; retune during a sweep produces no step (neighbour-ratio
  anti-click test, the Sonitus pattern — and seen red by retuning via reset
  first).
- **MB3** — `PresenceTracker`. Tests: track = 0 exact static gain; track = 1
  curve values at L = T, T−W/2, T−W measured on the realised shelf ±0.05 dB;
  bound never exceeded over the swept space; 64 vs 512 block bit-identical
  (break-check: remove the chunk-boundary cut, watch it fail); gain smoothing
  no-zipper.
- **MB4** — `DetailLift`. Tests: silence exact; −70 dBFS hiss lifted
  0.000 dB; −30 dBFS burst lifted per curve; vowel-level untouched; bound
  over sweep; neutral-branch bit-exact including a −0.0 input vector;
  break-check the floor factor. The Reiss paper (read) does not treat upward
  expansion, so the commit states the honest position: the curve is ours, the
  topology — log-domain smoothing after the static curve, smooth branching
  detector — is the paper's, verified against Eqs (7), (16) and §3.2.
- **MB5** — `MembranaEngine`. Tests: all-neutral bit-identity (40001 samples,
  denormal-hostile tail); silence exact; autoLevel holds 1 kHz within
  ±0.3 dB across the full distance sweep; aliasing/sideband sweep at max
  settings < −60 dBFS measured; 4-rate character match; CPU % measured
  (target < 1% stereo 48 kHz / 480-block, quoted).
- **MB6** — JUCE layer: schema-v1 parameters per the table (append-only from
  birth), units via `stringFromValue`, state round-trip via the render tool,
  presets. `tezla-measure membrana` command: proximity-vs-formula table,
  sphere limit table, fit error, presence curve, detail bounds, identity,
  CPU.
- **MB7** — editor in the house design (chain boxes MIC | POSITION | PRESENCE
  | DETAIL, hue rotated along the chain, arrows, lead/trim sizes, red
  switches, tinted dropdowns if any, wheel scrolls): left pane = **the live
  capsule curve drawn from the same coefficients that play** with hover dB
  readout; right pane = presence/detail activity lanes (Phonoss lane
  pattern). README, VOCAL-CHAIN.md (Membrana → Phonoss → …), registry flip,
  validator **47/47 on all twelve**, editor photographed at several states.

## Risks

- **Series conventions** — mitigated: magnitude-only use, limit tests, both
  Duda–Martens versions read; the convention footnote is on record.
- **ρ → 1 convergence cost** — clamped at 1.2, capped at 200 terms, cap
  asserted unreached; evaluation is design-time only.
- **FIR LF resolution** — sidestepped structurally: LF is analytic, the FIR
  target is exactly flat below ~600 Hz.
- **Presence/detail pumping** — hard bounds by construction, slow tracking
  constants, block-size-independence and full-space sweeps; silence-in tests
  keep the floor honest.
- **Scope creep into a "mic modeller"** — explicitly out: no named-mic
  emulation, no IR loading, no polar-pattern editor. The physics continuum is
  the product.
- **Superposition honesty** — directivity × diffraction composed, not the
  coupled scattering problem; stated in the README the way Anvil states its
  driver × enclosure × mic composition.

## Verification

Per phase: whole-tree build, full `./build/bin/tezla-tests`, break-checks
noted above. At MB6/MB7: `tezla-measure selftest` then `tezla-measure
membrana`; render-tool editor shots incl. `dump:` unit strings; Steinberg
validator on all twelve bundles; CI stays manual (§5) so everything runs
locally before push. The x86-64 gate of §2.3 holds: no ARM/macOS runs. The
acceptance test, as always, is the user's ears on the Windows rig.

## Continuity — how any session resumes this work

This section is the handoff. It is updated **in the same commit as each
phase**, so whichever assistant session picks the work up — after a context
loss, a model change, or a fresh clone — needs nothing beyond this file and
CLAUDE.md.

**Phase status** (flip `pending` → `done` in the phase's commit):

| phase | status |
|---|---|
| MB0 plan + registry + references | done |
| MB1 MicPattern + SphereDiffraction | done |
| MB2 MinimumPhaseFir lift + CapsuleEq | pending |
| MB3 PresenceTracker | pending |
| MB4 DetailLift | pending |
| MB5 MembranaEngine | pending |
| MB6 JUCE layer + measure command | pending |
| MB7 editor + close-out | pending |

**To resume** (a later phase, or a fix): read CLAUDE.md in full, then this
file; take the first `pending` phase. The non-negotiables that every phase
here honours, in one place:

- One phase = one commit. Tests written and RUN in that commit; every
  mechanism seen red first or break-checked (edit → red → revert), with the
  measured numbers pinned in the test comments and quoted in the commit
  message.
- Build the whole tree before pushing (`./scripts/build.sh NONE --test` or
  the cmake equivalent with no `--target`), run all tests, and run
  Steinberg's validator on any plugin whose bundle changed.
- The qemu-aarch64 cross-check is **deliberately not run** (CLAUDE.md 2.3
  gate); say so in every commit message rather than implying coverage.
- New first-party files carry the six-line licence header copied from a
  neighbour. No model identifiers in anything pushed. The commit footer comes
  from the harness — use whatever the current session mandates.
- Derive DSP and measure it; anything taken from a source is attributed at
  the point of use AND in `docs/DSP-REFERENCES.md` (CLAUDE.md §9). The one
  taken thing here is the Duda–Martens Appendix A–B evaluation algorithm —
  a published algorithm whose subtle misimplementation no measurement could
  fully catch, which is exactly §9's test for copying. Setters that clear or
  re-aim state carry no-op guards (`dsp::isExactly`). Continuous parameters
  are smoothed. Silence in → exact zeros out. Neutral settings are bit-exact,
  not merely transparent — by predicate branch, never arithmetic (the −0.0
  trap above).
- Parameter string IDs and every choice list are frozen at birth and
  append-only (CLAUDE.md §8).
- The three source papers are user-supplied PDFs, read first-hand 2026-09-01;
  their statuses and what each settles are recorded in
  `docs/DSP-REFERENCES.md` ("Microphone physics and presence — Membrana").
  escholarship.org and www.eecs.qmul.ac.uk are egress-blocked from this
  container; if another source is needed, ask the user with URLs per
  CLAUDE.md §9 — do not work around it silently.
- The prior art to copy patterns from: Phonoss (`plugins/Phonoss/`) for the
  engine skeleton, chunked control, bit-exact neutral predicates and the
  editor displays; Ferrite (`plugins/Ferrite/Dsp/TapeLoss.hpp`) for the
  min-phase FIR design being lifted in MB2; Malleus for the PLAN/Continuity
  discipline.
