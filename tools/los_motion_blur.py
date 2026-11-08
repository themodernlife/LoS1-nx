#!/usr/bin/env python3
"""Opt-in denser sampling for one translated motion-blur pixel shader.

Run after the optional early-alpha transformation and before shader compilation.
Only the sample accumulation is replaced. Velocity construction, projection,
deformation and output alpha processing remain in the translated source.
Generated game shader source stays in the build directory. This tool stores only
the expected kernel's digest and structural checks, not a copy of that source.
"""

import argparse
import hashlib
from pathlib import Path
import re
import sys


TARGET = "ps_7838fccdd9a82460.hlsl"
KERNEL_SHA256 = "bcc37498ede5223b56823cd9c9778b6c4d59abc656cb351d2a262b1cf89e20d4"
MARKER = "// LOS_ADAPTIVE_MOTION_BLUR: authored sampling helper."
KERNEL = re.compile(
    r"^[ \t]*r10\.xyzw[ \t]*=[^\n]*\n.*?^[ \t]*oC0\.xyzw[ \t]*=[^\n]*;\n",
    re.MULTILINE | re.DOTALL,
)
ENTRY = '#ifndef __spirv__\n[shader("pixel")]\n#endif\nvoid main('


def transform(source: str, helper: str) -> str:
    """Fail closed if the translator changes the recognized accumulation."""
    if not source.startswith("// pixel shader  hash=0x7838FCCDD9A82460 "):
        raise ValueError("unexpected shader identity")
    if MARKER in source:
        raise ValueError("already transformed; regenerate the HLSL dump first")
    matches = list(KERNEL.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"expected one accumulation region, found {len(matches)}")
    match = matches[0]
    kernel = match.group()
    fetches = re.findall(
        r"tfetch2D\(SceneSampler_Texture2DDescriptorIndex,\s*SceneSampler_SamplerDescriptorIndex,",
        kernel,
    )
    sums = re.findall(r"^\s*r\d+\.xyzw = r\d+\.xyzw \+ r\d+\.xyzw;", kernel, re.MULTILINE)
    canonical = "\n".join(line.strip() for line in kernel.splitlines() if line.strip())
    if len(fetches) != 11 or kernel.count("tfetch2D(") != 11 or len(sums) != 10:
        raise ValueError("expected eleven scene fetches and ten accumulation sums")
    if hashlib.sha256(canonical.encode()).hexdigest() != KERNEL_SHA256:
        raise ValueError("accumulation digest mismatch; review the new translator output")
    if source.count(ENTRY) != 1:
        raise ValueError("expected one pixel entrypoint declaration")
    if helper.count("float4 losAdaptiveMotionBlur(") != 1:
        raise ValueError("unexpected adaptive helper declaration")

    # Sampler slot zero is Renut's permanent linear-clamp sampler. Resource
    # lookup retains the actual scene descriptor and its texture-view swizzle.
    replacement = (
        "\toC0.xyzw = losAdaptiveMotionBlur(\n"
        "\t\tgetTexture2D(SceneSampler_Texture2DDescriptorIndex), "
        "g_SamplerDescriptorHeap[0], r9.xy, r0.wz);\n"
    )
    result = source[:match.start()] + replacement + source[match.end():]
    return result.replace(ENTRY, MARKER + "\n" + helper.rstrip() + "\n\n" + ENTRY, 1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path, help="directory of translated ps_*.hlsl files")
    parser.add_argument(
        "--helper", type=Path,
        default=Path(__file__).resolve().parents[1] / "app/src/renut_engine/gpu/shaders/motion_blur_filter.hlsli",
    )
    args = parser.parse_args()
    target = args.folder / TARGET
    try:
        result = transform(target.read_text(encoding="utf-8"), args.helper.read_text(encoding="utf-8"))
        target.write_text(result, encoding="utf-8", newline="\n")
    except (OSError, ValueError) as error:
        print(f"los_motion_blur: {target}: {error}", file=sys.stderr)
        return 1
    print(f"los_motion_blur: adaptive sampling applied to {TARGET}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
