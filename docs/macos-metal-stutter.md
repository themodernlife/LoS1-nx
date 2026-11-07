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

### Further isolation and current limits

The existing runtime logs show gameplay averages near 4-5 ms of render-thread
work and 12-14 ms of GPU execution, alongside much larger isolated game-thread
intervals. These averages do not prove the absence of CPU or GPU spikes.
The first two comparison runs synchronously flushed diagnostic rows, so their
results include that measurement overhead. The final diagnostic implementation
uses a bounded background writer to avoid disk writes on the measured threads;
its timings need a fresh capture before comparing against those earlier runs.

System memory was heavily compressed during inspection, but the snapshot was
outside the timed gameplay window and does not establish that paging caused the
hitches. No unrelated applications or system settings were changed. The planned
queue/paging gameplay capture was interrupted by the Mac locking at level setup.
Its menu/locked-session data is not used as gameplay evidence.

The next useful measurement is a village run with the final buffered trace,
correlating long `render_queue_wait` spans with guest Swap gaps, and long
`render_batch` spans with nested shader, GPU-fence, and drawable waits. If the
render queue is empty during a hitch, capture the game thread's call stacks and
I/O/paging activity in that interval. Actual display presentation timestamps or
an Instruments Metal System Trace are still needed to quantify visible pacing.

Validation completed: ARM64 game build; 8,192-pixel offscreen Metal API validation
test with zero failures; live startup and village movement/combat with the
swapchain correction and API validation off; 4,000 buffered trace records from
eight threads with duration filtering; analyzer interval/window/thread checks;
and clean patch application to pristine pinned plume sources reproducing the
built files. The final buffered logger has not yet had a village capture, and
the changed swapchain has not yet had a live API-validation-on run.
