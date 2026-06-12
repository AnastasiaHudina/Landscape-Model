#ifndef PI
#define PI 3.14159265358979323846
#endif

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 localDir : TEXCOORD0;
};

TextureCube tex : register(t0);
SamplerState smplr : register(s0);

cbuffer IRRConstantBuffer : register(b0)
{
    int4 params;   // params.x = Nphi, params.y = Ntheta (например 64, 32)
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 N = normalize(input.localDir);

    // Построение касательного базиса
    float3 up = abs(N.y) < 0.999 ? float3(0,1,0) : float3(1,0,0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    int Nphi = max(params.x, 1);
    int Ntheta = max(params.y, 1);

    float3 irradiance = float3(0,0,0);
    float dphi = 2.0 * PI / Nphi;
    float dtheta = (PI / 2.0) / Ntheta;   // только полусфера

    for (int i = 0; i < Nphi; i++)
    {
        float phi = i * dphi;
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);

        for (int j = 0; j < Ntheta; j++)
        {
            float theta = (j + 0.5) * dtheta;   // срединная точка интервала
            float cosTheta = cos(theta);
            float sinTheta = sin(theta);

            // Направление в касательном пространстве
            float3 L_local = float3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
            // Переход в мировое пространство
            float3 L = normalize(L_local.x * T + L_local.y * B + L_local.z * N);

            float NdotL = max(dot(N, L), 0.0);
            if (NdotL > 0.0)
            {
                float3 radiance = tex.SampleLevel(smplr, L, 0).rgb;
                float weight = sinTheta * dtheta * dphi;   // элемент телесного угла
                irradiance += radiance * NdotL * weight;
            }
        }
    }

    // Нормировка: телесный угол полусферы = 2pi, но мы уже учли вес, поэтому просто возвращаем
    //???
    
    // Делим на pi, чтобы получить корректную облучённость для Ламбертова BRDF
    irradiance = irradiance / PI;   
    return float4(irradiance, 1.0);
}