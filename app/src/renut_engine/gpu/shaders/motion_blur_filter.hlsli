#ifndef LOS_MOTION_BLUR_FILTER_HLSLI
#define LOS_MOTION_BLUR_FILTER_HLSLI

// Keep the supplied velocity and shutter interval. Add samples when that
// interval covers more pixels so fast motion forms a continuous blur instead
// of separated copies. The upper bound limits work for extreme velocities.
float4 losAdaptiveMotionBlur(Texture2D<float4> scene, SamplerState linearClamp,
                             float2 uv, float2 stepUV)
{
    uint width, height;
    scene.GetDimensions(width, height);
    float2 stepPixels = abs(stepUV * float2(width, height));
    float spanPixels = 10.0 * max(stepPixels.x, stepPixels.y);
    if (spanPixels == 0.0)
        return scene.SampleLevel(linearClamp, uv, 0.0);

    uint sampleCount = (uint)clamp(ceil(spanPixels) + 1.0, 11.0, 257.0);
    float4 total = 0.0;
    [loop]
    for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        float offset = -5.0 + 10.0 * float(sampleIndex) / float(sampleCount - 1);
        total += scene.SampleLevel(linearClamp, uv + offset * stepUV, 0.0);
    }
    return total / float(sampleCount);
}

#endif
