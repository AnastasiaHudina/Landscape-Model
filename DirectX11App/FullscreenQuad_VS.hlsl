/*Назначение: Аналогичен HDRToCubeMap_VS.hlsl, но используется для генерации irradiance map, 
prefiltered env map и BRDF LUT. Имеет те же константные буферы.*/

struct VS_INPUT
{
    uint vertexId : SV_VertexID;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 localPos : POSITION1;   // меняем uv на localPos
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;

    // Fullscreen triangle (покрывает экран)
    float2 pos[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2(3.0, -1.0)
    };

    output.position = float4(pos[input.vertexId], 0.0, 1.0);
    // Генерируем localPos как нормализованные координаты (диапазон -1..1)
    // В BRDF LUT используется только x и y, но структура требует float4
    output.localPos = float4(pos[input.vertexId].x, pos[input.vertexId].y, 1.0, 1.0);

    return output;
}

/*
struct VS_INPUT
{
    uint vertexId : SV_VertexID;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;

    // fullscreen triangle (3 вершины)
    float2 pos[3] =
    {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2(3.0, -1.0)
    };

    float2 uv[3] =
    {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    output.position = float4(pos[input.vertexId], 0.0, 1.0);
    output.uv = uv[input.vertexId];

    return output;
}*/