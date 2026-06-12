#include "PBR.hlsl"

static const uint PREFILTERED_MIPS = 6;   // должно совпадать с PREFILTERED_MIPS в C++ коде

Texture2D colorTexture : register(t0);
Texture2D normalMapTexture : register(t1);
Texture2D detailTexture : register(t2);
Texture2D flowTexture : register(t3);
Texture2D roughnessTexture : register(t4);   // карта шероховатости
Texture2D metalnessTexture : register(t5);   // карта металличности (необязательно)

// IBL текстуры
TextureCube DiffuseIrradianceMap : register(t6);
TextureCube PrefilteredEnvMap : register(t7);
Texture2D BRDFLut : register(t8);

// Сэмплеры для IBL (линейная фильтрация и mipmap)
SamplerState linearSampler : register(s1);
SamplerState linearMipSampler : register(s2);

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
    float4 dirLightDir;    // направление света
    float4 dirLightColor;  // цвет + интенсивность в w
    //float4 manualPBRParams; // x - useManual (1.0 = manual), y - roughness, z - metalness
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
    
    // Шероховатость/металличность
    // Или ручное управление
    // Или из текстур
    float roughness;
    float metalness;
    //roughness = 0.55;
    //roughness = 0.55; // вместо texture sample для проверки бликов!!
    //roughness = roughnessTexture.Sample(colorSampler, pixel.uv).r; // берём красный канал

    //roughness = 1.0 - roughnessTexture.Sample(colorSampler, pixel.uv).g; // берём зелёный канал
    
    roughness = 1.0 - roughnessTexture.Sample(colorSampler, pixel.uv).r; // инвертируем!!

    //roughness = pow(roughness, 0.3); // делает значения ближе к 1
    //roughness = max(roughness, 0.5);  // Ограничиваем минимальное значение roughness - применяем минимальный roughness для гладких участков, чтобы не было слишком зеркальных поверхностей

    //roughness = clamp(roughness, 0.08, 1.0); // предотвращаем деление на ноль
    roughness = clamp(roughness * 0.5f, 0.05f, 1.0f);
    metalness = 0.0; // пока константа, позже можно из текстуры


    /*
    if (manualPBRParams.x > 0.5) // используем ручные параметры
    {
        roughness = clamp(manualPBRParams.y, 0.0001, 1.0);
        metalness = clamp(manualPBRParams.z, 0.0, 1.0);
    }
    else
    {
        //roughness = 0.8; // вместо texture sample для проверки бликов!!
        roughness = roughnessTexture.Sample(colorSampler, pixel.uv).r; // берём красный канал
        
        //roughness = 1.0 - roughnessTexture.Sample(colorSampler, pixel.uv).g; // берём зелёный канал
        //roughness = 1.0 - roughnessTexture.Sample(colorSampler, pixel.uv).r; // инвертируем!!
        
        //roughness = pow(roughness, 0.3); // делает значения ближе к 1
        //roughness = max(roughness, 0.5);  // Ограничиваем минимальное значение roughness - применяем минимальный roughness для гладких участков, чтобы не было слишком зеркальных поверхностей
        
        roughness = clamp(roughness, 0.08, 1.0); // предотвращаем деление на ноль
        metalness = 0.0; // пока константа, позже можно из текстуры
    }
    */
    // Применяем детали 
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

    float dielectricF0 = 0.04;

    // БЛОК FLOW 
    float occlusionFactor = 1.0f;
    float specularIblFactor = 1.0f;
    int flowMode = (int)flowInfo.x;
    if (flowMode > 0)
    {
        float flowLuminance = dot(flow, float3(0.299f, 0.587f, 0.114f));
        float flowMask = flowLuminance;

        // 1. Мягкое затенение света неба (Ambient Occlusion)
        occlusionFactor = 1.0f - flowMask;
        occlusionFactor = lerp(1.0f, occlusionFactor, 0.50f);

        // Применяем эффект в зависимости от выбранного режима
        if (flowMode == 1) // ВЛАЖНЫЕ УЩЕЛЬЯ (Мокрый, потемневший камень в низинах)
        {
            // Слегка сужаем маску влажности (степень 2.5), чтобы мокрый камень не вылезал на вершины холмов
            float wetMask = pow(flowMask, 2.5f);

            // Земля и камень сильно темнеют от влаги
            float3 wetAlbedo = albedo * float3(0.35f, 0.35f, 0.45f);
            albedo = lerp(albedo, wetAlbedo, wetMask);

            // Плавно переходим от текстурного roughness скал к влажному гладкому камню (0.25)
            roughness = lerp(roughness, 0.15f, wetMask);

            // Слегка приподнимаем F0 до 0.08 для характерного маслянистого блеска мокрой породы
            dielectricF0 = lerp(dielectricF0, 0.08f, wetMask);

            // Гасим Specular IBL во влажных низинах, чтобы убрать белесую пелену
            specularIblFactor = lerp(1.0f, 0.30f, wetMask);
        }
        else if (flowMode == 2) // КАМЕНИСТОЕ ДНО КАНЬОНА (Глубокие, сухие, матовые расщелины)
        {
            // Оставляем базовую широкую маску, так как сухая тень и каменистая крошка заполняют каньон целиком
            float3 wetAlbedo = albedo * 0.45f;
            albedo = lerp(albedo, wetAlbedo, flowMask);

            // Заставляем трещины быть абсолютно матовыми (0.85), чтобы они поглощали свет и казались глубокими впадинами
            roughness = lerp(roughness, 0.85f, flowMask);

            // Оставляем стандартный диэлектрик земли (4%)
            dielectricF0 = 0.04f;

            // Перезаписываем occlusionFactor и specularIblFactor, чтобы каньон ушел в глубокую темноту
            occlusionFactor = lerp(1.0f, 0.15f, flowMask);
            specularIblFactor = lerp(1.0f, 0.05f, flowMask);
        }
        else if (flowMode == 3) // ГОРНЫЕ РУЧЬИ (Тонкие зеркальные жилы чистой воды)
        {
            // Сильно сужаем маску (степень 4.0), чтобы вода текла строго тонкой нитью по центру русла
            float waterMask = pow(flowMask, 4.0f);

            // Вода физически прозрачная и глубокая, albedo в самом русле гасим на 90%
            float3 wetAlbedo = albedo * 0.10f;
            albedo = lerp(albedo, wetAlbedo, waterMask);

            // В самом центре ручья жестко затираем текстурный roughness до 0.02 (идеальное водное зеркало)
            roughness = lerp(roughness, 0.02f, waterMask);

            // Возвращаем физически честное значение чистой воды (0.02). 
            // Убираем ртутный блеск - теперь вода засияет только под правильным углом к Луне (Френель)
            dielectricF0 = lerp(dielectricF0, 0.02f, waterMask);

            // Не даем ночному небу превратить ручьи в серые светящиеся провода
            occlusionFactor = lerp(1.0f, 0.20f, waterMask);
            specularIblFactor = lerp(1.0f, 0.10f, waterMask);
        }
    }

    // Вычисление F0
    float3 F0 = GetF0(albedo, metalness, dielectricF0);

    float3 color = float3(0,0,0);

    // Fallback ambient на случай, если IBL не работает
    //float3 fallbackAmbient = ambientColor.rgb * albedo * (1.0 - metalness);
    //color += fallbackAmbient;

    // Ambient (упрощённый)
    //float3 ambient = ambientColor.rgb * albedo * (1.0 - metalness);
    //color += ambient;

    // === IBL Ambient ===
    float NdotV = max(dot(N, V), 0.0);
    //float3 R = reflect(-V, N);
    float3 R = normalize(reflect(-V, N)); //нормализовали

    // Диффузная irradiance
    float3 irradiance = DiffuseIrradianceMap.SampleLevel(linearSampler, N, 0).rgb;
    //float3 irradiance = PrefilteredEnvMap.SampleLevel(linearMipSampler, N, 3.0f).rgb;
    //float3 irradiance = DiffuseIrradianceMap.Sample(linearSampler, N).rgb;
    //float3 irradiance = float3(0.5, 0.5, 0.5);
    
    //if (irradiance.r < 0.001) irradiance = float3(1, 0, 0); // красный, если irradiance нулевая

    // Предварительно отфильтрованная окружающая среда (specular)
    float3 prefilteredColor = PrefilteredEnvMap.SampleLevel(linearMipSampler, R, roughness * (PREFILTERED_MIPS - 1)).rgb;

    // BRDF LUT
    float2 brdf = BRDFLut.Sample(linearSampler, float2(NdotV, roughness)).rg;

    float3 F = FresnelSchlickRoughness(F0, roughness, V, N);
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metalness);
    //float3 kD = (1.0 - F0) * (1.0 - metalness);
    //float3 kD = (1.0 - F) * (1.0 - metalness);

    float3 diffuseIBL = irradiance * albedo * kD;
    //float3 diffuseIBL = irradiance * albedo * kD / PI;
    float3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y);
    //float3 specularIBL = prefilteredColor * (0.04 * brdf.x + 0.0); //ПРОВЕРКА СПЕКУЛЯР - ТУТ ТО, ЧТО ДОЛЖНО БЫТЬ!!
    //float3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);
    
    // ПРИМЕНЯЕМ ФИЗИЧЕСКОЕ ЗАТЕНЕНИЕ К СВЕТУ НЕБА (через FLOW)
    diffuseIBL *= occlusionFactor;  // Ambient Occlusion
    specularIBL *= specularIblFactor; // Specular Occlusion

    // Ночь
    // Снижаем общую яркость ночного неба в 3-4 раза
    // Коэффициент 0.25f – 0.35f вернет в сцену глубокую ночную темноту
    //diffuseIBL *= 0.02f;
    //specularIBL *= 0.02f;

    // Ночь: убираем ровную паразитную засветку, но сохраняем микроблики
    diffuseIBL *= 0.03f; // Практически уводит тени в темноту
    specularIBL *= 0.12f; // Оставляет легкий отраженный свет на материале

    float3 ambientIBL = diffuseIBL + specularIBL;

    color += ambientIBL;

    

    
    // Один направленный свет
    float3 L_dir = normalize(-dirLightDir.xyz); // направление от поверхности к источнику
    //float3 L_dir = normalize(float3(1.0, 0.2, 0.0)); // почти сбоку, чуть сверху
    
    /*
    float3 originalDir = normalize(dirLightDir.xyz);
    float tiltAngle = radians(45.0); // 45 градусов от вертикали
    // Поворачиваем вокруг оси X (наклон в сторону +Z или -Z)
    float cosT = cos(tiltAngle);
    float sinT = sin(tiltAngle);
    float3x3 rotX = {
        1, 0, 0,
        0, cosT, -sinT,
        0, sinT, cosT
    };
    float3 tiltedDir = mul(originalDir, rotX);
    float3 L_dir = normalize(tiltedDir);
    */

    float NdotL_dir = max(dot(N, L_dir), 0.0);
    float3 radiance_dir = dirLightColor.rgb * dirLightColor.w;
    float3 H_dir = normalize(V + L_dir);
    float NdotH_dir = max(dot(N, H_dir), 0.0);
    float VdotH_dir = max(dot(V, H_dir), 0.0);
    float NdotV_dir = max(dot(N, V), 0.0);
    float D_dir = D_GGX(NdotH_dir, roughness);
    float G_dir = G_Smith(NdotV_dir, NdotL_dir, roughness);
    float3 F_dir = F_Schlick(F0, VdotH_dir);
    float3 specular_dir = (D_dir * G_dir * F_dir) / max(4.0 * NdotV_dir * NdotL_dir, 0.001);
    float3 kS_dir = F_dir;
    float3 kD_dir = (1.0 - kS_dir) * (1.0 - metalness);
    color += (kD_dir * albedo / PI + specular_dir) * radiance_dir * NdotL_dir;
    

    // Режимы отладки
    int renderMode = (int)renderModeInfo.x;
    if (renderMode != 0) // 0 – обычный PBR, выключен отладочный вывод
    {
        float3 debugColor = float3(0, 0, 0);

        // Для отладки используем направленный свет
        float3 L_dir = normalize(-dirLightDir.xyz);
        float NdotL_dir = max(dot(N, L_dir), 0.0);
        float3 H_dir = normalize(V + L_dir);
        float NdotH_dir = max(dot(N, H_dir), 0.0);
        float VdotH_dir = max(dot(V, H_dir), 0.0);
        float NdotV_dir = max(dot(N, V), 0.0);

        float D = D_GGX(NdotH_dir, roughness);
        float G = G_Smith(NdotV_dir, NdotL_dir, roughness);
        float3 F = F_Schlick(F0, VdotH_dir);

        float3 radiance = dirLightColor.rgb * dirLightColor.w;

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
            debugColor = diffuse * radiance * NdotL_dir;
        }
        break;
        case 5: // Specular Only
        {
            float3 specular = (D * G * F) / max(4.0 * NdotV_dir * NdotL_dir, 0.001);
            debugColor = specular * radiance * NdotL_dir;
        }
        break;
        case 6: // Diffuse IBL
            debugColor = diffuseIBL;
            break;
        case 7: // Specular IBL
            debugColor = specularIBL;
            break;
        case 8: // Fresnel IBL
            debugColor = F;
            break;
        case 9: // BRDF LUT visualization
            debugColor = float3(brdf.x, brdf.y, 0);
            break;
        case 10: // Irradiance Map (диффузная карта окружения)
        {
            //debugColor = PrefilteredEnvMap.SampleLevel(linearMipSampler, R, 5.0f).rgb;
            float3 irradiance = DiffuseIrradianceMap.Sample(linearSampler, N).rgb;
            debugColor = irradiance / (irradiance + 1.0);   // Reinhard tonemap
            break;
        }
        default:
            debugColor = float3(1, 0, 1); // magenta – ошибка
            break;
        }

        
        // Гамма-коррекция для отладочных режимов
        debugColor = pow(debugColor, 1.0 / 2.2);
        return float4(debugColor, 1.0);
    }

    // Иначе (renderMode == 0) – обычный PBR
    // Гамма-коррекция
    color = color / (color + 1.0);
    //color = saturate(color);
    color = pow(color, 1.0 / 2.2);
    
    return float4(color, 1.0);
}