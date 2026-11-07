# Direct Metal and DXMT feasibility

Original investigation: 2026-11-07. The assessment below records the baseline
before implementation. Work resumed at the user's request in the separate
`codex/dxmt-investigation` worktree, now based on macOS revision
`302b48887cf53e8df7897fc85824bb4bd98391b9`.

**Current status:** an opt-in native plume Metal prototype now builds and runs
the opening village. See [Native Metal testing](macos-metal.md) for implementation,
validation, launch instructions, and remaining limits. DXMT was not integrated.
No game-level speedup has been established.

## Baseline pipeline

```text
Recompiled ARM64 game
  -> Xbox 360 Direct3D hooks
  -> Renut resource, draw, resolve, and shader handling
  -> plume Vulkan backend
  -> Vulkan / MoltenVK
  -> Metal
```

There is no DXVK or vkd3d layer in this path. The game's embedded Xbox 360
Direct3D interface is intercepted by Renut; it is not a Windows D3D11 API
that can be redirected to another DLL. Windows builds have a separate plume
D3D12 backend, but the macOS build selects Vulkan unconditionally.

Evidence: `app/src/renut_engine/gpu/backend/backend.cpp` (`ResolveBackend`,
`CreateRenderInterface`), `backend/platform_macos.cpp`, and
`docs/renderer.md`. Shaders go from XenosRecomp's generated HLSL through DXC
to a compressed SPIR-V cache. The Windows path instead supports DXIL.

The existing performance baseline is in that revision's `docs/macos-build.md`:
about 59.57 FPS in the opening tutorial, and 51.59 to 59.17 FPS in three short
combat windows. These are previous measurements, not results of this study.
Removing MoltenVK does not by itself establish a performance improvement.

## DXMT has two materially different routes

The source inspected was upstream DXMT commit
[`647b0020ba5764e5593b12407afd76964fa11e20`](https://github.com/3Shain/dxmt/tree/647b0020ba5764e5593b12407afd76964fa11e20),
dated 2026-11-02. Its established advertised support is D3D10/11, but its
development source also includes D3D12, behind the disabled-by-default
[`enable_d3d12` option](https://github.com/3Shain/dxmt/blob/647b0020ba5764e5593b12407afd76964fa11e20/meson.options).
The D3D12 route should not inherit the maturity assessment of DXMT's D3D11
releases.

### D3D11: use DXMT's established API

```text
Game -> Renut -> new D3D11 backend -> native DXMT -> Metal
```

This needs a new host graphics backend and a different shader binding scheme.
Our plume copy has no D3D11 backend. The translated shaders target shader model
6.0 and use register spaces, unbounded texture arrays, and a renderer-wide
table of 8,192 textures and 256 samplers. DXMT's D3D11 path expects D3D11
shader bytecode and binding semantics. The existing SPIR-V or DXIL cache is
not a drop-in D3D11 shader cache.

A practical D3D11 design would bind the resources used by each draw, remap the
translated shader's fetches to those slots, and generate suitable DXBC. This
is a substantial renderer and shader-translator project. It is the least
attractive first experiment for this codebase.

Local evidence: `render/pipeline.cpp` (`BuildDrawLayout`), `backend/video.h`,
`app/thirdparty/reblue-XenosRecomp/XenosRecomp/shader_common.h`, and
`XenosRecomp/dxc_compiler.cpp`. Renderer paths above are relative to
`app/src/renut_engine/gpu/`.

### D3D12: reuse the existing D3D12 architecture

```text
Game -> Renut -> macOS adaptation of plume D3D12 -> native DXMT D3D12 -> Metal
```

This is the better DXMT-specific experiment because it preserves more of the
existing resource model and DXIL shader path. It still needs:

- An ARM64 native DXMT build and library linkage. Upstream documents
  [native Mach-O builds](https://github.com/3Shain/dxmt/blob/647b0020ba5764e5593b12407afd76964fa11e20/docs/DEVELOPMENT.md),
  so Wine is not inherently required. Its build requires LLVM 15 and the
  Xcode Metal toolchain.
- Porting plume's currently Windows-only D3D12 integration: native DirectX
  headers/ABI, D3D12MemoryAllocator, event/fence waits, adapter enumeration,
  and window presentation. Merely defining `_WIN32` on macOS would select
  incompatible platform code.
- A presentation bridge to the existing SDL Cocoa window and CAMetalLayer.
  Upstream DXMT's native
  [window implementation is headless](https://github.com/3Shain/dxmt/blob/647b0020ba5764e5593b12407afd76964fa11e20/src/util/wsi_window_headless.cpp),
  with a fixed reported size; its HWND-to-Metal-view path looks up Wine
  window-driver functions. A native library build does not establish a
  working SDL swapchain.
- Enabling DXIL generation/loading on macOS, which is currently excluded by
  the shader build and runtime's Windows guards.
- Resolving feature gaps before attempting broad gameplay. DXMT's D3D12
  [graphics pipeline](https://github.com/3Shain/dxmt/blob/647b0020ba5764e5593b12407afd76964fa11e20/src/d3d12/d3d12_pipeline_graphics.cpp)
  rejects geometry shaders, tessellation, and stream output. Renut has
  geometry-shader paths for rectangle lists and point sprites. Trace their
  actual use in LoS before deciding which replacements are needed.

DXMT's D3D12 `ResourceBarrier` implementation also says it currently forces
synchronization rather than using the application's barriers for optimization.
This is a reason to measure rather than assume a speedup.
[Source](https://github.com/3Shain/dxmt/blob/647b0020ba5764e5593b12407afd76964fa11e20/src/d3d12/d3d12_command_list.cpp)

## Plume Metal route assessed before implementation

```text
Game -> Renut -> plume Metal backend -> Metal
```

The pinned plume dependency already contains `plume_metal.cpp`, and the
current macOS build already produces its object in `out/mac/plume/libplume.a`.
Renut does not select this backend. It is the most natural first experiment
when the goal is specifically to remove the Vulkan runtime.

It is not ready to enable by changing the backend enum alone:

| Area | Required work |
| --- | --- |
| Shader library | Add a Metal library cache and loader. `MetalShader` accepts compiled Metal libraries; Renut currently loads SPIR-V or DXIL. |
| Resource bindings | Define a Metal binding contract. The current Vulkan route uses dynamic descriptor offsets; plume Metal inherits the unsupported default implementation. The alternative D3D12 root-descriptor operation also explicitly asserts in Metal. |
| Geometry shaders | Replace or emulate any rectangle/point expansion paths the game exercises. The Metal pipeline explicitly rejects geometry shaders. |
| Native backend helpers | Add Metal cases to resource/pipeline validation, memory reporting, blend constants, and device-loss handling. Several current helpers cast non-Windows objects to Vulkan types. |
| Readback | Implement or route around the missing texture-to-buffer copy described below. Audit game readback paths separately; the probe does not establish which path LoS uses. |
| Presentation | Verify that plume Metal's window/view expectations match our SDL CAMetalLayer integration, including resize and fullscreen. |

Plume's examples already demonstrate an offline HLSL -> SPIR-V -> MSL ->
metallib toolchain in `examples/cmake/PlumeShaders.cmake`. Using SPIR-V as an
offline shader intermediate would not introduce a Vulkan runtime or MoltenVK.
Renut's bindings, function constants, vertex fetch, and Y-coordinate conventions
still need explicit adaptation and validation.

### Local probe

An isolated ARM64 executable linked the existing plume archive directly to
Apple frameworks. Its linked libraries contain no Vulkan or MoltenVK library.
On the Apple M2 it created the Metal device, a 64x64 RGBA8 render target, a
command queue and fence, and submitted a clear with color (0.25, 0.5, 0.75, 1).

The generic plume texture-to-buffer `copyTextureRegion` call crashed. Source
inspection confirms that it handles buffer-to-texture and texture-to-texture,
but not texture-to-buffer. The latter falls through to a null destination
texture access in a release build.

After replacing only the probe's readback with a native Metal blit, the clear
passed pixel verification:

```text
Device: Apple M2
Readback: native Metal blit (plume readback unsupported)
First pixel: 64,128,191,255
Checked 4096 pixels; mismatched channels: 0
```

Local source and executable: `out/metal-probe/probe.cpp` and
`out/metal-probe/probe`. Passing an argument reproduces the unsupported plume
readback call. These scratch artifacts are ignored by git.

A separate attempt to compile a small `.metal` shader failed because the
Xcode Metal Toolchain component is absent, even though `xcrun --find metal`
resolves a launcher. No toolchain was installed during this study. This means
custom shader rendering was not tested; the successful result covers the
clear, queue completion, and native blit readback only. DXMT was inspected
but not built or run.

## Original recommended experiment sequence

1. Keep the working Renut/Vulkan build as the comparison baseline. Gather
   representative draw traces, including shadows, post-processing, particles,
   and readbacks, to establish required features.
2. For a DXMT-specific prototype, first build native ARM64 DXMT D3D12 at a
   pinned commit and validate an offscreen textured draw with DXIL, descriptor
   tables, depth, blending, and fence/readback behavior. Do not begin with a
   complete D3D11 rewrite. Separately prove native presentation.
3. For a Vulkan-free renderer with fewer new dependencies, extend the existing
   plume Metal probe through translated game shaders and the binding contract,
   then wire an opt-in `metal` backend into Renut. Add backend checks before
   permitting existing helpers to receive Metal objects.
4. Advance either route to gameplay only after the shader/resource tests pass.
   Compare identical scenes and settings: CPU and GPU frame time, frame-time
   spikes, warm/cold shader caches, memory, image correctness, and loading.
   Average FPS near the game's cap will hide improvements and regressions.

The original research phase changed no game renderer. The subsequent native
Metal implementation and its isolated test app are documented separately.
