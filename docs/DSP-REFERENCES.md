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
