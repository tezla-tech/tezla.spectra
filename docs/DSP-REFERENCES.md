# DSP references

Everything this repository leans on, with its licence. **Add a row before the
source influences any code.** A reference whose licence has not been checked is
a reference that has not been read.

The rule from [`../CLAUDE.md`](../CLAUDE.md) §2.1 applies throughout: papers,
open-source code with a compatible licence, published manuals and physics are
all fair game. Commercial binaries are not, in any form.

---

## Licence quick guide

**This project is AGPLv3** (see [`../LICENSE`](../LICENSE)), which is what JUCE's
free tier requires of anything built on it. That decides the table below.

| Licence | What it means for us |
|---|---|
| **MIT / BSD / ISC / zlib / Apache-2.0** | Copy freely. Keep the copyright notice. |
| **LGPL** | Link against it, or copy with attribution. |
| **GPLv3, GPLv2-or-later, AGPLv3** | Compatible. Copy with attribution — in a comment at the point of use *and* as a row here. |
| **GPLv2-only** | **Refused.** Cannot be combined with AGPLv3, however good it is. Check the per-file header: "GPLv2" and "GPLv2 or later" look identical at a glance and only one is usable. |
| **Paper / book** | Ideas and equations are not copyrightable. Cite it. Do not paste listings verbatim. |
| **Standards (ITU, EBU, AES)** | Published so they can be implemented. Type the tables in and cite the document. |

The default is still to derive and measure — see `CLAUDE.md` §9 for why that is
a working practice rather than a principle. Copying is for the things a
measurement could never tell you that you had got wrong: coefficient tables,
a standard's exact defined behaviour, a documented edge case.

---

## Frameworks and SDKs

| Project | Licence | Used for | Notes |
|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | AGPLv3 / commercial (free tier) | Plugin wrapper, GUI, parameters, state | Pinned in `cmake/FetchJUCE.cmake`. Fetched by CMake, not vendored. |
| [Steinberg VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) | **MIT** since v3.8 (Oct 2025) | The VST3 format itself | Bundled inside JUCE, so nothing to install. MIT means a direct-to-SDK plugin is also unencumbered if we ever drop JUCE. |

---

## Saturation, tape and valve modelling

| Source | Licence | Relevance |
|---|---|---|
| [Airwindows](https://github.com/airwindows/airwindows) (Chris Johnson) | MIT | ~300 small, readable algorithms: console summing, tape, density/saturation, dither. Permissive, so directly usable. The clearest available demonstration that small algorithms beat elaborate ones. |
| [ChowDSP AnalogTapeModel](https://github.com/jatinchowdhury18/AnalogTapeModel) | GPLv3 | Jiles–Atherton magnetic hysteresis with RK2/RK4/Newton–Raphson solvers, plus wow, flutter and degradation. **Read the DAFx-19 paper, implement from that**, do not copy the source. |
| Chowdhury, *"Real-Time Physical Modelling for Analog Tape Machines"*, DAFx-19 | paper | The derivation behind the above. This is the correct starting point for tape hysteresis. |
| [chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) | BSD-3 | Header-only wave digital filters for real circuit models. Permissive — usable directly. |
| [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils) | GPLv3 (some parts BSD) | Filters, resampling, general utilities. Check per-file licensing before borrowing anything. |

---

## Anti-aliasing

| Source | Licence | Relevance |
|---|---|---|
| Parker, Zavalishin & Le Bivic, *"Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution"*, [DAFx-16](https://www.dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf) | paper | The original antiderivative anti-aliasing (ADAA) method. Apply the antiderivative of the shaper, then differentiate in discrete time. First-order ADAA buys roughly the same alias suppression as 2× oversampling, for far less CPU. |
| Bilbao, Esqueda, Parker & Välimäki, *"Antiderivative Antialiasing for Memoryless Nonlinearities"*, IEEE SPL | paper | The theory behind ADAA and its higher-order forms. |
| [jatinchowdhury18/ADAA](https://github.com/jatinchowdhury18/ADAA) + [*Practical Considerations for Antiderivative Anti-Aliasing*](https://jatinchowdhury18.medium.com/practical-considerations-for-antiderivative-anti-aliasing-d5847167f510) | reference | The numerical traps: ADAA divides by the difference between consecutive samples, which blows up when that difference approaches zero. Every real implementation needs the fallback branch. |
| Vicanek, *[Note on Alias Suppression in Digital Distortion](https://vicanek.de/articles/AADistortion.pdf)* | paper | A compact alternative treatment. |
| *Antialiasing Piecewise Polynomial Waveshapers*, [DAFx-23](https://www.dafx.de/paper-archive/2023/DAFx23_paper_61.pdf) | paper | Extends the above to piecewise shapers — relevant to hard-knee clipping. |

**What ADAA is actually worth, measured.** On top of x4 oversampling with a
tanh shaper it buys **0.3 dB** at moderate drive — nothing. At +27 dB of drive
it buys **21 to 25 dB**, at every session rate. A tanh is smooth enough that
ADAA has little to fix until the signal is deep into the curve; that is exactly
where this music lives, which is why it is worth its extra evaluation and
divide per sample. With oversampling turned off entirely it is worth 5-9 dB.
Pinned in `tests/test_Adaa.cpp`.

**Measured baseline, from `tezla-measure clip-aliasing`:** a naive hard clipper
at 4× drive on a 1 kHz sine produces inharmonic energy at **−47 dB** at 48 kHz
and **−65 dB** at 192 kHz. Four times the rate buys about 18 dB. It never
reaches zero, because a hard clipper has infinite bandwidth — which is exactly
why oversampling alone is not the whole answer, and why the shaper itself has to
be band-limited (ADAA, or a smooth shaper) as well. These numbers are pinned in
`tests/test_Measurement.cpp`.

---

## Filters and virtual analogue

| Source | Licence | Relevance |
|---|---|---|
| Zavalishin, *The Art of VA Filter Design* (v2.1.2) | book | The standard text on TPT / zero-delay-feedback filters. **Read** — saved under [`technical references/sonitus/`](../technical%20references/sonitus/). It settled the open question about `SvfFilter`'s nonlinearity, and the answer was that the question had been framed wrongly; see below. |
| Robert Bristow-Johnson, *Audio EQ Cookbook* | public | The biquad coefficient formulas implemented in `shared/tezla-dsp/include/tezla/dsp/Biquad.hpp`. |
| [Surge XT](https://github.com/surge-synthesizer/surge) | GPLv3 | Filters, waveshapers, oversampling, and a well-organised large plugin codebase. Read for architecture; do not copy. |
| [Calf Studio Gear](https://github.com/calf-studio-gear/calf) | LGPL/GPL | Classic effect topologies, clearly written. |
| [Faust libraries](https://faustlibraries.grame.fr/) | permissive | Reference implementations worth comparing our measurements against. |
| Laakso, Välimäki, Karjalainen & Laine, *"Splitting the Unit Delay"*, IEEE Signal Processing Magazine 13(1), 1996 | paper | Fractional-delay filter design. `Comb.hpp` uses the 4-point Lagrange kernel. **Read** — saved under [`technical references/sonitus/`](../technical%20references/sonitus/), as page images with no text layer. The derived kernel matches its Design Guide 1 term for term, and the paper supplies two justifications the implementation had only assumed; see below. |
| Peterson & Barney, *"Control Methods Used in a Study of the Vowels"*, JASA 24(2), 1952 | paper | The vowel data in `Formant.hpp`, from **Table II**. **Read** — saved to [`technical references/sonitus/`](../technical%20references/sonitus/). The fifteen *frequencies* quoted from general reference were exactly right. The *amplitudes* were not: they had been one constant set of three for every vowel, where the paper gives them per vowel over a thirty-decibel span. See below. |

**Measured caveat on biquads.** Computing coefficients from the actual sample
rate is necessary but not sufficient for rate-independence. Bilinear-transform
warping makes an RBJ biquad diverge as it approaches Nyquist. For a 4 kHz
lowpass at Q 0.707:

| Test frequency | at 48 kHz | at 192 kHz | analogue prototype |
|---|---|---|---|
| 2 kHz | −0.247 dB | −0.263 dB | −0.263 dB |
| 6 kHz | −8.269 dB | −7.853 dB | −7.827 dB |
| 15 kHz | −29.89 dB | −23.31 dB | −22.98 dB |

The 192 kHz curve tracks the analogue prototype; the 48 kHz curve is the
outlier. Trust a plain biquad to be rate-independent only below about Fs/8. Put
anything whose high-frequency shape actually matters — a cabinet response, a
tape head bump, the tone stack inside a saturation stage — inside an oversampled
section. Pinned in `tests/test_Biquad.cpp`.

**And what a TPT structure does and does not fix.** `SvfFilter.hpp` is the
answer to that biquad table, so it is worth being exact about how much of it is
answered. Measured across 44.1 / 48 / 96 / 192 kHz, resonance 0.6, as the
spread between the highest and lowest reading:

| corner | at the corner | an octave above | two octaves above |
|---|---|---|---|
| 200 Hz | 0.000 dB | 0.004 dB | 0.017 dB |
| 1 kHz | 0.000 dB | 0.097 dB | 0.444 dB |
| 4 kHz | 0.000 dB | 1.689 dB | 10.638 dB |

**The corner is exact at every rate, and only the corner.** The prewarp cancels
precisely at `f = fc`, where all three outputs read `Q` — measured 1.9252 dB at
resonance 0.6, against 1.9251 from the algebra, at every corner and every rate.
Replacing `tan(πf/fs)` with the small-angle `πf/fs` — a structure with no
prewarp — moves that reading to 0.5475 dB for an 8 kHz corner at 44.1 kHz.

Above the corner the response still warps, and must: a discrete response is
symmetric about Nyquist, and Nyquist is a different frequency at every rate. So
the biquad rule survives in a weaker form — a TPT filter is rate-independent
where it counts, but a corner swept up towards Nyquist still belongs inside an
oversampled section. Pinned in `tests/test_SvfFilter.cpp`.

**Where a comb's notches are, and why there are two of them.** A delay shifts
every frequency by the same *time*, so a flanger's notches are evenly spaced —
at 1/(2D), 3/(2D), 5/(2D) — which is what makes a flange metallic and what makes
it useful when key-tracked onto a note's harmonic series. A first-order allpass
shifts every frequency by a different *phase*, so a phaser's notches bunch
around its corner and thin out either side; N stages give exactly N/2 of them.
Both are in the suite because neither replaces the other. Pinned in
`tests/test_Comb.cpp` and `tests/test_Phaser.cpp`, both against the transfer
function rather than against a handful of inequalities — which is what caught a
one-sample error in the delay line's read index, visible only as a 6.02 dB comb
peak measuring 5.946 dB at the first harmonic and 5.333 dB at the third.

---

### What the Scala specification corrected

Every one of these was **written from memory of the format and was wrong**, and
not one could have been found by measurement — a format's defined behaviour is
the case CLAUDE.md §9 says to take from the source rather than derive. The
quotations are from the saved pages.

| what | the specification | what the code did |
|---|---|---|
| **Negative mapping degrees** | *"There is no restriction to the degree numbers in the mapping … they can be any number, also negative, also lie outside the scale range."* | Refused them — and used `-1` as the marker for an unmapped key, so the two collided. `KeyboardMap::kUnmapped` is now `INT_MIN`, and `x` is the only marker. |
| **Short mappings** | *"At the end, unmapped keys may be left out."* | Refused a map with fewer entries than its size. Now the tail is padded silent, so the pattern stays its full width. |
| **An unmapped reference note** | *"If this is done with the frequency reference note it will be considered an error."* | Did not check. It has to be an error: the reference note is what pins the scale to a frequency. |
| **The formal octave past the scale** | *"If you want a mapping for a double octave range … make the scale degree to consider as formal octave parameter twice the size of the scale."* | Returned the scale's repeat for any degree past its size, so a formal octave of 24 on a 12-note scale sat **an octave flat every pattern** — the exact case the page calls out. Now octave-extended. |

Two things it confirmed rather than corrected, both worth recording because the
opposite reading is tempting:

- **Trailing text after a pitch value is normal, not an error.** The page lists
  `100.0 cents`, `100.0 C#` and `5/4 E\` among its valid pitch lines. A parser
  demanding the line hold nothing else — which is what the `.tzref` lesson would
  suggest — would refuse real archive files. Text *attached* to the value with no
  space is still refused here, which is stricter than the page's "anything after
  a valid pitch value should be ignored"; that is a deliberate divergence, since
  it catches a typo like `408.0.5` and no archive file relies on it.
- **Negative cents are legal; negative ratios are not.** *"Negative ratios are
  meaningless and should give a read error"*, while `-5.0` appears in the list of
  valid lines. Two different rules that are easy to conflate into one.

One divergence stands, deliberately: a note count of **0** is refused, where the
page allows it (*"The lower limit is 0, which is possible since degree 0 of 1/1
is implicit"*). A scale with no pitch lines has no repeat interval and cannot
tune a keyboard, so it is refused with that as the reason rather than silently
loaded as something unplayable.

### What Zavalishin settled about the filter's nonlinearity

The open question was recorded as *"the book discusses solving the zero-delay
feedback through a saturator by iteration, and this implementation keeps the
linear solve and shapes the integrator states afterwards; that is a standard
simplification, but standard here is second-hand."*

**The question was framed wrongly, and the book says so directly.** §6.3, on
feedback-loop saturation: *"…the resonance amount is increased by increasing the
amount of the feedback (thus e.g. the SVF filter doesn't fall into this
category)."* And §6.11, explicitly:

> As with 1-pole filters, the feedback in SVF is also not one creating the
> resonance, respectively the discussion from Section 6.3 does not apply either,
> and thus we can't simply put a saturator into the feedback loop. Actually, the
> purpose of the feedback in SVF is kind of an opposite of creating the
> resonance. The function of the feedback path containing the bandpass signal is
> to **dampen** the otherwise self-oscillating structure.

So the iterative machinery of §6.4–6.6 is about ladders and Sallen–Key filters,
not this one. `SvfFilter.hpp`'s comment already said the same thing in its own
words — *"in a ladder the resonance is positive feedback and saturating it
reduces the resonance; in a state-variable the resonance is reduced damping"* —
which is the derivation arriving where the book is.

The book's own route for an SVF is an **antisaturator** (`sinh`, faster than
linear) in parallel with the damping gain, so damping grows as level rises. Of
that structure he writes: *"The antisaturator in Fig. 6.52 effectively makes the
state of the first integrator saturate."* **That is what railing the integrator
states does, reached directly instead of through the damping path.** The
difference that remains: his nonlinearity sits inside the ZDF solve, so it
changes the solve; ours is applied to the state after an exact linear solve, so
the instantaneous response stays exactly linear and only the stored state is
bounded.

The book also supplies a test the implementation did not have. Of the SVF's
three outputs: *"Note that yHP + yBP1 + yLP = x, as for the linear SVF."* That is
an exact algebraic identity of the solve rather than a property of the response,
so it catches a wrong denominator or a wrong state update with no spectral
measurement that could itself be at fault. Measured worst error across
resonance and cutoff sweeps: **6.7e-16**. Breaking the denominator fails it
twelve times over.

Two things worth taking from it later, recorded rather than done:

- **§6.7, second-order saturation curves.** Replacing `tanh x` with `x/(1+|x|)`
  turns the nonlinear ZDF equation into a quadratic with an analytic solution —
  the route if we ever want the nonlinearity genuinely inside the loop without
  iterating.
- **Self-oscillation below R = 0.** The antisaturator route is what lets an SVF
  be driven past self-oscillation deliberately, which our fixed rail does not
  offer. Noted in the Sonitus roadmap.

### What Laakso settled about the comb's interpolator

The 4-point kernel in `Comb.hpp` was derived rather than copied. It matches the
paper's Design Guide 1 table term for term, with **D = 1 + fraction** — and a
test now checks the closed form against the paper's *general* product formula
(Eq. 42) rather than against a transcription of the same table, worst error
**4.4e-16** across a thousand fractions.

Two things the paper supplies that had only been assumed:

- **Why the delay is centred**, which was found here the hard way as an
  off-by-one that made the comb's loop a sample too long. Eq. 21: the smallest
  error for a given order is obtained when the delay sits at the *"center of
  gravity"* of the ideal impulse response — for odd order N, `M_opt = Int(D) −
  (N−1)/2`, which for four taps puts the interpolation point between the middle
  two. That is exactly where the fix landed.
- **Why Lagrange is the right family for a comb specifically:** *"The maximum of
  the magnitude response never exceeds unity when the delay is near to the half
  filter length. This is important in applications including feedback."* A comb
  *is* a feedback application, and an interpolator with gain anywhere above unity
  would compound every pass round the loop — the feedback cap bounds the
  coefficient, not the loop gain. Measured across 101 fractions and the whole
  band, the peak magnitude is 1.0 exactly, at DC, and never above.

The paper's own caveat is also worth recording: *"if a 4-tap Lagrange FD filter
is not good enough for a given purpose, it may be better to use an LS-based FIR
filter design method instead."* Ours is good enough — 0.225 dB of gain spread
across a swept fraction at 8 kHz, against linear interpolation's 1.248 — and the
alternative is named if it ever is not.

## Tuning

| Source | Licence | Relevance |
|---|---|---|
| [The Scala scale file format](https://www.huygens-fokker.org/scala/scl_format.html) and [keyboard map format](https://www.huygens-fokker.org/scala/help.htm#mappings) | public specification | `ScalaFile.hpp` parses both. **Read** — saved to [`technical references/sonitus/`](../technical%20references/sonitus/) after the proxy refused `huygens-fokker.org`. It settled four things the implementation had guessed wrong; see below. |
| Peterson & Barney, JASA 24(2), 1952 | paper | See the Formant row above. |

### What Peterson & Barney corrected

The frequencies survived contact with the paper unchanged — all fifteen match
Table II's adult-male row for the five vowels used, which are its columns for
/i ɛ ɑ ɔ u/ (heed, head, hod, hawed, who'd).

The **amplitudes** did not. The paper gives a relative level per vowel per
formant, referred to the first formant of [ɑ], and they span thirty decibels:

| | F1 | F2 | F3 |
|---|---|---|---|
| ee /i/ | −4 | **−24** | −28 |
| eh /ɛ/ | −2 | −17 | −24 |
| ah /ɑ/ | −1 | −5 | −28 |
| oh /ɔ/ | 0 | −7 | −34 |
| oo /u/ | −3 | −19 | **−43** |

`Formant.hpp` held one constant set — 0, −7, −12 — for every vowel, which gave
every vowel the same spectral balance. **The balance is most of what tells one
vowel from another.** An "ee" wants its second formant 24 dB below its first;
a constant −7 leaves it seventeen decibels too loud, which is most of the way to
not being an "ee".

One thing the paper is careful about and this now records: the amplitudes were
averaged across men, women and children, because the measurements "did not show
decided differences between classes of speakers". So they are not the male row
specifically, unlike the frequencies.

Measuring the fix produced a reading worth keeping, because a careless test
would have hidden it. The *summed* response at each formant's centre lands on
the table for F1 and F2, and reads **high for F3 — by up to 6.7 dB**:

|  | F1 | F2 | F3 |
|---|---|---|---|
| ee | −4.000 (−4) | −23.898 (−24) | −27.746 (−28) |
| eh | −2.000 (−2) | −16.931 (−17) | −23.670 (−24) |
| ah | −0.986 (−1) | −4.919 (−5) | −26.488 (−28) |
| oh | 0.016 (0) | −6.766 (−7) | −30.183 (−34) |
| oo | −3.000 (−3) | −18.557 (−19) | −36.322 (−43) |

That is the filter being right. The table describes each resonator's own peak;
the measurement is of three resonators summed, and a formant forty decibels
below its neighbours is buried under their skirts. The exact claim is checked
against the coefficients instead, and the summed response is held to a bound
the data explains rather than a tolerance wide enough to hide it.

**The scales are generated, not tabulated**, which is both a licence question and
a testing one — CLAUDE.md §2.1 and §9. A Pythagorean scale is a chain of 3/2s, a
quarter-comma meantone fifth is narrowed until four of them make a pure 5/4, and
Bohlen-Pierce is thirteen equal parts of 3/1. Writing the arithmetic down avoids
shipping anybody's data files *and* makes each scale checkable against the
property it was built for rather than against another table. `tests/test_Tuning.cpp`
asserts that a Pythagorean fifth is 701.955 cents and its limma bit-exactly
256/243, that four meantone fifths make an exact 5/4, that Kirnberger III's C–E is
exactly 5/4, and that each Carlos scale's minor third is as flat as its major
third is sharp.

**What is deliberately absent, and the line that decides it.** Javanese slendro
and pelog and the 22 shruti are *not* built in. They have no canonical tuning —
a slendro is whatever a particular gamelan was tuned to — so any specific
numbers would be one instrument's measurements presented as a standard. Those
are exactly what `.scl` loading is for. The maqam and dastgah entries that *are*
built in do not cross that line: they are **named theorists' published
constructions** — al-Farabi's ratio for Zalzal's fret, Farhat's mean neutral
seconds, the Arel–Ezgi–Uzdilek comma grid — attributable the same way
Werckmeister's temperament is, with their stories saying the living practice
bends around them.

**The three historical temperaments are a middle case.** Werckmeister III,
Kirnberger III and Vallotti are generated from their constructions, and the
constructions are written out in `Scales.hpp`. What comes from general reference
rather than from a source that could be read is *which* fifths each one tempers.
The defining intervals are asserted, so a wrong assignment would show up as a
wrong C–E; a wrong assignment that still gave the right C–E would not.

### The microtuning expansion — sources and access, honestly

The container's proxy blocks the primary literature, so these entries rest on
web-search snippets and general reference, recorded per CLAUDE.md §9. Each
scale's tests assert everything the construction lets them assert; what a test
cannot check is the attribution itself.

| Scales | Source | Access |
|---|---|---|
| Shur, Chahargah (Persian) | Hormoz Farhat, *The Dastgah Concept in Persian Music*, Cambridge 1990 | **Not read.** Search snippets confirm his two neutral-second families (125–145 and 150–170 cents) and proposed means of 135 and 165, plus the flexible-interval doctrine. The frame (pure 498/702/996, the 270-cent plus tone, which degrees carry the koron) is general reference. Tests pin the numbers used. |
| The seven Old Babylonian tunings | The tuning texts UET VII 74 / UET VI/3 899 and CBS 10996, via Kilmer, Gurney, West (*Music and Letters* 1994), Crickmore | **Not read.** Snippets confirm: the standard seven-name cycle order; nīd qabli read as the major-scale octave species (the Hurrian hymn tuning); embūbum as the palindromic mode; išartum as "strings 2 and 6 tuned to a fifth". The rotation-to-name assignment for the other four follows the cycle order around those anchors, and each story says the rising/falling debate renames the set without changing its sounds — all seven rotations ship either way. |
| Rast (Zalzal, just) | al-Farabi's *Kitab al-Musiqa al-Kabir* description of Zalzal's wusṭā fret, 27/22 | **Not read**; the 27/22 attribution is standard general reference. The construction (jins rast doubled at the fifth) is the ordinary definition of the maqam's scale. |
| Rast (Turkish, AEU) | Arel–Ezgi–Uzdilek theory, 53 Holdrian commas | **Not read.** Snippets confirm the 53-EDO basis, 9 commas per 9/8 tone, and Rast's implementation in it; the T-K-S comma sizes (9/8/5) are general reference. The test asserts the grid and the schismatic third. |
| Ptolemy even (homalon) diatonic | Ptolemy, *Harmonics* II | **Not read**; 12/11 · 11/10 · 10/9 is the standard citation of the genus, and the telescoping product is arithmetic the test checks. |
| Twelve lü | *Guanzi*, *Lüshi Chunqiu* — the san fen sun yi rule | **Not read**; the generation rule (alternating ×3/2, ×4/3 = a one-way chain of fifths) is standard general reference, and the test re-derives every degree in integers. |
| Partch 43 | Harry Partch, *Genesis of a Music*, 2nd ed. 1974 | **Not read here; reproduced, not derived.** The 43 ratios are Partch's artistic selection and there is no generating rule. The test verifies every structural property the book states — 43 degrees, strictly ascending, 11-limit, exact inversional symmetry — which would catch most transcription slips but not a symmetric pair swapped for another symmetric pair. |
| Golden phi | After Heinz Bohlen's 833-cent studies | Bohlen's own degree set was **not used** — it could not be fetched — so the entry is this instrument's own equal seven-fold division of his 833.09-cent repeat, and both the code and the panel say so. His exact scale can be added verbatim if the page is supplied: huygens-fokker.org/bpsite/833cent.html. |

---

**A compiler note that belongs here rather than in a build document.** GCC and
Clang default to `-ffp-contract=fast`, which may fuse `a*b + c` into a single
multiply-add with one rounding instead of two. On x86-64 that is invisible —
there is no FMA instruction without `-mfma` — but `fmadd` is baseline on
AArch64, so the same source computes different numbers on Apple Silicon. It
surfaced as `SvfFilter` failing to match the linear difference equation it
implements, bit for bit, on ARM64 only. `cmake/TezlaCompilerOptions.cmake` now
passes `-ffp-contract=off`, which is what MSVC's `/fp:precise` already does.

---

## Measurement practice

Three ways the harness has already lied, all now guarded by tests:

- **Peak-picking under-reads a sine near Nyquist.** A 16 kHz tone at 48 kHz has
  three samples per cycle and peak-picks at 0.866, which looks exactly like a
  filter 1.2 dB down. Use RMS × √2 for sine amplitude; it is exact at any sample
  density.
- **An FFT reads a start-up transient as broadband noise.** Including the
  oversampler's ramp reports -30 dB of aliasing for a chain that is really at
  -130 dB. Always analyse a steady-state window.
- **Full-band aliasing figures hide the number that matters.** A saturator can
  read -79 dB overall while being at -157 dB below 18 kHz, because the whole
  residual is one harmonic sitting in the decimator's transition band at 22 kHz.
  `analyseHarmonics` reports the audible band separately for this reason.

And one about the signal: generate test tones at a **bin-exact** frequency
(`binExactFrequency`), so the analysis needs no window and has no leakage. A
measured -110 dB floor is then real rather than an artefact of the measurement.

---

## Harmonic exciters and virtual bass — used for Halo

The exciter is old enough that its primary source is an expired patent, and the
bass-enhancement half has a substantial modern literature behind it.

| Source | Licence / status | What it was used for |
|---|---|---|
| Knoppel / Aphex Ltd., US 4,150,253, *Signal distortion circuit and method of use* (filed 1978, granted 1979) | Patent, long expired — a published disclosure | The original structure: a second-order Butterworth highpass into an asymmetric soft clipper, recombined with deliberate phase shift, and the "transient discriminate" idea behind Halo's Punch |
| Shekar & Smith, *Modeling the Harmonic Exciter*, AES 135th Convention e-Brief (2013) | Paper | Confirms that structure, and that soft clipping is what keeps the generated harmonics low-order |
| Oo, Gan & Lim, *Generalized harmonic analysis of the Arc-Tangent Square Root (ATSR) nonlinear device for virtual bass systems*, ICASSP 2010 | Paper | The even/odd split as the design axis. **Not** implemented as published: the ATSR even term `sqrt(1 - (gx)^2)` is imaginary past `\|gx\| = 1` and has infinite slope at the edge, which a plugin that must survive a hot drum bus cannot have. Halo's even curve is bounded and defined everywhere instead |
| Oo & Gan, *Harmonic and Intermodulation Analysis of Nonlinear Devices Used in Virtual Bass Systems*, AES 124th Convention (2008) | Paper | Why intermodulation, not THD, is the metric that matters for this class of device — hence `tezla-measure imd` |
| Gan, Kuo & Toh, *Virtual bass for home entertainment, multimedia PC, game station and portable audio systems*, IEEE Trans. Consumer Electronics (2001) | Paper | Missing-fundamental bass enhancement: the psychoacoustics behind Halo's Below mode |
| Le Brun, *Digital Waveshaping Synthesis*, JAES 27(4) (1979) | Paper | Chebyshev harmonic control — the basis of Halo's precision mode. `T_n(cos t) = cos(n t)`, so a weighted sum of Chebyshev polynomials fed a unit-amplitude sine is a harmonic recipe written in numbers. The paper's own framing of input amplitude as a *waveshaping index*, analogous to an FM modulation index, is Halo's Index control. Implemented from the identity rather than from any source: the pedestal and fundamental corrections in `ChebyshevGenerator.hpp` are Fourier coefficients of the composite map and are derived here |
| [`alpo/DeaDBeeF-virtual-bass-plugin`](https://github.com/alpo/DeaDBeeF-virtual-bass-plugin) | **MIT** | A readable reference implementation of ATSR, used to check the paper's formulation against working code |
| [Calf Studio Gear](https://calf-studio-gear.org/) | **GPLv3** | **Control layout and signal-flow structure only, read and not copied.** Its Exciter is four cascaded biquad highpasses into `tap_distortion` into two lowpasses; its Bass Enhancer is the mirror image. Halo's own curves are derived from the papers above and share no code with it |

### Licence position on Calf

`CLAUDE.md` §9 permits reading GPL code to understand a *technique* and forbids
copying it. What was read here was the class declarations in
`src/calf/modules_dist.h` — filter counts, filter orders, and which distortion
object is called — to understand the arrangement of blocks. Calf's distortion
itself is `dsp::tap_distortion`, the tube curve from TAP-plugins, also GPL.
**That curve was deliberately not read**, precisely so that Halo's shaper could
not be influenced by it. Halo's odd and even curves are stated in full in
`shared/tezla-dsp/include/tezla/dsp/HarmonicGenerator.hpp` and follow from the
papers and from ordinary calculus.

No Calf code is present in this repository, and none of it was consulted while
writing the generator.

---

## Dynamics, limiting and true peak — used for Capstone

The three documents live in [`../technical references/`](../technical%20references/),
because the egress proxy in the development container blocks all three domains.
They were read from there rather than fetched.

| Source | Licence / status | Used for |
|---|---|---|
| Perttu Hämäläinen, "Smoothing of the Control Signal without Clipped Output in Digital Peak Limiters", **DAFx-02**, Hamburg, 2002 | Conference paper — cite, do not paste | The max-filter (order-statistics) construction that makes a smoothed limiter gain provably non-clipping. §3.5 describes the dual we actually use, and warns of its hazard. |
| **ITU-R BS.1770-5** (11/2023), Annex 2 | ITU copyright; published for implementation | True-peak measurement: the 12.04 dB attenuation convention, the order-48 four-phase interpolating FIR, and the worst-case under-read table that decides our oversampling control. **The coefficient table is typed in verbatim** — the one thing in Capstone that is copied rather than derived. |
| Geraint Luff / Signalsmith Audio, "Designing a straightforward limiter", 2022 | Article, © Signalsmith Audio Ltd — cite, do not paste | A modern treatment of the same structure. The hold refinement — widening the minimum window without widening the smoothing — comes from here. |
| Dimitrios Giannoulis, Michael Massberg & Joshua D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", **JAES 60(6)**, 2012 | Journal paper — cite, do not paste; not fetched when this row was written, **since READ first-hand** (user-supplied PDF, 2026-09-01 — see the Membrana section below, which records what the reading confirmed) | The quadratic soft-knee gain-computer form. `GainComputer` has named it in a comment since it was written; this row is the record that was missing. The infinite-ratio specialisation used by Capstone was derived and measured here rather than transcribed, and Phonoss V1 generalises it back to a finite ratio — a derivation checked by measuring the realised ratio, and pinned bit-exact against the limiter form at 1/ratio = 0. |

---

## Loudness — Transpectus

| Source | Licence / status | Used for |
|---|---|---|
| **ITU-R BS.1770-5** (11/2023), Annex 1 | ITU copyright; published for implementation | The K-weighting filter pair, the `−0.691` offset, the 400 ms / 75 %-overlap block structure, and the two-stage gating (absolute `−70 LUFS`, relative `−10 LU`). The **printed 48 kHz coefficient tables are used as the target a design has to reproduce**, not as the coefficients themselves — see below. |
| **EBU R 128** (2020) and **EBU Tech 3341** | EBU, freely published | The −23 LUFS broadcast target, and the compliance test cases the meter is checked against rather than eyeballed. `tests/test_LoudnessMeter.cpp` implements cases 1–3 with the ±0.1 LU tolerance the document specifies. |
| **libebur128** (Jan Kokemüller) | MIT | The analogue-prototype parameters that generalise the K-weighting to any sample rate: shelf `f0 = 1681.974450955533`, `G = 3.999843853973347`, `Q = 0.7071752369554196`; high-pass `f0 = 38.13547087602444`, `Q = 0.5003270373238773`. Attributed at the point of use in `LoudnessMeter.hpp`. |
| **AES TD1008** | AES technical document | The recommendation the streaming platforms converged on, and the reason their targets cluster at −14 to −16 LUFS. Context for the platform table in Transpectus, which is stored with a verification date because those numbers change. |

### Why the coefficients are designed rather than typed

BS.1770 prints its K-weighting coefficients **for 48 kHz only**. They look like
constants and they are not: used unchanged at 44.1 or 96 kHz they give a filter
with the wrong corner frequencies and a loudness reading that is quietly wrong.
This is CLAUDE.md §6 in its purest form, and it is the one thing most likely to
be got wrong by someone reading the Recommendation quickly.

So the filters are designed from the analogue prototype at the host's actual
rate. The prototype parameters above are taken under §9 — the standard *is* the
definition, so no measurement of ours could say a filter of our own was the
wrong one to have chosen — and the proof that they are the right prototype is
that at 48 kHz they reproduce the Recommendation's own printed numbers to
**8.9e-16**, which is double rounding rather than agreement to a tolerance.

`tests/test_LoudnessMeter.cpp` asserts that, and — because that check alone
would still pass for a meter that ignored its sample rate entirely — also
asserts that the filter at 96 kHz is a *different* filter, and that a −23 dBFS
tone reads −23.0 LUFS at 44.1, 48, 96 and 192 kHz.

One residual worth stating rather than hiding: the standard's −0.691 dB offset
is a fixed constant, but the bilinear transform warps the filter's 1 kHz gain
slightly with rate — +0.7005 dB at 44.1 kHz against +0.6707 at 192 kHz. So the
same tone reads −22.990 at 44.1 kHz and −23.020 at 192 kHz. That spread is in
the Recommendation, not in this code, and it is five times inside EBU's
±0.1 LU tolerance.

### What Capstone derives rather than takes

The guarantee itself was derived and measured before any of the three was read,
and the measurement is in `tests/test_LimiterCore.cpp`: a centred running minimum
followed by any non-negative unit-sum kernel supported on the same window puts
the smoothed gain provably below the gain each sample requires. Measured
overshoot, one ULP; the three plausible alternatives overshoot by 0.22, 0.86 and
1.91 against a 0.5 ceiling.

Hämäläinen's paper max-filters the *level* and needs a clipping-control term to
compensate a one-pole detector, and his §3.5 notes that the dual formulation —
a min filter on the *gain*, which is ours — can drive gain below zero. Ours
cannot: it is a convex combination of values in `[0, 1]`. That difference is why
the structures are not the same, and it is worth the paragraph.

Two GPL implementations were found while searching and **not read beyond their
README**: [x42/sound-gambit](https://github.com/x42/sound-gambit) and
[ryukau/OfflineLimiter](https://github.com/ryukau/OfflineLimiter). The second's
documented failure — that limiting in an oversampled domain and then decimating
can come back over the ceiling, needing up to four iterative passes — is what
ruled that architecture out here. That is a fact about the problem, not a piece
of their code.

---

## Indexes worth keeping open

- [olilarkin/awesome-musicdsp](https://github.com/olilarkin/awesome-musicdsp) — curated index of everything else
- [musicdsp.org](https://www.musicdsp.org/) — the old archive; still the fastest route to a working snippet
- [DAFx paper archive](https://www.dafx.de/paper-archive/) — free, searchable, the primary source for most of the above

---

## Valve and amplifier modelling — used for Anvil

The papers themselves are in [`technical references/anvil/`](../technical%20references/anvil/),
fetched by the user because this container's egress proxy refuses `dafx.de` and
`arxiv.org` outright — see `CLAUDE.md` §9 on asking rather than working around it.

| Source | Licence / access | What we take |
|---|---|---|
| **Macak, Schimmel & Holters**, *Simulation of Fender Type Guitar Preamp Using Approximation and State-Space Model*, Proc. DAFx-12, York, 2012 | DAFx proceedings, freely published. **Read.** | The **Dempwolf 12AX7 model and its fitted constants**, reproduced verbatim in its Table 1 and §4.1: `Gg 6.06e-4, Cg 13.9, ξg 1.354; Gk 2.14e-3, Ck 3.04, ξk 1.303, μ 100.8`. Typed in at [`tools/include/tezla/measure/Triode12AX7.hpp`](../tools/include/tezla/measure/Triode12AX7.hpp) as a **measurement reference only** — it is not shipped in any plugin. Also the state-space formulation and the spline-table approach we chose *not* to follow. |
| **Dempwolf & Zölzer**, *A physically-motivated triode model for circuit simulations*, Proc. DAFx-11, Paris, 2011 | DAFx proceedings. **Not read** — reached only through the verbatim reproduction above. | The model and constants, second-hand but complete. Cited because it is the primary source for numbers we use; the row is honest that we read them in the DAFx-12 paper. |
| **Giampiccolo, D'Angelo, Bernardini & Sarti**, *A Quadric Surface Model of Vacuum Tubes for Virtual Analog Applications*, Proc. DAFx-23, Copenhagen, 2023 | CC-BY 4.0. **Read.** | Their model, once the constrained coefficients of eq. (13) are substituted, is exactly a **squared linear form** `(a·Vpk + b·Vgk + c)²` — the same power law as Cardarilli's at an exponent of 2.0. Used as one of the four published exponents our curve is checked against, not as an implementation. Also their summary of the Koren and Cardarilli models: Koren's 12AX7 `k = 1.4`, Cardarilli's plate law a 3/2 power of `Vgk + Vpk/μ + h`. |
| **Cohen & Hélie**, *Real-Time Simulation of a Guitar Power Amplifier*, Proc. DAFx-10, Graz, 2010 | DAFx proceedings. In the reference folder for the power-amp stage. | Push-pull output stage and transformer, for Anvil's power section. |
| **Hughes & Kettner**, zenTera DSM-modelling amplifier manual | Manufacturer documentation, read for what a control *does* — `CLAUDE.md` §2.1. | Context for what "Dynamic Sector Modeling" claims: a circuit whose shape adapts continuously with the strength, frequency and harmonic content of the signal, rather than a static snapshot. Read as a design brief, not a specification to reproduce. |
| **Leach**, *Loudspeaker Voice-Coil Inductance Losses: Circuit Models, Parameter Estimation, and Effect on Frequency Response*, JAES vol. 50 no. 6, 2002 | JAES. **Not read** — the result is standard textbook material and is used as a target to fit against, not as an implementation. | The finding that a voice coil's impedance rises as roughly `f^0.6` with a phase near 55°, not `f^1` at 90°, because of eddy currents in the pole piece. `SpeakerLoad`'s two-element approximation is derived and then *measured* against that exponent: slope **0.542** over 400 Hz – 5 kHz, phase **+55°**. Nothing is copied; the paper supplies the number the fit is checked against. |
| **Thiele** and **Small**, the loudspeaker small-signal parameter papers, JAES 1971–1972 | JAES. **Not read directly** — the electrical equivalent circuit and the relations below are in every loudspeaker textbook and in the datasheet of every driver sold. | The driver's electrical equivalent circuit and the standard relations `Res = Re·Qms/Qes`, `Lces = Res/(ωs·Qms)`, `Cmes = 1/(ωs²·Lces)`. Written as a netlist in [`SpeakerLoad.hpp`](../shared/tezla-dsp/include/tezla/dsp/SpeakerLoad.hpp) and checked against the physics rather than against a source: the impedance peak must be `Re·(1 + Qms/Qes)` and purely resistive at resonance, and a test asserts both. |

### What Anvil derives rather than takes

The triode **structure** is derived from the space-charge law, not copied: a
3/2 power of the accelerating voltage, exactly zero below cutoff, normalised to
unity small-signal gain. What that buys is a closed-form antiderivative, so the
stage antialiases exactly — and none of the four published models above has one,
because a softplus raised to a non-integer power cannot be integrated in closed
form.

The published models are used as the thing to be **measured against**, which is
the only honest way to find out whether the trade was worth it. Fitted over the
cutoff half against Dempwolf's 12AX7 on a real load line: **rms 0.0118, worst
2.39% of peak swing, and a fitted exponent of 1.585** — within 6% of Child's
3/2, and well inside the 1.303–2.0 spread the published fits of the same bottle
already disagree over.

The grid-conduction side is deliberately *not* fitted, and diverges by 1.27
normalised units at +3 V. That is the size of the two mechanisms — grid current
and plate bottoming — that belong in the stage's dynamics rather than in a
memoryless curve, and a test asserts the gap stays open.

**The cabinet is synthesised, never captured.** `CLAUDE.md` §2.1: an impulse
response taken from a commercial cabinet is that cabinet's measured property,
and shipping one means shipping somebody else's product. Every curve in
[`Cabinet.hpp`](../shared/tezla-dsp/include/tezla/dsp/Cabinet.hpp) is built from
the mechanism that produces it — the enclosure's alignment, the rear radiation
of an open back, cone breakup, the driver's own top from cone mass and coil
inductance, and the microphone's position and distance — and every number in its
comments was measured from that code. No impulse response, curve, preset or
parameter table has been taken from any product.

**The transformer's frequency dependence is ours.** Cohen and Hélie's power
amplifier is explicit that its transformer is "a simple linear model",
parameterised from datasheet inductances — which is the right call for what they
were measuring and leaves the mechanism on the table. `PowerAmp` integrates the
voltage across the primary and lets the permeability fall as the flux rises;
setting `coreSaturation` to zero recovers exactly the linear model, which is
what the addition is measured against.

---

## SoundFont playback -- Svarayantra

| Source | Licence | Access | Used for |
|---|---|---|---|
| tsf.h (TinySoundFont, schellingb) | MIT | full source read; copy in `technical references/svarayantra/` | RIFF/hydra layout cross-check, timecents pin (-11950), envelope segment walk, loop-mode semantics |
| FluidSynth `fluid_gen.c` | LGPL-2.1-or-later | full source read; copy in `technical references/svarayantra/` | **the generator defaults/ranges table, taken verbatim** into `Sf2Generators.hpp` -- a defaults table is knowledge measurement cannot verify, so it is copied and attributed rather than derived |
| FluidSynth `fluid_voice.c`, `fluid_adsr_env.h`, `fluid_mod.c`, `fluid_sffile.c` | LGPL-2.1-or-later | full source read; same folder | zone-resolution semantics (absolute vs relative generators, global zones), envelope section conversions, default-modulator shapes |
| SoundFont 2.04 specification PDF | spec document | **NOT retrievable from this container** -- the URL is recorded in `technical references/svarayantra/readme.md` for the user to fetch | would settle: the exact concave-curve table, custom modulator transforms, sm24 details |
| DLS Level 1 default articulation | spec (published) | recalled convention, not fetched | the velocity square law (attenuation = 20 log10(127^2/vel^2) dB) implemented as the stand-in for SF2's default concave velocity modulator; both reach 0 dB at full velocity |

Two places the references *disagree* and this implementation follows the
specification's own stated convention instead, with tests pinning the choice
by measurement: the volume envelope's decay/release slope (tsf inherits
LinuxSampler's ~80 dB exponential constant, FluidSynth ramps a linear value
against a 960 cB table; ours traverses the conventional 100 dB range in the
stated time -- `test_Sf2Envelope.cpp` pins -10 dB after 0.1 s of a 1 s
decay), and ROM samples (carried by the parser, refused by the model).

Nothing from either implementation was pasted except the defaults table;
the parser, model, envelope, voice and engine are derived from the format's
structure and measured (CLAUDE.md section 9's rule, both halves).

---

## Tape machines -- Ferrite

| Source | Licence | Access | Used for |
|---|---|---|---|
| Jatin Chowdhury, "Real-time Physical Modelling for Analog Tape Machines", DAFx-19 | paper (LaTeX source ships in the GPLv3 repo below) | **read first-hand, in full** -- dafx.de and arxiv.org are proxy-blocked, but the author includes the paper's LaTeX in the repository; copy in `technical references/ferrite/` | the whole model: Karlqvist record head collapsing to H = NEI/g, the Jiles-Atherton dM/dt form with Langevin guards, trapezoidal H-dot recursion, tanh continued fraction, physical tape constants (Ms 3.5e5 A/m, k 27 kA/m, a 22 kA/m, c 0.17, alpha 1.6e-3), the playback loss product e^(-kd)(1-e^(-k delta))/(k delta) sinc(kg/2), TC-260 bias practice, the pulse-train flutter characterization |
| AnalogTapeModel (CHOW Tape Model), jatinchowdhury18 | GPL-3.0 (compatible with AGPL-3.0-only via GPLv3 s13) | full source read; key files copied to `technical references/ferrite/` with the repo LICENSE | the production decisions the paper predates: normalized J-A (Ms ~ 1, k 0.47875), the musical mapping drive->a, sat->Ms, bias->c (taken with attribution), Newton-Raphson with analytic dM/dt-prime and Talpha = T/1.9, per-solver input clamps and blow-up guards, bias-as-parameter (no carrier) in the default mode, loss FIR by frequency sampling with its 20 Hz wavenumber floor, the head-bump peak rule f = v*0.0254/(gap*500), TC-260 flutter partials (f, 2f, 3f at -230/-80/-99 us, phases 0, 13pi/4, -pi/10, centre +350 us), wow's Ornstein-Uhlenbeck amplitude process |
| Jiles & Atherton 1986; Jiles 1992; Bertram, *Theory of Magnetic Recording*; Camras, *Magnetic Recording Handbook* | papers/books | **not read** -- cited through the DAFx paper only | the provenance of the physical constants; every number used here is quoted from the DAFx paper's own citations of them |

What Ferrite copies is confined to knowledge measurement cannot verify --
the equation, the fitted constants, the flutter partial set, the mapping
structure -- each attributed at the point of use. Everything with observable
behaviour (the solver, parameter ranges, the minimum-phase loss design, the
generators, the engine) is derived and pinned by measurement, per section 9.

---

## Physical modelling -- Malleus

The modal percussion instrument. The rule of section 9 applied here: the mode
mathematics is derived and pinned by test; the two things measurement cannot
check -- an empirical partial recipe and a published component behaviour --
are taken from the literature and say so.

| Source | Licence / status | Used for |
|---|---|---|
| Free-free bar mode equation, cos x cosh x = 1 (Euler-Bernoulli beam theory) | mathematics, no licence | Bar mode ratios 1 : 2.756 : 5.404 : 8.933, root-found in-house at design time and pinned against the classic figures in `tests/test_ModeShapes.cpp` |
| Circular membrane modes = zeros of Bessel J_m (classical acoustics) | mathematics, no licence | Membrane ratio table, computed in-house via `std::cyl_bessel_j` bisection at design time, pinned against 2.405 / 3.832 / 5.136 / 5.520 |
| Stiff string f_n = n sqrt(1 + B n^2) (piano inharmonicity, standard result) | mathematics, no licence | The String end of the Material morph and the Stretch control's physical anchor |
| Julius O. Smith, *Physical Audio Signal Processing* (online book, ccrma.stanford.edu) | freely readable; **not fetched from this container** -- the two-pole resonator formulation used is standard textbook DSP and is derived and measured here rather than transcribed | `ModalResonator`'s per-mode resonator and the modal-synthesis framing |
| N. H. Fletcher & T. D. Rossing, *The Physics of Musical Instruments* (Springer) | book; **not read first-hand from this container** | The church bell's minor-third partial series (hum 1/2, prime 1, tierce 1.2, quint 1.5, nominal 2, ...) -- an empirical founders' profile with no closed form, trusted through the standard organology literature and marked as such at the point of use. Everything else those chapters cover is derived instead |
| McIntyre & Woodhouse's bow-friction family (hyperbolic stick-slip curve; DAFx/JASA literature) | papers; **not fetched from this container** (dafx.de refused at the network layer) | The Bow exciter's friction curve *shape*, and the rosin coefficients mu_s = 0.8 / mu_d = 0.3 -- the standard figures quoted throughout the bowed-string literature, attributed again at the point of use in `plugins/Malleus/Dsp/Bow.hpp`. The implementation is built from the standard curve form and then measured -- onset map vs pressure and speed, mode-lock spectrum, boundedness sweep, rate independence -- rather than transcribed; if the papers become available the curve constants should be revisited against them. The hair-compliance one-poles that stabilise the discrete loop are a stated engineering construction (the compliance is real physics, the corner is a tuned voicing constant), not a transcription |
| Vactrol (LED + LDR) behaviour: fast light-on, slow nonlinear dark-decay | physics of a component, modelled from the mechanism per section 2.1 | `LowpassGate` -- the west-coast low-pass gate |

The membrane, bar and plate tables, the morph, and the Overtone Lock quantise
are all checkable by measurement, so per section 9 they are **built, not
taken**: the tests pin the low-mode values every acoustics text agrees on,
which is the copy-proof kind of citation.

---

## Vocal dynamics -- Phonoss

The rule of section 9 applied to a channel strip: the dynamics mathematics is
textbook and is derived and measured here; the one thing worth taking is the
knee form, recorded in the dynamics table above.

| Source | Licence / status | Used for |
|---|---|---|
| Giannoulis, Massberg & Reiss (above) | see the dynamics table | The soft-knee compressor curve, generalised to a finite ratio in `GainComputer` |
| Linkwitz-Riley crossover (Linkwitz, JAES 1976; standard result) | mathematics, no licence | The de-esser's band split, through the existing `dsp::LinkwitzRiley4` |
| Feed-forward compressor topology (level -> static curve -> smoothing -> makeup), and hysteresis on a gate's two thresholds | standard engineering, no licence | `dsp::CompressorCore` and Phonoss's `Gate`. Both are built from the mechanism and then measured -- the ratio table, the attack and release times, the chatter counts -- rather than transcribed from anywhere |

**The gate needs two mechanisms, not one, and measurement is what settled
that.** The first draft of its test assumed hysteresis and hold were
interchangeable ways of stopping chatter. They are not: on a 400 Hz tone
sitting exactly on the threshold and wobbling +/-0.5 dB at 5 Hz, the gate flips
1600 times in two seconds with neither, **20 times with a 40 ms hold and no
hysteresis**, and once with 3 dB of hysteresis. Hold cannot bridge a 100 ms
excursion; hysteresis cannot bridge a real gap between syllables. Two problems,
two mechanisms, and the table is in `tests/test_Compressor.cpp`.

**Sibilance as a ratio rather than a level is our own construction**, not a
transcription. The observation behind it -- that an /s/ is characterised by
high-band energy being large *relative to* the voice's body, which is why a
fixed high-band threshold both lisps and over-esses -- is ordinary phonetics
(sibilant fricatives put their energy above about 4 kHz while vowels put it in
the formants below 3 kHz), and the detector is built from that and then
measured: the gate that decides whether it works is that a sung vowel swept
across 40 dB produces no reduction while an /s/ at those same levels produces
the same reduction at every one of them.

Nothing here is taken from any commercial de-esser, compressor or channel
strip. No binary has been inspected and no curve extracted.

---

## Telephony -- Crossbar

Section 9's exception, and the clearest example of it in the repository.
Everywhere else here the rule is *derive and measure*; a telephone tone is the
opposite case, because **measurement cannot tell you that 941 Hz should have
been 940**. Every frequency, level and cadence below is a published standard
figure, taken deliberately, attributed at the point of use in
`plugins/Crossbar/Dsp/ToneTables.hpp` and pinned by a test in the commit that
introduced it.

| Source | Licence / status | Used for |
|---|---|---|
| ITU-T Recommendation Q.23, *Technical features of push-button telephone sets* | ITU standard; **not fetched from this container** -- itu.int is refused by the egress proxy. The four low and four high group frequencies are quoted identically by every secondary source consulted, including ETSI ES 201 235-2 and the Q.24 abstract | The 4x4 DTMF matrix: rows 697 / 770 / 852 / 941 Hz, columns 1209 / 1336 / 1477 / 1633 Hz, and the 1.5-1.8% frequency tolerance the plugin's own accuracy is judged against |
| ITU-T Recommendation Q.24 (national DTMF receiver requirements) | ITU standard; abstract only | Normal and reverse **twist** -- the receiver must accept 8 dB normal / 4 dB reverse, which is why Crossbar's Twist control is centred at the +2 dB transmitters actually use and swept over the range a decoder tolerates |
| Bell System **Precise Tone Plan** (AT&T, 1976) | historic standard, published; secondary sources only -- **the original Bell practice was not obtained** | The North American call-progress set: dial 350+440 continuous; audible ringing 440+480 at 2 s on / 4 s off; busy 480+620 at 0.5/0.5; reorder ("fast busy") 480+620 at 0.25/0.25; and the reference levels -13 / -19 / -24 dBm those four are specified at |
| Receiver-off-hook (howler / ROH) tone, North American Numbering Plan practice | published practice; secondary sources | 1400 + 2060 + 2450 + 2600 Hz at 0.1 s on / 0.1 s off, at a level deliberately far above every other in-band signal -- it is meant to be heard across a room |
| AT&T / Bellcore **Special Information Tone** (SIT) | published standard; secondary sources | The three-tone intercept: first segment 913.8 Hz (short variant) or 985.2 Hz (long), second 1370.6 / 1428.5 Hz, third always 1776.7 Hz; segments of 274 ms or 380 ms with the third always 380 ms |
| British Telecom / BT network tones (the UK set) | published practice; secondary sources | Dial 350+450 continuous (the 100 Hz beat is the point); engaged 400 Hz at 0.375/0.375; ringing 400+450 at 0.4 on / 0.2 off / 0.4 on / 2.0 off; number unobtainable 400 Hz continuous; congestion 400 Hz at 0.4/0.35/0.225/0.525 **with the second burst 6 dB louder** |
| ITU-T Recommendation **G.711**, *Pulse code modulation (PCM) of voice frequencies* | ITU standard; **not fetched from this container**. The segment structure below is reconstructed from the standard's well-known form and then verified three ways by measurement | The two companding laws. mu-law: 14-bit input, bias 33, clip 8159, eight segments ending at `(64 << s) - 1`; A-law: 13-bit input, no bias, segments ending at `(32 << s) - 1` with the first two sharing a step. Both reconstruct at the interval midpoint, giving the maximum output magnitudes 8031/8192 and 4032/4096 that the plugin's ceiling test pins |
| Sun Microsystems' `g711.c` (the CCITT reference implementation lineage; carried by SoX, MBROLA and many others) | "Users may copy or modify this source code without charge" -- permissive, and compatible | **Consulted for the constants only** -- BIAS 0x84, CLIP 8159, and the segment-end tables. No code taken: the segment ends are `(64 << s) - 1` and `(32 << s) - 1`, which is just where the octaves fall, so `Companding.hpp` derives them and the bit packing follows from the structure rather than from anyone's source file |
| ITU-T Recommendation G.712 (transmission performance of PCM channels) | ITU standard; not fetched | The 300-3400 Hz toll band that the BAND control's default reproduces |
| ITU-T Recommendation G.722 (7 kHz audio-coding within 64 kbit/s) | ITU standard; not fetched | The 50-7000 Hz wideband option and the 16 kHz rate the RATE list names |

**What the companding laws were verified against, since the standard was not
readable.** `shared/tezla-dsp/include/tezla/dsp/Companding.hpp` derives the
segment structure rather than carrying a table, so the tests stand in for the
standard's text. Four independent statements, in
`tests/test_Companding.cpp`:

| what is asserted | measured |
|---|---|
| every code word reconstructs at the **midpoint** of the range of inputs producing it | worst deviation 2.4e-07 over the 128 positive codes, which is the sweep's own resolution |
| the SNR does not follow the level -- the defining property of companding | slope +0.067 dB/dB for mu-law and +0.062 for A-law against **+0.970 for a linear 8-bit quantiser**, over 0 to -40 dBFS |
| encoding is monotone, and decoding then re-encoding lands on the same value | holds for all 256 codes of each law; mu-law's two zero codes (+0 and -0) are the one place a code-level round trip legitimately does not |
| the ceilings the structure implies | 8031/8159 = 0.984312 and 4032/4095 = 0.984615, both 0.137 dB down |

The few decibels of ripple on the companded SNR curves (4.3 dB for mu-law, 4.8
for A-law) is the segment structure showing, not error: eight straight pieces
approximating a log curve dip slightly wherever a peak sits just above a
segment boundary. A smooth log law would not ripple, and would not be G.711.

**What was not obtainable, and what that costs.** The ITU-T texts, the Bell
practice documents and the BT specification are all refused by this container's
egress proxy, so every figure above reached the plugin through search results
and secondary technical sources rather than through the standard itself. The
figures are consistent across the sources consulted and are not obscure -- they
are quoted identically by ETSI documents, vendor application notes and
telephony references -- but *"the standard says X"* and *"every secondary
source says the standard says X"* are different claims and this row is the
difference. If the primary documents become available, the numbers worth
re-checking first are the SIT segment durations (274 vs 276 ms appears both
ways in secondary sources) and the BT congestion tone's level step.

Nothing here is taken from any commercial plugin, sample library or recording.
No binary has been inspected. The tones are generated from their published
frequencies, which is the only way this could have been done anyway: a
telephone tone *is* its specification.

---

## Microphone physics and presence — Membrana

Membrana models a microphone from mechanism — sphere diffraction, the
first-order gradient, level-tracking dynamics — and never from any measured
commercial microphone. The three load-bearing papers were **read first-hand
from user-supplied PDFs** (2026-09-01): the container's egress proxy refuses
both hosting domains (escholarship.org and www.eecs.qmul.ac.uk were tested and
returned blocked), so per CLAUDE.md §9 the URLs were given to the user, who
fetched them. That access route is part of the record.

| Source | Licence / status | Used for |
|---|---|---|
| Richard O. Duda & William L. Martens, "Range dependence of the response of a spherical head model", **JASA 104(5):3048–3058, Nov 1998**, DOI 10.1121/1.423886 | Journal paper — cite, do not paste. **READ first-hand, user-supplied PDF, 2026-09-01** | The exact rigid-sphere scattering series at finite source range (their Eqs 7–8), and — the thing actually *taken* under §9's copy-what-measurement-cannot-check rule — **the Appendix A–B evaluation algorithm**: the Q_m-polynomial substitution that keeps every intermediate bounded where raw spherical-Hankel recursion overflows, its seeds (Q_0 = z, Q_1 = z − z², Q_{−1} ≡ z), the derivative form (A7), the assembled per-term expression (A10), and the stopping rule (two successive terms with fractional change below threshold). Attributed again at the point of use in `plugins/Membrana/Dsp/SphereDiffraction.hpp`. Also: the proof the sphere response is **minimum-phase at every range and angle** (Sec II.D), which is what makes Membrana's minimum-phase FIR realisation faithful rather than convenient; the pinned limit values the tests assert (+6 dB HF on-axis limit, +3 dB at μ = 1, ≈ −13 dB at θ = 150°/μ = 30, the ρ = 1.25 HF rise of +2 dB against the far-field +6); and footnote 1, which fixes their e^(i(kr−ωt)) convention as the conjugate of Kuhn's and Rabinowitz's — so magnitude-only use is convention-proof. Provenance of the algorithm: Bauck & Cooper (1980), extended by the authors to finite range. |
| Richard O. Duda & William L. Martens, "Range-Dependence of the HRTF for a Spherical Head" (conference version; eScholarship item 0kb7r9m9) | Conference paper — cite, do not paste. **READ first-hand, user-supplied PDF, 2026-09-01** | The same model, plus the measurements: their bowling-ball HRTFs confirm the series for ρ ≥ 2, and below ρ = 2 it was their *source* that stopped being a point — the series itself is exact. Read first, and superseded in detail by the JASA version above. |
| Rabinowitz, Maxwell, Shao & Wei, "Sound localization cues for a magnified head", *Presence* 2:125–129 (1993) | **Not read — paywalled, and deliberately not pursued.** | The origin of the series formula, per the read papers. Not needed: the series is the standard exterior-scattering solution, the read papers state it in full with an evaluation algorithm, and the limit tests pin the structure. Recorded so nobody buys it thinking it gates anything. |
| Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", **JAES 60(6)**, 2012 | Journal paper — cite, do not paste. **Now READ first-hand, user-supplied PDF, 2026-09-01** — this upgrades the Capstone-era row above, which recorded it as cited-not-read. | The design recommendation Membrana's two dynamics stages adopt: feed-forward, with the **smoothing detector in the log domain placed after the gain computer** (§3.2 and conclusion) — no attack lag, no fixed detector threshold, a guaranteed-smooth return to 0 dB, a freely variable knee, and (their Fig 9) minimum distortion of the gain modulation. Eq (7): the step-invariant one-pole α = e^(−1/(τ·fs)) whose coefficient can move without clicks — confirming the house `EnvelopeFollower` form. Eqs (14)–(17): the decoupled and branching detectors and their smooth variants; Membrana uses the smooth branching form in the log domain. Also verified: Eq (4) is the quadratic soft-knee `GainComputer` has cited since Capstone. **The paper does not treat upward expansion** ("the analysis in this paper was limited to the design of standard compressors") — Membrana's presence and detail lift curves are our own design on the paper's validated methodology, and no printed expander equation exists to have been transcribed. |
| Beranek & Mellow, *Acoustics: Sound Fields and Transducers*; Eargle, *The Microphone Book*; Wiener, JASA 19:444–451 (1947) | books/paper — **not read**, background only | The proximity effect is derived in `plugins/Membrana/PLAN.md` from the spherical-wave pressure gradient — a two-line derivation whose result is verified by measurement (the +3.01 dB/546 Hz cardioid-at-5-cm pin and the exact zeros: omni everywhere, any gradient pattern at θ = 90°). These references would only re-derive it; listed so a successor knows they were considered and why they were not required. |

Nothing here is taken from any commercial microphone: no measured response of
a named product, no published polar plot traced, no brand names in presets or
documentation. A capsule diameter range ("large-diaphragm bodies run
~50 mm") is public documentation and is the only kind of product fact used.

---

## Products referenced as sonic targets only

Named in this repository to describe a *sound* or a *workflow*. No binary has
been inspected, no code reverse engineered, no artwork or preset data used.

- PSP VintageWarmer / VintageWarmer2 — single- and multi-band analogue-style
  saturation into compression/limiting, with VU and PPM metering and a
  double-sampling ("FAT") mode. Behaviour understood from the publicly available
  operation manual.
- Steinberg Warp — amp and cabinet simulation (three amp voicings, three cabinet
  types), from the Cubase VST era, built on Hughes & Kettner's DSM. The target
  for **Anvil**. Understood from published descriptions and from H&K's own
  manual for their DSM amplifiers; no binary inspected, no impulse response
  extracted, and none ever will be — Anvil's cabinets are synthesised from
  driver and enclosure physics for exactly that reason.
- Antares Tube, Waves L1 / Renaissance Verb, Bitcrusher — further points of
  reference for later plugins.
