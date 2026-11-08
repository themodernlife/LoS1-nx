# Native Metal cutscene ghosting investigation

Investigated on 2026-11-08 against experimental commit `8fedccc`. This is a
visual and source-code investigation, not a confirmed root cause or a fix.

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
- The scene appears to be an in-engine QTE finisher that returns to gameplay,
  rather than prerecorded video playback.

These observations favor motion-blur ghosting or another rendering/sampling
artifact over display tearing. They do not establish whether the effect is
intended, inherited from the shared renderer, or specific to Metal.

Three subagents independently reviewed the clip, Metal presentation, and
cutscene resource/sampling paths. Their findings are consistent with this
classification. The recorded run used native Metal on Apple M2 at x1 internal
render scale, with the FPS overlay enabled.

## Leading explanation and concrete parity gap

The shape and regular spacing of the trails are consistent with a motion-blur
pass using a small number of samples. Excessive blur strength, incorrect motion
inputs, or overly sharp sampled images could make those samples conspicuous.
No offending draw, shader, or input texture has been captured yet.

There is one concrete sampler difference worth testing:

- `app/src/renut_engine/gpu/render/textures.cpp`, in sampler construction, maps
  the guest LOD bias to `desc.mipLODBias` using `fetch.LodBias() / 32.0f`.
- `app/thirdparty/plume/plume_vulkan.cpp` applies that value to
  `VkSamplerCreateInfo::mipLodBias`.
- `app/thirdparty/plume/plume_metal.cpp`, in `MetalSampler::MetalSampler`, sets
  the filters, address modes, anisotropy, comparison, and LOD clamps, but does
  not apply `desc.mipLODBias`. The current SPIR-V-to-MSL bridge also does not
  compensate for it.

This could change sampled mip levels and sharpness. It is not evidence that
this particular effect is wrong: a single-mip blur input would make mip bias
irrelevant, and the affected pass's bias and mip count are still unknown.

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

1. Reproduce the same QTE finisher with native Metal and Vulkan, using the same
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
4. A verified motion-blur disable setting would provide another useful test.
   The existing `los_engine_vars_dump` bridge can enumerate engine variables,
   but this investigation did not identify a verified blur variable. Do not
   invent a variable name or assume an unverified toggle worked.

## Evidence and scope

The original movie remains in the parent workspace. Extracted frames and
contact sheets are ignored local files under `out/cutscene-shearing/`, including
`frame-090.png` and `clip-agent/frames-080-099.jpg`. Runtime evidence comes from
the isolated Metal app's `logs/los_014.log` and configuration. No movie, frame,
game data, or shader dump is committed.

This investigation changed documentation only. It did not restart the running
game, change rendering settings, modify the save profile, or alter the main
checkout. A matched Vulkan replay and GPU pass capture remain outstanding.
