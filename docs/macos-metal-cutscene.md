# Native Metal cutscene ghosting investigation

Investigated on 2026-11-08 in the separate native Metal worktree. A Metal
clear-state defect is now reproduced and fixed, but the user confirmed that
the tumble trails remain with normal camera blur. It is not the cutscene fix.

## Latest result and candidate correction

Latest user validation of the adaptive-sampling build: ordinary riding blur
looks fine, but the tumble still shears and smudges. The sampling change is
not a solution to the reported tumble defect. Riding recovery followed both
restoring normal camera history and enabling adaptive sampling, so it does
not isolate a benefit from the sampling change itself. Preserve this as an
opt-in experiment, not a verified renderer correction for the tumble.

Normal-history input capture now includes horse riding, with all four textures
and effective constants captured successfully. The later bursts caught the
pause screen after Gabriel fell, not the tumble itself. The camera's previous
position advances by approximately one frame: its displacement is 0.97-1.00
times the per-frame displacement measured across six-frame capture intervals.
Matrix inversion error changes the predicted blur extent by less than 0.1
pixel. These samples do not support stale camera history as the explanation.

The riding pass still averages only eleven point-filtered samples over a
camera-blur extent of up to 84-87 pixels. That leaves roughly eight pixels
between taps, consistent with separated outlines. Object-motion extents in
these same samples are smaller, up to 11-14 pixels. The actual tumble inputs
and a matched Vulkan replay remain uncaptured, so this is not proof of a
Metal-specific translation defect.

`LOS_ADAPTIVE_MOTION_BLUR` is an opt-in quality candidate, default off. It
replaces only this shader's scene-sample accumulation with an authored helper.
It retains the velocity construction, deformation warp, camera history, blur
endpoints, and output processing. Linear filtering and a sample count based on
the actual texture dimensions fill the gaps between taps. The count ranges
from 11 to 257; spans above 256 pixels can still have wider gaps. The extra
samples may increase GPU cost, especially at higher render scales.

The transformation checks the shader identity, kernel structure, and digest
before writing. Generated shader text remains in the ignored build directory.
The candidate is enabled only in this experimental worktree's local CMake
cache. The user's subsequent test rejects it as a sufficient tumble fix.

The candidate builds and packages successfully with no shader-override
compilation fallbacks. The actual shared filter passes Metal GPU tests for
stationary detail, continuous 41-pixel trails, and constant color at the
sample cap. Including the prior clear-state tests, all 32,768 checked pixels
pass with Metal API validation enabled. `los_022.log` is the candidate launch
with ordinary camera history and blur enabled, default deformation settings,
the FPS overlay, and the same explicitly selected isolated save profile.

Correction after the user's horse-riding screenshot: `SkipCamera` affects
object-velocity projection as well as the camera postprocess. Its secondary
interface at object +4 shares the same camera history field +88 and matrix
+24. Replacing that previous camera while retaining previous object poses can
remove camera/object tracking cancellation, producing smeared actors against
sharp scenery. The earlier claim that object blur was unchanged was wrong.
Those tests are not pure camera-postprocess isolation and do not establish
faulty bone history. Restore normal history before capturing the motion data.

The deformation-velocity-zero test still showed excessive riding blur; c15 = 0
is verified in `los_020.log`. The screenshot is the user's Desktop file
`Screenshot 2026-11-08 at 2.17.12 PM.png` (the actual filename has a narrow
nonbreaking space before PM). No image is committed.

The user reported that the camera-isolation test improved the tumble but left
the warg scene very blurry. Gameplay inputs have deformation velocity enabled
(c15 = 0.02). That contribution can produce additional blur even when camera
history is suppressed. Its presence alone is not evidence of faulty input data.

Source review found a concrete Metal defect independent of that hypothesis:
rectangle clears bind utility drawing state, then restored the previous cache
without restoring the actual encoder. The next draw could skip rebinding its
unchanged pipeline or viewport because the stale cache claimed they were already
active. `plume-native-metal.patch` now keeps the cache aligned with the state
actually installed by the clear, allowing the existing dirty-state machinery
to restore the normal draw.

`tools/macos/metal_smoke.cpp` reproduces this without gameplay. The old backend
fails 1,024 pixels after a color clear and 1,024 after a depth clear. The patched
backend passes all 20,480 pixels, including depth rejection and the original
descriptor/dynamic-constant cases, with Metal API validation enabled. The patch
reproduces both native files from pinned originals, and the ARM64 game build and
packaging pass. Logs are ignored under `out/cutscene-shearing/fix/`:
`clear-regression-before.log`, `clear-regression-after.log`, and
`clear-state-build.log`.

User validation of that corrected build with normal camera blur restored
reported incorrect tumble blur again. Retain the independently verified clear
correction without attributing a cutscene improvement to it.

The subsequent diagnostic restored camera suppression and zeroed deformation's
extra blur velocity: `RT_PixelMotionBlur=1`,
`RT_PixelMotionBlurSkipCamera=1000000`, `RT_DeformationVelScale=0`.
The c15 term adds sample spacing proportional to the deformation texture's
paired-channel differences. Zeroing it retains the object-velocity blur and
c14 deformation warp. This checks the remaining broad blur in the warg scene;
it was not an underlying renderer correction and did not solve the riding
blur. The history interaction above supersedes its original interpretation.

## Observed artifact

The user confirmed that the reported shearing is the repeated outlines around
Gabriel during fast movement, approximately 1.5-2 seconds into the supplied
`Untitled.mov`. The 5.499-second recording contains 342 progressive H.264 frames
at 2940x1912, including the game window and desktop.

- Frame 90 at 1.500 seconds has several regularly spaced copies of the moving
  character's silhouette. Frame 91 at 1.517 seconds retains the effect as the
  character moves. Consecutive frames around this interval show the same pattern.
- The overlay and window chrome remain sharp. There is no clear horizontal seam
  dividing two otherwise coherent frames, as expected with ordinary tearing.
- The character becomes sharp as movement slows around three seconds.
- The scene is the Hunting Path horse-chase QTE/fall sequence, identified by
  comparison with the user's subsequent reference video. It appears to be
  rendered in-engine rather than prerecorded video playback.

These observations favor motion-blur ghosting or another rendering/sampling
artifact over display tearing. The local movie alone does not establish
whether the effect is intended, inherited from the shared renderer, or specific
to Metal. The reference comparison below narrows that uncertainty.

Three subagents independently reviewed the clip, Metal presentation, and
cutscene resource/sampling paths. Their findings are consistent with this
classification. The recorded run used native Metal on Apple M2 at x1 internal
render scale, with the FPS overlay enabled.

## Reference comparison supplied by the user

Viewed JamesInDigital's
[Castlevania Lords of Shadow - Chapter 1 - Hunting Path](https://www.youtube.com/watch?v=spsQQXgPzsY&t=195s)
in the browser. The user mentioned 3:15, while the supplied link starts at
3:37. Checked the chase around 3:14-3:21 and stepped through the matching tumble
around 3:35-3:36, including player times 215.242, 215.509, 215.742, and 216.242
seconds. The browser reported a decoded video size of 1920x1080.

The reference has motion softness during the tumble, but the inspected moving
silhouettes and coat edges do not show the dense, regularly spaced copies seen
in the local Metal recording. This strengthens the rendering-defect hypothesis
and weakens the explanation that our severe trails are simply the intended
effect. It does not identify the faulty pass or distinguish shared Renut
behavior from Metal-specific behavior.

The reference platform, graphics settings, and capture processing are not
verified. These are corresponding action samples, not an exact frame-aligned
or pixel-equivalent comparison. A controlled Vulkan/Metal replay remains the
next test; do not treat the reference as proof of the missing mip-bias theory.

## Leading explanation and concrete parity gap

The shape and regular spacing of the trails are consistent with a motion-blur
pass using a small number of samples. Excessive blur strength, incorrect motion
inputs, or overly sharp sampled images could make those samples conspicuous.
The blur shader and its fetch/scalar inputs have since been identified. Riding
inputs are captured, but the tumble's individual texture contents are not.

There is one concrete sampler difference worth testing:

- `app/src/renut_engine/gpu/render/textures.cpp`, in sampler construction, maps
  the guest LOD bias to `desc.mipLODBias` using `fetch.LodBias() / 32.0f`.
- `app/thirdparty/plume/plume_vulkan.cpp` applies that value to
  `VkSamplerCreateInfo::mipLodBias`.
- `app/thirdparty/plume/plume_metal.cpp`, in `MetalSampler::MetalSampler`, sets
  the filters, address modes, anisotropy, comparison, and LOD clamps, but does
  not apply `desc.mipLODBias`. The current SPIR-V-to-MSL bridge also does not
  compensate for it.

This could change sampled mip levels and sharpness in other passes. The later
`LOS_BLUR_DIAG` capture in `los_017.log` records one mip and zero LOD bias for
all four inputs of pixel shader `7838FCCDD9A82460`. It also records zero texture
exponent adjustment. Neither omission explains that captured blur pass.

Shared renderer comments in `graphics_options.cpp` describe sparse blur taps
becoming visible when their inputs contain more detail than expected. Those
comments refer to inherited game-specific examples, not proof of this LoS
pass. The recording used x1 scale, so a simple high-resolution enhancement
explanation is not supported by this run.

## Paths reviewed without a confirmed defect

- Presentation retains each drawable through GPU completion and resolves the
  current drawable when creating the render encoder. Acquire, render, and
  present use the same queue. The default presentation configuration enables
  vsync. No specific swapchain fault was found that explains the repeated
  silhouettes in the supplied frames.
- Frame-slot upload storage waits for GPU completion before reuse. Dynamic
  constants are snapshotted per draw. Exported stream descriptors are replaced
  and retired rather than immediately overwritten. No concrete resource reuse
  race was identified in those paths.
- Pixel-center adjustment, depth resolves, and Metal stencil views have
  existing handling. They remain possible inputs to a bad effect, but the
  review did not establish a defect in this scene.

These are code-review findings, not exhaustive validation of every GPU resource
or synchronization path.

## Tests that distinguish the remaining possibilities

1. Reproduce the Hunting Path QTE/fall with native Metal and Vulkan, using the same
   executable, checkpoint, internal resolution, and graphics settings. Preserve
   the user's profile by making a separate diagnostic copy before replaying.
   If both backends show matching trails, investigate the shared renderer and
   the original game's effect before changing Metal-specific behavior.
2. Capture the affected frame through the existing offscreen capture API in
   `banjo_renut_capture.h`. It runs before drawable acquisition in `present.cpp`,
   so trails present there would exclude drawable/display presentation as their
   origin. On Metal this capture still includes `RecordScanOut` and its gamma
   conversion. The `banjo_renut_capture_raw` option currently implements a raw
   source copy only for deko3d, not Metal. Capture waits for the GPU and can
   perturb timing, so use it for image diagnosis, not a pacing benchmark.
3. If the effect differs between backends, capture the blur pass and compare
   its shader, constants, source texture dimensions and mip counts, sampler
   filters/LOD bias, and any motion/depth inputs. Test the missing LOD bias only
   after confirming that the pass actually uses a nonzero bias and mipmapped
   input.
4. Completed: the user confirmed that `RT_PixelMotionBlur=0` removes the
   outlines (`los_016.log` verifies the override). This is only a workaround.
   The next user-driven test keeps `RT_PixelMotionBlur=1` and sets
   `RT_PixelMotionBlurSkipCamera=1000000` for the session. The latter is a
   countdown, not a boolean: its guest consumer decrements it and invalidates
   camera history. The postprocess then selects current view-projection for
   the previous camera matrix. The object-velocity pass remains enabled.
   Later code review found that this also substitutes the camera used by
   object velocities. It cannot cleanly distinguish those two paths and
   must not be reported as the underlying fix. Use normal-history input
   captures for further diagnosis.

## Subsequent shader and runtime checks

The blur shader averages eleven point-filtered scene samples. All fourteen
texture instructions inherit the guest filters, have normalized coordinates,
zero fetch offset and zero instruction LOD bias. The decoded instructions and
translated math agree. Velocity is RGBA8 UNORM; depth is R32_FLOAT. The examined
Metal format mappings and resolve/swizzle compensation do not establish a bug.

With captured velocity power 0.005 and zero deformation velocity, the
RGBA8 object-velocity path is bounded to 6.4 horizontal pixels between taps at
1280x720, or 64 pixels across the kernel. Camera-derived velocity is not bounded
by that texture's range. This helps distinguish the paths but does not identify
the cause without affected-scene evidence. The velocity multiplier is computed
by the guest as `max(1 / (VelFPS * dt) - 0.33, 0) * VelScale`; the recorded
values alone do not demonstrate faulty timing.

`LOS_BLUR_DIAG=1` logs the captured fetch state and scalar inputs at most once
per second for 300 changed samples. It does not modify rendering. The ARM64
diagnostic build and packaging passed. The user will navigate and judge future
gameplay tests; do not spend time automating travel back to the cutscene.

## Evidence and scope

The original movie remains in the parent workspace. Extracted frames and
contact sheets are ignored local files under `out/cutscene-shearing/`, including
`frame-090.png` and `clip-agent/frames-080-099.jpg`. Runtime evidence comes from
the isolated Metal app's `logs/los_014.log` and configuration. No movie, frame,
game data, or shader dump is committed.

The initial investigation changed documentation only. Subsequent work adds
opt-in diagnostics, session-only engine-variable tests and the clear-state
correction described above. The main source checkout remains untouched. The
`los_018.log` direct launch used the default save root; the next test copies its
latest state into a fresh isolated profile and passes the root explicitly.
No saves are copied back. A matched Vulkan tumble replay and capture of its
individual inputs remain outstanding. The work is preserved on
`codex/native-metal` in the user's fork; the smudging remains an open issue.
