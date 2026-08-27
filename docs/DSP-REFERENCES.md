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
| Zavalishin, *The Art of VA Filter Design* (free PDF) | book | The standard text on topology-preserving transform / zero-delay-feedback filters. Read this before writing any filter whose cutoff is modulated. |
| Robert Bristow-Johnson, *Audio EQ Cookbook* | public | The biquad coefficient formulas implemented in `shared/tezla-dsp/include/tezla/dsp/Biquad.hpp`. |
| [Surge XT](https://github.com/surge-synthesizer/surge) | GPLv3 | Filters, waveshapers, oversampling, and a well-organised large plugin codebase. Read for architecture; do not copy. |
| [Calf Studio Gear](https://github.com/calf-studio-gear/calf) | LGPL/GPL | Classic effect topologies, clearly written. |
| [Faust libraries](https://faustlibraries.grame.fr/) | permissive | Reference implementations worth comparing our measurements against. |

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

## Products referenced as sonic targets only

Named in this repository to describe a *sound* or a *workflow*. No binary has
been inspected, no code reverse engineered, no artwork or preset data used.

- PSP VintageWarmer / VintageWarmer2 — single- and multi-band analogue-style
  saturation into compression/limiting, with VU and PPM metering and a
  double-sampling ("FAT") mode. Behaviour understood from the publicly available
  operation manual.
- Steinberg Warp — amp and cabinet simulation (three amp voicings, three cabinet
  types), from the Cubase VST era.
- Antares Tube, Waves L1 / Renaissance Verb, Bitcrusher — further points of
  reference for later plugins.
