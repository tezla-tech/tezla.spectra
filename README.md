# tezla.spectra

64-bit **VST3** audio plugins for Windows 11 / FL Studio 2026 — built for
dubstep and DnB/jungle production.

Analogue character first: tube, tape and transformer saturation, cabinet
colouration, musical dynamics. Clean digital when you want it to disappear.
Everything is written from scratch, from physics and published research —
no reverse engineering of anyone's product.

## Contents

- [The plugins](#the-plugins)
- [Status](#status)
- [Squashing bugs](#squashing-bugs)
- [Quick start (Windows 11)](#quick-start-windows-11)
- [Quick start (macOS)](#quick-start-macos)
- [Prebuilt binaries](#prebuilt-binaries)
- [How it's put together](#how-its-put-together)
- [Sample rates](#sample-rates)
- [Documentation](#documentation)
- [Licensing](#licensing)

---

## The plugins

**[Emberdrive](plugins/Emberdrive/)** measures identically to within 0.01 dB at
44.1, 48, 96 and 192 kHz, holds its output level within 0.33 dB across a 30 dB
drive range, and keeps audible-band aliasing below −220 dB. It also has a
wavefolder with a ×100 range for when none of that is the point. Numbers, not
adjectives: see [its README](plugins/Emberdrive/README.md).

**[Halo](plugins/Halo/)** is a harmonic exciter that adds harmonics without also
adding a copy of the source — which is what every exciter built on the classic
highpass-distort-blend structure does, and why their blend controls double as
EQs. Its wet path measures −271 dB at the fundamental. Because there is no
source in that path, it can widen the harmonics on their own and leave the sub
exactly where it was.

It also has a **Chebyshev precision mode** (Le Brun, JAES 1979) where you name
the harmonics rather than pick a curve and take what comes: ask for the 5th and
every other component measures 123 dB below it. See
[its README](plugins/Halo/README.md).

Both have a **modulation layer**: three tempo-syncable LFOs and a level
follower, assignable to any continuous control by arming a source and dragging
a ring on the knob. With nothing assigned a plugin is byte-for-byte what it was
before the feature existed, which is a test rather than an intention.

**[Capstone](plugins/Capstone/)** is the last plug: a true-peak brickwall
limiter and a clipper in the same slot. Its ceiling is a theorem rather than a
setting — every stage after the gain computer can only make the gain smaller,
so the clamp at the end has nothing left to do, measured at 6.1e-15. Set to
Strict it holds the ceiling on everything measured here, where a sample-peak
limiter is 3.01 dB over on a tone at a quarter of the sample rate. The clipper
shaves transient tips so the limiter is not asked to duck the whole mix, which
is how a drum bus gets loud without pumping. See
[its README](plugins/Capstone/README.md).

**[Transpectus](plugins/Transpectus/)** is the one that does not touch the
audio — its `process()` takes a `const double* const*`, so that is enforced by
the compiler rather than by a code review. It reads loudness to ITU-R BS.1770-5,
true peak, spectrum and stereo image, and it answers the questions that actually
decide a master: **how much will Spotify turn this down**, and **what did the
last 2 dB of limiting cost.** Every number it shows is either a published
standard or a curve you measured yourself — no invented target curves, no genre
folklore. A −23 dBFS tone reads within **0.0203 LU of −23.000 at 44.1, 48, 96
and 192 kHz**, because BS.1770 prints its coefficients at one rate only and this
filter is designed at whatever rate the host is running. Point at the spectrum
for a crosshair reading frequency, the nearest note in cents and the level
there; a violet peak-hold trace keeps the worst case until you clear it; and
either large panel can be maximised or lifted into its own window. See
[its README](plugins/Transpectus/README.md).


---

## Status

| Plugin | Type | Status |
|---|---|---|
| **[Emberdrive](plugins/Emberdrive/)** | Saturation, wavefolder, rectifier, crusher, feedback, 3-band, modulation | v0.4.0 — 47/47 on Steinberg's validator |
| **[Halo](plugins/Halo/)** | Harmonic exciter, bass enhancer, Chebyshev harmonic synthesis, modulation | v0.3.0 — 47/47 on Steinberg's validator |
| **[Capstone](plugins/Capstone/)** | True-peak brickwall limiter and clipper for the end of the chain | v0.1.0 — 47/47 on Steinberg's validator |
| **[Transpectus](plugins/Transpectus/)** | Loudness, true peak, spectrum, correlation and goniometer — analysis only, bit-exact passthrough | v0.1.0 — 47/47 on Steinberg's validator |

316 framework-free DSP tests pass on Linux, Windows, macOS and ARM64.

See [`plugins/README.md`](plugins/README.md) for the plugin registry.

---

## Squashing bugs

Every bug below was found by building a probe and printing numbers, not by
reading code. They are collected here because the *way* each one hid is more
useful than the fix, and because several were invisible to a test that was
already passing.

The house rules that came out of them live in
[`CLAUDE.md`](CLAUDE.md) §7 and §10.

### A passing test can prove nothing

**Capstone's ceiling sweep — 972 combinations, the plugin's whole selling
point — passed with the limiter deliberately broken.** Halving the minimum
window against the smoother's support should have let peaks through. It did
not, because the clamp at the end of `LimiterCore` holds the ceiling whatever
reaches it: every peak reading landed exactly on the ceiling while the clamp
was removing **1.02 of full scale**. The limiter had quietly become a clipper
and no peak measurement could say so.

The lesson generalises to any guard placed last: **measure what the guard had
to do, not what came out after it.** `getClampExcess()` exists for that, and
reads 6.1e-15 on a correct chain against 1.02 on the broken one. Both sweeps
now go red on that break.

### A reset hiding inside a setter — three times

1. **`DcBlocker::prepare` resets state**, and Emberdrive used it to retune the
   expert DC corner. A first-order highpass's memory *is* its last input and
   output, so zeroing it mid-stream stepped the output by the whole previous
   sample. It ticked once per change from the day it shipped. Fixed with
   `retune()`, which moves the corner and keeps the state.

2. **Emberdrive rebuilt its voicing once per block**, so the output depended on
   the host's buffer size — 64-sample and 512-sample blocks disagreed by
   **0.296 of full scale** while a parameter settled. No arrangement of a
   per-call timer fixes that; the sample loop has to be cut at the timer's
   boundary. Now exactly zero at 64, 100 and 512.

3. **`BypassMixer::setLatency` clears the delay line**, because a ring at a new
   length holds nothing meaningful — and Capstone pushed the latency into it
   once per block. Every callback wiped the dry path. Bypassed at 64-sample
   blocks with 53 samples of latency, **83% of the output samples were exactly
   zero** and the rest jumped 0.4985 between neighbours where the signal itself
   steps 0.0196. It sounded like buffer underruns because structurally it was
   one. Reported by ear before any test caught it; the guard now lives in
   `BypassMixer`, so no caller can reintroduce it.

### Compensating for something that has not happened yet

**Emberdrive's auto-trim used the drive gain's target rather than its current
value.** With a 20 ms ramp, that compensates a level the signal has not reached,
and a level follower driving Drive took a limiter set to −0.3 dBFS to **+1.3
dBFS**. Verified by reverting the fix and watching the test fail.

### Check the instrument before trusting it

Four measurement bugs, each of which produced a confident wrong number:

- A filter "failure" that was **peak-picking under-reading a 16 kHz tone at
  48 kHz** — three samples per cycle reads 0.866, which looks exactly like a
  filter 1.2 dB down. Use RMS, never peak, for a sine's amplitude.
- A **3 dB scaling error** in the harness's own dBFS reference.
- An aliasing comparison that analysed the **first block rather than a settled
  one**. The DFT treats its block as circular and ADAA's first call primes its
  state, so one wrong sample spread a flat floor across every bin and read as
  25 dB of aliasing that did not exist.
- A true-peak probe run at 0.45 of the sample rate, where **the sample grid is
  too dense for the worst case to be reachable** — every ratio read identically
  and the ITU's own bound looked wrong.

### Being right about the theory and wrong about the plugin

**Capstone's true-peak ratio was first scaled by host rate**, the way
[`CLAUDE.md`](CLAUDE.md) §6 scales oversampling and the Recommendation itself
permits. Measured at 192 kHz, that put Standard on a ratio of 1, reading
**1.506 dB under the true peak — identical to Off**, the setting it exists to
improve on. The reduction assumes the metered content is band-limited to about
20 kHz, and a limiter sitting after a clipper cannot assume that. Ratios are
fixed; Strict costs about four times Standard and the control says so.

### One that was only a test

**`BoxStackSmoother::setLength` clamped its floor to the stage count**, so an
attack of zero gave a support of 4 and three samples of latency that the plugin
would then have reported to the host as zero. Four boxes of length 1 have a
support of 1.

### A test that was only a decoration

The rule at the top of this section says a passing test proves nothing until it
has been seen to fail. Here is that happening in the small.

Transpectus writes its reference curves to plain-text `.tzref` files, and those
files are meant to move between the Windows rig and the Mac. Windows text tools
write CRLF; the parser split on `\n` only. So the strip went in, a test went in
alongside it, and both passed.

**Then the strip was removed, and the test still passed.** `strtod` happens to
stop at a carriage return, and `sscanf`'s `%zu` does too — so a CRLF file
already loaded correctly, by luck, and the test had been asserting something
that was true whether the code was there or not.

Chasing why turned up the actual bug underneath. **`strtod`'s result was being
taken on trust.** It returns `0.0` for text it cannot read and reports nothing,
so a corrupt file loaded as ninety-six zeros — a perfectly flat reference,
which is to say a plausible-looking measurement, from a function whose whole
stated job was to refuse exactly that. Requiring the full line to be consumed
fixed it, and made the carriage-return strip load-bearing at the same time:
without it, a Windows-written file is now rejected outright.

Both tests were then seen red, each for its own reason. The decoration became a
test only because it was checked.

### Visible, enabled, and completely unreachable

Reported by ear, so to speak — by using the thing. Popping Transpectus's
spectrum out into its own window and then docking it back left that panel's
buttons gone, and reopening the plugin brought them back.

Both halves of that are the same fact. **Z-order is insertion order**, and
docking a panel re-adds it as a child, which puts it in *front* of buttons that
were added long before. The buttons were still visible and still enabled — they
were behind an opaque panel, which from the outside is indistinguishable from
having been hidden. Reopening the editor "fixed" it by rebuilding in the
original order.

The fix is one line; the useful part is that this class of bug is now
detectable. `tezla-render editor hit:<id>` asks whether a control is actually
the thing a click at its own centre would reach, and names what it hits instead:

```
spectrum-max is NOT reachable at (807, 177) -- a click there hits spectrum
```

Seen red by adding the chrome before the panels and removing the raise, which
reproduces the same burial without any clicking at all.

---

## Quick start (Windows 11)

Install [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/),
[CMake](https://cmake.org/download/) and [Git](https://git-scm.com/download/win) —
all free. Then:

```bat
git clone https://github.com/tezla-tech/tezla.spectra.git
cd tezla.spectra

scripts\build.bat                        :: build everything, Release
scripts\build.bat Emberdrive             :: build one plugin
scripts\build.bat Emberdrive,Foo         :: build a list
scripts\build.bat -install               :: also copy .vst3 to the system folder
scripts\build.bat -list                  :: show available plugin targets
scripts\build.bat NONE -test             :: DSP + tests only, seconds, no JUCE
```

`build.bat` is a plain batch file: it runs in ordinary `cmd.exe`, needs no
PowerShell, and **does not ask you to relax any script execution policy**. If
you would rather run no script at all, the whole build is two CMake commands —
`docs/BUILD.md` §3 has the manual recipe.

Built plugins land in `build\plugins\<Plugin>\<Plugin>_artefacts\<Config>\VST3\`.
`-install` copies them to `C:\Program Files\Common Files\VST3\` (run the prompt
as Administrator), which is where FL Studio scans by default.

Full instructions, including where to get every tool and how to fix a failed
build: **[`docs/BUILD.md`](docs/BUILD.md)**.

## Quick start (macOS)

Needs only the Xcode Command Line Tools and CMake, both free. Builds **VST3 and
Audio Unit**, universal for Apple Silicon and Intel:

```bash
xcode-select --install
brew install cmake ninja

git clone https://github.com/tezla-tech/tezla.spectra.git
cd tezla.spectra
./scripts/build.sh Emberdrive --install
```

Full instructions, including the Gatekeeper quarantine issue that stops
downloaded plugins loading: **[`docs/BUILD-MACOS.md`](docs/BUILD-MACOS.md)**.

## Prebuilt binaries

CI **does not run by itself** — no push trigger, no pull-request trigger. Start
it when you want Windows and macOS builds:

**Actions → CI → Run workflow.**

The **Run workflow** button only appears once **CI** is selected in the left
sidebar; the "All workflows" view does not show it, which is the usual reason
for not finding it. Leave the version box empty to just test and build — the
bundles appear under **Artifacts** at the bottom of the run. Type a version to
cut a GitHub Release instead.

Pushing a tag does the same and always releases:

```bash
git tag v0.5.0 && git push origin v0.5.0
```

See **[`docs/CI.md`](docs/CI.md)**.

---

## How it's put together

```
shared/tezla-dsp/     Header-only, framework-free C++20 DSP. No JUCE.
plugins/<Name>/Dsp/   That plugin's DSP. Also framework-free.
plugins/<Name>/Source/  Thin JUCE layer: parameters, state, editor.
tests/ + tools/       Unit tests and offline measurement (THD, aliasing, response).
```

The DSP is deliberately independent of any plugin framework. It compiles and
runs with plain g++/clang/MSVC, so aliasing, distortion and frequency response
are **measured offline before anything reaches a DAW** — and so moving to CLAP
or a standalone app later costs a wrapper rather than a rewrite.

Design rules, sample-rate/oversampling policy and audio-quality standards live
in [`CLAUDE.md`](CLAUDE.md).

---

## Sample rates

Sessions here run at 48, 96 or 192 kHz, and the plugins must sound identical at
all of them. Nonlinear stages run at a controlled internal rate, with
oversampling set to **Auto** by default: ×4 at 48 kHz, ×2 at 96 kHz, off at
192 kHz — all landing near the same ~192 kHz effective rate. Every oversampling
control's tooltip reports what Auto is actually doing at your current session
rate. Manual override is always available if you want to spend or save CPU.

---

## Documentation

- [`docs/BUILD.md`](docs/BUILD.md) — toolchain setup and build guide (Windows)
- [`docs/BUILD-MACOS.md`](docs/BUILD-MACOS.md) — the same for macOS, plus AU and Gatekeeper
- [`docs/CI.md`](docs/CI.md) — what continuous integration builds and where to get it
- [`docs/PLUGIN-CONVENTIONS.md`](docs/PLUGIN-CONVENTIONS.md) — parameters, presets, UI, versioning
- [`docs/DSP-REFERENCES.md`](docs/DSP-REFERENCES.md) — papers and open-source references used, with licences
- [`plugins/README.md`](plugins/README.md) — plugin registry and how to add a new one

---

## Licensing

**This project is licensed under the GNU Affero General Public License v3** —
see [`LICENSE`](LICENSE). That follows from the toolchain rather than being a
separate choice: these plugins are built against JUCE's free tier, which is
AGPLv3, so they already were. Saying so explicitly is what lets other people's
open-source DSP be used here, and what tells you what you may do with ours.

In practice: use it, modify it, ship it, as long as what you ship stays under
the same licence and its source stays available. If you need a plugin you can
close, JUCE sells a commercial licence and you would need to replace anything
here that came from an AGPL or GPL source.

Third-party dependencies and any externally derived DSP are listed with their
licences in [`docs/DSP-REFERENCES.md`](docs/DSP-REFERENCES.md). The rule for
adding to that list is in [`CLAUDE.md`](CLAUDE.md) §9: derive and measure by
default, copy only what a measurement could never tell you that you had got
wrong, and attribute twice — at the point of use and in the references.

VST is a trademark of Steinberg Media Technologies GmbH. Any commercial plugin
named in this repository is referenced only to describe a *sound* or a
*workflow*; no proprietary code, artwork or data has been used.
