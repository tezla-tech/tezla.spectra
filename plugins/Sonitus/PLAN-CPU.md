# Sonitus — multicore voice rendering and a lane-parallel unison bank (PARKED)

**Status: parked 2026-09-02 at the user's request**, in favour of the analogue
drift work. Nothing here has been built. What would unpark it: the user saying
go, item by item — multicore first, then SIMD, the order they were asked for.
The plan is kept verbatim so the next session starts from a design, not a
blank page; the measurements it rests on are from this branch's stress work
(`README.md`, "The ×8 stress case") and are current as of the parking date.

## Context

Sonitus at ×8 oversampling with 16 voices of 7+7 unison costs about 1400 ms of
CPU per second of audio on one core, and the user's rig shows it as a pinned
meter. Render quality (a bounce can run ×8 while the session plays at ×2) is
done. The two items here move or shrink the *live* cost, and the user's rule
applies to both: **the output must not change by a single bit** — every change
is held to the 32 golden renders (8 patches × 4 factors, `cmp` on raw doubles)
and to the full test suite.

Where the ×8 second goes (profile + feature-removal breakdown): unison stack
≈ 42 %, filter drive rail (`tanh`) ≈ 21 %, fold ADAA (`sin`) ≈ 11 %, tube
(`pow`/`exp`) ≈ 7 %, sub sine ≈ 5 %, the rest mangle and decimation.

**The user's SIMD question, answered.** The lane path is not "the saw" but the
whole polynomial family: **saw, pulse (square is pulse at width 0.5), triangle
and double saw** all go through the same arithmetic (phase → value → polyBLEP
step corrections → feedback history), so one lane-parallel implementation
covers all four, in both banks. **Sine, vintage, dome and harmonic cannot be
lane-parallel and bit-exact**, because each calls libm per sample (`std::sin`,
`std::exp`, `std::cos`), and a vector `sin` is a different function in the last
bits. Noise is integer arithmetic and cheap; it stays scalar. Elsewhere in the
voice the same test decides: the filter rail (`tanh`), the fold ADAA (`sin`),
the tube (`pow`, `exp`) are transcendental per sample and stay scalar;
`Adsr::process` and `Formant::processChannel` are libm-free but tiny or
stereo-only. Anything that wants a vector approximation belongs in
`docs/ROADMAP.md` §7, gated on the user's per-item approval after a null test.

Order of work: **multicore first (MC0–MC3), then SIMD (SI0–SI3).** Each phase
is one commit; every test is seen red before it counts; the goldens are 32/32
after every phase, with the new path both on and off; the whole tree is built
before each push; every commit says the qemu-aarch64 cross-check was not run
(CLAUDE.md 2.3 gate).

---

## Facts the design rests on (verified in the code, 2026-09-02)

- `Engine::process` (`Dsp/SonitusEngine.cpp`) walks the block in control
  chunks of `Voice::kControlIntervalSamples = 32` internal samples. At each
  boundary it runs, serially: `advanceGlobalSources`, `voices_.advanceGlide`,
  `voices_.applyControls`, `applyGlobalModulation` (reads the **tracked**
  voice's envelope levels and velocity), `aimComb` (reads `trackedFrequency()`
  and the tracked voice's levels, publishes readouts). Then `renderChunk`
  renders ≤32 samples: per sample `voices_.process (l, r)` then `mangle (l, r)`.
- The tracked voice is the youngest **active** voice, and `Adsr::isActive()`
  flips to false **per sample inside `Adsr::process`** (`Adsr.hpp`, the release
  branch). So the control values for chunk k depend on voice state after chunk
  k−1, which means the parallel unit is the **chunk**, never the block.
  Per-block voice rendering would change which voice the comb follows — not
  bit-exact.
- Voices share nothing during rendering: each owns its oscillators, filters,
  envelopes and random generators seeded from its index
  (`VoiceManager::prepare` → `voice.prepare (rate, index + 1)`). `Voice::process`
  writes only its own state and `age_`.
- Summation today: `l = 0.0`, then each active voice does `l += out * gain` in
  index order; an inactive voice returns without touching `l`. An accumulator
  that starts at +0.0 and sums in round-to-nearest can never become −0.0, so
  adding +0.0 for an inactive voice is an exact no-op — per-voice buffers that
  leave zeros for idle voices, summed in index order, reproduce today's bits.
- `UnisonBank::process` loops the 7 oscillators; shape, width, morph, feedback
  and sync are **uniform per bank**; only phase, increment and history differ
  per oscillator. The only production use of `UnisonBank::voice (int)` is
  `bankB_.voice (index).sync (frac)` in `SonitusVoice.hpp` (`propagateSync`);
  tests use it once (`test_UnisonBank.cpp`, an increment check).
- `Oscillator` has a large tested public surface (`advance`, `didWrap`,
  `getWrapFraction`, `sync`, feedback identities, `naiveShapeSample`,
  `StepCorrector`) and is also the sub oscillator. It stays exactly as it is
  and becomes the **reference** the lane path is tested against.
- Nothing in the tree uses threads; there is no `find_package(Threads)`.
  `Denormals.hpp` already includes `<xmmintrin.h>` under `_MSC_VER` guards — the
  precedent for guarded x86 intrinsics in shared DSP. FTZ/DAZ is per-thread
  register state: **every worker needs its own `ScopedNoDenormals`.**
- `-ffp-contract=off` / `/fp:precise` are deliberate and tree-wide (a fused
  multiply-add made an SVF differ between x86 and ARM). **A vector path must use
  separate mul and add — never `_mm256_fmadd_pd`.** The AVX2 option sets no
  CMake macro; both GCC and MSVC define `__AVX2__` (MSVC never defines `__FMA__`).
- Measurement: `tezla-measure sonitus-stress` (`tools/measure_main.cpp`,
  key/value args parsed in pairs) and the scratch golden renderer. CPU tests in
  the suite use `CHECK_CPU_BUDGET` (`tests/TestFramework.hpp`), pattern in
  `tests/test_MalleusEngine.cpp`. `test_Sonitus.cpp` has no budget test.
- JUCE 9.0.1 offers `juce::Thread::startRealtimeThread (RealtimeOptions)` and
  `AudioProcessor::audioWorkgroupUpdated` (macOS workgroups). The Sonitus
  processor has no `releaseResources` override; workers would be torn down in
  the destructor.

---

## Item 1 — Multicore: voices rendered on worker threads, summed in voice order

### Design

**Unit of parallelism: the 32-sample control chunk.** At each chunk the audio
thread publishes a job — chunk length and the per-voice output buffers — and
every participant (workers *and* the audio thread) **claims voice slots by
`fetch_add` on a shared counter** until none are left. Each claimed voice
renders its chunk into its own buffer (`l = 0; r = 0; voice.process (l, r);
out[i] = l …` — the same per-sample call as today, so an idle voice leaves
zeros). When the audio thread has claimed its last voice it spins until the
finished-voice count equals the slot count, then sums the per-voice buffers in
index order into the chunk's left/right and runs `mangle` per sample exactly as
now. `activeVoiceCount`, `trackedVoice` and the readouts are only ever read on
the audio thread, after the join.

Work-claiming rather than fixed partitions is what makes correctness independent
of the workers: with no worker running, the audio thread claims everything and
the join completes at once; a worker that is descheduled can only delay the one
voice it holds. It also load-balances idle slots (which cost nothing) against
sounding ones.

**Sync protocol** (framework-free, `std::atomic` only, C++20):
- `generation` (uint32): the audio thread stores chunk length and resets
  `nextVoice = 0`, `finished = 0`, then increments `generation` (release).
- Workers spin on `generation` (acquire) for a bounded time (~100 µs — longer
  than the inter-chunk gap, which is one chunk of mangle), then park with
  `std::atomic<uint32_t>::wait` (futex on Linux, `WaitOnAddress` on Windows,
  `__ulock` on macOS; MSVC 2022, GCC 11+, libc++ 11+ all have it) and increment
  `parked`. The audio thread calls `notify_all` **only when `parked > 0`**, so a
  streaming block costs no syscalls; the first chunk of a block after a 10 ms
  host gap pays one wake, and to hide it the audio thread pokes the workers at
  the top of `process()` before the oversampler bookkeeping.
- Ordering: chunk parameters are written before the counters are reset and the
  generation bumped; a late worker from the previous generation that still
  `fetch_add`s reads the current job and renders it correctly, and its
  `finished` increment counts for the current chunk. Documented in the header
  with the argument, and exercised under ThreadSanitizer once (GCC has it).
- Shutdown: `stopWorkers()` sets a flag, bumps the generation, notifies; a
  worker returns from `runWorker` and the owning thread joins it.

**Ownership and threads.** The engine stays framework-free and exposes:
`setMulticore (bool)` (from `EngineParameters::multicore`), `bool runWorker (int
index)` (one wait-render-signal cycle; returns false after stop),
`stopWorkers()`, `resetWorkers()`, `workerCount()`/`parkedWorkers()` for tests.
The JUCE layer owns the threads: a `juce::Thread` subclass whose `run()` loops
`while (! threadShouldExit() && engine.runWorker (i))` under a
`dsp::ScopedNoDenormals` that lives for the thread's lifetime, started with
`startRealtimeThread (RealtimeOptions().withPriority (9))`. Threads are created
in `prepareToPlay` whenever `hardware_concurrency() > 1`, **whether or not the
switch is on** — parked threads cost nothing, and it is what makes the switch
live rather than "takes effect after the host re-initialises the plugin" (thread
creation is not real-time safe, so it cannot happen in `processBlock`). Count =
`min (cores − 1, 3)`: the audio thread is the fourth participant, and FL Studio's
own mixer threads want the rest. `audioWorkgroupUpdated` is overridden to store
the host workgroup and workers join it at start — written for the deferred
macOS build, not exercised. Tests drive the identical engine API with
`std::thread`.

**Per instance, opt-in.** A process-wide pool was considered and rejected: FL
Studio runs mixer tracks concurrently, so two instances fanning out through one
pool would serialise on it and each would wait on the other's chunk. Per-instance
pools of ≤3 parked threads are what other multicore synths do; twenty instances
is sixty sleeping threads, which is nothing. The failure mode is honest and
documented: on a project that already saturates every core, multicore adds sync
overhead and can hurt — hence off by default and a tooltip that says so.

**Parameter and UI.** `multicore` (bool, next free schema version, default off —
neutral by construction: off is today's code path, untouched). A `LampButton`
("Multicore") in the KEYBOARD group on the FILTER page beside Mode / Voices /
Glide — the group whose Voices tooltip already says it "decides the CPU bill".
Tooltip reads live values: cores on this machine, workers attached, whether it
is on, and the cost sentence: *moves the voices onto N extra threads so the
host's meter reads lower; total CPU is a little higher for the hand-offs;
bit-identical output either way; leave it off if the project already keeps
every core busy.*

**Idle skip unchanged**: no fan-out when the engine is skipping. When multicore
is off the existing `renderChunk` runs verbatim.

### Files

- `Dsp/VoiceManager.hpp` — `renderVoiceChunk (index, left, right, n)`, the
  per-voice chunk buffers (`32 voices × 2 × 32` doubles, 16 KB, inline),
  `sumChunk (left, right, n)` in index order.
- `Dsp/VoiceFanout.hpp` (new, framework-free) — the job, counters, spin/park,
  `claim`, `runWorker`, `stopWorkers`; six-line licence header.
- `Dsp/SonitusEngine.hpp/.cpp` — `EngineParameters::multicore`, `renderChunk`
  chooses direct vs fan-out, the worker API pass-through, the per-block poke.
- `Source/PluginProcessor.h/.cpp` — parameter (`ids::multicore`), `pullParameters`,
  `VoiceWorker : juce::Thread`, create in `prepareToPlay` (idempotent),
  stop+join in the destructor, `audioWorkgroupUpdated`, `describeMulticore()`.
- `Source/PluginEditor.cpp` — the LampButton via `addToggle`, the live tooltip
  refresh alongside the render/OS ones.
- `tests/CMakeLists.txt`, `tools/CMakeLists.txt` — `find_package(Threads)` +
  `Threads::Threads` (the DSP headers themselves only need `<atomic>`).
- `tools/measure_main.cpp` — `--threads N` for `sonitus-stress` (std::thread
  workers driving `runWorker`), usage lines updated (six sites).
- `tests/test_Sonitus.cpp` — the tests below. Docs: README (CPU section and a
  "Multicore" subsection with the measured table), CLAUDE.md §7 bullet (worker
  threads: own denormal guard, fixed summation order, opt-in),
  `docs/PLUGIN-CONVENTIONS.md` one paragraph.

### Phases

- **MC0 — fan-out path with no threads.** `renderVoiceChunk` + `sumChunk` +
  `VoiceFanout` with the audio thread as the only participant; `renderChunk`
  uses it when `multicore` is on. Test: same MIDI (notes overlapping, stealing —
  more notes than polyphony — and short releases so voices go idle mid-chunk) on
  three patches (brutal, drift, sync) with the switch off and on → `memcmp`
  equal. Seen red by summing in reverse order. Goldens 32/32 both ways.
- **MC1 — workers.** The protocol, `runWorker`/`stopWorkers`, `std::thread`
  harness in the tests: 1 and 3 workers → `memcmp` equal to MC0's single-thread
  output; the switch toggled every few blocks mid-stream → equal; after the idle
  skip engages, `parkedWorkers() == workerCount()` and stays so (activity, not
  silence — CLAUDE.md §7); a `CHECK_CPU_BUDGET` that the *audio-thread* time with
  3 workers is below the single-thread time (loose, printed, only asserted when
  `hardware_concurrency() >= 4`). One TSAN run of the fan-out tests. `--threads`
  in `sonitus-stress`; the table for off / x2 / x4 / x8 at 0 and 3 workers goes
  in the commit message.
- **MC2 — JUCE layer.** Parameter, LampButton, `VoiceWorker`, workgroup hook,
  tooltips, validator 47/47, README, conventions, CLAUDE.md. Editor photographed.
- **MC3 — the user's ears and meter**, on the rig: FL Studio's CPU meter at ×8
  with the switch off and on; nothing here can measure that.

### Risks

- **Priority inversion / host thread policy.** Workers at real-time priority on
  Windows are what every multicore synth does; if the host's own threads starve
  them the audio thread renders the remaining voices itself (claiming), so the
  worst case is today's speed plus the spin budget, never a stall.
- **Wake latency at block start.** Mitigated by the poke at the top of
  `process()` and the bounded spin; measured in MC1 (first-chunk time vs later
  chunks).
- **Denormals on workers** — the guard is per thread and the test compares bits,
  so a missing guard fails the equality test on any patch with a tail.
- **MSVC**: `std::atomic::wait/notify` and `std::thread` are standard C++20 and
  in the MSVC 2022 STL; the JUCE thread class is cross-platform. Cannot be run
  here; the user's Windows build is the check.

---

## Item 2 — SIMD: a lane-parallel unison bank for the polynomial shapes

### Design

**One algorithm, three lane widths.** A new header
`shared/tezla-dsp/include/tezla/dsp/LaneOscillators.hpp` holds the oscillator
step as a template over a vector type `V`: `struct Lanes { phase[8], increment[8],
history0[8], history1[8], corrCurrent[8], corrNext[8], triangleState[8],
wrapped[8], wrapFraction[8] }` plus the bank-uniform values (shape, width,
`doubleOffset`, feedback, `pendingSync` + fraction, `triangleNormalise`,
`squareMean`). `advance<V> (lanes, phaseMod)` transliterates
`Oscillator::advance` + `scheduleEdges` + `valueAtPhase` + `StepCorrector`
**token for token** in mask form: every branch becomes a compare mask and a
bitwise select, every state write is masked by `laneActive = lane < voiceCount`
so a padded lane is never written (the scalar bank never advances an oscillator
beyond `voiceCount_`, and a later `setVoiceCount` must find its state exactly as
`reset` left it), and `scheduleEdges`'s `increment <= 0` early return becomes a
mask too. No "add 0.0 where the scalar did not add"; `StepCorrector::addStep`
fires only in lanes where the scalar `if` fires. Division and `floor` are
IEEE-exact in vector form; the horizontal sum `left += value * gainL[i]` is done
**serially in lane order 0..6** after extracting lanes, never as a tree.

`shared/tezla-dsp/include/tezla/dsp/Simd.hpp` (new): `Vec1d` (plain double),
`Vec2d` (`__m128d`, SSE2 — the x86-64 baseline, no SSE4.1 assumed: blend is
and/andnot/or on the compare mask; `floor` is `cvttpd2dq` → `cvtepi32_pd` →
subtract-one-where-greater, exact for |x| < 2³¹ and the reachable domain is
|x| < 64: phase is wrapped to [0, 1) every sample, PM adds at most 16 × ~1.5
cycles, feedback at most ~1.5 — pinned by a test that sweeps ±64 against
`std::floor` and by a comment stating the bound), `Vec4d` (`__m256d`, under
`#if defined(__AVX2__)`, `_mm256_floor_pd`). Operations: load/store, add, sub,
mul, div, floor, min, max, `<`, `<=`, `>=`, select, and/andnot. **No FMA
intrinsics** (see Facts). Everything else — ARM, or any build with
`TEZLA_DISABLE_SIMD` — instantiates `Vec1d`, which is the same source evaluated
one lane at a time, so the fallback is not a second implementation.

`UnisonBank` keeps its public interface; `voice (int)` is replaced by
`syncAll (frac)` (the one production use) and `incrementOf (index)` (the one
test use). Internally it holds `Lanes` as the single source of truth for all
nine shapes: the polynomial four run `advance<Vec4d>` ×2 or `advance<Vec2d>` ×4
(8 lanes, 7 used); the libm shapes run `advance<Vec1d>` lane by lane, where
`Vec1d::sin/exp/cos` call libm exactly as `Oscillator` does. There is no state
conversion when the shape changes because there is only one state.
`Oscillator.hpp` is untouched: it remains the sub oscillator and the reference.

**Where the win is.** The unison stack is ≈42 % of the ×8 second; 7 lanes in
two AVX2 vectors instead of seven branchy scalar iterations should take that
part down by 2.5–4× (AVX2) or ~2× (SSE2), i.e. **roughly −25–30 % at ×8 with
`-avx2`, −15–20 % without** — an estimate to be replaced by the stress table.
The filter rail, fold and tube stay scalar (transcendental per sample); a
vector `tanh`/`sin`/`pow` would not be bit-exact and goes to ROADMAP §7 only if
the user asks. "SIMD across voices" (16 voices in lanes) was rejected: it means
rewriting every per-voice component in structure-of-arrays form and is still
blocked by the same libm calls.

### Files

- `shared/tezla-dsp/include/tezla/dsp/Simd.hpp` (new), `LaneOscillators.hpp`
  (new), `UnisonBank.hpp` (internals + the two accessor replacements);
  `Dsp/SonitusVoice.hpp` (`syncAll`); `tests/test_Simd.cpp` (new),
  `tests/test_LaneOscillators.cpp` (new), `tests/test_UnisonBank.cpp`
  (accessor), `tests/CMakeLists.txt`; `CMakeLists.txt` +
  `cmake/TezlaCompilerOptions.cmake` (`TEZLA_DISABLE_SIMD` → `TEZLA_NO_SIMD`
  define, the escape hatch if MSVC objects); `docs/BUILD.md` (option row + a
  paragraph under the AVX2 note), README (numbers), `docs/DSP-REFERENCES.md`
  (nothing taken — derived; say so only if a source is consulted).

### Phases

- **SI0 — `Simd.hpp`.** All three widths compiled into the test binary where
  the flags allow (`Vec4d` only under `__AVX2__`; the AVX2 check build is
  `build-avx2-check`). Tests: every op against its scalar definition over
  random and edge inputs, bit for bit; `floor` sweep ±64 in fine steps plus the
  exact integers and ±0.0; select on NaN payloads.
- **SI1 — `LaneOscillators` for saw / pulse / triangle / doubleSaw.** The
  equivalence test: for each shape × widths {0.02 … 0.98} × morph {0, 0.3, 0.6,
  1} × feedback {0, 0.5, 1} × increments (incl. 0, very low, near 0.49) × PM
  offsets × sync events at swept fractions × padded lanes and a mid-run
  `setVoiceCount` change — run `Oscillator` and `advance<V>` for every available
  `V` side by side and `memcmp` every sample and every state field afterwards.
  Seen red by dropping one clamp. The increment-0 lane and the padded-lane
  contract are separate named tests.
- **SI2 — the libm shapes in `Vec1d` and the bank switch-over.** `UnisonBank` on
  `Lanes`; `syncAll`; existing `test_UnisonBank` and `test_Oscillator` green
  unchanged; equivalence for all nine shapes; **goldens 32/32** on SSE2 and on
  the AVX2 build; the stress table before/after for off / x2 / x4 / x8; the
  `-avx2` line in BUILD.md updated with the new numbers.
- **SI3 — `TEZLA_DISABLE_SIMD` + docs.** Option, scripts untouched (a CMake
  flag is enough for an escape hatch), BUILD.md, README, CLAUDE.md §7 bullet:
  a vector path is held to the scalar reference bit for bit, and never uses FMA.

### Risks

- **MSVC intrinsics warnings/errors at `/W4`** — cannot be compiled here.
  Mitigation: the intrinsics are the plain SSE2/AVX2 set that MSVC has had for a
  decade, kept in one header, and `TEZLA_DISABLE_SIMD` gets the user a build if
  anything objects; the scalar instantiation is the same code, so nothing is
  lost but the speed.
- **A missed masked write** shows up as a padded-lane or shape-switch difference;
  the equivalence test's state comparison is what catches it, so it compares
  state, not only output.
- **Auto-vectoriser interference**: with `-mavx2` GCC may already vectorise some
  of the scalar bank; the gain is measured, not assumed, and if SSE2 buys little
  that is reported as such.

---

## Verification (end to end, both items)

1. `./scripts/build.sh NONE --test` — whole DSP tree, suite green.
2. Break-checks recorded in each commit message (which line was broken, what
   went red).
3. Goldens: `sonitus-golden` on all 8 patches × 4 factors, with `--threads 0/3`
   and (SIMD phases) SSE2 vs AVX2 builds — `cmp` identical, 32/32 each way.
4. `tezla-measure selftest` then `tezla-measure sonitus-stress --os all
   --threads 0` and `--threads 3`; tables quoted.
5. Whole tree `cmake --build build` (plugins, render tools, ui-preview), 0
   warnings; Steinberg validator on the Sonitus bundle 47/47; editor
   photographed for the new lamp and its tooltip.
6. One ThreadSanitizer build of the fan-out tests (`-fsanitize=thread`, GCC).
7. The qemu-aarch64 cross-check is **not** run (CLAUDE.md 2.3); ARM gets the
   `Vec1d` path and untested worker code, both stated in the commits.
8. The acceptance test is the user's rig: FL Studio's CPU meter and ears at ×8,
   with multicore off and on.
