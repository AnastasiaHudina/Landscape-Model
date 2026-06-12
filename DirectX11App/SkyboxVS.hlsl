cbuffer CameraBuffer : register(b0)
{
    matrix view;
    matrix proj;
};

struct VS_INPUT
{
    float3 pos : POSITION;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 dir : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    // Убираем translation ИЗ VIEW
    matrix viewNoTranslation = view;
    viewNoTranslation._41 = 0.0f;
    viewNoTranslation._42 = 0.0f;
    viewNoTranslation._43 = 0.0f;

    float4 pos = float4(input.pos, 1.0f);

    float4 viewPos = mul(pos, viewNoTranslation);
    float4 projPos = mul(viewPos, proj);

    // всегда на дальнем плане
    output.pos = float4(projPos.xy, projPos.w, projPos.w);

    // направление для кубмапы
    //output.dir = input.pos;
    output.dir = normalize(mul(input.pos, (float3x3)viewNoTranslation));

    return output;
}