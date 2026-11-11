#!/usr/bin/env python3
"""Opt-in, hash-guarded 21-tap LoS motion-blur quality experiment.

Evidence: horse-fall GPU capture 2026-11-09, draw 4390, guest pixel shader
7838FCCDD9A82460. Its eleven scene samples span up to about 71 pixels in the
object-motion region. Replace only the final sampling kernel, preserving its
endpoints, input motion, deformation, camera reconstruction, and output tests.
All translated game shader text remains in the ignored build directory.
"""
import argparse
import hashlib
from pathlib import Path

SHADER = "ps_7838fccdd9a82460.hlsl"
START = "\tr10.xyzw = r0.wzwz * c255.zzww + r9.xyxy;"
END = "\talphaTest(oC0.w, g_AlphaTestFunc, g_AlphaThreshold);"
ORIGINAL_KERNEL_SHA256 = "dc2ed31b56b6611d5a573a12c22e89701f01419049736ce64e1f917b92cb552c"
KERNEL = """\t// LOS_RENUT_DENSE_MOTION_BLUR: same endpoints, 21 uniform samples.
\tconst float2 losBlurExtent = r0.wz * c253.zz;
\tfloat4 losBlurSum = 0.0;
\t[loop]
\tfor (uint losBlurSample = 0; losBlurSample < 21; ++losBlurSample) {
\t\tconst float losBlurPosition = -1.0 + float(losBlurSample) * (2.0 / 20.0);
\t\tlosBlurSum += tfetch2D(SceneSampler_Texture2DDescriptorIndex,
\t\t\tSceneSampler_SamplerDescriptorIndex, r9.xy + losBlurExtent * losBlurPosition,
\t\t\tfloat2(0, 0)).xyzw;
\t}
\t// Retain the original kernel's total weight, including its literal constant.
\toC0.xyzw = losBlurSum * ((11.0 / 21.0) * c253.xxxx);
"""


def rewrite(source):
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError("Unexpected blur shader structure; refusing to patch")
    start = source.index(START)
    end = source.index(END, start)
    original = source[start:end]
    if hashlib.sha256(original.encode()).hexdigest() != ORIGINAL_KERNEL_SHA256:
        raise ValueError("Original blur kernel changed; review it before updating this experiment")
    return source[:start] + KERNEL + source[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hlsl_directory", type=Path)
    args = parser.parse_args()
    shader = args.hlsl_directory / SHADER
    result = rewrite(shader.read_text())
    shader.write_text(result)
    print(f"Dense motion blur: {SHADER}, 11 -> 21 samples, unchanged endpoints")


if __name__ == "__main__":
    main()
