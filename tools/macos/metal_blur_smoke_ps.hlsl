#include "motion_blur_filter.hlsli"

[[vk::binding(0, 0)]] Texture2D<float4> textures[2];
[[vk::binding(0, 3)]] SamplerState linearClamp;
[[vk::binding(3, 5)]] cbuffer DrawConstants {
    float2 stepUV;
    float2 inverseTargetSize;
    uint textureIndex;
};

float4 main(float4 position : SV_Position) : SV_Target {
    return losAdaptiveMotionBlur(textures[textureIndex], linearClamp,
                                 position.xy * inverseTargetSize, stepUV);
}
