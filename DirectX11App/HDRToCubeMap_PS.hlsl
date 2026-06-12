Texture2D tex : register(t0);
SamplerState smplr : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localDir : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 dir = normalize(input.localDir);
    float2 uv = float2(
        0.5 + atan2(dir.x, dir.z) / (2.0 * 3.1415926535),
        0.5 - asin(dir.y) / 3.1415926535
    );

    uv.x = frac(uv.x); // горизонтальная цикличность HDR panorama
    uv.y = clamp(uv.y, 0.001, 0.999); // только вертикально clamp
    float4 color = tex.Sample(smplr, uv);   
    return color;
    // для отладки скайбокса
    //return float4(normalize(input.localDir) * 0.5 + 0.5, 1.0);
}

