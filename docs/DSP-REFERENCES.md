# DSP references

Everything this repository leans on, with its licence. **Add a row before the
source influences any code.** A reference whose licence has not been checked is
a reference that has not been read.

The rule from [`../CLAUDE.md`](../CLAUDE.md) §2.1 applies throughout: papers,
open-source code with a compatible licence, published manuals and physics are
all fair game. Commercial binaries are not, in any form.

---

## Licence quick guide

| Licence | What it means for us |
|---|---|
| **MIT / BSD / ISC / zlib** | Copy freely. Keep the copyright notice. |
| **LGPL** | Fine to link against; copying source into ours is not. |
| **GPL / AGPL** | Reading it to learn a *technique* is fine. Copying code makes our plugin GPL. Prefer deriving from the paper. |
| **Paper / book** | Ideas and equations are not copyrightable. Cite it. Do not paste listings verbatim. |

If GPL code ever does get pasted in, say so loudly, record it here, and licence
the plugin accordingly.

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
| Le Brun, *Digital Waveshaping Synthesis*, JAES 27(4) (1979) | Paper | Chebyshev harmonic control. Not used yet; it is the basis of the planned Precision mode |
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
