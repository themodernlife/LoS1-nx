# Movement trails investigation

## Current changes and acceptance status, 2026-11-09

The regular installed app is unchanged. All tests below are isolated apps in
this worktree's ignored `out/` directory. Nothing has been merged or pushed.

| Version | Actual change | Player verification |
| --- | --- | --- |
| Blur Off | Set `RT_PixelMotionBlur=0`; original executable/shaders | Both scenes look perfect, with this blur pass disabled |
| Motion Data, ALWAYS | Leave blur game-controlled; bypass depth testing only for object velocities | Both scenes seem OK; hidden geometry can also contribute |
| Motion Precision | Leave blur game-controlled and restore original EQUAL depth testing; mark generated vertex position `precise` | Pending; not covered by the earlier ALWAYS approval |

The source changes currently comprise:

- `app/src/renut_engine/gpu/render/draw.cpp`: opt-in captures and depth-test
  experiments. Normal launches keep the original depth comparison. These
  diagnostics are not the proposed production repair.
- `patches/renut-deps/reblue-XenosRecomp-precise-position.patch`: candidate
  compiler change, adding `precise` to vertex position output for Renut. It
  aims to prevent arithmetic contraction/reassociation from causing different
  depth results in scene and velocity passes. This mechanism is a hypothesis,
  not an established explanation for the player's artifact.
- This document and `docs/progress-log.md`: evidence and test history.

`out/Motion Precision.app` is the current candidate. It uses the diagnostic
draw object with no depth override and a rebuilt shader cache. A comparison
against the existing cache found exactly 372 changed vertex shaders and zero
changed pixel shaders (1,634 pixel shaders total). Thus the blur sampling code
and strength are unchanged. The generated HLSL was modified locally to match
the proposed compiler patch; the dependency source in the main checkout was
not edited. This validates the candidate shader change, not yet a complete
build through the patched dependency workflow.

Shader compilation, cache compilation, linking, and startup passed. A sample
SPIR-V comparison shows 32 `NoContraction` decorations after adding `precise`,
versus zero before. Build commands, cache comparison, and runtime provenance
are under `out/precise-position/`; runtime log is
`out/Motion Precision.app/Contents/MacOS/logs/los_001.log`. Visual acceptance
still requires both Wargs and the horse-knockoff cutscene in this candidate.

## Root-cause instrumentation, 2026-11-09

The player replayed both scenes with normal settings in `los_009.log` and
reported both still terrible. The periodic draw/resolve trace shows the
velocity shader `CFA9F37557D702F5` rendering to RGBA8 UNORM at 1280x720,
resolving to `B54204C0`, then being sampled by `7838FCCDD9A82460`. Its
depth control is `00700722`: depth test enabled, no depth writes, EQUAL.
This trace samples one frame every 30 seconds and does not establish that
the exact horse-knockoff moment was traced.

An opt-in `LOS_MOTION_DIAG` path in `render/draw.cpp` saves raw guest float
constants and reads the velocity render target every 120 frames. It leaves
the normal blur settings untouched. Files go to an existing directory named
by that environment variable. The existing readback helper writes XRGB8
bytes, with forced alpha 255, so these captures do **not** measure the fourth
velocity channel. Constants are before shader-literal overlays; unused lanes
may contain unrelated values and are not evidence of corrupt inputs.

The isolated `out/Motion Data.app` is linked from the existing main checkout's
`out/mac` objects (main `95fc9d1`) with only the worktree's diagnostic draw
object substituted. The other source delta from this branch is the already
committed FPS display work; it is not included in this worktree's diff.
Compile/link commands and logs live in `out/motion-diag-build`. Runtime
libraries match the original test app by SHA-256. The original test app,
its Off launcher, and the regular app were not replaced. A bundle-signing
attempt failed because of the runtime folder layout; direct launch of the
linker's original signed executable succeeded. Initial startup attempts
failed before rendering; only `Motion Data.app/.../logs/los_003.log` is the
successful instrumented replay.

The player completed that replay. It produced 25 velocity images and 132
constant snapshots under `out/motion-captures`. The camera inverse/current
matrices approximately cancel (maximum element error below 0.0012 in these
snapshots), and the blur power is normally 0.005. There is no demonstrated
bad matrix or strength constant yet. The captured gameplay velocity image
has unexpectedly sparse nonzero pixels, while the menu book exhibits
triangle-shaped holes. Because the velocity shader writes a nonzero bias
even for stationary surfaces, depth rejection is a concrete suspect.
Capture sparsity alone is not proof: geometry/visibility and the missing
alpha channel still constrain interpretation.

`LOS_MOTION_DEPTH_TEST=always` or `lequal` changes only this velocity shader's
EQUAL test, for a causal diagnostic. The default preserves the original
comparison. ALWAYS deliberately ignores occlusion and must not be shipped
as a fix. Whether the underlying mismatch is depth precision, transformed
geometry, or something else remains unproven.

The ALWAYS comparison launched successfully (`Motion Data.app` log
`los_004.log`, `[motion] diagnostic velocity depth comparison always`). Its
menu-book captures have continuous silhouette coverage, unlike the holes in
the original capture. These are different runs/poses, not a pixel-matched
pair. The application was closed normally at 19:21:19. The reproducible
local launchers `out/Motion Data.command` and
`out/Motion Velocity Depth Always.command` now use the verified direct-launch
method and explicitly select their diagnostic mode.

At the player's request, relaunched the same ALWAYS comparison, recorded in
`Motion Data.app/Contents/MacOS/logs/los_005.log`. This log confirms the
game's own engine values and the velocity-only ALWAYS comparison. The player
reports "ok seems like both are ok now" for Wargs and the horse-knockoff
cutscene. This supports depth rejection in the velocity pass as a cause of
the reported artifact: bypassing it improves both while retaining blur.
The exact numerical or geometry mismatch behind that rejection is still
unknown. A final fix must restore reliable velocity coverage while retaining
occlusion; the current ALWAYS experiment is not that fix. Regular-app
configuration and default renderer behavior remain unchanged.

## Corrected player verification plan, 2026-11-09

The affected tumble is in the cutscene when Gabriel is knocked off the horse.
It is not the player-controlled dodge/tumble. Acceptance requires that cutscene
and Warg motion both look natural, retaining appropriate blur.

Player results:

| Run | Warg motion | Horse-knockoff cutscene tumble |
| --- | --- | --- |
| Original configuration (player's prior observation) | Correct | Wrong |
| Forced `RT_PixelMotionBlur=1` (`los_005.log`) | Terrible | Terrible |
| No override, game-controlled (`los_006.log`) | Wrong | Wrong |
| Forced `RT_PixelMotionBlur=0` (`los_008.log`) | Looks perfect (player report) | Looks perfect (player report) |
| Blur enabled, velocity depth ALWAYS (`Motion Data.app` `los_005.log`) | Seems OK (player report) | Seems OK (player report) |

The player reported "that looks perfect" after the Off run and the explicit
request to assess both Wargs and the horse-knockoff cutscene. Record this as
a pass for both tested visuals. `los_008.log` confirms Vulkan/Renut and
`RT_PixelMotionBlur: 1 -> 0`. The Off launcher preserves this working test
configuration; the regular app has not been changed.

This isolates the objectionable output to the pixel-motion-blur path in these
tested scenes. It does not establish whether the root cause is the original
effect, incorrect velocity/depth inputs, translated shaders, or another
implementation detail. It also does not validate later cutscenes or the full
campaign. Disabling this pass is a confirmed visual workaround for this test,
not yet a renderer-correctness fix.

The previous instructions to perform gameplay tumbles were incorrect and are
superseded. The forced-on run is an intervention, not an original baseline:
`EngineVarsOnSwap` rewrites the setting at each Swap, and the worker reinforces
it every 250 ms. A startup default of 1 does not establish that the game keeps
it enabled throughout gameplay or a cutscene. We have not yet measured whether
this switch changes at the horse-knockoff transition.

The regular and diagnostic app's executable, runtime library, and Xenos library
were compared on November 9 and are byte-identical by SHA-256. This removes a
binary difference between those two local apps as an explanation for the
comparison, but does not identify the cause of either visual problem.

1. Completed the no-override run (`los_006.log`, game's own values). The
   player reports both Wargs and the horse-knockoff cutscene still wrong.
   The earlier good-Warg observation has not been reproduced. Forced-on alone
   does not explain the problem, and the earlier good case remains unresolved.
2. Completed forced-off (`los_008.log`): the player reports the result looks
   perfect for the requested comparison. Keep this configuration as the
   accepted visual reference for both scenes.
3. If needed, use Xenos with original engine settings as a reference at the
   same cutscene. Do not equate its slower presentation with correct motion.
   Trace the game-controlled blur settings and velocity inputs around the
   horse-knockoff transition before selecting a targeted repair.
4. A proposed fix must preserve the original Warg appearance and correct the
   cutscene. Ordinary gameplay running/dodging is a regression check only.

Use fixed settings per run, one game instance, and player reports after each
run. Do not use the timed Compare launcher for these acceptance checks.
Local launchers omit empty command-line assignments because this executable's
parser can consume the next option as their value. The diagnostic TOML keeps
`los_engine_vars` and `los_engine_vars_cycle` empty and `los_engine_vars_ab=0`.
The Original launcher supplies none of these flags; the On/Off launchers
explicitly override only their intended nonempty settings.

## Report and isolation

The initial November 7 report was interpreted as a running artifact. The
player clarified on November 9 that the bad character tumble occurs in the
horse-knockoff cutscene; original Warg blur looks correct. Investigation is
on `codex/movement-artifacts`, based on fork `main` at `71a12c5`. Uncommitted
FPS/menu/build changes in the main checkout and the separate Metal worktree
are outside this investigation.

The local `out/Movement Investigation.app` reuses a copy of the existing
packaged executable and runtime libraries. It is **not a fresh build of this
branch**. Its executable SHA-256 is recorded in `out/runtime-provenance.json`.
It has a distinct bundle ID and its own configuration, logs, and copied profile
under `out/user-data`. The game assets are referenced externally. Launchers pass
`user_data_root` on the command line because ReXApp resolves paths before
loading the TOML file.

## Evidence

- `out/Movement Investigation.app/Contents/MacOS/logs/los_001.log` reports
  Vulkan on Apple M2 and internal resolution 1280x720 (1x).
- The same run's `los_engine_vars_dump` reports `RT_PixelMotionBlur` as an
  integer, current/default value 1. Associated float defaults are
  `RT_PixelMotionBlurValue=1`, `RT_PixelMotionBlurVelFPS=25`,
  `RT_PixelMotionBlurVelScale=5`, and `RT_PixelMotionBlurVelPower=0.005`.
  These are observed values, not evidence that a particular value is wrong.
- The existing extracted shader manifest identifies
  `ps_7838fccdd9a82460` as `rendertarget/pixelmotionblur` and
  `ps_cfa9f37557d702f5` as `rendereffect/pixelmotionblur`.
- Inspection of their locally generated HLSL shows the latter computes a
  velocity encoding from projected positions. The former combines velocity
  and deformation inputs with camera reprojection, then averages eleven
  scene samples. A wrong velocity input or the intended blur itself could
  therefore produce a movement-dependent trail.
- `renut_vsync` defaults to true in `backend/present.cpp`; the regular app's
  TOML does not override it. This and the reported localized double image make
  motion blur a better first test than changing presentation timing. Neither
  observation rules out a renderer fault.

## Runtime validation

The second isolated run (`los_002.log`) logged `RT_PixelMotionBlur: 1 -> 0`
at the first disabled phase and returned to the original-values phase after
another 30 seconds. Startup and menu rendering continued. This verifies that
the test switch is accepted; that startup check did not visually compare the reported artifact. Player
results from the later forced-on run are recorded above. Phase FPS numbers are not benchmarks: the run crossed
startup/menu scenes while another game instance was active.

## Controlled comparison

The existing `los_engine_vars` mechanism can disable the game's own pass
without changing renderer code, shaders, or gameplay translation:

```toml
los_engine_vars = "RT_PixelMotionBlur=0"
los_engine_vars_ab = 0
los_engine_vars_cycle = ""
```

For an explicit on/off cycle, use:

```toml
los_engine_vars = ""
los_engine_vars_cycle = "RT_PixelMotionBlur=1|RT_PixelMotionBlur=0"
los_engine_vars_ab = 30
```

The worker starts eight seconds after launch; phase changes and applied values
are logged with `[engine vars]`. Cycle timing begins at worker startup, not
when gameplay starts. Test settings supersede menu shadow settings, so keep
the comparison at the original full-shadow defaults. This session has not
changed the regular app's settings.

Local launchers in `out/` select **Motion Blur Original**, **Motion Blur On**, **Motion Blur Off**, or
**Motion Blur Compare**. Quit the diagnostic instance before changing modes.
They explicitly select Renut and the copied profile. Run the same route and
turns in the same scene, observing the character's silhouette and weapon.
Use only one running game when judging frame pacing or performance.

Removing the trail with the pass off would implicate the motion-blur path,
but would not prove its implementation is correct. If that test succeeds,
compare velocity inputs and the same pass against Xenos before deciding
whether to offer a preference or repair a renderer defect. If it fails,
investigate scene blur and per-draw vertex/constant snapshots separately.

No game-derived artifacts are included in Git. No renderer fix is claimed. Player observations are recorded separately above.
