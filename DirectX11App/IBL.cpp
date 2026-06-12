#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include "IBL.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }
#endif

//функция поиска ярчайшего пикселя и сохранения направления
static DirectX::XMFLOAT3 FindSunDirectionFromHDR(float* hdrData, int width, int height)
{
    float maxLum = 0.0f;
    int maxX = 0, maxY = 0;

    const int step = 8; // ускоряем поиск, пропуская пиксели
    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            float* pixel = hdrData + (y * width + x) * 4;
            float lum = pixel[0] * 0.2126f + pixel[1] * 0.7152f + pixel[2] * 0.0722f;
            if (lum > maxLum) {
                maxLum = lum;
                maxX = x;
                maxY = y;
            }
        }
    }

    // Перевод координат пикселя в сферические углы (equirectangular)
    float u = ((float)maxX + 0.5f) / width;
    float v = ((float)maxY + 0.5f) / height;

    float theta = (u - 0.5f) * 2.0f * DirectX::XM_PI;   // азимут
    float phi = (0.5f - v) * DirectX::XM_PI;          // угол места

    DirectX::XMFLOAT3 dir;
    dir.x = cosf(phi) * sinf(theta);
    dir.y = sinf(phi);
    dir.z = cosf(phi) * cosf(theta);

    DirectX::XMVECTOR vecDir = DirectX::XMLoadFloat3(&dir);
    vecDir = DirectX::XMVector3Normalize(vecDir);
    // Инвертируем: направление ОТ источника (лучи света падают на сцену)
    vecDir = DirectX::XMVectorNegate(vecDir);

    DirectX::XMFLOAT3 result;
    DirectX::XMStoreFloat3(&result, vecDir);
    return result;
}

// Матрицы видов для 6 граней куба (используются при генерации cubemap)
static DirectX::XMMATRIX g_cubeViews[6] = 
{
    // +X
    DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0,0,0,0),
                              DirectX::XMVectorSet( 1, 0, 0,0),
                              DirectX::XMVectorSet(0, 1, 0,0)),
    // -X
    DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0,0,0,0),
                              DirectX::XMVectorSet(-1, 0, 0,0),
                              DirectX::XMVectorSet(0, 1, 0,0)),
    // +Y
    DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0,0,0,0),
                              DirectX::XMVectorSet( 0, 1, 0,0),
                              DirectX::XMVectorSet(0, 0,-1,0)),
    // -Y
    DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0,0,0,0),
                              DirectX::XMVectorSet( 0,-1, 0,0),
                              DirectX::XMVectorSet(0, 0, 1,0)),
    // +Z
    DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0,0,0,0),
                              DirectX::XMVectorSet( 0, 0, 1,0),
                              DirectX::XMVectorSet(0, 1, 0,0)),
    // -Z
    DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0,0,0,0),
                              DirectX::XMVectorSet( 0, 0,-1,0),
                              DirectX::XMVectorSet(0, 1, 0,0))
};

// Шейдеры
static ID3D11VertexShader* g_pFullscreenQuadVS = nullptr;
static ID3D11VertexShader* g_pHDRToCubeVS = nullptr;

static ID3D11PixelShader* g_pHDRToCubePS = nullptr;
static ID3D11PixelShader* g_pIrradiancePS = nullptr;
static ID3D11PixelShader* g_pPrefilterPS = nullptr;
static ID3D11PixelShader* g_pBRDFPS = nullptr;

// Sampler
static ID3D11SamplerState* g_pLinearSampler = nullptr;
static ID3D11SamplerState* g_pLinearMipSampler = nullptr;
static ID3D11SamplerState* g_pIrradianceSampler = nullptr;

// Вспомогательная загрузка CSO
static ID3DBlob* LoadCSO(const wchar_t* path)
{
    ID3DBlob* blob = nullptr;
    if (FAILED(D3DReadFileToBlob(path, &blob)))
        return nullptr;
    return blob;
}

// Создание cubemap
static bool CreateCubeMapTexture(
    ID3D11Device* device,
    UINT size,
    UINT mipLevels,
    DXGI_FORMAT format,
    ID3D11Texture2D** ppTex,
    ID3D11ShaderResourceView** ppSRV)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = size;
    desc.Height = size;
    desc.MipLevels = mipLevels;
    desc.ArraySize = 6;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, ppTex)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = mipLevels;

    if (FAILED(device->CreateShaderResourceView(*ppTex, &srvDesc, ppSRV)))
        return false;

    return true;
}

// Создание RTV для одной грани
static ID3D11RenderTargetView* CreateFaceRTV(
    ID3D11Device* device,
    ID3D11Texture2D* tex,
    UINT face,
    UINT mip)
{
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rtvDesc.Texture2DArray.FirstArraySlice = face;
    rtvDesc.Texture2DArray.ArraySize = 1;
    rtvDesc.Texture2DArray.MipSlice = mip;

    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(device->CreateRenderTargetView(tex, &rtvDesc, &rtv)))
        return nullptr;

    return rtv;
}

// Отрисовка fullscreen quad
static void DrawFullscreenQuad(ID3D11DeviceContext* context)
{
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->Draw(4, 0);
}

IBL::IBL() {}
IBL::~IBL()
{
    if (m_irradianceCB) m_irradianceCB->Release();
    if (m_prefilterCB) m_prefilterCB->Release();
}

bool IBL::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_device = device;
    m_context = context;

    // Загружаем шейдеры
    ID3DBlob* blob = nullptr;

    blob = LoadCSO(L"FullscreenQuad_VS.cso");
    if (!blob) return false;
    device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pFullscreenQuadVS);
    blob->Release();

    blob = LoadCSO(L"HDRToCubeMap_VS.cso");
    if (!blob) return false;
    device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pHDRToCubeVS);
    blob->Release();

    blob = LoadCSO(L"HDRToCubeMap_PS.cso");
    if (!blob) return false;
    device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pHDRToCubePS);
    blob->Release();

    blob = LoadCSO(L"IrradianceMap_PS.cso");
    if (!blob) return false;
    device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pIrradiancePS);
    blob->Release();

    blob = LoadCSO(L"PrefilterEnvMap_PS.cso");
    if (!blob) return false;
    device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pPrefilterPS);
    blob->Release();

    blob = LoadCSO(L"BRDFLUT_PS.cso");
    if (!blob) return false;
    device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pBRDFPS);
    blob->Release();

    // Sampler
    D3D11_SAMPLER_DESC samp = {};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    samp.MaxLOD = D3D11_FLOAT32_MAX; // Разрешает видеокарте использовать все MIP-уровни
    samp.MinLOD = 0.0f;
    samp.MipLODBias = 0.0f;
    samp.MaxAnisotropy = 1;
    samp.ComparisonFunc = D3D11_COMPARISON_NEVER;

    device->CreateSamplerState(&samp, &g_pLinearSampler);
    device->CreateSamplerState(&samp, &g_pLinearMipSampler);

    // Сэмплер для irradiance (только линейная фильтрация, без мип-уровней)
    D3D11_SAMPLER_DESC irradianceSampDesc = {};
    irradianceSampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT; // линейная внутри уровня, без мип-переходов
    irradianceSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    irradianceSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    irradianceSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    irradianceSampDesc.MinLOD = 0;
    irradianceSampDesc.MaxLOD = 0;   // принудительно используем только уровень 0
    irradianceSampDesc.MipLODBias = 0.0f;
    irradianceSampDesc.MaxAnisotropy = 1;
    irradianceSampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    if (FAILED(device->CreateSamplerState(&irradianceSampDesc, &g_pIrradianceSampler)))
        return false;

    // Константный буфер для irradiance (N1, N2)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(int) * 4; // 16 байт
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_irradianceCB))) return false;

    // Константный буфер для prefilter (roughness)
    cbDesc.ByteWidth = sizeof(float) * 4; // 16 байт
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_prefilterCB))) return false;

    //D3D11_BUFFER_DESC cbDesc = {};
    // После создания m_prefilterCB просто перезаписываем поля той же cbDesc
    cbDesc.ByteWidth = sizeof(DirectX::XMMATRIX);   // одна матрица inverseViewProj
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = 0;
    if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &m_pCubeMapCB)))
        return false;

    /*
    //kloofendal_48d_partly_cloudy_puresky_2k.hdr
    //qwantani_moon_noon_puresky_2k.hdr
    if (!LoadHDRTexture(L"landscape/qwantani_moon_noon_puresky_2k.hdr")) {
        OutputDebugString(L"InitIBL: Failed to load HDR\n");
        return false;
    }
    */

    // Загружаем HDR один раз (для текстуры и для поиска направления)
    const wchar_t* hdrPath = L"landscape/qwantani_moon_noon_puresky_2k.hdr";
    char narrow[512];
    wcstombs(narrow, hdrPath, 512);
    int width, height, comp;
    float* hdrData = stbi_loadf(narrow, &width, &height, &comp, 4);
    if (!hdrData) {
        OutputDebugString(L"InitIBL: Failed to load HDR file\n");
        return false;
    }

    // 1. Вычисляем направление луны (самый яркий пиксель)
    m_sunDirection = FindSunDirectionFromHDR(hdrData, width, height);

    // 2. Создаём текстуру HDR из этих же данных
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.SampleDesc.Count = 1;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = hdrData;
    initData.SysMemPitch = width * 4 * sizeof(float);

    ID3D11Texture2D* hdrTex = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &hdrTex);
    if (FAILED(hr)) {
        stbi_image_free(hdrData);
        OutputDebugString(L"InitIBL: Failed to create HDR texture\n");
        return false;
    }
    hr = m_device->CreateShaderResourceView(hdrTex, nullptr, &m_resources.hdrSRV);
    hdrTex->Release();
    stbi_image_free(hdrData);
    if (FAILED(hr)) {
        OutputDebugString(L"InitIBL: Failed to create HDR SRV\n");
        return false;
    }

    // Вычисляем количество mip-уровней для кубмапы (для размера 256 -> 9)
    UINT cubeMipLevels = 0;
    UINT temp = CUBEMAP_RES;
    while (temp > 0) {
        cubeMipLevels++;
        temp >>= 1;
    }
    // Создаём кубмапу со ВСЕМИ mip-уровнями
    if (!CreateCubeMapTexture(device, CUBEMAP_RES, cubeMipLevels, DXGI_FORMAT_R16G16B16A16_FLOAT,
        &m_resources.cubeMap, &m_resources.cubeMapSRV))
        return false;

    // Используем кубмапу как текстуру скайбокса
    m_resources.skyboxSRV = m_resources.cubeMapSRV;
    if (m_resources.skyboxSRV)
        m_resources.skyboxSRV->AddRef();

    /*
    // Создаём скайбокс из уже загруженной HDR-текстуры
    if (!CreateSkyboxFromHDRTexture()) {
        OutputDebugString(L"InitIBL: Failed to create Skybox from HDR texture\n");
        return false;
    }
    */

    // Создаём текстуры
    /*
    CreateCubeMapTexture(device, CUBEMAP_RES, 1, DXGI_FORMAT_R16G16B16A16_FLOAT,
        &m_resources.cubeMap, &m_resources.cubeMapSRV);*/

    //CreateCubeMapTexture(device, IRRADIANCE_RES, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, &m_resources.irradianceMap, &m_resources.irradianceSRV);

    CreateCubeMapTexture(device, IRRADIANCE_RES, IRRADIANCE_MIPS, DXGI_FORMAT_R16G16B16A16_FLOAT,
        &m_resources.irradianceMap, &m_resources.irradianceSRV);

    CreateCubeMapTexture(device, PREFILTERED_RES, PREFILTERED_MIPS, DXGI_FORMAT_R16G16B16A16_FLOAT,
        &m_resources.prefilteredMap, &m_resources.prefilteredSRV);

    // Создаём projection матрицу для cubemap (90° FOV)
    m_projCubemap = DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, 0.1f, 10.0f);

    // стартуем генерацию
    m_stage = IBL_STAGE_CUBEMAP;
    m_currentFace = 0;
    m_currentMip = 0;

    return true;
}

void IBL::Update()
{
    switch (m_stage)
    {
    case IBL_STAGE_CUBEMAP:
        RenderCubemapFace();
        break;

    case IBL_STAGE_IRRADIANCE:
        RenderIrradianceFace();
        break;

    case IBL_STAGE_PREFILTER:
        RenderPrefilterStep();
        break;

    case IBL_STAGE_BRDF:
        RenderBRDF();
        m_stage = IBL_STAGE_DONE;
        break;

    default:
        break;
    }
}

void IBL::RenderCubemapFace()
{
    m_context->RSSetState(nullptr);
    m_context->OMSetDepthStencilState(nullptr, 0);

    auto rtv = CreateFaceRTV(m_device, m_resources.cubeMap, m_currentFace, 0);
    if (!rtv) return;

    float clear[4] = { 0,0,0,1 };
    m_context->ClearRenderTargetView(rtv, clear);
    // Отвязываем входной ресурс, чтобы избежать конфликта
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    m_context->PSSetShaderResources(0, 16, nullSRVs);
    //ID3D11ShaderResourceView* nullSRV = nullptr;
    //m_context->PSSetShaderResources(0, 1, &nullSRV);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    m_context->VSSetShader(g_pHDRToCubeVS, nullptr, 0);
    m_context->PSSetShader(g_pHDRToCubePS, nullptr, 0);

    m_context->PSSetSamplers(0, 1, &g_pLinearSampler);

    // УСТАНАВЛИВАЕМ ВХОДНУЮ ТЕКСТУРУ (HDR)
    m_context->PSSetShaderResources(0, 1, &m_resources.hdrSRV);

    // Используем projection матрицу, созданную в Init
    DirectX::XMMATRIX proj = m_projCubemap;
    // Матрица вида для текущей грани
    DirectX::XMMATRIX view = g_cubeViews[m_currentFace];
    // Вычисляем inverse view-projection
    DirectX::XMMATRIX viewProj = view * proj;
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
    DirectX::XMMATRIX invViewProjT = DirectX::XMMatrixTranspose(invViewProj);
    // Обновляем константный буфер (m_pCubeMapCB должен быть создан в Init)
    m_context->UpdateSubresource(m_pCubeMapCB, 0, nullptr, &invViewProjT, 0, 0);
    // Привязываем константный буфер к вершинному шейдеру (регистр b0)
    m_context->VSSetConstantBuffers(0, 1, &m_pCubeMapCB);

    // Устанавливаем viewport под размер кубмапы
    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)CUBEMAP_RES;
    vp.Height = (FLOAT)CUBEMAP_RES;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);

    DrawFullscreenQuad(m_context);

    rtv->Release();

    m_currentFace++;
    if (m_currentFace >= 6)
    {
        // Генерируем mip-уровни для всей кубмапы (заполняет все LOD корректными усреднёнными данными)
        m_context->GenerateMips(m_resources.cubeMapSRV);

        m_currentFace = 0;
        m_stage = IBL_STAGE_IRRADIANCE;
    }
}

void IBL::RenderIrradianceFace()
{
    m_context->RSSetState(nullptr);
    m_context->OMSetDepthStencilState(nullptr, 0);

    auto rtv = CreateFaceRTV(m_device, m_resources.irradianceMap, m_currentFace, 0);
    if (!rtv) return;

    float clear[4] = { 0,0,0,1 };
    m_context->ClearRenderTargetView(rtv, clear);
    // Отвязываем входной ресурс, чтобы избежать конфликта
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    m_context->PSSetShaderResources(0, 16, nullSRVs);
    //ID3D11ShaderResourceView* nullSRV = nullptr;
    //m_context->PSSetShaderResources(0, 1, &nullSRV);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    m_context->VSSetShader(g_pHDRToCubeVS, nullptr, 0);
    m_context->PSSetShader(g_pIrradiancePS, nullptr, 0);

    m_context->PSSetSamplers(0, 1, &g_pIrradianceSampler);

    // Входная текстура – уже сгенерированная кубмапа
    m_context->PSSetShaderResources(0, 1, &m_resources.cubeMapSRV);

    // Обновление константного буфера m_pCubeMapCB
    DirectX::XMMATRIX proj = m_projCubemap;
    DirectX::XMMATRIX view = g_cubeViews[m_currentFace];
    DirectX::XMMATRIX viewProj = view * proj;
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
    DirectX::XMMATRIX invViewProjT = DirectX::XMMatrixTranspose(invViewProj);
    m_context->UpdateSubresource(m_pCubeMapCB, 0, nullptr, &invViewProjT, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_pCubeMapCB);

    // Константный буфер с параметрами N1, N2
    //int params[4] = { 32, 32, 0, 0 }; // слишком мало?
    //int params[4] = { 128, 64, 0, 0 };
    int params[4] = { 256, 128, 0, 0 }; // N1=256, N2=128
    //int params[4] = { 256, 0, 0, 0 };
    //int params[4] = { 64, 32, 0, 0 };   // Nphi = 64, Ntheta = 32
    m_context->UpdateSubresource(m_irradianceCB, 0, nullptr, params, 0, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_irradianceCB);

    // Устанавливаем viewport под размер irradiance карты
    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)IRRADIANCE_RES;
    vp.Height = (FLOAT)IRRADIANCE_RES;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);

    DrawFullscreenQuad(m_context);

    rtv->Release();

    m_currentFace++;
    if (m_currentFace >= 6)
    {
        // Генерируем MIP-уровни для irradiance карты
       //m_context->GenerateMips(m_resources.irradianceSRV);

        m_currentFace = 0;
        m_stage = IBL_STAGE_PREFILTER;
    }
}

void IBL::RenderPrefilterStep()
{
    m_context->RSSetState(nullptr);
    m_context->OMSetDepthStencilState(nullptr, 0);

    auto rtv = CreateFaceRTV(m_device, m_resources.prefilteredMap, m_currentFace, m_currentMip);
    if (!rtv) return;

    float clear[4] = { 0,0,0,1 };
    m_context->ClearRenderTargetView(rtv, clear);
    // Отвязываем входной ресурс, чтобы избежать конфликта
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    m_context->PSSetShaderResources(0, 16, nullSRVs);
    //ID3D11ShaderResourceView* nullSRV = nullptr;
    //m_context->PSSetShaderResources(0, 1, &nullSRV);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    m_context->VSSetShader(g_pHDRToCubeVS, nullptr, 0);
    m_context->PSSetShader(g_pPrefilterPS, nullptr, 0);

    m_context->PSSetSamplers(0, 1, &g_pLinearMipSampler);

    // Входная текстура – кубмапа
    m_context->PSSetShaderResources(0, 1, &m_resources.cubeMapSRV);

    // Обновление константного буфера m_pCubeMapCB
    DirectX::XMMATRIX proj = m_projCubemap;
    DirectX::XMMATRIX view = g_cubeViews[m_currentFace];
    DirectX::XMMATRIX viewProj = view * proj;
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
    DirectX::XMMATRIX invViewProjT = DirectX::XMMatrixTranspose(invViewProj);
    m_context->UpdateSubresource(m_pCubeMapCB, 0, nullptr, &invViewProjT, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_pCubeMapCB);

    // Передаём roughness для текущего mip-уровня
    float roughness = (float)m_currentMip / (float)(PREFILTERED_MIPS - 1);
    float roughnessVal[4] = { roughness, 0.0f, 0.0f, 0.0f };
    m_context->UpdateSubresource(m_prefilterCB, 0, nullptr, roughnessVal, 0, 0);
    m_context->PSSetConstantBuffers(0, 1, &m_prefilterCB);

    // Вычисляем размер текущего mip-уровня (каждый уровень в 2 раза меньше)
    UINT mipSize = (PREFILTERED_RES >> m_currentMip);
    if (mipSize < 1) mipSize = 1;
    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)mipSize;
    vp.Height = (FLOAT)mipSize;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    m_context->RSSetViewports(1, &vp);

    DrawFullscreenQuad(m_context);

    rtv->Release();

    m_currentFace++;
    if (m_currentFace >= 6)
    {
        m_currentFace = 0;
        m_currentMip++;

        if (m_currentMip >= (int)PREFILTERED_MIPS)
        {
            m_stage = IBL_STAGE_BRDF;
        }
    }
}

void IBL::RenderBRDF()
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = BRDF_LUT_RES;
    desc.Height = BRDF_LUT_RES;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    m_device->CreateTexture2D(&desc, nullptr, &m_resources.brdfLUT);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    HRESULT hr = m_device->CreateShaderResourceView(
        m_resources.brdfLUT,
        &srvDesc,
        &m_resources.brdfLUTSRV
    );
    if (FAILED(hr))
    {
        OutputDebugString(L"RenderBRDF: Failed to create BRDF LUT SRV\n");
        return;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* rtv = nullptr;
    m_device->CreateRenderTargetView(m_resources.brdfLUT, &rtvDesc, &rtv);
    if (!rtv) return;

    float clear[4] = { 0,0,0,1 };
    m_context->ClearRenderTargetView(rtv, clear);
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    m_context->PSSetShaderResources(0, 16, nullSRVs);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    m_context->VSSetShader(g_pFullscreenQuadVS, nullptr, 0);
    m_context->PSSetShader(g_pBRDFPS, nullptr, 0);

    DrawFullscreenQuad(m_context);

    rtv->Release();
}

bool IBL::LoadHDRTexture(const wchar_t* filename)
{
    // Конвертируем wchar_t в char для stb_image
    char narrow[512];
    wcstombs(narrow, filename, 512);

    int x, y, comp;
    float* data = stbi_loadf(narrow, &x, &y, &comp, 4); // 4 канала RGBA
    if (!data) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = x;
    desc.Height = y;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.SampleDesc.Count = 1;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = x * 4 * sizeof(float);

    ID3D11Texture2D* hdrTex = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &hdrTex);
    if (FAILED(hr)) { stbi_image_free(data); return false; }

    hr = m_device->CreateShaderResourceView(hdrTex, nullptr, &m_resources.hdrSRV);
    hdrTex->Release();
    stbi_image_free(data);
    return SUCCEEDED(hr);
}

void IBL::Shutdown()
{
    // Освобождаем все ресурсы, созданные в IBL
    SAFE_RELEASE(m_resources.hdrSRV);
    SAFE_RELEASE(m_resources.skyboxSRV);      // теперь это AddRef-ссылка на cubeMapSRV
    SAFE_RELEASE(m_resources.cubeMap);
    SAFE_RELEASE(m_resources.cubeMapSRV);
    SAFE_RELEASE(m_resources.irradianceMap);
    SAFE_RELEASE(m_resources.irradianceSRV);
    SAFE_RELEASE(m_resources.prefilteredMap);
    SAFE_RELEASE(m_resources.prefilteredSRV);
    SAFE_RELEASE(m_resources.brdfLUT);
    SAFE_RELEASE(m_resources.brdfLUTSRV);

    SAFE_RELEASE(m_irradianceCB);
    SAFE_RELEASE(m_prefilterCB);
    SAFE_RELEASE(m_pCubeMapCB);

    SAFE_RELEASE(g_pIrradianceSampler);

}

// Функция для создания скайбокса из HDR-текстуры
bool IBL::CreateSkyboxFromHDRTexture()
{
    // Сначала нужно убедиться, что HDR текстура уже загружена (m_resources.hdrSRV)
    if (!m_resources.hdrSRV) {
        OutputDebugString(L"CreateSkyboxFromHDRTexture: HDR texture is not loaded\n");
        return false;
    }

    // Используем HDR-текстуру для создания кубмапы скайбокса
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 512;  // Размер кубмапы скайбокса
    desc.Height = 512;
    desc.MipLevels = 1;
    desc.ArraySize = 6;  // 6 граней для кубмапы
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.SampleDesc.Count = 1;

    ID3D11Texture2D* skyboxTex = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &skyboxTex);
    if (FAILED(hr)) {
        OutputDebugString(L"CreateSkyboxFromHDRTexture: Failed to create texture\n");
        return false;
    }

    // Создание Shader Resource View для кубмапы
    hr = m_device->CreateShaderResourceView(skyboxTex, nullptr, &m_resources.skyboxSRV);
    skyboxTex->Release();
    if (FAILED(hr)) {
        OutputDebugString(L"CreateSkyboxFromHDRTexture: Failed to create Shader Resource View\n");
        return false;
    }

    return true;
}
/*
// Загрузка текстуры для скайбокса
bool IBL::LoadSkyboxTexture(const wchar_t* filename)
{
    // Конвертируем wchar_t в char для stb_image
    char narrow[512];
    wcstombs(narrow, filename, 512);

    int x, y, comp;
    float* data = stbi_loadf(narrow, &x, &y, &comp, 4); // 4 канала RGBA
    if (!data) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = x;
    desc.Height = y;
    desc.MipLevels = 1;
    desc.ArraySize = 6;  // Для кубмапы 6 граней
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.SampleDesc.Count = 1;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = x * 4 * sizeof(float);

    ID3D11Texture2D* skyboxTex = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &skyboxTex);
    if (FAILED(hr)) { stbi_image_free(data); return false; }

    hr = m_device->CreateShaderResourceView(skyboxTex, nullptr, &m_resources.skyboxSRV);
    skyboxTex->Release();
    stbi_image_free(data);
    return SUCCEEDED(hr);
}
*/