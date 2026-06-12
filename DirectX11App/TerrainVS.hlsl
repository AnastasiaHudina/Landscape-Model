cbuffer GeomBuffer : register(b0)
{
    float4x4 m;
    float4x4 normalM;
    float4 pbrParams; // x - roughness, y - metalness, z - unused, w - hasNormalMap
    float4 posAngle;
};

cbuffer SceneBuffer : register(b1)
{
    float4x4 vp;
    float4 cameraPos;
    float4 lightInfo; // x - light count, y - use normal maps, z - show normals, w - detail strength
    struct Light { float4 pos; float4 color; float intensity; float3 padding; };
    Light lights[10];
    float4 ambientColor;
    float4 flowInfo;   // x - use flow
    float4 renderModeInfo; // x - render mode
};

struct VSInput
{
    float3 pos : POSITION;
    float3 tangent : TANGENT;
    float3 norm : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 worldPos : POSITION;
    float3 tangent : TANGENT;
    float3 norm : NORMAL;
    float2 uv : TEXCOORD;
    nointerpolation float4 geomData : GEOM_DATA; // x - roughness, y - metalness, w - hasNormalMap
};

VSOutput main(VSInput vertex)
{
    VSOutput result;
    float4 worldPos = mul(float4(vertex.pos, 1.0), m);
    result.pos = mul(worldPos, vp);
    result.worldPos = worldPos.xyz;

    result.tangent = mul(float4(vertex.tangent, 0.0f), normalM).xyz;
    result.norm = mul(float4(vertex.norm, 0.0f), normalM).xyz;
    result.uv = vertex.uv;
    result.geomData = pbrParams; // передаём roughness (не используется), metalness (не используется), hasNormalMap
    return result;
}