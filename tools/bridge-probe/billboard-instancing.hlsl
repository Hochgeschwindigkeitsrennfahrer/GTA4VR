// #013 standalone feasibility probe, not a replacement game shader.
// One compact instance provides center, two half axes and color. The GPU
// expands the shared unit quad; the same instance buffer serves both eyes.
row_major float4x4 ViewProjection : register(c0);

struct Result {
    float4 position : POSITION0;
    float4 color : COLOR0;
};

Result InstancedVS(float2 corner : POSITION0, float4 center : TEXCOORD0,
                   float4 right : TEXCOORD1, float4 up : TEXCOORD2,
                   float4 color : COLOR0) {
    Result output;
    float3 world = center.xyz + corner.x * right.xyz + corner.y * up.xyz;
    output.position = mul(float4(world, 1), ViewProjection);
    output.color = color;
    return output;
}

Result ExpandedVS(float3 world : POSITION0, float4 color : COLOR0) {
    Result output;
    output.position = mul(float4(world, 1), ViewProjection);
    output.color = color;
    return output;
}

float4 ColorPS(Result input) : COLOR0 {
    return input.color;
}
