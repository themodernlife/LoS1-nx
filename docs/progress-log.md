# Progress log

Newest first. Each entry: what was done, what was found, what is next.

## 2026-11-08: compare cutscene ghosting with the supplied reference video

- Inspected the user's YouTube reference around 3:15 and the matching tumble
  at 3:35-3:36. Identified the scene as the Hunting Path horse-chase QTE/fall.
  Reference frames have motion softness without the dense repeated outlines
  visible in the local Metal movie, strengthening the rendering-defect theory.
- Updated `docs/macos-metal-cutscene.md` with timestamps and comparison limits.
  The source platform/settings are unverified, and this does not isolate Metal
  from shared Renut or prove a sampler cause. A controlled backend comparison
  remains next. Only documentation changed in the experimental worktree.

## 2026-11-08: investigate cutscene ghost outlines in the supplied movie

- Used three subagents to inspect the clip, Metal presentation, and cutscene
  resource/sampling paths. The user confirmed the repeated silhouettes around
  Gabriel at approximately 1.5-2 seconds are the reported artifact. Consecutive
  frames favor motion-blur ghosting over conventional horizontal tearing.
- Found a concrete sampler parity gap: Vulkan applies the guest mip LOD bias,
  while the native Metal sampler and shader bridge currently omit it. Its
  relevance to this effect is unproven; the blur pass and its input mip counts
  have not been captured. No specific presentation or resource reuse bug was
  confirmed. The recorded run used x1 internal render scale.
- Recorded evidence, caveats, and distinguishing tests in
  `docs/macos-metal-cutscene.md`. Next: matched-scene Vulkan/Metal replay and an
  offscreen frame capture, followed by pass-level inspection if they differ.
- Changed documentation only in the experimental worktree. Kept extracted
  frames ignored, and left the current game, save profile, and main untouched.

## 2026-11-08: attribute Metal village hitches and validate the swapchain

- Completed the final buffered logger's village movement/combat capture. In
  forty seconds, 2,375 present intervals had a 16.660 ms median, nine exceeded
  33.333 ms, and the largest was 127.164 ms. Shader creation stayed below
  3.821 ms. No dropped trace records were reported.
- Separated drawable blocking from broader CPU stalls. Three roughly 50 ms
  intervals contain 44-46 ms drawable waits and game-thread queue backpressure.
  The largest hitch also has a 146 ms guest Swap gap, a long render-queue wait,
  and a slow batch outside shader/drawable/fence spans. System memory compression
  activity coincides with that hitch but does not prove causation.
- A separate System Trace attributes a 79.531 ms hitch largely to scheduling:
  the render thread spent 64.917 ms ready but off the CPU and 5.360 ms running.
  Recorded VM operations were tiny during that hitch. It also independently
  confirms a 28.602 ms semaphore wait inside `CAMetalLayer nextDrawable`.
  Aligned clocks through 280 pacing endpoints; rejected inconsistent exported
  wall-clock metadata. Profiling overhead and competing workloads remain limits.
- Metal System Trace collected CPU stacks and drawable waits, but its GPU and
  display execution exports were empty. Documented that limitation, recording
  overhead, retained scheduler window, exact evidence, and next isolation steps
  in `docs/macos-metal-stutter.md`. No unsupported optimization or speedup claim.
- Completed live movement, both attack types, jumping, and pause with Metal API
  validation enabled. Native Metal on Apple M2 was confirmed, with no new API
  validation or renderer errors. Closed test processes afterward. Existing FPS
  overlay, build, graphics settings, and isolated profile were preserved.
- Only investigation documentation changed this session. Kept raw traces,
  binaries, and generated data ignored; main and its profile remain untouched.

## 2026-11-07: investigate opening-village stutters in native Metal

- Reproduced movement/combat hitches with the FPS overlay on and Metal API
  validation off. Added opt-in `LOS_FRAME_TRACE` CPU spans for presentation,
  GPU-fence waits, shader/pipeline work, pacing, and render-queue activity, plus
  `tools/macos/analyze_frame_trace.py` for bounded gameplay summaries. Normal
  launches do not record traces.
- A CPU sample confirmed waits inside `CAMetalLayer nextDrawable`. Corrected
  plume's delayed drawable release and the non-atomic slot index advanced by a
  GPU callback. The render thread now advances slots; presentation releases the
  slot reference while its completion handler owns the in-flight reference.
- Two 35.26-second combat segments did not establish a stutter improvement:
  median intervals stayed near 16.7 ms, but the repeat run still had long gaps,
  including 255-278 ms between guest Swaps outside shader/presentation spans.
  Documented full results and limitations in `docs/macos-metal-stutter.md`.
- Moved diagnostic writes to a bounded background writer after the initial
  comparison to avoid synchronous trace I/O on the measured threads. The final
  logger's village capture is pending: the Mac locked during level setup. The
  unfinished queue/paging capture is not counted as gameplay evidence.
- ARM64 build and 8,192-pixel offscreen Metal API validation passed. Exercised
  the corrected swapchain in live village combat with validation off. Verified
  4,000 buffered records from eight threads, analyzer interval/window/thread
  behavior, shell syntax, and patch reproduction from pinned plume originals.
  Live swapchain validation with the API layer on remains outstanding.
- Kept all changes in `codex/dxmt-investigation`; main and its profile are
  untouched. No claim that all stutters are fixed. Next: capture final queue
  timings, then attribute guest-thread stalls and actual display pacing.

## 2026-11-07: include the FPS overlay in the native Metal test app

- Rebased `codex/dxmt-investigation` onto fork main's FPS update `95fc9d1`.
  Preserved the previous branch at `codex/metal-before-fps-rebase-20261107`.
  Resolved the build-script overlap by forwarding both the FPS and Metal
  options, and retained both progress histories.
- Configured the existing isolated build with `LOS_FPS_EXPERIMENT=ON`, rebuilt,
  and repackaged. The existing toolchain, Metal backend selection, application
  configuration, and isolated save path were preserved. Packaged executable
  and runtime-library hashes match the rebuilt outputs.
- Visually verified the FPS and average ms/frame overlay updating during
  startup in windowed mode. Runtime logs confirm the experiment was enabled
  and the renderer was native Metal on Apple M2. No new Metal API validation
  failures appeared; the standalone test checked 8,192 pixels with zero failures.
- Updated the native Metal build instructions to enable the overlay and
  clarified that its `Renut` label also applies to the native Metal backend.
  This was a startup overlay check, not a new gameplay benchmark.

## 2026-11-07: rebase native Metal experiment onto fork main

- Checkpointed the native Metal prototype, preserving its original state on
  `codex/metal-before-main-rebase-20261107`, then rebased
  `codex/dxmt-investigation` onto fork `origin/main` at `71a12c5`.
  The experiment now tracks that fork branch instead of upstream main.
- Kept the latest platform and DXC selection, unused-registration exclusion,
  and app-icon packaging. Only the progress log required conflict resolution;
  both histories were retained. The active main checkout's in-progress edits
  were left untouched and are not included in this published-base rebase.
- Rebuilt the ARM64 game and Metal test. The Metal API validation test checked
  8,192 clear/draw pixels with zero failures. Confirmed the unused registration
  callback stays out of the build and XenosRecomp receives ARM64 settings.
- Repackaged the isolated Metal app. Its executable and runtime libraries match
  the new build, the new icon is included, and the existing Metal configuration
  and isolated save path are preserved. No new gameplay or benchmark run was
  performed during this rebase; the earlier gameplay results remain historical.

## 2026-11-07: opt-in native Metal prototype

- Resumed work at the user's request in `codex/dxmt-investigation`, isolated
  from the active checkout. Brought in the known macOS baseline `302b488`.
- Added `renut_gpu_backend = "metal"`, SPIR-V to MSL translation using the
  SDK's pinned SPIRV-Cross, and native resource/pipeline checks, memory reporting,
  and blend constants. Vulkan remains the normal default and a working fallback.
- Added a reproducible plume patch for dynamic constant offsets, native shader
  libraries, readback, deferred-clear flushing, and pixel-format/stencil views.
  Added a Metal GPU regression test: 8,192 clear/draw pixels, zero failures under
  API validation, including sparse bindings and differing per-draw constants.
- Built the game and exercised title/menu rendering, opening village, movement,
  both attack types, jumping, and pause/resume on Apple M2 with API validation.
  The six-minute successful run reported no shader/compiler/validation failures.
  Saves and a thumbnail were created in the isolated test profile. Also checked
  the same binary's Vulkan title screen.
- Added `tools/macos/metal.sh` for separate packaging, launching, and GPU tests,
  with a distinct bundle ID and explicit isolated save path. Documented the
  implementation and limitations in `docs/macos-metal.md`.
- Installed Xcode's Metal Toolchain component for the shader investigation.
  Native runtime shader compilation uses Metal's library compiler.
- Remaining: full campaign and effects coverage, a persistent Metal shader cache,
  and matched-scene performance measurements. No speedup claim; no DXMT code
  integration. Both tested backends retain Renut's existing no-geometry fallback.

## 2026-11-07: DXMT and direct Metal feasibility

- Traced the active path: Renut -> plume Vulkan -> MoltenVK -> Metal. There
  is no DXVK layer to replace. Added `docs/macos-metal-options.md` with the
  integration points and a staged experiment for DXMT and native Metal.
- Inspected DXMT at `647b0020ba5764e5593b12407afd76964fa11e20`. Its established
  D3D11 route would need a new backend and shader binding model. Its current
  source also has opt-in D3D12 and native Mach-O builds, but geometry-shader
  rejection, native window integration, and synchronization need work.
- Confirmed our pinned plume already builds a Metal backend. An isolated
  ARM64 probe created its device and submitted a 64x64 clear on Apple M2.
  Plume's texture-to-buffer copy crashed because that direction is missing;
  a native Metal blit readback verified all 4,096 cleared pixels successfully.
- Custom Metal shader compilation could not run because the Xcode Metal
  Toolchain component is absent. DXMT was not built, no game renderer was
  switched, and no gameplay performance claim follows from this probe.
- Next: trace the game's required features, then validate either native
  DXMT D3D12 plus presentation or plume Metal's translated shaders/bindings
  in isolation before adding an opt-in game backend.

## 2026-11-07: build-time FPS comparison experiment

- `app/CMakeLists.txt`: added optional `LOS_FPS_EXPERIMENT` (default OFF),
  scoped to the menu translation unit. ON requires the dual-renderer build
  (`LOS_RENUT_RENDERER=ON`) and enables the counter at each launch, including
  with preserved local configs. Normal builds retain the saved FPS preference.
- `tools/macos/build.sh`: forwards the experiment environment variable to
  CMake so fresh builds can select the same behavior.
- `app/src/los_engine_vars.cpp`: extracted the existing atomic Swap increment
  into `CountSwap`, allowing Xenos to count without applying Renut's per-Swap
  engine-variable writes. Renut's existing counting point remains unchanged.
- `app/src/renut_engine/gpu/d3d/hooks/device.cpp`: the existing Swap wrapper
  now counts Xenos before calling the original guest function, preserving its
  arguments and rendering path. Previously the FPS overlay stayed at zero on
  Xenos because only Renut's `PaceSwap` advanced the shared counter. No guest
  override address, generated code, or renderer selection was changed.
- `app/src/los_menu.cpp`: labels the selected renderer, displays guest FPS and
  average ms/frame, and initializes the baseline at dialog creation rather
  than displaying an artificial zero for the first completed sample. Both
  renderers use guest Swaps over half-second windows, not UI repaint counts.
  Experiment startup enables the counter without writing the config.
- Runtime validation exposed an existing macOS ImGui context mismatch: the
  SDK drawer called the app's FPS dialog, whose executable-local ImGui copy
  had no current context. The first run stalled at `SetNextWindowPos` (local
  `out/pr-prep/fps-renut-startup-sample.txt`). Both app dialogs now select the
  context from their supplied IO and restore the previous context on return.
  Renut's independent overlay keeps its separate context.
- `docs/macos-build.md`: documents enabling/disabling the experiment, switching
  renderers without rebuilding, comparable conditions, and metric meaning.
- Validation: Release ARM64 build and packaging passed; the existing empty
  deployment target was preserved. Visually verified the Renut counter over
  the Besieged Village pause screen and the Xenos counter over startup/loading
  screens in windowed mode. Xenos fullscreen produced a black window; the
  cause remains unresolved, and comparison commands use windowed mode.
  F5 automation did not establish a successful menu toggle, so that interaction
  remains unverified. These are overlay smoke tests, not gameplay benchmarks.
  ON/OFF configure checks confirm only `los_menu.cpp` receives the experiment
  definition; the local cache is left ON and packaged executable hashes match.
  Existing `fpscr.h` compiler warnings remain. Shell syntax and diff checks pass.

## 2026-11-07: move experimentation to the fork's main branch

- Closed upstream draft PR #3 and the superseded fork draft PR #1 at the
  user's request to experiment in `themodernlife/LoS1-nx` before proposing
  upstream contributions. The existing macOS branches remain available.
- Fast-forwarded `main` to the combined macOS support and usability work.
  Local `origin` now names the fork and `upstream` names the original project;
  `main` tracks `origin/main`, and Git pushes and GitHub CLI commands default
  to the fork. The independent DXMT worktree was not incorporated.
- Future upstream proposals should start from a fresh `upstream/main` branch
  with selected changes cherry-picked or consolidated after checking their
  dependencies. Use `main` for experiments; propose only the selected changes
  upstream when the user requests a contribution.
- This changes repository workflow and documentation only; no new build or
  gameplay validation was performed.

## 2026-11-07: select application setup explicitly by target platform

- Added explicit Switch, Windows, macOS, and Linux cases before renderer
  setup, with an error for unsupported targets. This uses the target platform;
  DXC selection continues to use the build host for cross-compilation.
- Windows and Linux retain `los.toml`; macOS uses its setup module; Switch
  retains its existing embedded configurations in the target setup below.
- The incremental ARM64 build passes. Generated build commands still select
  `los_mac.toml`, forward ARM64 to XenosRecomp, and exclude the unused callback.
  Other target builds remain untested.

## 2026-11-07: select DXC explicitly for each supported host

- Flattened DXC selection into Windows, macOS, and Linux cases, followed by
  an unsupported-host error. Other systems no longer inherit Linux settings.
- Shared executable paths and the Unix library wrapper are configured after
  host selection; the bundled compiler choices remain unchanged.
- The incremental ARM64 build passes, and the generated wrapper compiles
  `present_ps.hlsl` to SPIR-V. Windows and Linux execution remain untested.

## 2026-11-07: finish platform build setup before updating the PRs

- Kept macOS shader-tool settings in the macOS module and loaded that module
  before configuring the renderer. The shared forwarding loop now consumes
  the platform's settings without naming macOS-specific variables.
- Included the complete change in the minimal support branch so it does not
  depend on the optional diagnostics and usability follow-up.
- The incremental ARM64 build and Cocoa/Metal/Vulkan runtime smoke test pass.
  XenosRecomp receives the ARM64 architecture and CMake policy setting; the
  unused registration callback remains excluded and all 171 gameplay units
  retain O3. Full gameplay benchmarks were not repeated.

## 2026-11-07: omit the unused registration callback from the Mac build

- Corrected the earlier startup explanation: this executable registers via
  `PPCImageConfig.func_mappings` and the SDK's loop over `PPCFuncMappings`.
  Its generated `los_RegisterFunctions` callback has no callers. The compiler
  delay was in compiling that unused callback, not running game startup.
- Removed only `los_register.cpp` from the macOS entrypoint target instead of
  compiling it with O0, disabling PCH, and disabling GlobalISel. The mappings,
  gameplay compilation, and DLL registration are unchanged.
- The native build passes. The data table exactly matches all 59,682 callback
  entries and is unchanged after rebuilding; the callback is absent from the
  compile commands and binary, while all 171 gameplay units remain at O3.

## 2026-11-07: deferred diagnostics and macOS workflow

- Split optional work from the minimal macOS renderer contribution: opt-in
  presenter throughput reporting, shader-extraction completion detection,
  build/package/launch helpers, app-icon resources, and audio/keyboard defaults.
- This follow-up depends on the minimal macOS support PR. Its added behavior
  is isolated in one commit so it can be reviewed separately from the base.
- Restored the previously built and tested implementations without altering
  the core platform integration. Generated game files remain excluded.
- Both branches pass an incremental ARM64 build and a Cocoa/Metal/Vulkan
  runtime smoke test. The follow-up also passes shell syntax and temporary
  packaging checks for libraries, icon, asset links, defaults, and preservation
  of edited configuration. The earlier gameplay benchmarks were not repeated.

## 2026-11-07: minimal macOS renderer support

- Added Cocoa/Metal window integration and Vulkan loader preparation behind
  the platform interface, macOS shader-compiler selection, Apple Clang build
  workarounds, and the embedded SDK's MoltenVK staging-path fix.
- Kept the corrected shader-extractor command because a fresh build needs it.
  Moved diagnostics, retry handling, convenience scripts, app artwork, and
  audio/keyboard default changes into a separate follow-up contribution.
- Validation is limited to Apple M2 / macOS 15.6: opening gameplay before the
  platform refactor, followed by a native build and window/Vulkan smoke test.
  Other platforms and the full campaign remain untested.

## 2026-11-03: version 0.5.0

- Release 0.5.0 (`out/nx/elf/los_0.5.0.elf`): the codegen of batch 2 with round 1 of the post-codegen steps (round 2
  measured 1 % slower and stays off), the port menu's characters-only shadows (R_ShadowMapsMask=2), Balanced = character
  shadows only, the FPS counter, NRO version 0.5.0, the console's deko3d_shaders.bin of 2026-10-29 (619 variants).
- Shadow modes (console, heavy spot, stock clocks, 960x540): full 21.8, world only 23.4, characters only 28.6, off
  28.4 fps. The world's shadows are the cost.
- PGO: the instrumented build is made (`los_pgo_generate.elf`, `C:\LoS1-nx\out\nx\los_pgo_record`); waiting for
  Mark's recording session. tools/pgo_merge.sh merges its streams with devkitA64's gcov-tool under Wine.

## 2026-10-31: codegen speed options, batch 1

- Found while reading renut-nx (ChanseyIsTheBest's Banjo port): LoS1-nx (from mof-nx's tree) was generated with none of
  sa2-nx's codegen speed options and none of its post-codegen steps, so every guest register lived in ctx memory. The
  flat game-thread profile of 2026-10-28 was this.
- Batch 1 (`app/codegen_flavor.toml`, included by the manifest): sa2-nx's set without non_argument_as_local. Generated
  with the SDK fork's own generator (`sdk/out/linux-amd64/rexgluerd`), not upstream. sa2-nx's
  `tools/read_before_write.py`: 46 reports, one real (0x824BF7BC, a shared tail reading the parent's r31:
  share_registers in overrides.toml), the rest checked and left. NRO 68.3 -> 58.9 MB.
- Mark's console, stock clocks (CPU 1020, GPU 460.8), 960x540, full shadows, 30 fps cap, standing after the lycans
  (the same spot and settings as the 13.8-14.0 fps of 2026-10-29): **18.3-18.4 fps over 27 windows of 10 s**, the main
  thread at 98 %. About +31 %. No errors; nothing different noticed.
- Post-codegen steps from sa2-nx (`tools/post_codegen.py`: pch_no_volatile, loop_barriers; 26,566 barriers):
  find_self_loops 0 in recompiled functions, NRO 53.8 MB. Console, same spot and settings: **20.5-20.7 fps** over 16
  windows (batch 1: 18.4), main thread 98 %, no errors. 14 -> 20.6 fps (+47 %) for the two steps together.
- Batch 2 built (2026-11-01, `los_codegen_b2.elf`): non_argument_as_local with a keep list of 94 functions (67 r12
  funclets, 22 vector functions, the r11 producers). The --with-option audit leaves 4 hand-over reports, all r11 saved
  into a setjmp buffer by the generator (dead after a longjmp). NRO 51.7 MB. find_self_loops: one new self-branch,
  sub_8287F398 (`cbz w25, .`): the guest loop itself never ends when its count r28 is 0 (no memory read on that path,
  so not a hoisted load; the registers as locals only made it visible). Console, same spot and settings: **22.0-22.2
  fps** over 11 windows (cleanup: 20.6), the fight after the load 16.6-18.7 (cleanup: 14.4-14.6), main thread 98 %,
  no errors. Tonight's three steps together: 14.0 -> 22.2 fps (+59 %).
- Then: batch 2 (non_argument_as_local; sa2-nx `tools/non_argument_audit.py`: 67 EH funclets taking r12, a group of
  about 26 vector functions passing v32-v61 between them, 44 r11 reads after calls to check), then the post-codegen
  steps (pch_no_volatile with loop barriers first).

## 2026-10-29: shadow tests and the port menu

- Mark's console cycles (stock clocks: CPU 1020 MHz, GPU 460.8 MHz, 960x540, 30 fps cap; the heavy scene after the
  fight at his save, standing still; each phase's frame rate from its own Swaps):
  - `R_ShadowMaps=0`: 17.1, 18.0 and 20.2 fps against 13.2 to 14.4 with the game's own values (about +35 %).
  - `R_LightShadows=0`: 17.2 and 20.0 fps, the same gain; one phase showed world shadows with jagged edges (his note;
    which phase is a guess).
  - `R_ShadowMapsMask=1` (only the world casts shadows): about +1.2 fps (+9 %).
  - `R_ShadowMapsSplitDrawModels` 1 or 0: about +0.5 fps. `R_LightLimit=1`, `R_DistMLOD1=0.5`: no change.
  - Mark: no shadows looks fine, the world shadows look nicer.
- CPU and texture cycle (same scene, 15 s phases, three rounds in the heavy part): `R_SkipAnimInterp=1`,
  `R_SkipNodeFX=1`, `R_UseEVA=0`, `R_LightSort=4`, `TextureLOD=1`, `R_Anisotropy=0`: every phase 13.8 to 14.0 fps, the
  game's own 13.8 to 13.9. None helps. TextureLOD is probably read only at texture load, so that one is untested in
  effect.
- The port menu (`src/los_menu.cpp`): - and + together on the Switch (the SDK's input driver sends F5), F5 on the PC.
  Presets Quality (full shadows, 1280x720), Balanced (world shadows, resolution by clock), Performance (no shadows,
  960x540); shadows full / world / off (`los_shadows`), handheld resolution auto / 540 / 720
  (`los_handheld_resolution`), frame cap auto / 30 / none (`los_fps_cap`). Applied at once; "Save and close" rewrites
  those three lines in los.toml (other lines kept; an older `sa2_render_height_handheld` line commented out).
- Mark's console test of the menu: every preset applied live and each "Save and close" wrote los.toml. The text was
  too small in handheld: now ImGui's default font at 26 px (twice its 13 px design) with double padding.
- Release test2: the console's `deko3d_shaders.bin` (619 variants, a superset of the 600 sent on 2026-10-28) in the zip.
- SDK: `ReXApp::AppOverlayOpen()` so the gamepad goes to an app's own menu as it does to the SDK's overlays, and
  `config_file_path()`.
- `los_engine_vars.cpp`: the thread always runs; with no test key it keeps the menu's shadow setting, and restores the
  game's values when the player picks full shadows again.

## 2026-10-28 (evening): the engine's own console variables

- Fresh look at 30 fps at stock clocks. The engine keeps the PC version's detail switches as console variables (the
  decrypted image's strings: R_ShadowMaps, R_LightShadows, R_LightLimit "Max. enabled lights per model (0..6)",
  R_UseMLOD, R_DrawMLODMin, R_DistMLOD1 "Model LOD 1 distance coef." and 1,760 more), and its Lua has the PC's video
  menu and a debug menu over them. Layout from their static constructors (sub_821A2290): a linked list, head
  0x82D1AFDC, next +28, name +32, type +41 (2 integer, 4 float), value +44, default +48.
- Not a lead: catch-up work at low frame rates (PhysX on its own thread under 1 %; the main thread's time per frame
  followed the CPU clock and not the resolution), and the thread layout (the main thread already has core 0 alone).
- `src/los_engine_vars.cpp`: `los_engine_vars = "NAME=VALUE;..."` sets them, every second and at every Swap (the
  game sets R_DistMLOD1 back every frame); `los_engine_vars_ab = <s>` alternates with the game's own values, logged;
  `los_engine_vars_dump` lists all. Linux run: 1,764 variables found, the settings hold.
- Mark's console A/B (heavy scene, 540p, stock clocks): shadows, lights, light limit 1 and LOD distance 0.5 together
  against the game's own values: about 17.5 fps against about 14 (each pair in favour, by 0 to 7 fps; he moved), the
  render thread 13-18 ms a frame against 25-35. Shadows alone: inconclusive (90 s of the heavy scene, and half of the
  10 s reports straddled two phases: shadows off 13.8 and 19.1 fps, on 13.7 and 13.7).
- `los_engine_vars_cycle`: several phases in turn, each phase's frame rate counted from its own Swaps.

## 2026-10-28: stock clocks (960x540)

- Asked for: 30 fps on stock clocks, as ds3-nx (Dead Space 3 on ReXGlue) gets with 960x540 and the official 460.8 MHz
  handheld GPU profile. Mark's 2026-10-26 console log was not at stock clocks: CPU 1785-2091 MHz and GPU 460.8 MHz
  (sys-clk). Heavy scenes there: 17-25 fps, the GPU at 40-49 ms a frame, the main game thread at about 90 % of a core.
- The Switch could not render below 720p: the shader package was made with the passes scale1 and fragscale (1x only),
  and Renut had no 540p scale. Now: Renut's scale code 7 is 3/4 (it was 4/3, the full ambient occlusion of 480p, never
  drawn on deko3d; patch `reblue-XenosRecomp-540p-scale.patch` for the shaders' table), `renut_resolution = "540p"`,
  and `build_switch.sh shaders` makes the package without the two passes (sa2-nx's 1.0.3.1 list).
- None of this game's 2,006 shaders reads a texture's scale code, so the package test (deko3d::PackageScales) found no
  scaling package and kept 1x; it now also accepts the resolution_scale word that fragscale folds (596 shaders).
- `los_stock_clocks.cpp`: handheld with the CPU at its stock clock at start: 960x540, unless los.toml sets
  sa2_render_height_handheld (`los_stock_540p = false` turns it off). sa2-nx's `sa2_gpu_boost.cpp`: the official
  handheld GPU profile (460.8 MHz, memory unchanged).
- Linux run at 540p (lavapipe): the 3D title scene draws right at 960x540. Switch build: 21 steps (the shader package
  and an incremental build). Shader variant file: the PC list's 350 variants made again (110 unchanged) plus the
  console's own, 417 in all.
- Mark's stock-clock tests (CPU 1020 MHz; his sys-clk profile changed after the start, so the start check was right):
  720p: light 46-60 fps (GPU 15-21 ms), medium 32-36, heavy 10-17 with the main game thread at 97-98 %. 540p forced in
  los.toml: the heavy scene's GPU time 35-40 ms -> 20-23 ms, 9-14 fps: the game thread is the limit. Its stack samples
  (switch_profile_stacks, 2,261 in the heavy scene): 67 % the game's own code with no function above 2 %, 10 % the
  resource copies, 8 % the game's Direct3D, 4 % the renderer. LTO + PGO (sa2-nx: about 15 %) and cheaper copies would
  not bring the heavy scenes to 30 at stock clocks; Mark chose the cap for now.
- 30 fps cap at stock clocks in handheld (`los_fps_cap`, -1 by default; the pacing takes at least two refreshes a
  frame), and the clock checked again every 5 s (a sys-clk profile changed in game switches 540p and the cap).

## 2026-10-26: Renut's renderer mapped to this game

- Mark's first play (emulated GPU): the first level plays through; only the Travel Book menus flicker (the emulated
  GPU's "invalid texture fetch constant" warnings).
- Renut's renderer mapped to this Direct3D (XDK 2.0.11626): 41 functions, 3 data words and every device offset
  ([d3d-map.md](d3d-map.md)).
- Shader extractor for the .packed archives (`tools/los_dump_shaders.py`): 2,006 shaders from both discs, every one
  the game used at its title screen on the Linux test.
- Linux runs (lavapipe) found and fixed four things this game does that Mirror of Fate does not: the render thread's
  wait for the display's flip count, the ring space query, the device's own vertex declaration, predicated tiling
  ([renderer.md](renderer.md)). The game now runs with the renderer to its 3D title scene without a crash, but the
  frames captured there are black; whether that is lavapipe or the renderer is for Mark's PC test.
- Mark's first `-Renut` build: the shader translator's second run (Renut's early alpha copies) stopped at 98 % with
  no message; the first run had built all 2,006 (32 pixel shaders failed DXIL validation, "coord uninitialized": no
  D3D12 code for them, the Vulkan code is there). The early alpha pass is off in `build_pc.ps1` for now.
- Mark's PC had never pulled (its branch had no upstream: `git pull` did nothing); with the code in place the shader
  step still stopped at 98 %: XenosRecomp exits with code 2 when any shader fails, and the 32 DXIL validation
  failures count. The PC build now makes Vulkan code only (`RENUT_DXIL=OFF`, the renderer then always uses Vulkan).
- Mark's PC build (Vulkan): runs at 60 fps, but black. The same on the Linux run, whose captures work for Mirror of
  Fate. Cause: this Direct3D builds the declaration of an FVF draw inside the device (+0x3050) and binds that, so the
  renderer had no vertex layout for the 2D draws. Each element list now gets a registered copy: the Linux run shows
  the loading screen and the title screen.
- Mark's PC test (Vulkan): "looks and plays well"; the Travel Book menus no longer flicker.
- First Switch build (`tools/linux/build_switch.sh` in the cloud session, about 1 h 20 min on 2 cores): 2,006 shaders
  in the deko3d package (32 kept without the GLSL passes: uam failed on their rewrite). No asset pack on the Switch:
  the game files are 6.7 GB and a FAT32 card holds no file over 4 GB, so game_root/ is read as it is.
- Mark's first console test: "Switch: app initialization failed" at once (no Vulkan driver for the emulated GPU): the
  Switch configure never turned `LOS_RENUT_RENDERER` on, so the NRO had no Renut and fell back to the emulated GPU.
  `build_switch.sh configure` now turns it on; rebuilt (282 steps).
- Mark's second console test: plays through the first level into the second ("pretty dang successful"; "good, not
  quite great", some stutter). Log: 37-53 fps in light scenes, 17-25 fps in the heavy ones with the GPU at 40-47 ms
  a frame (GPU-bound); 130 run-time uam compiles at about 70 ms each (the stutter; no deko3d_shaders.bin yet).
- Shader variants for the console: 350, from the PC run's pipeline list (`out/deko3d_cache/deko3d_shaders.bin`), to be
  merged with what the console compiled itself.
- Disc 2: the game's SwapDiscThread (sub_82199600, XBoxSwapDisc.cpp) asks XamSwapDisc for the other disc, waits on
  its event, then checks for `Disc.1` / `Disc.2` in the game folder. XamSwapDisc was a stub that never set the event.
  Now `<game folder>_disc2` (or `game_data_root_disc2`) is mounted as `\Device\Cdrom1`, and XamSwapDisc points game:
  and d: at the disc asked for and sets the event (`sdk/src/system/runtime.cpp`, `sdk/src/kernel/xam/xam_info.cpp`).
  Not yet tested at the actual swap (late in the game).
- Shader file merged: the console's own 177 variants (118 of them matched the PC list's keys) and the PC list's 232
  others, 409 in all, on Mark's card with the disc 2 build. Mark: "very performant"; he is playing on and will send
  logs at a stopping point or a problem.
- Next: GPU time in the heavy scenes (from his next log), the swap to disc 2 when he gets there.

## 2026-10-26: first PC boots

- Mark's first build stopped at Mirror of Fate's shader dump step (no .bshdat files: "no inputs"). The dump and
  Renut's renderer now only run with `build_pc.ps1 -Renut`.
- First boot (Mark's PC, emulated GPU, D3D12): the game opens Data00.packed and English.packed, looks for `D:\Voices\`
  and `D:\mods\` (not on the disc), opens audio, sets the GPU interrupt callback (824A39F0), names its threads
  (SwapDiscThread, FileAlignThread, PS_Thread, PxSimulate_Thread), then "Call to invalid or unregistered function at
  guest address 0x826FFE88".
- `tools/pointer_fixpoint.py` over the image: 156 candidates, 131 accepted (among them 0x826FFE88), 25 rejected (8
  jump tables, 4 funclets, 11 no code, 1 no terminator, 1 fragment); the second pass found none.
- setjmp/longjmp found ahead of the Lua panic Mirror of Fate's third boot hit: 0x826F0A00 and 0x826F0CD0 (by Mirror
  of Fate's copies). Codegen: 59,682 functions, no warnings, setjmp emitted in 8 files.

## 2026-10-25: start

- Feasibility scan of disc 1's `default.xex` ([scan.md](scan.md)): 59,556 functions, one override, every import in
  the SDK; XDK 2.0.11626, whose Direct3D device layout differs from SA2's and Mirror of Fate's.
- Disc 2 listed: 425 files, 6.6 GB: 394 Bink videos, `Data00.packed` (641 MB) and `Data01.packed` (1.68 GB) of its
  own (disc 1's: 653 MB and 1.37 GB), the same `English.packed` (36.6 MB) and `Music.packed` (220 MB), the same
  `default.xex`.
- Repository made from mof-nx's tree (the SDK fork, the app, Renut's renderer, the tools), renamed for this game.
  Renut's renderer is off (`LOS_RENUT_RENDERER`) until its Direct3D map is made for this XDK.
- Linux build (clang 20, emulated GPU): compiles; the link needed sa2-nx's self-check stubs outside the renderer's
  glue (`app/src/los_stubs.cpp`). A 60 s run with only `default.xex` in the game folder: no crash; the game names a
  `SwapDiscThread`, tries to create `D:\filelist.2Bpack` on the (read-only) game disc, then to open it (not found).
