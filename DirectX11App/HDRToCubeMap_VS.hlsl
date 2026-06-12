cbuffer CubeMapCB : register(b0)
{
    float4x4 invViewProj;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localDir : TEXCOORD0;
};

PS_INPUT main(uint vertexId : SV_VertexID)
{
    PS_INPUT o;

    float2 pos[4] =
    {
        float2(-1.0,  1.0),
        float2(1.0,  1.0),
        float2(-1.0, -1.0),
        float2(1.0, -1.0)
    };

    float2 p = pos[vertexId];

    o.position = float4(p, 0.0, 1.0);

    float4 clip = float4(p, 1.0, 1.0);
    float4 world = mul(clip, invViewProj);

    o.localDir = normalize(world.xyz / world.w);

    return o;
}