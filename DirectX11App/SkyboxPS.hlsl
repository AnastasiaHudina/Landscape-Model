TextureCube skyboxTexture : register(t9);
SamplerState skyboxSampler : register(s0);

struct PS_INPUT
{
    float4 pos : SV_POSITION;  // Позиция экрана
    float3 dir : TEXCOORD0;    // Направление для кубмапы
};


float4 main(PS_INPUT input) : SV_TARGET
{
    // Нормализуем направление
    float3 dir = normalize(input.dir);

    float3 color = skyboxTexture.Sample(skyboxSampler, dir).rgb;

    return float4(color, 1.0f);
}

/*
// отладка скайбокса
float4 main(PS_INPUT input) : SV_TARGET
{
    float3 dir = normalize(input.dir);
    return float4(dir * 0.5 + 0.5, 1.0);
}
*/