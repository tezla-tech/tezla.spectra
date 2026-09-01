# Continuous integration

[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) **does not run by
itself.** There is no push trigger and no pull-request trigger: it starts when
you ask for it, or when a version tag is pushed. It does three things.

### Starting a run

**Actions → CI → Run workflow.**

The **Run workflow** button only appears once **CI** is selected in the left
sidebar — the "All workflows" view does not show it, and that is the usual
reason for not being able to find it. Type a version into the box to cut a
release, or leave it empty to just test and build.

Pushing a tag matching `v*` starts the same run and always cuts a release.

### Why it is not automatic

Every commit used to start one. Because `cancel-in-progress` kills the previous
run, a normal working session left a column of cancelled runs and burned macOS
minutes — billed at 10× on private repositories — on builds nobody was going to
download. Thirty-five runs had accumulated this way, all but a handful of them
cancelled, before the trigger was changed.

The cost of the change is real and worth stating: **nothing now checks a push.**
Run the tests locally — `scripts\build.bat NONE -test` on Windows,
`./scripts/build.sh NONE --test` elsewhere — and start CI by hand before
tagging.

---

## 1. DSP tests, on all three platforms

Linux, Windows and macOS each build the framework-free DSP with
`-DTEZLA_PLUGINS=NONE`, run the full test suite, and run `tezla-measure
selftest` to confirm the measurement harness itself is sound before anything
trusts a number it produces.

**No JUCE, no download, no GUI stack.** This is what the DSP/wrapper split in
[`CLAUDE.md`](../CLAUDE.md) §4 buys: a numerical difference between MSVC and
clang shows up in about a minute rather than after a full plugin build, and the
job needs nothing installed on the runner.

## 1b. The same tests, cross-compiled for ARM64

A Linux runner cross-compiles the suite for AArch64 and runs it under
`qemu-user`. Apple Silicon is ARM, and an x86-only assumption in the DSP
compiles cleanly, runs happily and does nothing there — which is exactly how a
denormal guard that only handled x86 reached a release. This job catches that
class of bug in about a minute on a 1× runner, rather than waiting on a Mac.

**Normally switched off, deliberately — see CLAUDE.md §2.3.** The job is
skipped on the default `windows` run, and the local reproduction below is not
run either, until the x86-64 Windows build is bug-squashed and feature-complete.
The technique is right; the timing is not, and every minute spent here is a
minute not spent on the platform the plugins are actually being played on. It is
written down so it is ready when the gate lifts.

It has been run **once** since, on 2026-09-01, because the user asked directly
for a macOS/Apple-Silicon build — the `v0.88.8-sonitus` and `v0.88.8` runs. That
was an exception to the gate and not the lifting of it.

Reproduce it locally, with `gcc-aarch64-linux-gnu` and `qemu-user` installed:

```bash
cmake -B build-arm -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
      -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
      -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
      -DCMAKE_EXE_LINKER_FLAGS=-static -DTEZLA_PLUGINS=NONE
cmake --build build-arm
TEZLA_EMULATED=1 qemu-aarch64 build-arm/bin/tezla-tests
```

### `TEZLA_EMULATED`, and why the budget tests need it

**Set it whenever you run the suite under an emulator, and never otherwise.**
A wall-clock CPU budget is a claim about real hardware; under emulation it
measures the emulator. Measured on this tree, `qemu-aarch64` costs **8.8× to
29.8×** native:

| budget test | native x86-64 | under qemu-aarch64 |
|---|---|---|
| Ferrite, one stereo instance | 15.8% of a core | 139.4% |
| lowpass gate | 0.24% | 4.31% |
| Malleus, 16 bowed voices | 17.9% | 497.4% |
| Malleus, dead engine | 0.36% | 9.30% |
| 64-mode resonator | 0.37% | 11.02% |
| Phonoss full strip | 1.33% | 20.59% |

All six failed on the first emulated run of the grown suite, and not one was a
defect. With the variable set, `CHECK_CPU_BUDGET` still runs the work — so a
crash, a hang or a NaN is still caught — still prints the figure, and marks it
`NOT ASSERTED`. Only the comparison is withheld, and it is withheld visibly.

The binary cannot detect this for itself: a cross-built AArch64 binary is
identical whether it runs under qemu or on an Apple Silicon Mac, and guessing
from the architecture would be wrong in the one case that matters — **real ARM64
hardware must assert**, so it sets nothing.

### Build with clang before spending a macOS runner

**clang is installed in the development container, and a clang build is a
near-free dress rehearsal for Apple clang.** Use it:

```bash
cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Release -DTEZLA_PLUGINS=NONE \
      -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
cmake --build build-clang -j$(nproc) && ./build-clang/bin/tezla-tests
```

It is not the same compiler as Apple's, and it is glibc rather than libc++, so
it cannot promise everything. But it shares the diagnostics and most of the
front end, and it would have caught both of the macOS problems found so far
without a single 10× runner minute:

- **`std::cyl_bessel_j` does not exist on libc++.** C++17's special maths
  functions were never implemented there, so Apple clang refuses the call and
  `ModeShapes.hpp` would not compile at all — Malleus had never once built on
  macOS, and neither libstdc++ nor MSVC had any reason to say so. It is now
  computed by `tezla::dsp::besselJ`, a trapezoidal rule on Bessel's integral,
  agreeing with `std::cyl_bessel_j` to 8.861e-15 over the range used.
- **15 `-Wunused-lambda-capture` warnings**, which GCC does not emit at all.

### Stack frames, and the Windows-only crash they cause

**MSVC gives a thread 1 MB of stack; Linux gives 8.** A test that holds a big
object by value passes here and takes the *whole binary* down on the Windows
runner with `tezla-dsp (SEGFAULT)` — and ctest reports one failed test out of
one, so the log has to be read to find out which test died.

It has happened twice. A Sonitus `Engine` is 414 kB and a `VoiceManager`
404 kB; four tests held two-to-four engines, and one lambda holding a *single*
manager was inlined twice into the same frame. "Only one on the stack" is not a
safe rule, because you cannot count what the optimiser will inline.

Measure it instead of reasoning about it:

```bash
cmake -S . -B build-su -DCMAKE_BUILD_TYPE=Release -DTEZLA_PLUGINS=NONE \
      -DCMAKE_CXX_FLAGS="-fstack-usage"
cmake --build build-su --target tezla-tests
cat $(find build-su -name '*.su') | awk -F'\t' '$2+0 > 100000 {print $2, $1}' | sort -rn
```

Measured on this tree: the offending function used **812,288 bytes** and is
**17,120** now; the largest frame anywhere in `tests/` is 149,488 (Malleus).
Anything approaching a few hundred kB should go on the heap.

**What does not work, checked rather than assumed:** running the suite under
`ulimit -s 1024` on Linux. It passes *with the bug present* — GCC reuses stack
slots between scopes where MSVC allocates one per inlined scope — so it is not
a reproduction and must not be quoted as one.

## 2. Plugin builds, downloadable

Windows and macOS build the actual plugins and upload them as run artifacts:

| Platform | Formats | Architecture |
|---|---|---|
| Windows | VST3 | x86-64 |
| macOS | VST3 + Audio Unit | arm64 by default in CI (`mac_arch` input); universal on request, and always for a local build |

**Only Windows, unless you ask for more.** The "platforms" box on the Run
workflow form takes `windows` (the default) or `all`; a tag push covers Windows.
`all` adds the macOS DSP tests, the macOS plugin build and the emulated ARM64
suite.

**And only some plugins, if you ask for that.** The "plugins" box takes `ALL`
(the default) or a comma-separated list of names — `Sonitus`, or
`Emberdrive,Halo`. It narrows the *plugin build* and nothing else: the DSP tests
still cover the whole tree, because they are framework-free and cost a minute.
An unknown name fails the configure with the list of real ones; anything that is
not a plain comma-separated list of names is rejected by the plan job before it
can reach a `-D` flag.

It exists so an expensive platform can be proved on one plugin before the suite
is committed to it. That is how the first macOS/Apple-Silicon run was done:
`v0.88.8-sonitus` built one plugin on both platforms, and `v0.88.8` followed for
all twelve once it was green. A subset release says so in the first line of its
own notes, because a download whose contents you have to guess at becomes a bug
report later.

The plugin job also carries a **90-minute ceiling**. That is a guard, not an
estimate — see **Cost** below for the six-hour run it exists because of.

That is a phase rather than a policy, and CLAUDE.md §2.3 says when it ends: the
x86-64 Windows build being finished. The rig is Windows 11 and FL Studio and
the loop that matters is build → load → play → say what is wrong; nobody has
loaded any of these in a Mac DAW yet, so a macOS build is an artefact nobody
downloads. It is also the most expensive thing in the workflow — see **Cost**.
When Mac testing starts, run `all` and this paragraph is what changes.

A Windows-only release says so in its own notes rather than quietly shipping
half of what the notes describe.

Find them under **Actions → the run → Artifacts**, at the bottom of the page.
They are kept for 30 days.

JUCE is cloned once and cached, keyed on the version read out of
[`cmake/FetchJUCE.cmake`](../cmake/FetchJUCE.cmake) — so bumping the pin there
is the only change needed and the cache invalidates itself.

## 3. Releases

There are two ways to ask for one, and if you were expecting a release and got
a skipped job, it is because neither happened.

### Push a version tag

```bash
git tag v0.5.0
git push origin v0.5.0
```

### Or run the workflow by hand

**Actions → CI → Run workflow**, then type a version into the box. Useful when
you want a build to hand to someone without committing to a tag first — the tag
is created for you, pointing at the exact commit that was built and tested.

Leaving the box empty just builds, and cuts no release.

### Either way

The version must look like `v1.2.3`, optionally with a pre-release suffix
(`v1.2.3-beta.1`). Anything else fails the job rather than creating a tag you
did not mean. Both routes package the artifacts into per-platform zips and
attach them to a GitHub Release, with notes that include the macOS quarantine
instructions — without those, a downloaded plugin will not load and the DAW's
error message blames the plugin.

The build job writes all of this into the run summary, so the reason a release
did or did not happen is on the run page rather than only in this file.

### A cancelled macOS job does not cancel the release

**This is the single most expensive lesson in this file.** Five release runs on
2026-09-01 produced nothing, and not one of them failed for a reason connected
to the code.

The mechanism is worth writing down because it is not obvious. **A matrix job's
aggregate `result` is `cancelled` if *any* leg was cancelled.** One dead macOS
leg therefore made `needs.test.result` and `needs.build.result` read
`cancelled`, which matched neither `success` nor `skipped`, which skipped
`build` and then `release`. Run 47 threw away a completed Windows test job and
never even started the Windows plugin build. Run 50 was thirty minutes into a
healthy twelve-plugin Windows build whose release had already been doomed by a
job that died fifteen minutes in.

So `build` and `release` now accept `cancelled` from `test`, `release` accepts
anything but `skipped` from `build`, and the macOS *build* leg additionally
carries `continue-on-error` (keyed on `matrix.name`, not `matrix.os`, so it
cannot silently stop matching when the runner label moves). Note that
`continue-on-error` was **not** sufficient on its own: it converts a *failure*,
and a job cancelled for want of a runner — or killed at a time limit — is not a
failure. The `if` gates are the load-bearing half.

**Proven, not theorised:** on run 51 the macOS leg was cancelled and the
release published anyway, carrying the Windows binaries. That also settles a
documentation ambiguity — `!cancelled()` does *not* block a job whose
dependency was cancelled; it only blocks when the whole run is cancelled.

What still stops a release, deliberately:

- a test that **ran and failed** (`failure` is in neither list);
- somebody cancelling the whole run (`!cancelled()`);
- nothing having been built at all — the Package step exits 1 when no
  artefacts were downloaded, so a run where every leg died publishes nothing
  rather than an empty release.

### Two different macOS failures, and only one of them is fixed

Do not conflate these; the first was diagnosed wrongly for several hours.

**1. Starvation (runs 45, 47, 49, 50).** The macOS job cancelled at *exactly*
15m01s after creation with `runner_id: 0` — no runner ever assigned — while
Windows picked one up in three seconds. This looked like an account-level
entitlement problem. On run 51, with the label moved from `macos-latest` to
`macos-14`, a runner was assigned in **three seconds**. So the entitlement
reading was probably wrong. **The cause remains confounded** — the label
changed at the same moment capacity may simply have recovered, and one run
cannot separate them. Keep `macos-14` because it works, not because it is
proven to be why.

**2. Duration (run 51) — unresolved.** Same commit, same JUCE cache, both legs
side by side:

| Leg | Formats | Architectures | Cores | Build step |
|---|---|---|---|---|
| Windows | VST3 | x86-64 | 4 | **37m49s** → uploaded |
| macOS | VST3 + AU | arm64 + x86_64 | 3 | **6h04m23s** → killed |

GitHub cancelled the macOS job on its own **360-minute ceiling**, which
`timeout-minutes` cannot raise. Nothing was wrong with the build — six hours of
Apple clang with no compile error, the first time this suite has ever been
compiled on a Mac. It is simply about four times the work on three quarters of
the cores: two architectures times two plugin formats.

A twelve-plugin universal macOS build therefore **does not reliably fit in a
GitHub-hosted job.** The options, each costing something real:

- **arm64 only** — roughly halves the compile, loses Intel Mac support;
- **the `plugins` input** — a few plugins per run, no single whole-suite zip;
- **split the matrix** per format or per architecture — more jobs, and the
  release layout changes.

**The user chose the first, on 2026-09-01, as the CI default.** The `mac_arch`
input (`arm64` | `universal`) drives `CMAKE_OSX_ARCHITECTURES` on the macOS
leg, and the architecture is baked into the artefact name —
`macos-arm64-vst3-au` or `macos-universal-vst3-au` — so the zip says what it
is and the release notes read the name back rather than guess. What changed is
the *CI default*, not the target: a local build is still universal unless
`-DTEZLA_UNIVERSAL_BINARY=OFF` is passed, because a local build has no six-hour
limit. Intel Mac users of a CI release self-compile for now; the notes say so.

A Windows-only release says which of these two situations it is in: the notes
distinguish "macOS was never asked for" from "macOS was asked for and did not
produce binaries", decided from what the run *planned* rather than from what it
produced. v0.88.8-alpha shipped before that distinction existed and carries the
wrong sentence — it tells the reader to re-run with `platforms: all`, which is
exactly what had been done.

---

## Downloaded builds and macOS quarantine

**CI builds are not code signed.** macOS attaches a quarantine attribute to
anything downloaded, and Gatekeeper refuses unsigned quarantined plugins — the
DAW then reports the plugin as damaged, or does not list it at all.

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/*.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/*.component
```

Signing properly needs a paid Apple Developer account; see
[`BUILD-MACOS.md` §4](BUILD-MACOS.md#4-gatekeeper--read-this-before-filing-a-bug).
Locally built plugins are never quarantined, so this only affects downloads.

---

## Cost

macOS runner minutes bill at **10×** on private repositories; Windows at 2×,
Linux at 1×. The macOS plugin build is the expensive job, and building a
universal binary compiles everything twice.

Measured, from run 38:

| Job | Duration |
|---|---|
| DSP tests (Linux) | 47 s |
| DSP tests (macOS) | 1 m 05 |
| DSP tests (Windows) | 1 m 17 |
| DSP tests (ARM64, emulated) | 6 m 31 |
| Plugins (Windows) | 9 m 20 |
| **Plugins (macOS)** | **6 h 04, then cancelled** |

**Two lessons in that table, and neither is the obvious one.**

The emulated ARM64 job is the one people notice, and it is 2% of the wall time.
Cutting it saves nothing worth having, and it is the only thing here that
catches an AArch64-specific numerical bug — it found the FMA-contraction one,
which was invisible on x86-64 and always active on ARM.

The macOS plugin build is everything, and the cause was **link-time
optimisation**, not the platform. Identical work took Windows nine minutes under
MSVC's LTCG. JUCE's recommended flag is a plain `-flto`, which on Apple clang
means *monolithic* LTO — the whole program merged into one module and optimised
on one core — for six plugins in two formats, twice over for a universal binary.
`TEZLA_LTO` is now **off** by default; see [`BUILD.md`](BUILD.md#a-note-on-tezla_lto).
The workflow does not pass it, so nothing turns it back on by accident.

Two things guard against the next one. The plugin job has a **90-minute
`timeout-minutes`**, so a hang costs ninety minutes of a 10× runner rather than
however long it takes somebody to notice. And the **plugins box** lets an
expensive platform be proved on one plugin first — which matters more now than
it did when that table was measured, because there were six plugins then and
there are twelve today.

If you later want the cheap `test` job back on every push while leaving the
expensive `build` job manual, split it into a second workflow file with its own
`on: push` — a single workflow cannot give one job a trigger the others do
not have.

---

## What CI does not do

- **It does not run the plugin validators.** Steinberg's `validator` and Apple's
  `auval` both catch real problems, and wiring them in is a natural next step —
  it was left out to keep the runs quick. Locally, both are documented in
  [`BUILD-MACOS.md` §6](BUILD-MACOS.md#6-validating).
- **It does not test the plugin in a host.** Nothing automated does; that is
  what the standalone build and your ears are for.
- **It does not sign or notarise.** That needs an Apple Developer account and
  secrets in the repository.

---

## A caveat about this workflow

That paragraph used to say the workflow had never run on a Windows or macOS
runner. It has: run 23 built and uploaded a Windows VST3 bundle and a macOS
universal VST3 + AU bundle, and run 36 passed the DSP tests on Linux, Windows,
macOS and emulated ARM64. Bundle collection on Windows — where a VST3 bundle is
a *folder* containing `Contents/x86_64-win/Name.vst3`, a file with the same
extension, which is why the workflow matches `-type d` — is settled by those
runs, and so is quoting under Git Bash.

**The caveat that remains is the one that matters, and no CI run can retire
it:** nobody has loaded any of these bundles into a DAW, on either platform.
This project is developed in a Linux container. "CI is green" means the code
compiles and the DSP measures correctly. It does not mean FL Studio scans the
plugin, Logic loads the Audio Unit, or Gatekeeper lets a downloaded build run.
Say which of those you mean rather than letting one stand for all of them.

Two smaller things are still unproven here:

- **Whether a 90-minute plugin job is enough for a twelve-plugin universal
  macOS build.** The timeout is a guard chosen against a nine-minute Windows
  build and a six-hour LTO pathology, not a measurement. If it ever trips, read
  the job's timing before raising it — a hang and a slow build want different
  fixes.
- **The `auval` and `validator` runs**, which happen locally and not here. See
  **What CI does not do**.
