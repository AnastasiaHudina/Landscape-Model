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

// Fresnel-Schlick (используется для точечных и направленных источников)
// Формула: F = F0 + (1 - F0) * (1 - V·H)^5
// F0 – базовое отражение при перпендикулярном взгляде
float3 F_Schlick(float3 F0, float VdotH)
{
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

// Fresnel-Schlick (используется для IBL с учётом шероховатости)
// Аппроксимация для IBL: 
//   - вместо (1 - F0) используется (max(1 - roughness, F0) - F0)
//   - вместо V·H используется N·V
// Формула: F = F0 + ( max(1 - roughness, F0) - F0 ) * (1 - cosTheta)^5
float3 FresnelSchlickRoughness(float3 F0, float roughness, float3 V, float3 N)
{
    float cosTheta = max(dot(V, N), 0.0); // косинус угла между направлением взгляда (V) и нормалью (N)
    float3 F = F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
    return F;
}

// Вспомогательная функция для получения F0 из параметров материала
// Для диэлектриков F0 = dielectricF0 (обычно 0.04)
// Для металлов F0 = albedo (цвет металла)
float3 GetF0(float3 albedo, float metalness, float dielectricF0)
{
    return lerp(dielectricF0.xxx, albedo, metalness);
}

#endif //PBR_HLSL