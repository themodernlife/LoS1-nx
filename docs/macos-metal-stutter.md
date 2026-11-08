# Native Metal stutter investigation

## Capture a reproduction

Close the isolated Metal app, then run from the investigation worktree:

```sh
LOS_FRAME_TRACE="$PWD/out/metal-frame-trace.csv" MTL_DEBUG_LAYER=0 \
  tools/macos/metal.sh play --no-fullscreen
```

Reproduce movement and combat in the opening village, then close the app. Use
seconds from the first trace event to isolate gameplay from loading and menus:

```sh
python3 tools/macos/analyze_frame_trace.py out/metal-frame-trace.csv \
  --start 120 --end 180 > out/metal-frame-summary.json
```

Choose the actual gameplay times for that run. The trace is disabled unless
`LOS_FRAME_TRACE` names a file. It overwrites that file at startup, records CPU
wall time and thread IDs, and writes bounded batches from a background thread
every 100 ms without `fsync`. The game/render threads never write trace rows to
disk. Hard exit can lose the final batch; overflow reports dropped events to
stderr and makes the capture incomplete. Tracing and computer automation still
add overhead. Profile
with Metal API validation off; validate correctness separately with it on.

`frame_intervals` measures the gap between returns from guest-triggered
`Video::Present` calls. `guest_swap_intervals` measures the gap between returns
from guest pacing. Neither is a measurement of frames reaching the display.
Overlay-only presents can happen during guest stalls and are not counted as
guest frames. Nested timing spans overlap; do not add their durations together.
A worker compilation overlapping a hitch does not establish that it blocked the
render thread. The summary identifies each overlapping span's thread.

| Event | What it measures |
| --- | --- |
| `drawable_acquire` | Swapchain acquisition, including Metal `nextDrawable` |
| `present_submit` | Host presentation submission, not display completion |
| `gpu_slot_wait` | Waiting for a previously submitted frame-slot fence |
| `frame_submit_advance` | Command submission and frame-slot advancement |
| `gpu_submit_wait` | Synchronous command submission and GPU fence wait |
| `metal_shader`, `metal_translate`, `metal_library` | Whole shader creation and its translation/Metal compilation phases |
| `pipeline_compile` | Render pipeline construction |
| `pipeline_prebuild_wait` | Prebuilt pipeline lookup, including mutex/worker waits |
| `pipeline_list_sync` | Pipeline-list flush and filesystem synchronization |
| `guest_pace` | Guest frame pacing, including the intentional sleep |
| `render_batch` | Render-thread batch processing, including any nested waits |
| `render_queue_wait` | Render thread acquiring its queue lock and waiting for work |
| `guest_queue_wait` | Game thread waiting for the render queue's frame-ahead limit |

The three render-queue events omit spans shorter than 1 ms to limit logging.
All other events include short calls. A CPU sample can supplement the trace:

```sh
sample <verified-test-app-pid> 8 1 -file out/metal-cpu-sample.txt
```

Keep CPU sampling outside comparison windows because it perturbs the run.

## Initial findings, 2026-11-07

Apple M2, 16 GB RAM, macOS 15.6, ARM64 Release, FPS overlay on, windowed native
Metal, unchanged graphics settings and isolated profile. There was no concurrent
build during the timed gameplay windows. Runs use manual/automated movement and
attacks, not a deterministic replay; they cannot establish a benchmark speedup.

The baseline trace reproduced hitches after the scene's pipelines were warm.
An 8-second CPU sample independently found the render thread in
`MetalSwapChain::acquireTexture -> CAMetalLayer nextDrawable -> semaphore wait`.
The baseline also contained intervals not explained by presentation or shader
creation. Individual Metal shaders in the selected combat segment took at most
6.36 ms, with no pipeline builds and a maximum prebuilt lookup of 0.015 ms.

### Drawable lifetime correction

The pinned plume backend retained each presented drawable in its swapchain slot
until a later acquire. It called `nextDrawable` before releasing the old slot,
which can leave no drawable available for that call. A GPU completion callback
also advanced the non-atomic slot index while the render thread read it.

The native Metal dependency patch now drops the slot's reference after committing
presentation, while the completion callback keeps the in-flight drawable alive.
The acquiring thread advances the slot index. This removes the cross-thread index
race and unnecessary drawable retention. Apple recommends releasing drawables
promptly because acquisition blocks when the limited pool is exhausted:
[Metal drawables guidance](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/Drawables.html).

The first before/after segments each cover 35.26 seconds of village movement and
attacks. Both use identical initial timing instrumentation and validation off:

| CPU timing | Baseline | With drawable correction |
| --- | ---: | ---: |
| Present intervals measured | 2,094 | 2,051 |
| Median present interval | 16.674 ms | 16.654 ms |
| 99th-percentile present interval | 24.392 ms | 27.243 ms |
| Longest present interval | 67.175 ms | 316.114 ms |
| Intervals over 50 ms | 4 | 10 |
| Average drawable acquisition | 5.863 ms | 3.623 ms |
| Longest drawable acquisition | 61.464 ms | 75.071 ms |

These captures **do not show that the correction fixes stuttering**. Although
average acquisition fell, tail latency did not improve. The longest repeat-run
gaps occurred outside the instrumented shader/presentation calls, and the game
thread also had 255-278 ms gaps between Swaps. Render-queue timing was added next
to distinguish delayed game work from slow renderer batches.

Local ignored evidence: `out/metal-stutter-baseline.csv` at 193.336-228.596 s,
`out/metal-stutter-fixed.csv` at 122.015085-157.275085 s, their combat summary JSON
files, and `out/metal-stutter-sample.txt`. Raw traces, game binaries, and generated
shader data are not committed.

## Buffered village capture, 2026-11-08

Completed a fresh 40-second movement/combat window with the final background
writer and queue timings, validation off, the same build/settings, and no
concurrent build or Instruments recording. No dropped trace events were reported.
This run is diagnostic, not a controlled comparison with the earlier logger.

| CPU timing | Buffered capture |
| --- | ---: |
| Present intervals measured | 2,375 |
| Median present interval | 16.660 ms |
| 99th-percentile present interval | 23.254 ms |
| Longest present interval | 127.164 ms |
| Intervals over 33.333 ms / 50 ms | 9 / 2 |
| Longest guest Swap interval | 145.924 ms |
| Longest drawable acquisition | 46.350 ms |
| Longest frame-slot fence wait | 18.932 ms |
| Longest shader creation / prebuilt lookup | 3.821 ms / 0.081 ms |

There are at least two distinct stall patterns:

- Three roughly 49-50 ms present intervals contain 44-46 ms drawable waits.
  The game thread simultaneously waits about 32 ms for the render queue's
  frame-ahead limit. These are presentation-side stalls propagating back into
  the game thread; the current trace cannot distinguish GPU delays from delayed
  compositor/drawable availability.
- The largest interval contains a 50.847 ms render-queue wait followed by a
  49.622 ms render batch, with neither accounted for by a long shader, drawable,
  or instrumented GPU-fence call. `render_queue_wait` includes both mutex
  acquisition and waiting for work, so this alone does not prove an empty queue.
  The game also stops producing Swaps for about 146 ms. Its independent runtime
  log reports that gap and ten missing audio frames in the same ten-second
  reporting window. This is broader than a slow presentation call.

A one-second system-wide `vm_stat` observer recorded 33,023 decompressed pages
and 23,833 compressed pages in the sample covering the largest hitch, about
516 MiB and 372 MiB at the machine's 16 KiB page size. That sample had zero swap
ins/outs and 705 page-ins. This is a memory-pressure correlation, not proof that
the game faulted those pages or that disk swapping caused the hitch. Other
compression bursts did not produce the largest frame gaps. No unrelated apps
were closed and no system settings were changed.

Local ignored evidence: `out/metal-stutter-buffered-20261108.csv`, window
170.354782-210.354782 s, its marker/summary JSON files,
`out/metal-stutter-vm-20261108.jsonl`, and runtime `los_009.log` under the test
app's `Contents/MacOS/logs/`. The memory observer covers only the first part of
the combat window. Its timestamps follow the most recently flushed trace row,
so correlation has approximately one sample period of uncertainty.

### Instruments and validation

A separate 20-second Metal System Trace captured 1,073 Core Animation drawable
waits, up to 23.868 ms, and a 206.008 ms command-list encoding interval. The
buffered logger independently measured a 208.139 ms render batch in that run.
Only ten running render-thread samples and eight running game-thread samples
fall in the long encoding interval. Sampling only running threads cannot tell
whether the missing time is blocked, descheduled, or unobserved; the sampled
texture/stream hashing functions must not be blamed for the whole interval.

The capture includes the transition to the Health Font tutorial and has
substantial instrumentation overhead: `__kdebug_trace64` is the leaf in 877 of
5,504 render-thread samples. It is not used as a performance comparison. GPU
execution and displayed-surface exports contained no rows, so this capture does
not establish GPU execution time or visible frame pacing. The trace saved
successfully with a warning about backdated signpost intervals. Local evidence
is `out/metal-stutter-20261108.trace` and its XML exports.

### Scheduler attribution

A separate System Trace identifies scheduling delay in a 79.531 ms present
interval. Its default windowed recording retained ten seconds of the requested
twenty seconds. The retained window includes movement/combat and has 588 present
intervals, four over 33.333 ms and one over 50 ms. It is also an instrumented
diagnostic run, not a benchmark.

The CSV and scheduler clocks were aligned using 280 matching guest pacing sleep
endpoints, whose offsets agree within 3.2 microseconds. The exported TOC wall
clock gave an inconsistent offset and was rejected. At scheduler time
7.091396-7.170927 seconds:

| Thread state during the 79.531 ms hitch | Render thread | Game thread |
| --- | ---: | ---: |
| Running | 5.360 ms | 7.829 ms |
| Preempted | 54.210 ms | 44.329 ms |
| Runnable, waiting to execute | 10.707 ms | 2.514 ms |
| Blocked | 9.197 ms | 24.826 ms |

The render thread spends 64.917 ms ready but off the CPU. Its 29.847 ms batch
contains two long preemptions; a later 20.674 ms present span includes 19.561 ms
preempted while submitting a Cocoa run-loop wakeup. Those wall durations are
not evidence that hashing or the Cocoa call consumes that much CPU. The game
thread also waits on a guest synchronization object for part of the interval.
Only about 0.008 ms of recorded game-thread VM operations overlap this hitch.
Across the retained ten seconds, the longest VM operation is 0.869 ms and the
longest decompression is 0.143 ms. Memory pressure remains a suspect for the
earlier capture, but page-fault latency does not explain this captured hitch.

The same scheduler capture independently confirms a 28.630 ms drawable acquire
contains a 28.602 ms semaphore wait inside `CAMetalLayer nextDrawable`. Thus
ready-thread scheduling delays and drawable blocking are separately observed.
The responsible competing workload is not established, and Instruments itself
can contribute contention. This does not prove the earlier 127 ms hitch has
the same cause or that normal unprofiled gameplay suffers identical delays.

Local evidence: `out/metal-scheduler-20261108.trace`, its syscall/thread-state/VM
exports, `out/metal-stutter-scheduler-20261108.csv`, the alignment JSON, and
`out/metal-scheduler-20261108-hitch.json`. Keep these machine-local artifacts out
of Git. The test process was closed after capture.

### Live API validation

The corrected swapchain also completed a separate live village movement/combat,
jump, and pause run with `MTL_DEBUG_LAYER=1`. The log confirms API validation
enabled and native Metal on Apple M2, with no new API validation errors or
renderer errors. This validates the exercised path, not the full campaign.
The validation process was closed afterward; normal launches still default to
validation off. Evidence: `out/metal-validation-20261108-stderr.log` and
runtime `los_010.log`.

### Remaining isolation

The existing runtime logs show gameplay averages near 4-5 ms of render-thread
work and 12-14 ms of GPU execution, alongside much larger isolated game-thread
intervals. These averages do not prove the absence of CPU or GPU spikes.
The next CPU investigation should compare scheduling with less profiling and
lower background contention before changing thread priority. No unrelated apps
were closed for this test. Drawable stalls still need actual display timestamps
plus GPU completion timing to distinguish missed GPU deadlines from compositor
availability. The current measurements do not justify changing frame pacing,
disabling cache-correctness checks, or claiming a shader-cache fix will remove
these hitches. No new performance fix was made on the basis of these captures.

Validation completed: ARM64 game build; 8,192-pixel offscreen Metal API validation
test with zero failures; live startup and village movement/combat with the
swapchain correction and API validation off; 4,000 buffered trace records from
eight threads with duration filtering; analyzer interval/window/thread checks;
and clean patch application to pristine pinned plume sources reproducing the
built files. The final buffered logger and corrected live swapchain have now
been exercised in the village, including a separate API-validation-on run.
