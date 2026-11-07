# Experimental native Metal renderer

The macOS build now includes an opt-in plume Metal backend. The rendering path is:

```text
Recompiled game -> Renut -> plume Metal -> Apple Metal
```

Renut reuses its existing shader cache. SPIRV-Cross translates the cached SPIR-V
into Metal Shading Language, and Metal compiles each shader on first use. SPIR-V
is a shader intermediate here; draw, compute, resource, and presentation calls go
directly to Metal. DXMT is not used. Vulkan/MoltenVK remains packaged for the
existing fallback, and normal macOS launches still default to Vulkan.

## Build and launch the isolated test app

Use the separate investigation worktree. The main checkout and its app are
independent. Follow [macOS setup](macos-build.md) for prerequisites and game data.

1. Build and run the GPU regression test:

   ```sh
   cd /Users/themodernlife/Development/lords-of-shadow-dxmt-investigation
   LOS_FPS_EXPERIMENT=ON tools/macos/build.sh
   tools/macos/metal.sh smoke
   ```

2. Close any previous Metal test app, then package it:

   ```sh
   tools/macos/metal.sh package
   ```

3. Launch it:

   ```sh
   tools/macos/metal.sh play
   ```

The application is `out/Lords of Shadow Metal.app`, with its own bundle identifier.
Its configuration is `Contents/MacOS/los.toml` inside that app. It selects
`renut_gpu_backend = "metal"` and puts saves in `out/metal-user-data/los`, including
when launched from Finder. Packaging preserves an existing configuration.
Close the app before rebuilding its package or changing backends.

The build above enables the FPS overlay at every launch, even when an existing
configuration has it disabled. It shows game FPS and average milliseconds per
frame over half-second windows. The label is `Renut` for both native Metal and
Vulkan; use the renderer log to confirm which backend is active. F5 opens the
port menu, where the counter can be hidden for the current session.

For an existing build, enable the overlay without changing the toolchain:

```sh
cmake -S app -B out/mac -DLOS_FPS_EXPERIMENT=ON
cmake --build out/mac --target los -j 4
tools/macos/metal.sh package
```

To test the Vulkan fallback using the same binary and isolated profile:

```sh
tools/macos/metal.sh play --renut_gpu_backend=vulkan
```

The command-line override lasts for that launch. The next ordinary launch uses
the backend in the configuration. Normal `tools/macos/package.sh` and `play.sh`
retain their existing behavior. `LOS_METAL_BACKEND=OFF tools/macos/build.sh`
excludes the native integration at build time; the default is ON.

For Metal API validation, close the test app and use:

```sh
MTL_DEBUG_LAYER=1 tools/macos/metal.sh play
```

Runtime logs are in the app's `Contents/MacOS/logs/`. Confirm the line
`Renderer: Metal on Apple M2` (or your GPU name). Compiler/validation output is
appended to `out/metal-game-stderr.log`. Failed MSL compilations save their source
under `out/metal-shader-errors/`. Those generated shaders stay local and ignored.

## Implementation

- `backend/metal_backend.cpp` translates shaders, restores D3D clip-space Y,
  maps descriptor sets 0 through 7 and push constants at Metal buffer 8, and
  pads sparse MSL argument-buffer bindings to match plume's resource table.
  It also supplies native memory reporting, texture/pipeline checks, and blend
  constants. The existing SDK-pinned SPIRV-Cross target is reused.
- The tracked `patches/renut-deps/plume-native-metal.patch` adds source-library
  shader construction, dynamic constant-buffer descriptors with per-draw argument
  snapshots, blend constants, texture-to-buffer readback, clear flushing before
  blits, texture-view usage/stencil-plane handling, and corrected drawable
  ownership/slot advancement. Fetching dependencies
  applies it; macOS Metal CMake configuration also applies it to existing copies.
- The standalone smoke test uses the same shader translation code as the game.
  It verifies a clear-only readback and two textured draws with different constant
  offsets and a sparse binding. All 8,192 output pixels must match.
- The existing SDL Cocoa window/CAMetalLayer integration supplies native
  presentation without an additional window bridge.

## Stutter diagnostics

The opening-village investigation reproduced both drawable-acquisition waits and
long gaps between guest frames. A drawable lifetime/index correction is included,
but the before/after capture did not demonstrate a stutter fix. See the
[measurements and opt-in capture instructions](macos-metal-stutter.md).

## Validation on 2026-11-07

The latest build includes fork main's FPS update at `95fc9d1` with
`LOS_FPS_EXPERIMENT=ON`. The ARM64 build, 8,192-pixel test, and packaging passed.
The FPS and average ms/frame overlay visibly updated during a windowed startup
run, with logs confirming native Metal on Apple M2 and no new API validation
failures. This did not repeat full gameplay or establish a performance comparison.

An earlier rebase brought the experiment onto fork `origin/main` at `71a12c5`.
The ARM64 game build and the 8,192-pixel Metal validation test passed again, and
the isolated app was repackaged with the latest icon and its existing configuration.
The gameplay and Vulkan checks below describe the original prototype run; they
were not repeated after the rebase.

Apple M2, macOS 15.6, ARM64 release build:

- Full application build succeeded.
- Metal API validation: 8,192 regression-test pixels, zero mismatches.
- Game run with Metal API validation: logos, title book, menus, opening village,
  movement, direct/area attacks, jumping, and pause/resume were exercised. Rain,
  shadows, lighting, HUD, and combat effects were visible. Save data and its
  thumbnail were created in the isolated profile.
- No shader compilation errors or Metal API validation failures appeared in the
  successful six-minute run. An earlier launch exposed the missing pixel-format
  view usage flag; that was fixed before this run.
- The same executable was launched with Vulkan and visually reached the title
  screen, confirming the fallback still works.
- The dependency patch applies cleanly to the saved pinned source and reproduces
  the tested files. Packaging and shell syntax checks passed.

This is an experimental backend, not a full-campaign validation or a benchmark.
There is no claimed speedup. First-use shader/pipeline compilation can hitch;
there is no application-managed persistent Metal library/pipeline cache yet.
Both native Metal and the existing MoltenVK device report no geometry shaders,
so Renut keeps its existing rectangle-list/point fallback. Later effects and
levels, Disc 2, controller input, and other hardware need validation. Non-macOS
platforms were not rebuilt for this change.
