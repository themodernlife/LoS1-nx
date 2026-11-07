float4 main(uint id : SV_VertexID) : SV_Position {
    const float2 positions[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    return float4(positions[id], 0, 1);
}
