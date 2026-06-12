#pragma once

#include <DirectXMath.h>

struct Light
{
    DirectX::XMFLOAT4 pos;
    DirectX::XMFLOAT4 color;   // теперь цвет храним в диапазоне 0..1
    float intensity;           // множитель яркости
    float padding[3];          // для выравнивания (16 байт)
};

// Функция вычисления освещения по модели Фонга
DirectX::XMFLOAT3 CalculateColor(
    DirectX::XMFLOAT3 objColor,
    DirectX::XMFLOAT3 objNormal,
    DirectX::XMFLOAT3 pos,
    DirectX::XMFLOAT3 cameraPos,
    Light lights[10],
    int lightCount,
    DirectX::XMFLOAT4 ambientColor,
    bool showNormals = false,
    float shine = 0.0f)
{
    using namespace DirectX;
    XMFLOAT3 finalColor = { 0, 0, 0 };

    // Режим отображения нормалей (для отладки)
    if (showNormals)
    {
        return XMFLOAT3(
            objNormal.x * 0.5f + 0.5f,
            objNormal.y * 0.5f + 0.5f,
            objNormal.z * 0.5f + 0.5f
        );
    }

    // Окружающее освещение
    finalColor.x = objColor.x * ambientColor.x;
    finalColor.y = objColor.y * ambientColor.y;
    finalColor.z = objColor.z * ambientColor.z;

    // Вклад от каждого источника света
    for (int i = 0; i < lightCount; i++)
    {
        XMVECTOR normalVec = XMLoadFloat3(&objNormal);
        XMVECTOR lightPosVec = XMLoadFloat4(&lights[i].pos);
        XMVECTOR posVec = XMLoadFloat3(&pos);

        // Направление к источнику света
        XMVECTOR lightDirVec = XMVectorSubtract(lightPosVec, posVec);
        XMVECTOR lightDistVec = XMVector3Length(lightDirVec);
        lightDirVec = XMVectorDivide(lightDirVec, lightDistVec);

        float lightDist;
        XMStoreFloat(&lightDist, lightDistVec);

        // Квадратичное затухание
        float atten = 1.0f / (lightDist * lightDist);
        atten = (atten > 1.0f) ? 1.0f : atten;

        // Рассеянная составляющая (диффузная)
        XMVECTOR diffuseDot = XMVector3Dot(lightDirVec, normalVec);
        float diffuse = XMVectorGetX(diffuseDot);
        diffuse = (diffuse > 0.0f) ? diffuse : 0.0f;

        // Цвет источника света с учётом интенсивности
        float intensity = lights[i].intensity;
        XMFLOAT3 lightColor = { lights[i].color.x * intensity, lights[i].color.y * intensity, lights[i].color.z * intensity };

        // Добавляем диффузную составляющую
        finalColor.x += objColor.x * diffuse * atten * lightColor.x;
        finalColor.y += objColor.y * diffuse * atten * lightColor.y;
        finalColor.z += objColor.z * diffuse * atten * lightColor.z;

        // Зеркальная составляющая (если есть блеск)
        if (shine > 0.0f)
        {
            XMVECTOR viewDirVec = XMLoadFloat3(&cameraPos);
            viewDirVec = XMVectorSubtract(viewDirVec, posVec);
            viewDirVec = XMVector3Normalize(viewDirVec);

            XMVECTOR reflectDirVec = XMVector3Reflect(XMVectorNegate(lightDirVec), normalVec);
            XMVECTOR specularDot = XMVector3Dot(viewDirVec, reflectDirVec);
            float specular = XMVectorGetX(specularDot);
            specular = (specular > 0.0f) ? specular : 0.0f;

            // Учет коэффициента блеска
            specular = powf(specular, shine);

            // Добавляем зеркальную составляющую
            finalColor.x += objColor.x * 0.5f * specular * atten * lightColor.x;
            finalColor.y += objColor.y * 0.5f * specular * atten * lightColor.y;
            finalColor.z += objColor.z * 0.5f * specular * atten * lightColor.z;
        }
    }

    return finalColor;
}