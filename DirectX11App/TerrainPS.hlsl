#include "PBR.hlsl"

Texture2D colorTexture : register(t0);
Texture2D normalMapTexture : register(t1);
Texture2D detailTexture : register(t2);
Texture2D flowTexture : register(t3);
Texture2D roughnessTexture : register(t4);   // карта шероховатости
Texture2D metalnessTexture : register(t5);   // карта металличности (необязательно)

SamplerState colorSampler : register(s0);

cbuffer SceneBuffer : register(b1)
{
    float4x4 vp;
    float4 cameraPos;
    float4 lightInfo; // x - lightCount, y - useNormalMaps, z - showNormals, w - detailStrength
    struct Light { float4 pos; float4 color; float intensity; float3 padding; };
    Light lights[10];
    float4 ambientColor;
    float4 flowInfo;   // x - useFlow
    float4 renderModeInfo; // x - renderMode
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

float4 main(VSOutput pixel) : SV_Target0
{
    // Сэмплируем текстуры
    float3 albedo = colorTexture.Sample(colorSampler, pixel.uv).rgb;
    float3 detail = detailTexture.Sample(colorSampler, pixel.uv).rgb;
    float3 flow = flowTexture.Sample(colorSampler, pixel.uv).rgb;
    //float roughness = 0.5; // вместо texture sample для проверки бликов!!
    //float roughness = roughnessTexture.Sample(colorSampler, pixel.uv).r; // берём красный канал
    float roughness = 1.0 - roughnessTexture.Sample(colorSampler, pixel.uv).r; // инвертируем!!
    //roughness = pow(roughness, 0.3); // делает значения ближе к 1
    //roughness = max(roughness, 0.5);  // Ограничиваем минимальное значение roughness - применяем минимальный roughness для гладких участков, чтобы не было слишком зеркальных поверхностей
    roughness = clamp(roughness, 0.0001, 1.0); // предотвращаем деление на ноль
    float metalness = 0.0; // пока константа, позже можно из текстуры

    // Применяем детали (опционально)
    float detailStrength = lightInfo.w;
    albedo = albedo * (1.0 + (detail.r - 0.5) * detailStrength);

    // Получаем нормаль (с учётом карты нормалей)
    float3 N = normalize(pixel.norm);
    if (lightInfo.y > 0.5 && pixel.geomData.w > 0.5)
    {
        float3 texNormal = normalMapTexture.Sample(colorSampler, pixel.uv).rgb;
        texNormal = texNormal * 2.0 - 1.0;
        float3 bitangent = normalize(cross(pixel.tangent, pixel.norm));
        float3x3 TBN = float3x3(pixel.tangent, bitangent, pixel.norm);
        N = normalize(mul(texNormal, TBN));
    }

    // Направление взгляда
    float3 V = normalize(cameraPos.xyz - pixel.worldPos);

    // Вычисление F0
    float dielectricF0 = 0.04;
    float3 F0 = GetF0(albedo, metalness, dielectricF0);

    float3 color = float3(0,0,0);

    // Ambient (упрощённый)
    float3 ambient = ambientColor.rgb * albedo * (1.0 - metalness);
    color += ambient;

    // Основное освещение от источников
    for (int i = 0; i < (int)lightInfo.x; i++)
    {
        float3 L = normalize(lights[i].pos.xyz - pixel.worldPos);
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            float dist = length(lights[i].pos.xyz - pixel.worldPos);
            float attenuation = 1.0 / (dist * dist);
            float3 radiance = lights[i].color.rgb * lights[i].intensity * attenuation;

            float D = D_GGX(NdotH, roughness);
            float G = G_Smith(NdotV, NdotL, roughness);
            float3 F = F_Schlick(F0, VdotH);

            float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
            float3 kS = F;
            float3 kD = (1.0 - kS) * (1.0 - metalness);

            color += (kD * albedo / PI + specular) * radiance * NdotL;
        }
    }

    // Применение Flow (как у вас было)
    if (flowInfo.x > 0.5)
    {
        float flowLuminance = dot(flow, float3(0.299f, 0.587f, 0.114f));
        color *= (1.0f - flowLuminance);
    }

    // Режимы отладки
    int renderMode = (int)renderModeInfo.x;
    if (renderMode != 0) // 0 – обычный PBR, выключен отладочный вывод
    {
        float3 debugColor = float3(0, 0, 0);
        // Для отладки используем первый источник света
        if (lightInfo.x > 0)
        {
            float3 L = normalize(lights[0].pos.xyz - pixel.worldPos);
            float3 H = normalize(V + L);
            float NdotL = max(dot(N, L), 0.0);
            float NdotV = max(dot(N, V), 0.0);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);
            float dist = length(lights[0].pos.xyz - pixel.worldPos);
            float attenuation = 1.0 / (dist * dist);

            float D = D_GGX(NdotH, roughness);
            float G = G_Smith(NdotV, NdotL, roughness);
            float3 F = F_Schlick(F0, VdotH);

            switch (renderMode)
            {
            case 1: // Normal Distribution
                debugColor = float3(D, D, D);
                break;
            case 2: // Geometry
                debugColor = float3(G, G, G);
                break;
            case 3: // Fresnel
                debugColor = F;
                break;
            case 4: // Diffuse Only
            {
                float3 kD = (1.0 - F) * (1.0 - metalness);
                float3 diffuse = kD * albedo / PI;
                float3 radiance = lights[0].color.rgb * lights[0].intensity * attenuation;
                debugColor = diffuse * radiance * NdotL;
            }
            break;
            case 5: // Specular Only
            {
                float D = D_GGX(NdotH, roughness);
                float G = G_Smith(NdotV, NdotL, roughness);
                float3 F = F_Schlick(F0, VdotH);
                float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
                float3 radiance = lights[0].color.rgb * lights[0].intensity * attenuation;
                debugColor = specular * radiance * NdotL;
            }
            break;
            default:
                debugColor = float3(1, 0, 1); // magenta – ошибка
                break;
            }
        }
        // Гамма-коррекция для отладочных режимов
        debugColor = pow(debugColor, 1.0 / 2.2);
        return float4(debugColor, 1.0);
    }
    // Иначе (renderMode == 0) – обычный PBR, продолжаем
    // Гамма-коррекция
    color = pow(color, 1.0 / 2.2);
    return float4(color, 1.0);
}