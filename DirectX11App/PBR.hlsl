#ifndef PBR_HLSL
#define PBR_HLSL

#ifndef PI
#define PI 3.14159265358979323846
#endif

// Normal Distribution Function (GGX / Trowbridge-Reitz)
// Формула: D = a^2 / (pi * ( (N·H)^2 * (a^2 - 1) + 1 )^2 )
// a = roughness
// NdotH = max(dot(N, H), 0)
float D_GGX(float NdotH, float roughness)
{
    float a = roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Geometry Function (Schlick-GGX) для одного направления
// Формула: G_Schlick(NdotV) = NdotV / (NdotV * (1 - k) + k)
// где k = (roughness + 1)^2 / 8
float G_SchlickGGX(float NdotV, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith Geometry Function (произведение для двух направлений)
// G = G_Schlick(NdotV) * G_Schlick(NdotL)
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick
// Формула: F = F0 + (1 - F0) * (1 - V·H)^5
// F0 – базовое отражение при перпендикулярном взгляде
float3 F_Schlick(float3 F0, float VdotH)
{
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

// Вспомогательная функция для получения F0 из параметров материала
// Для диэлектриков F0 = dielectricF0 (обычно 0.04)
// Для металлов F0 = albedo (цвет металла)
float3 GetF0(float3 albedo, float metalness, float dielectricF0)
{
    return lerp(dielectricF0.xxx, albedo, metalness);
}

#endif //PBR_HLSL