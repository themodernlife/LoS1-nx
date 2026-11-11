# Progress log

Newest first. Each entry: what was done, what was found, what is next.

## 2026-11-10: isolate the Renut investigation from main

- Preserve the existing depth-bias/position-dot candidate, GPU capture tools,
  and 21-sample blur experiment on `codex/renut-blur-investigation`.
- This is an organizational snapshot of existing work, not a new fix or
  acceptance result. No implementation code was changed or rebuilt here.
- Main remains at `95fc9d1`. Existing local builds, captures, dependencies,
  profiles, and test apps remain in the original checkout's ignored paths;
  they must not be assumed to match main after the source checkout switches.

## 2026-11-10: reduced blur sampling after the user's performance test

- The user reported the 65-sample experiment looked acceptable but ran at a
  very low frame rate. This is qualitative feedback, not a measured benchmark
  or proof that the motion inputs match the original console.
- Reduced the guarded kernel in `tools/renut_dense_motion_blur.py` to 21 samples,
  with matching spacing and weight normalization. It keeps the same endpoints
  and halves the original eleven-sample spacing, using about one-third of the
  scene reads of the 65-sample version. Actual frame-rate improvement is pending.
- Updated the CMake option description and capture guide to describe the new
  test and the reason for reducing its cost. The original shader boundaries
  and rejection guards passed validation. The 65-sample app is preserved for
  comparison; the new test uses a separate checkpoint copy.
- Release ARM64 build passed with no shader override fallback. Comparing all
  2,006 cache entries against the original baseline again found only PS
  `7838FCCDD9A82460` changed. Packaged and launched `Dense Blur 21 Test.app`;
  verified its startup logo renders. Build, cache comparison, binary hash,
  launch arguments, and runtime log are in `out/dense-blur-21-20261110/`.
  Horse-fall quality and frame rate remain for the user's comparison.

## 2026-11-10: built the denser motion-blur sampling experiment

- Added `tools/renut_dense_motion_blur.py` to replace only the eleven scene
  samples in PS `7838fccdd9a82460` with 65 uniform samples over the same endpoints
  and with the original total weight. Motion, camera, deformation, alpha-test,
  and blend calculations remain unchanged. A source-block digest rejects an
  unexpected translator output instead of silently applying a stale override.
- Added default-off `LOS_RENUT_DENSE_MOTION_BLUR` in `gpu.cmake`, using the existing
  HLSL override workflow, and documented enable/disable steps in
  `docs/macos-gpu-capture.md`. This makes the sampling-quality experiment
  reversible without editing game-derived generated sources in the repository.
- Enabled the option locally and completed the Release ARM64 `los` build.
  Guard checks reject changed input and reapplication; preserved shader prefix
  and suffix match the baseline. Compared all 2,006 shader cache entries:
  only the intended pixel shader changed. Existing compiler/linker warnings
  remain. Logs and verification records are in ignored `out/dense-blur-20261110/`.
- Packaged and launched separate `Dense Blur Test.app`, with a matching binary
  hash and a writable copy of the preserved capture checkpoint. Its `.command`
  launcher selects that isolated profile. Verified the rendered title screen;
  the prior ReNut Update app remains available for comparison.
- Next: user compares the horse fall and checks frame rate. Denser sampling
  may reduce repeated outlines but costs extra texture reads; this is not yet
  a confirmed runtime visual fix or a validation of the motion values.

## 2026-11-09: isolated the captured blur pass and reproduced its output

- Mapped guest PS `7838fccdd9a82460` to captured Metal source
  `4C47F8F499E9DA79`, pipeline `0x131562400`, and first-frame draw 4390.
  Xcode did not retain the guest shader hashes in pipeline names. Read-only
  decoding of the capture's zlib store/index and resource records established
  the mapping; Xcode verified the matching draw and resource bindings.
- Exported raw KTX scene, velocity RGBA, sampled depth, deformation, and output
  textures at that draw. These are local game-derived artifacts under ignored
  `out/gpu-captures/20261109-215443-721425/blur-analysis/`, not source assets.
  Dynamic offsets and constants are recorded there in JSON. The character is
  sharp in the scene input and blurred in this pass's output.
- A local NumPy reconstruction of the eleven-tap pass agrees with the raw GPU
  output to about 0.404/255 mean absolute channel error (99th percentile 3.85).
  This is an approximate analytical reproduction, not bit-exact GPU emulation.
  Controlled variants isolate camera, object, and deformation contributions.
- At this frame, the object branch covers about 26.8% of pixels. Its full
  sampling span has median 39.5 pixels and 99th percentile 70.7 pixels.
  Velocity alpha is preserved and nonconstant. Deformation has nonzero values
  in only 0.28% of texels. Coherent velocity coverage in this frame does not
  establish that the depth-equality issue is fixed generally.
- Tested 65 samples over the same endpoints in the offline reproduction. It
  smooths discrete repeated outlines while retaining strong blur. This suggests
  sampling density as a targeted quality experiment; it does not prove the
  supplied motion values are correct or that the original console looks alike.
  `comparison.png`, `findings.json`, `analyze.py`, and `dense.py` preserve the
  comparison and local analysis. No new runtime shader change was made.

## 2026-11-09: captured and replayed the blurred horse fall

- The user triggered the prepared 60-frame capture. Waited for finalized
  metadata and closure of the game's trace file handles; the resulting local
  `out/gpu-captures/20261109-215443-721425/scene.gputrace` occupies about 13 GB.
- Replayed successfully in Xcode: 179 command buffers, 5,092 render encoders,
  32,560 draw calls. The first present, command 6241, has an attachment showing
  Gabriel falling with visible motion blur. The final summary image is the
  subsequent fight; use the early present attachments to inspect the fall.
- The capture now provides a repeatable GPU reproduction of the failing scene.
  No additional shader changes were made and no root cause is established.
  Next: locate the blur draw preceding present 6241, inspect its scene/depth,
  object velocity (including alpha), deformation inputs, and camera matrices.
  Local `verification.json` records the trace UUID and replay findings.

## 2026-11-09: repeatable GPU capture for motion-blur investigation

- The user's horse-knockoff screenshot still shows severe blur after the
  depth/position experiment. No further shader fix is claimed.
- Added `tools/macos/capture_gpu.py`: previously this workflow required manual
  capture environment setup; now it launches the chosen app with MoltenVK's
  on-demand capture, finds its process-owned FIFO, preserves a starting profile,
  uses a separate writable copy, and records executable/profile hashes and args.
  All captures, saves, logs, and the compiled helper stay under ignored `out/`.
- Added `tools/macos/capture_hotkey.m`: Control+Option+B requests 60 subsequent
  frames by default, without input-monitoring or Accessibility permission.
  Existing traces are protected from overwrite; the helper exits with the game.
  The Python CLI can also request a bounded frame count for smoke testing.
- Validated helper compilation/registration, Python syntax, and a two-frame
  title-screen capture using bundled MoltenVK 1.4.1. Opened the trace in Xcode
  and replayed it successfully: Frame ready, five command buffers, six draws.
  This establishes capture/replay, not reproduction of the horse scene yet.
- Local session `out/gpu-captures/20261109-215443-721425/` retains the verified
  `menu-smoke.gputrace`; the running game is armed for `scene.gputrace`.
  Next: user captures the fall; inspect motion/depth/deformation inputs and the
  final blur pass. Captured GPU work can be replayed, while C++ renderer changes
  still require a new capture. This is not a gameplay save-state implementation.

## 2026-11-09: targeted reNut depth and shader experiment

- On fork `main`, adapted two changes from reNut `e5c57e7` and its
  reblue-XenosRecomp dependency `bf1b365`. This is a visual test candidate,
  not a confirmed repair for Wargs or the horse-knockoff cutscene.
- `app/src/renut_engine/gpu/d3d/d3d.h`: names the front/back depth-bias words.
  Evidence: the local generated LoS `sub_824962E0` stores the same input at
  10836/10844 (`0x2A54`/`0x2A5C`) and enables ModeControl bits 11/12.
  The inherited native N&B state code is not evidence of LoS's active setter;
  this check used LoS's original translated function. No new hook or override.
- `render/draw.cpp`: previously ignored constant polygon offset; now converts
  it into the host depth bias using reNut's D24FS8/D32 and D24S8/D32 handling,
  with native D24 handling retained for deko3d. Selects the surviving face,
  rejects nonfinite values, and bounds integer conversion. Pipeline-key reuse
  now checks the bias words, so an offset-only change cannot reuse stale state.
  Slope-scaled bias remains outside this constant-bias port, as in reNut's
  source implementation. Its effect on LoS remains unverified.
- `render/pipeline.h` and `render/pipeline.cpp`: include bias/clamp in the
  pipeline key and descriptor. The list version is 5; version 4 caches are
  rebuilt. Existing version 3 prefix conversion initializes the appended
  fields to zero. This prevents pipelines with different offsets from aliasing.
- `patches/renut-deps/reblue-XenosRecomp-position-dots.patch`: carries reNut's
  compensated position-dot calculation and dependency scan as a reproducible
  dependency patch. The unmodified move-only scan produced a byte-identical
  LoS cache, so the scan also follows LoS's lane-wise Add/Mul/Mad chains before
  the position export. Only identified dot products use the helper; this is
  not the earlier whole-output `precise` experiment. The helper preserves its
  operation order with `precise`; its MoltenVK cost needs player evaluation.
- Motion blur remains game-controlled and the velocity pass retains its
  original EQUAL depth comparison. The separate depth-packing overflow and
  broader renderer upgrades were not folded into this comparison.
- Local build logs, shader-cache comparison, launcher and a copied save are
  under ignored `out/renut-update-20261109`. The test bundle is
  `out/ReNut Update.app`, launched through `out/ReNut Update.command`.
  Release ARM64 build and the Cocoa/Vulkan platform smoke test passed. The
  original deployment-target setting remains empty. All 2,006 shader entries
  are present: 223 vertex shaders changed and all 1,634 pixel shaders are
  byte-identical to the pre-experiment cache after decompression. The dependency
  patch passes reverse-application checking; the packaged executable matches
  the build by SHA-256. Existing `fpscr.h` width warnings remain.
  Startup reached guest draws at 1280x720 on Vulkan/Apple M2; the log confirms
  game-controlled engine values. Visual acceptance and performance are pending
  the player's comparison of both scenes. The candidate uses an independent
  copied profile.

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
