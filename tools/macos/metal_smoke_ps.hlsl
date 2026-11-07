[[vk::binding(0, 0)]] Texture2D<float4> textures[2];
[[vk::binding(3, 5)]] cbuffer DrawConstants {
    float4 tint;
    uint textureIndex;
};
float4 main() : SV_Target {
    return textures[textureIndex].Load(int3(0, 0, 0)) * tint;
}
