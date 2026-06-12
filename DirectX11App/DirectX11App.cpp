// === ПОЯСНЕНИЕ К УПРАВЛЕНИЮ ===
//На мышь - приближать/отдалять
//Левая кнопка мыши - вращение камерой при полёте
//Клавиши wasd - перемещение камеры 
//Клавиши q/e - изменение высоты над поверхностью
//

// Standard Windows Headers
#include <windows.h>
#include <windowsx.h>

// DirectX Headers
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>

// ImGui Headers - добавляем файлы из папки "imgui"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

// C++ Standard Library
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <vector>
#include <string>
#include <malloc.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Включаем заголовочные файлы для работы с текстурами и освещением
#include "Texture.h"
#include "Light.h"
#include "IBL.h"

// Skybox
ID3D11VertexShader* g_pSkyboxVS = nullptr;
ID3D11PixelShader* g_pSkyboxPS = nullptr;
ID3D11InputLayout* g_pSkyboxInputLayout = nullptr;
ID3D11Buffer* g_pSkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxIndexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxConstantBuffer = nullptr;
UINT g_skyboxIndexCount = 0;

#define TINYEXR_IMPLEMENTATION
#include "tinyexr/tinyexr.h"

// Явное объявление для ImGui функции
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Перечисление возможных пост-эффектов
enum PostProcessEffect {
    PP_NONE = 0,
    PP_SEPIA = 1,
    PP_COLD_TINT = 2,
    PP_NIGHT_VISION = 3
};

// === СТРУКТУРЫ ДЛЯ КОНСТАНТНЫХ БУФЕРОВ ===

struct SkyboxConstants
{
    DirectX::XMMATRIX world;
    DirectX::XMMATRIX viewProj;
    DirectX::XMFLOAT4 cameraPosAndMode;
};

// Геометрический буфер для каждого объекта (матрица модели, матрица нормалей, параметры материала)
struct GeomBuffer
{
    DirectX::XMFLOAT4X4 m;
    DirectX::XMFLOAT4X4 normalM;
    DirectX::XMFLOAT4 pbrParams; // x - roughness, y - metalness, z - unused, w - hasNormalMap
    DirectX::XMFLOAT4 posAngle;
};

// Буфер сцены (данные, общие для всех объектов)
struct SceneBuffer
{
    DirectX::XMFLOAT4X4 vp;                 // матрица вида-проекции
    DirectX::XMFLOAT4 cameraPos;             // позиция камеры в мировых координатах
    DirectX::XMFLOAT4 lightInfo;             // x - количество источников, y - использовать карты нормалей, z - показывать нормали, w - сила детали
    Light lights[10];                        // массив источников света (до 10)
    DirectX::XMFLOAT4 ambientColor;          // цвет фонового (ambient) освещения
    DirectX::XMFLOAT4 flowInfo;               // x - использовать Flow 
    DirectX::XMFLOAT4 renderModeInfo; // x - renderMode, yzw - не используются (padding)
    DirectX::XMFLOAT4 dirLightDir;    // направление света (x,y,z), w не используется
    DirectX::XMFLOAT4 dirLightColor;  // цвет света (rgb) + интенсивность в w
    //DirectX::XMFLOAT4 manualPBRParams; // для ручных PBR параметров: x - useManual (1.0), y - roughness, z - metalness, w - unused
};

// Буфер для постпроцессинга
struct PostProcessBuffer
{
    int effectType;    // 0 - нет, 1 - сепия, 2 - холодный тон, 3 - ночной режим
    int padding[3];    // Выравнивание
};

// Геометрический буфер для маленьких сфер (визуализация источников света)
struct SmallSphereGeomBuffer
{
    DirectX::XMFLOAT4X4 m;      // матрица модели
    DirectX::XMFLOAT4 color;     // цвет сферы (цвет источника)
};

// === ПРОТОТИПЫ ФУНКЦИЙ ===
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
bool InitWindow(HINSTANCE hInstance, int nCmdShow);
bool InitDirectX();
bool InitTerrain();
float GetTerrainHeight(float x, float z);
bool InitShaders();
bool InitBuffers();
bool LoadTexture();
bool LoadNormalMap();
bool LoadDetailTexture();
bool LoadFlowTexture();
bool LoadRoughnessTexture();
bool InitSmallSpheres();
void UpdateLightIntensities();
void Render();
void Cleanup();
void ResizeSwapChain(UINT width, UINT height);
void UpdateCamera();
bool SetupBackBuffer();
bool InitColorBuffer();
void RenderSmallSpheres();
void CreateSphere(size_t latCells, size_t lonCells, UINT16* pIndices, DirectX::XMFLOAT3* pPos);

// Глобальные переменные
HWND g_hWnd = NULL;
ID3D11Device* m_pDevice = nullptr;                 // устройство Direct3D
ID3D11DeviceContext* m_pDeviceContext = nullptr;   // контекст устройства (для рисования)
IDXGISwapChain* m_pSwapChain = nullptr;            // цепочка обмена (для отображения на экране)
ID3D11RenderTargetView* m_pBackBufferRTV = nullptr; // представление заднего буфера как цели рендера

IBL g_ibl;         // глобальный экземпляр

// === ПЕРЕМЕННЫЕ ДЛЯ ОСВЕЩЕНИЯ ===
Light m_lights[10];                                 // массив источников света
DirectX::XMFLOAT4 m_ambientColor = { 0.1f, 0.1f, 0.2f, 1.0f }; // цвет фонового освещения
int m_lightCount = 0;                               // количество активных источников
bool m_useNormalMaps = true;                        // флаг использования карт нормалей
bool m_showNormals = false;                          // флаг визуализации нормалей (для отладки)
bool m_showLightBulbs = false;                        // показывать ли сферы-лампочки для источников

// === ПЕРЕМЕННЫЕ ДЛЯ КАРТЫ НОРМАЛЕЙ ===
ID3D11Texture2D* m_pTextureNM = nullptr;             // текстура карты нормалей
ID3D11ShaderResourceView* m_pTextureViewNM = nullptr; // шейдерное представление карты нормалей

// === ПЕРЕМЕННЫЕ ДЛЯ ТЕКСТУРЫ ДЕТАЛЕЙ ===
ID3D11ShaderResourceView* m_pDetailTextureView = nullptr; // шейдерное представление текстуры деталей

// === ПЕРЕМЕННЫЕ ДЛЯ ТЕКСТУРЫ FLOW ===
ID3D11ShaderResourceView* m_pFlowTextureView = nullptr;
int m_flowModeIndex = 2;  // 0 – Обычный, 1 – Влажные ущелья, 2 – Каменистое дно, 3 – Горные ручьи

// === ПЕРЕМЕННЫЕ ДЛЯ ТЕКСТУР PBR ===
ID3D11ShaderResourceView* m_pRoughnessTextureView = nullptr; // шейдерное представление карты шероховатости
ID3D11ShaderResourceView* m_pMetalnessTextureView = nullptr; // шейдерное представление карты металличности (пока константно)
int g_renderMode = 0;           // 0 - обычный PBR, 1 - NDF, 2 - Geometry, 3 - Fresnel и т.д.

// === Управление PBR параметрами ===
//bool m_useManualRoughnessMetalness = false;  // флаг: true – ручные параметры
//float m_manualRoughness = 0.5f;              // значение roughness (0..1)
//float m_manualMetalness = 0.0f;              // значение metalness (0..1)

// === ПЕРЕМЕННЫЕ ДЛЯ ПОСТПРОЦЕССИНГА ===
ID3D11Texture2D* m_pColorBuffer = nullptr;           // текстура для промежуточного рендера (цвет)
ID3D11RenderTargetView* m_pColorBufferRTV = nullptr; // цель рендера для промежуточной текстуры
ID3D11ShaderResourceView* m_pColorBufferSRV = nullptr; // шейдерное представление для чтения в постпроцессинге
ID3D11PixelShader* m_pPostProcessPixelShader = nullptr;   // пиксельный шейдер постпроцессинга
ID3D11VertexShader* m_pPostProcessVertexShader = nullptr; // вершинный шейдер постпроцессинга (рисует треугольник)
ID3D11Buffer* m_pPostProcessBuffer = nullptr;        // константный буфер для параметров постпроцессинга
int m_postProcessEffect = 0;                         // текущий выбранный эффект (0 - нет)
float m_detailStrength = 0.5f;                       // сила влияния детали (0..2)

// === ПЕРЕМЕННЫЕ ДЛЯ МАЛЕНЬКИХ СФЕР (ВИЗУАЛИЗАЦИЯ ИСТОЧНИКОВ) ===
ID3D11Buffer* m_pSmallSphereVertexBuffer = nullptr;   // вершинный буфер сферы
ID3D11Buffer* m_pSmallSphereIndexBuffer = nullptr;    // индексный буфер сферы
ID3D11VertexShader* m_pSmallSphereVertexShader = nullptr;   // вершинный шейдер для сфер
ID3D11PixelShader* m_pSmallSpherePixelShader = nullptr;     // пиксельный шейдер для сфер
ID3D11InputLayout* m_pSmallSphereInputLayout = nullptr;     // описание входных данных вершин
ID3D11Buffer* m_pSmallSphereGeomBuffers[10];                 // константные буферы для каждой сферы
UINT m_smallSphereIndexCount = 0;                            // количество индексов в сфере

// === ПЕРЕМЕННЫЕ ДЛЯ ImGui ===
bool m_showImGui = true;                             // показывать ли интерфейс ImGui

// === ПЕРЕМЕННЫЕ ДЛЯ ПЕРЕМЕЩЕНИЯ КАМЕРЫ ===
bool m_keyW = false;    // клавиша W
bool m_keyA = false;    // клавиша A
bool m_keyS = false;    // клавиша S
bool m_keyD = false;    // клавиша D
bool m_keyQ = false;    // клавиша Q (вверх)
bool m_keyE = false;    // клавиша E (вниз)

// === ПЕРЕМЕННЫЕ ДЛЯ КУБИКА === 
// не используется, но оставлено
ID3D11Buffer* m_pVertexBuffer = nullptr;
ID3D11Buffer* m_pIndexBuffer = nullptr;
ID3D11VertexShader* m_pVertexShader = nullptr;       // основной вершинный шейдер для ландшафта
ID3D11PixelShader* m_pPixelShader = nullptr;         // основной пиксельный шейдер для ландшафта
ID3D11InputLayout* m_pInputLayout = nullptr;         // описание входных данных для основного шейдера

// === ПЕРЕМЕННЫЕ ДЛЯ ЛАНДШАФТА ===
ID3D11Buffer* m_pTerrainVertexBuffer = nullptr;      // вершинный буфер ландшафта
ID3D11Buffer* m_pTerrainIndexBuffer = nullptr;       // индексный буфер ландшафта
ID3D11Buffer* m_pTerrainGeomBuffer = nullptr;        // геометрический буфер ландшафта
UINT m_terrainIndexCount = 0;                         // количество индексов
UINT m_terrainGridSizeX = 256;                        // кол-во вершин по X - больше не являются ограничением, т.к. значение позже перезаписывается (размер ограничен только шагом)
UINT m_terrainGridSizeZ = 256;                        // кол-во вершин по Z
// настройка масштаба ландшафта
// исходные данные: 5 км × 5 км × 2 км → отношение высоты к ширине = 2/5 = 0.4 (??) фигня какая-то(
float m_terrainWidth = 20.0f;                          // ширина ландшафта по X
float m_terrainDepth = 20.0f;                          // глубина ландшафта по Z
float m_terrainHeightScale = 3.5f;                     // масштаб высоты

// === ПЕРЕМЕННЫЕ ДЛЯ МАТРИЦ И УПРАВЛЕНИЯ ===
ID3D11Buffer* m_pSceneBuffer = nullptr;               // константный буфер сцены

// === ПЕРЕМЕННЫЕ ДЛЯ БУФЕРА ГЛУБИНЫ ===
ID3D11Texture2D* m_pDepthBuffer = nullptr;            // текстура глубины
ID3D11DepthStencilView* m_pDepthStencilView = nullptr; // представление глубины/трафарета
ID3D11RasterizerState* m_pRasterizerState = nullptr;  // состояние растеризатора
ID3D11RasterizerState* m_pRasterizerCullFront = nullptr;

// === ПЕРЕМЕННЫЕ ДЛЯ СОСТОЯНИЙ ГЛУБИНЫ ===
ID3D11DepthStencilState* m_pNormalDepthState = nullptr;     // Для непрозрачных объектов (reversed depth)
ID3D11DepthStencilState* m_pSkyboxDepthState = nullptr;

// === ПЕРЕМЕННЫЕ ДЛЯ BLEND STATES ===
ID3D11BlendState* m_pOpaqueBlendState = nullptr;    // Для непрозрачных объектов (без смешивания)

// === ПЕРЕМЕННЫЕ ДЛЯ ТЕКСТУР ===
ID3D11Texture2D* m_pTexture = nullptr;                // основная текстура (альбедо)
ID3D11ShaderResourceView* m_pTextureView = nullptr;   // шейдерное представление основной текстуры
ID3D11SamplerState* m_pSampler = nullptr;             // сэмплер для текстурирования

ID3D11SamplerState* g_pSkyboxSampler = nullptr;
ID3D11SamplerState* g_pLinearSampler = nullptr;
ID3D11SamplerState* g_pLinearMipSampler = nullptr;


UINT m_width = 1280;                                  // ширина окна
UINT m_height = 720;                                  // высота окна

// === ПЕРЕМЕННЫЕ ДЛЯ УПРАВЛЕНИЯ КАМЕРОЙ ===
// === ПЕРЕМЕННЫЕ ДЛЯ КАМЕРЫ (режим полёта) ===
DirectX::XMFLOAT3 m_camPos = { 0.0f, 0.0f, 0.0f };
float m_yaw = 0.0f;          // горизонтальный угол (в радианах)
float m_pitch = 0.0f;        // вертикальный угол
float m_heightOffset = 2.0f; // расстояние над поверхностью
std::vector<float> m_terrainHeights; // высоты ландшафта для интерполяции

DirectX::XMFLOAT4X4 m_viewMatrix;
DirectX::XMFLOAT4X4 m_projMatrix;

bool m_rbPressed = false;                              // нажата ли правая кнопка мыши
int m_prevMouseX = 0, m_prevMouseY = 0;                // предыдущие координаты мыши
bool m_lbPressed = false;                              // нажата ли левая кнопка мыши

static const float CameraRotationSpeed = DirectX::XM_PI * 2.0f; // скорость вращения камеры
// Переменные для времени (плавное движение)
LARGE_INTEGER g_prevTime;
LARGE_INTEGER g_freq;
float g_deltaTime = 0.0f;

// Макрос безопасного освобождения COM-объектов
#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }

// === СТРУКТУРА ВЕРШИНЫ С НОРМАЛЯМИ И КАСАТЕЛЬНЫМИ ===
struct TextureTangentVertex
{
    float x, y, z;           // Позиция
    float tx, ty, tz;        // Касательный вектор (tangent)
    float nx, ny, nz;        // Нормаль
    float u, v;              // Текстурные координаты
};

// Инициализация шейдеров (основных для ландшафта)
bool InitShaders()
{
    HRESULT result = S_OK;
    ID3DBlob* pVSBlob = nullptr;
    ID3DBlob* pPSBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Компиляция вершинного шейдера из файла
    result = D3DCompileFromFile(L"TerrainVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", flags, 0, &pVSBlob, &pErrorBlob);
    if (FAILED(result))
    {
        if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        return false;
    }
    result = m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pVertexShader);
    if (FAILED(result))
    {
        pVSBlob->Release();
        OutputDebugString(L"Ошибка создания TerrainVS\n");
        return false;
    }
    // Проверка привязки вершинного шейдера
    if (!m_pVertexShader) {
        OutputDebugString(L"Не удается привязать TerrainVS к пайплайну\n");
        pVSBlob->Release();
        return false;
    }

    // Компиляция пиксельного шейдера из файла
    result = D3DCompileFromFile(L"TerrainPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    if (FAILED(result))
    {
        if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        pVSBlob->Release();
        return false;
    }
    result = m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pPixelShader);
    if (FAILED(result))
    {
        pVSBlob->Release();
        pPSBlob->Release();
        OutputDebugString(L"Ошибка создания TerrainPS\n");
        return false;
    }
    // Проверка привязки пиксельного шейдера
    if (!m_pPixelShader) {
        OutputDebugString(L"Не удается привязать TerrainPS к пайплайну\n");
        pPSBlob->Release();
        return false;
    }

    // Создание input layout (остаётся без изменений)
    static const D3D11_INPUT_ELEMENT_DESC InputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    result = m_pDevice->CreateInputLayout(InputDesc, 4,
        pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pInputLayout);

    pVSBlob->Release();
    pPSBlob->Release();
    if (pErrorBlob) pErrorBlob->Release();

    // Проверка привязки Input Layout
    if (!m_pInputLayout) {
        OutputDebugString(L"Не удается привязать Input Layout к пайплайну\n");
        return false;
    }

    // Привязка шейдеров и input layout к пайплайну
    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);

    // Проверка привязки шейдеров
    ID3D11VertexShader* boundVS = nullptr;
    ID3D11PixelShader* boundPS = nullptr;
    m_pDeviceContext->VSGetShader(&boundVS, nullptr, nullptr);
    m_pDeviceContext->PSGetShader(&boundPS, nullptr, nullptr);

    if (boundVS != m_pVertexShader || boundPS != m_pPixelShader) {
        OutputDebugString(L"Ошибка привязки шейдеров TerrainPS и TerrainVS к пайплайну\n");
        return false;
    }

    return SUCCEEDED(result);
}

// === ФУНКЦИЯ СОЗДАНИЯ СФЕРЫ ===
// Заполняет массивы позиций вершин и индексов для сферы заданной точности (latCells, lonCells)
void CreateSphere(size_t latCells, size_t lonCells, UINT16* pIndices, DirectX::XMFLOAT3* pPos)
{
    // Генерируем вершины сферы (направления из центра)
    for (size_t lat = 0; lat < latCells + 1; lat++)
    {
        for (size_t lon = 0; lon < lonCells + 1; lon++)
        {
            int index = (int)(lat * (lonCells + 1) + lon);
            float lonAngle = 2.0f * (float)DirectX::XM_PI * lon / lonCells + (float)DirectX::XM_PI;
            float latAngle = -(float)DirectX::XM_PI / 2 + (float)DirectX::XM_PI * lat / latCells;

            // Вычисляем единичный вектор направления
            DirectX::XMFLOAT3 r;
            r.x = sinf(lonAngle) * cosf(latAngle);
            r.y = sinf(latAngle);
            r.z = cosf(lonAngle) * cosf(latAngle);

            pPos[index] = r;
        }
    }

    // Генерируем индексы для треугольников (два треугольника на ячейку)
    for (size_t lat = 0; lat < latCells; lat++)
    {
        for (size_t lon = 0; lon < lonCells; lon++)
        {
            size_t index = lat * lonCells * 6 + lon * 6;
            pIndices[index + 0] = (UINT16)(lat * (lonCells + 1) + lon + 0);
            pIndices[index + 2] = (UINT16)(lat * (lonCells + 1) + lon + 1);
            pIndices[index + 1] = (UINT16)((lat + 1) * (lonCells + 1) + lon);
            pIndices[index + 3] = (UINT16)(lat * (lonCells + 1) + lon + 1);
            pIndices[index + 5] = (UINT16)((lat + 1) * (lonCells + 1) + lon + 1);
            pIndices[index + 4] = (UINT16)((lat + 1) * (lonCells + 1) + lon);
        }
    }
}

// Создание сферы для скайбокса (все вершины на расстоянии 1 от центра)
void CreateSkyboxSphere(UINT slices, UINT stacks, std::vector<float>& vertices, std::vector<UINT>& indices)
{
    vertices.clear();
    indices.clear();

    for (UINT stack = 0; stack <= stacks; ++stack)
    {
        float phi = (float)stack / (float)stacks * DirectX::XM_PI;          // 0..PI
        float y = cosf(phi);
        float r = sinf(phi);

        for (UINT slice = 0; slice <= slices; ++slice)
        {
            float theta = (float)slice / (float)slices * DirectX::XM_2PI;   // 0..2PI
            float x = r * cosf(theta);
            float z = r * sinf(theta);
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }

    for (UINT stack = 0; stack < stacks; ++stack)
    {
        for (UINT slice = 0; slice < slices; ++slice)
        {
            UINT i0 = stack * (slices + 1) + slice;
            UINT i1 = (stack + 1) * (slices + 1) + slice;
            UINT i2 = (stack + 1) * (slices + 1) + slice + 1;
            UINT i3 = stack * (slices + 1) + slice + 1;

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i0);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }
}

// === ФУНКЦИИ ЗАГРУЗКИ ТЕКСТУР И КАРТЫ НОРМАЛЕЙ ИЗ DDS и PNG ===

// Загрузка основной текстуры (альбедо) из PNG
bool LoadTexture()
{
    HRESULT result = S_OK;

    TextureDesc textureDesc;
    // Загружаем PNG  (используем функцию из Texture.cpp)
    if (!LoadPNG(L"landscape/Terrain003_4K_Color.png", textureDesc))   // имя файла
        return false;

    // Проверка поддержки формата (теперь это несжатый RGBA)
    UINT formatSupport = 0;
    if (FAILED(m_pDevice->CheckFormatSupport(textureDesc.fmt, &formatSupport)) ||
        !(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D))
    {
        free(textureDesc.pData);
        return false;
    }

    // Описание текстуры 2D
    D3D11_TEXTURE2D_DESC desc = {};
    desc.ArraySize = 1;
    // Для альбедо используем sRGB-формат, если он поддерживается
    DXGI_FORMAT sRGBFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (SUCCEEDED(m_pDevice->CheckFormatSupport(sRGBFormat, &formatSupport)) &&
        (formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D))
    {
        desc.Format = sRGBFormat;
    }
    else
    {
        desc.Format = textureDesc.fmt; // fallback на линейный
    }
    desc.MipLevels = textureDesc.mipmapsCount;          // сколько mip-уровней загружено
    desc.Usage = D3D11_USAGE_IMMUTABLE;                 // неизменяемая (после загрузки)
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;        // для использования в шейдерах
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Height = textureDesc.height;
    desc.Width = textureDesc.width;

    // Подготовка массива субресурсов (по одному на каждый MIP-уровень)
    std::vector<D3D11_SUBRESOURCE_DATA> data(desc.MipLevels);
    const BYTE* pSrcData = reinterpret_cast<const BYTE*>(textureDesc.pData);
    UINT w = textureDesc.width;
    UINT h = textureDesc.height;

    for (UINT i = 0; i < desc.MipLevels; i++)
    {
        UINT pitch = w * 4;   // 4 байта на пиксель (RGBA)
        UINT levelSize = pitch * h;

        data[i].pSysMem = pSrcData;
        data[i].SysMemPitch = pitch;
        data[i].SysMemSlicePitch = 0;

        pSrcData += levelSize;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    // Создание текстуры
    result = m_pDevice->CreateTexture2D(&desc, data.data(), &m_pTexture);
    free(textureDesc.pData);   // освобождаем загруженные данные
    if (FAILED(result))
    {
        OutputDebugString(L"LoadTexture: Failed to create texture\n");
        return false;
    }

    // Создание шейдерного представления (SRV) для одной текстуры
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    result = m_pDevice->CreateShaderResourceView(m_pTexture, &srvDesc, &m_pTextureView);
    if (FAILED(result))
        return false;

    // Создание сэмплера (анизотропная фильтрация)
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MinLOD = -FLT_MAX;
    samplerDesc.MaxLOD = FLT_MAX;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 16;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    result = m_pDevice->CreateSamplerState(&samplerDesc, &m_pSampler);
    return SUCCEEDED(result);
}

// Загрузка карты нормалей из DDS (сжатый формат BC)
bool LoadNormalMap()
{
    HRESULT result = S_OK;

    TextureDesc textureDesc;
    if (!LoadDDS(L"landscape/Terrain003_4K_NM2.dds", textureDesc))
    {
        return false;
    }

    // Проверяем поддержку формата
    UINT formatSupport = 0;
    if (FAILED(m_pDevice->CheckFormatSupport(textureDesc.fmt, &formatSupport)) ||
        !(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D))
    {
        free(textureDesc.pData);
        return false;
    }

    // Создаем текстуру (аналогично LoadTexture, но для сжатого формата)
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = textureDesc.fmt;
    desc.ArraySize = 1;
    desc.MipLevels = textureDesc.mipmapsCount;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Height = textureDesc.height;
    desc.Width = textureDesc.width;

    // Для сжатых форматов шаг строки вычисляется через блоки 4x4
    UINT32 blockWidth = DivUp(desc.Width, 4u);
    UINT32 blockHeight = DivUp(desc.Height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(desc.Format);
    const char* pSrcData = reinterpret_cast<const char*>(textureDesc.pData);

    std::vector<D3D11_SUBRESOURCE_DATA> data;
    data.resize(desc.MipLevels);
    for (UINT32 i = 0; i < desc.MipLevels; i++)
    {
        data[i].pSysMem = pSrcData;
        data[i].SysMemPitch = pitch;
        data[i].SysMemSlicePitch = 0;

        pSrcData += pitch * blockHeight;
        blockHeight = std::max<UINT32>(1u, blockHeight / 2);
        blockWidth = std::max<UINT32>(1u, blockWidth / 2);
        pitch = blockWidth * GetBytesPerBlock(desc.Format);
    }

    result = m_pDevice->CreateTexture2D(&desc, data.data(), &m_pTextureNM);
    free(textureDesc.pData);

    if (FAILED(result))
        return false;

    // Создаем view для текстуры
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    result = m_pDevice->CreateShaderResourceView(m_pTextureNM, &srvDesc, &m_pTextureViewNM);
    return SUCCEEDED(result);
}

// Загрузка текстуры деталей (черно-белая карта для наложения микрорельефа)
bool LoadDetailTexture()
{
    HRESULT result = S_OK;

    TextureDesc textureDesc;
    if (!LoadPNG(L"landscape/Terrain003_4K_Details.png", textureDesc))
        return false;

    // Проверка поддержки формата
    UINT formatSupport = 0;
    if (FAILED(m_pDevice->CheckFormatSupport(textureDesc.fmt, &formatSupport)) ||
        !(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D))
    {
        free(textureDesc.pData);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = textureDesc.fmt;                     // DXGI_FORMAT_R8G8B8A8_UNORM
    desc.ArraySize = 1;
    desc.MipLevels = textureDesc.mipmapsCount;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Height = textureDesc.height;
    desc.Width = textureDesc.width;

    // Подготовка массива субресурсов
    std::vector<D3D11_SUBRESOURCE_DATA> data(desc.MipLevels);
    const BYTE* pSrcData = reinterpret_cast<const BYTE*>(textureDesc.pData);
    UINT w = textureDesc.width;
    UINT h = textureDesc.height;

    for (UINT i = 0; i < desc.MipLevels; i++)
    {
        UINT pitch = w * 4;
        UINT levelSize = pitch * h;

        data[i].pSysMem = pSrcData;
        data[i].SysMemPitch = pitch;
        data[i].SysMemSlicePitch = 0;

        pSrcData += levelSize;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    ID3D11Texture2D* pDetailTexture = nullptr;
    result = m_pDevice->CreateTexture2D(&desc, data.data(), &pDetailTexture);
    free(textureDesc.pData);
    if (FAILED(result))
    {
        OutputDebugString(L"LoadDetailTexture: Failed to create texture\n");
        return false;
    }

    // Создание шейдерного представления
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    result = m_pDevice->CreateShaderResourceView(pDetailTexture, &srvDesc, &m_pDetailTextureView);
    pDetailTexture->Release(); // теперь управляет SRV
    if (FAILED(result))
        return false;

    return true;
}

// Загрузка текстуры Flow 
bool LoadFlowTexture()
{
    HRESULT result = S_OK;

    TextureDesc textureDesc;
    if (!LoadPNG(L"landscape/Terrain003_4K_Flow.png", textureDesc))
        return false;

    // Проверка поддержки формата
    UINT formatSupport = 0;
    if (FAILED(m_pDevice->CheckFormatSupport(textureDesc.fmt, &formatSupport)) ||
        !(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D))
    {
        free(textureDesc.pData);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = textureDesc.fmt;                     // DXGI_FORMAT_R8G8B8A8_UNORM
    desc.ArraySize = 1;
    desc.MipLevels = textureDesc.mipmapsCount;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Height = textureDesc.height;
    desc.Width = textureDesc.width;

    // Подготовка массива субресурсов
    std::vector<D3D11_SUBRESOURCE_DATA> data(desc.MipLevels);
    const BYTE* pSrcData = reinterpret_cast<const BYTE*>(textureDesc.pData);
    UINT w = textureDesc.width;
    UINT h = textureDesc.height;

    for (UINT i = 0; i < desc.MipLevels; i++)
    {
        UINT pitch = w * 4;
        UINT levelSize = pitch * h;

        data[i].pSysMem = pSrcData;
        data[i].SysMemPitch = pitch;
        data[i].SysMemSlicePitch = 0;

        pSrcData += levelSize;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    ID3D11Texture2D* pFlowTexture = nullptr;
    result = m_pDevice->CreateTexture2D(&desc, data.data(), &pFlowTexture);
    free(textureDesc.pData);
    if (FAILED(result))
    {
        OutputDebugString(L"LoadFlowTexture: Failed to create texture\n");
        return false;
    }

    // Создание шейдерного представления
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    result = m_pDevice->CreateShaderResourceView(pFlowTexture, &srvDesc, &m_pFlowTextureView);
    pFlowTexture->Release(); // теперь управляет SRV
    if (FAILED(result))
        return false;

    return true;
}

// Загрузка текстуры шероховатости (roughness) из PNG
bool LoadRoughnessTexture()
{
    HRESULT result = S_OK;

    TextureDesc textureDesc;
    if (!LoadPNG(L"landscape/Terrain003_4K_Protrusion.png", textureDesc))   // имя файла
        return false;

    // Проверка поддержки формата (линейный RGBA)
    UINT formatSupport = 0;
    if (FAILED(m_pDevice->CheckFormatSupport(textureDesc.fmt, &formatSupport)) ||
        !(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D))
    {
        free(textureDesc.pData);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = textureDesc.fmt;                     // DXGI_FORMAT_R8G8B8A8_UNORM
    desc.ArraySize = 1;
    desc.MipLevels = textureDesc.mipmapsCount;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Height = textureDesc.height;
    desc.Width = textureDesc.width;

    // Подготовка массива субресурсов (все MIP-уровни)
    std::vector<D3D11_SUBRESOURCE_DATA> data(desc.MipLevels);
    const BYTE* pSrcData = reinterpret_cast<const BYTE*>(textureDesc.pData);
    UINT w = textureDesc.width;
    UINT h = textureDesc.height;

    for (UINT i = 0; i < desc.MipLevels; i++)
    {
        UINT pitch = w * 4;   // 4 байта на пиксель (RGBA)
        UINT levelSize = pitch * h;

        data[i].pSysMem = pSrcData;
        data[i].SysMemPitch = pitch;
        data[i].SysMemSlicePitch = 0;

        pSrcData += levelSize;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    ID3D11Texture2D* pRoughnessTexture = nullptr;
    result = m_pDevice->CreateTexture2D(&desc, data.data(), &pRoughnessTexture);
    free(textureDesc.pData);
    if (FAILED(result))
    {
        OutputDebugString(L"LoadRoughnessTexture: Failed to create texture\n");
        return false;
    }

    // Создание шейдерного представления (SRV)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    result = m_pDevice->CreateShaderResourceView(pRoughnessTexture, &srvDesc, &m_pRoughnessTextureView);
    pRoughnessTexture->Release(); // теперь управляет SRV
    if (FAILED(result))
        return false;

    return true;
}

// Инициализация маленьких сфер для визуализации источников света
bool InitSmallSpheres()
{
    HRESULT result = S_OK;

    // Параметры сферы 
    static const size_t SphereSteps = 8;  // количество сегментов (низкополигональная сфера)
    std::vector<DirectX::XMFLOAT3> sphereVertices;
    std::vector<UINT16> indices;

    size_t vertexCount = (SphereSteps + 1) * (SphereSteps + 1);
    size_t indexCount = SphereSteps * SphereSteps * 6;
    m_smallSphereIndexCount = (UINT)indexCount;

    sphereVertices.resize(vertexCount);
    indices.resize(indexCount);

    // Генерируем геометрию сферы
    CreateSphere(SphereSteps, SphereSteps, indices.data(), sphereVertices.data());

    // Уменьшаем размер сферы (источники света маленькие)
    for (auto& v : sphereVertices)
    {
        v.x *= 0.125f;
        v.y *= 0.125f;
        v.z *= 0.125f;
    }

    // Создаем vertex buffer (вершинный буфер)
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = (UINT)(sphereVertices.size() * sizeof(DirectX::XMFLOAT3));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = 0;
    vbDesc.MiscFlags = 0;
    vbDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA vbData;
    vbData.pSysMem = sphereVertices.data();
    vbData.SysMemPitch = 0;
    vbData.SysMemSlicePitch = 0;

    result = m_pDevice->CreateBuffer(&vbDesc, &vbData, &m_pSmallSphereVertexBuffer);
    if (FAILED(result)) return false;

    // Создаем index buffer (индексный буфер)
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = (UINT)(indices.size() * sizeof(UINT16));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibDesc.CPUAccessFlags = 0;
    ibDesc.MiscFlags = 0;
    ibDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = indices.data();
    ibData.SysMemPitch = 0;
    ibData.SysMemSlicePitch = 0;

    result = m_pDevice->CreateBuffer(&ibDesc, &ibData, &m_pSmallSphereIndexBuffer);
    if (FAILED(result)) return false;

    // Шейдеры для маленьких сфер (просто закраска цветом)
    const char* smallSphereVSSource = R"(
        cbuffer GeomBuffer : register(b0)
        {
            float4x4 m;
            float4 color;
        };
        
        cbuffer SceneBuffer : register(b1)
        {
            float4x4 vp;
            float4 cameraPos;
            float4 lightInfo;
            struct Light { float4 pos; float4 color; float intensity; float3 padding; };
            Light lights[10];
            float4 ambientColor;
        };

        struct VSInput
        {
            float3 pos : POSITION;
        };

        struct VSOutput
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        VSOutput main(VSInput vertex)
        {
            VSOutput result;
            float4 worldPos = mul(float4(vertex.pos, 1.0), m);
            result.pos = mul(worldPos, vp);
            result.color = color;
            return result;
        }
    )";

    const char* smallSpherePSSource = R"(
        struct VSOutput
        {
            float4 pos : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(VSOutput pixel) : SV_Target0
        {
            return pixel.color;
        }
    )";

    // Компилируем и создаем шейдеры
    ID3DBlob* pSmallSphereVSBlob = nullptr;
    ID3DBlob* pSmallSpherePSBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    result = D3DCompile(smallSphereVSSource, strlen(smallSphereVSSource),
        "SmallSphereVS", nullptr, nullptr, "main", "vs_5_0", flags, 0, &pSmallSphereVSBlob, &pErrorBlob);
    if (FAILED(result)) {
        if (pErrorBlob) {
            OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return false;
    }

    result = m_pDevice->CreateVertexShader(pSmallSphereVSBlob->GetBufferPointer(),
        pSmallSphereVSBlob->GetBufferSize(), nullptr, &m_pSmallSphereVertexShader);
    if (FAILED(result)) {
        pSmallSphereVSBlob->Release();
        return false;
    }

    result = D3DCompile(smallSpherePSSource, strlen(smallSpherePSSource),
        "SmallSpherePS", nullptr, nullptr, "main", "ps_5_0", flags, 0, &pSmallSpherePSBlob, &pErrorBlob);
    if (FAILED(result)) {
        if (pErrorBlob) {
            OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        pSmallSphereVSBlob->Release();
        return false;
    }

    result = m_pDevice->CreatePixelShader(pSmallSpherePSBlob->GetBufferPointer(),
        pSmallSpherePSBlob->GetBufferSize(), nullptr, &m_pSmallSpherePixelShader);
    if (FAILED(result)) {
        pSmallSphereVSBlob->Release();
        pSmallSpherePSBlob->Release();
        return false;
    }

    // Создаем input layout для маленьких сфер (только позиция)
    D3D11_INPUT_ELEMENT_DESC smallSphereLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    result = m_pDevice->CreateInputLayout(smallSphereLayout, 1,
        pSmallSphereVSBlob->GetBufferPointer(),
        pSmallSphereVSBlob->GetBufferSize(),
        &m_pSmallSphereInputLayout);

    pSmallSphereVSBlob->Release();
    pSmallSpherePSBlob->Release();
    if (pErrorBlob) pErrorBlob->Release();

    if (FAILED(result)) return false;

    // Создаем константные буферы для каждой маленькой сферы
    D3D11_BUFFER_DESC smallSphereGeomBufferDesc = {};
    smallSphereGeomBufferDesc.ByteWidth = sizeof(SmallSphereGeomBuffer);
    smallSphereGeomBufferDesc.Usage = D3D11_USAGE_DEFAULT;  // будет обновляться через UpdateSubresource
    smallSphereGeomBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    smallSphereGeomBufferDesc.CPUAccessFlags = 0;
    smallSphereGeomBufferDesc.MiscFlags = 0;
    smallSphereGeomBufferDesc.StructureByteStride = 0;

    // Инициализируем все буферы
    for (int i = 0; i < 10; i++)
    {
        SmallSphereGeomBuffer geomData;
        DirectX::XMMATRIX model = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX modelT = DirectX::XMMatrixTranspose(model);
        DirectX::XMStoreFloat4x4(&geomData.m, modelT);
        geomData.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

        D3D11_SUBRESOURCE_DATA initData = { &geomData, 0, 0 };
        result = m_pDevice->CreateBuffer(&smallSphereGeomBufferDesc, &initData, &m_pSmallSphereGeomBuffers[i]);
        if (FAILED(result)) return false;
    }

    return true;
}


// === ФУНКЦИЯ ИНИЦИАЛИЗАЦИИ ПОСТПРОЦЕССИНГА ===
bool InitPostProcess()
{
    HRESULT result = S_OK;

    // Шейдеры для постпроцессинга (мульти-эффект)
    // Вершинный шейдер рисует один треугольник, покрывающий весь экран (используя SV_VertexID)
    const char* postProcessVSSource = R"(
        struct VSInput
        {
            uint vertexId : SV_VertexID;
        };
        
        struct VSOutput
        {
            float4 pos : SV_POSITION;
            float2 uv : TEXCOORD;
        };
        
        VSOutput main(VSInput vertex)
        {
            VSOutput result;
            
            float4 pos = float4(0, 0, 0, 0);
            
            // Один треугольник вместо квадрата: три вершины покрывают экран
            switch (vertex.vertexId)
            {
                case 0:
                    pos = float4(-1, 1, 0, 1);
                    break;
                case 1:
                    pos = float4(3, 1, 0, 1);
                    break;
                case 2:
                    pos = float4(-1, -3, 0, 1);
                    break;
            }
            
            result.pos = pos;
            result.uv = float2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
            
            return result;
        }
    )";

    const char* postProcessPSSource = R"(
    struct VSOutput
    {
        float4 pos : SV_POSITION;
        float2 uv : TEXCOORD;
    };
    
    Texture2D colorTexture : register(t0);
    SamplerState colorSampler : register(s0);
    
    // Константный буфер для постпроцессинга
    cbuffer PostProcessBuffer : register(b0)
    {
        int effectType;    // 0 - нет, 1 - сепия, 2 - холодный тон, 3 - ночной режим
        int padding[3];
    };
    
    float3 ApplySepia(float3 color)
    {
        // Коэффициенты для фильтра сепии
        float rr = 0.393f;
        float rg = 0.769f;
        float rb = 0.189f;
        
        float gr = 0.349f;
        float gg = 0.686f;
        float gb = 0.168f;
        
        float br = 0.272f;
        float bg = 0.534f;
        float bb = 0.131f;
        
        float red = (rr * color.r) + (rg * color.g) + (rb * color.b);
        float green = (gr * color.r) + (gg * color.g) + (gb * color.b);
        float blue = (br * color.r) + (bg * color.g) + (bb * color.b);
        
        return float3(red, green, blue);
    }
    
    float3 ApplyColdTint(float3 color)
    {
        // Холодный тон: усиливаем синий и зеленый, уменьшаем красный
        // Фиксированная интенсивность (без настройки)
        float3 coldTint = float3(0.6f, 0.8f, 1.0f);
        return color * coldTint;
    }
    
    float3 ApplyNightVision(float3 color)
    {
        // Ночное видение (зелено-синее)
        float gray = dot(color, float3(0.299f, 0.587f, 0.114f));
        return float3(0.1f, gray, gray);
    }
    
    float4 main(VSOutput pixel) : SV_Target0
    {
        float3 color = colorTexture.Sample(colorSampler, pixel.uv).rgb;
        float3 finalColor = color;
        
        // Применяем эффекты в зависимости от типа
        if (effectType == 1) // Сепия
        {
            finalColor = ApplySepia(color);
        }
        else if (effectType == 2) // Холодный тон
        {
            finalColor = ApplyColdTint(color);
        }
        else if (effectType == 3) // Ночной режим
        {
            finalColor = ApplyNightVision(color);
        }
        
        // Ограничиваем значения от 0 до 1
        finalColor = clamp(finalColor, 0.0f, 1.0f);
        
        return float4(finalColor, 1.0f);
    }
)";


    ID3DBlob* pPostProcessVSBlob = nullptr;
    ID3DBlob* pPostProcessPSBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Компилируем вершинный шейдер
    result = D3DCompile(
        postProcessVSSource,
        strlen(postProcessVSSource),
        "PostProcessVS",
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        flags,
        0,
        &pPostProcessVSBlob,
        &pErrorBlob
    );

    if (FAILED(result))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return false;
    }

    // Создаем вершинный шейдер
    result = m_pDevice->CreateVertexShader(
        pPostProcessVSBlob->GetBufferPointer(),
        pPostProcessVSBlob->GetBufferSize(),
        nullptr,
        &m_pPostProcessVertexShader
    );

    if (FAILED(result))
    {
        pPostProcessVSBlob->Release();
        return false;
    }

    // Компилируем пиксельный шейдер
    result = D3DCompile(
        postProcessPSSource,
        strlen(postProcessPSSource),
        "PostProcessPS",
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        flags,
        0,
        &pPostProcessPSBlob,
        &pErrorBlob
    );

    if (FAILED(result))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        pPostProcessVSBlob->Release();
        return false;
    }

    // Создаем пиксельный шейдер
    result = m_pDevice->CreatePixelShader(
        pPostProcessPSBlob->GetBufferPointer(),
        pPostProcessPSBlob->GetBufferSize(),
        nullptr,
        &m_pPostProcessPixelShader
    );

    // Освобождаем blob-объекты
    if (pPostProcessVSBlob) pPostProcessVSBlob->Release();
    if (pPostProcessPSBlob) pPostProcessPSBlob->Release();
    if (pErrorBlob) pErrorBlob->Release();

    // Создаем константный буфер для постпроцессинга (динамический, чтобы менять эффект)
    D3D11_BUFFER_DESC postProcessBufferDesc = {};
    postProcessBufferDesc.ByteWidth = sizeof(PostProcessBuffer);
    postProcessBufferDesc.Usage = D3D11_USAGE_DYNAMIC;   // будет часто обновляться
    postProcessBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    postProcessBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    postProcessBufferDesc.MiscFlags = 0;
    postProcessBufferDesc.StructureByteStride = 0;

    PostProcessBuffer initPostProcessData;
    initPostProcessData.effectType = 0;
    initPostProcessData.padding[0] = initPostProcessData.padding[1] = initPostProcessData.padding[2] = 0;

    D3D11_SUBRESOURCE_DATA postProcessInitData = { &initPostProcessData, 0, 0 };

    result = m_pDevice->CreateBuffer(&postProcessBufferDesc, &postProcessInitData, &m_pPostProcessBuffer);
    if (FAILED(result)) return false;

    return SUCCEEDED(result);
}

// Точка входа в приложение
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Для детерминированной случайности
    srand(12345);

    // Инициализация COM для WIC
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        MessageBox(NULL, L"Не удалось инициализировать COM!", L"Ошибка", MB_OK);
        return -1;
    }

    // Создание окна
    if (!InitWindow(hInstance, nCmdShow))
    {
        MessageBox(NULL, L"Не удалось создать окно!", L"Ошибка", MB_OK);
        return -1;
    }

    // Инициализация DirectX
    if (!InitDirectX())
    {
        MessageBox(NULL, L"Не удалось инициализировать DirectX!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Инициализация таймера для плавного движения
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_prevTime);

    // Инициализация ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::StyleColorsDark();

    // Загружаем шрифт с кириллицей (можно указать другой путь к .ttf)
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Calibri.ttf", 14.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
    if (!font)
    {
        // Если шрифт не загрузился - используем стандартный (без кириллицы)
        io.Fonts->AddFontDefault();
    }

    // Инициализация ImGui для Win32 и DirectX11
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(m_pDevice, m_pDeviceContext);

    // Создание шейдеров
    if (!InitShaders())
    {
        MessageBox(NULL, L"Не удалось создать шейдеры!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Инициализация ландшафта (генерация вершин, индексов, загрузка карты высот)
    if (!InitTerrain())
    {
        MessageBox(NULL, L"Не удалось инициализировать ландшафт!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Создание константных буферов
    if (!InitBuffers())
    {
        MessageBox(NULL, L"Не удалось инициализировать буферы!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Загружаем основную текстуру
    if (!LoadTexture())
    {
        MessageBox(NULL, L"Не удалось загрузить текстуры!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Загружаем карту нормалей
    if (!LoadNormalMap())
    {
        MessageBox(NULL, L"Не удалось загрузить карту нормалей!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Загружаем текстуру деталей
    if (!LoadDetailTexture())
    {
        MessageBox(NULL, L"Не удалось загрузить текстуру деталей!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Загружаем текстуру Flow
    if (!LoadFlowTexture())
    {
        MessageBox(NULL, L"Не удалось загрузить текстуру Flow!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Загружаем текстуру шероховатости (roughness)
    if (!LoadRoughnessTexture())
    {
        MessageBox(NULL, L"Не удалось загрузить текстуру шероховатости!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Инициализация камеры (режим полёта)
    m_camPos.x = 0.0f;
    m_camPos.z = -5.0f;                     // чтобы видеть центр
    m_camPos.y = GetTerrainHeight(m_camPos.x, m_camPos.z) + m_heightOffset;
    m_yaw = -DirectX::XM_PIDIV4;            // примерно -45° (как было раньше)
    m_pitch = 0.2f;                          // немного смотрим вниз

    // Инициализация маленьких сфер для визуализации источников
    if (!InitSmallSpheres())
    {
        MessageBox(NULL, L"Не удалось инициализировать маленькие сферы!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Инициализация постпроцессинга
    if (!InitPostProcess())
    {
        MessageBox(NULL, L"Не удалось инициализировать постпроцессинг!", L"Ошибка", MB_OK);
        Cleanup();
        return -1;
    }

    // Инициализация источников света
    m_lightCount = 0;
    // Основной источник
    m_lights[0].pos = DirectX::XMFLOAT4(-5.0f, 6.9f, -1.0f, 1.0f);
    m_lights[0].color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f); // белый цвет
    m_lights[0].padding[0] = m_lights[0].padding[1] = m_lights[0].padding[2] = 0.0f;
    // Эмбиент
    //m_ambientColor = DirectX::XMFLOAT4(0.3f, 0.3f, 0.4f, 1.0f); // светлый ambient
    m_ambientColor = DirectX::XMFLOAT4(0.05f, 0.05f, 0.1f, 1.0f); //темный эмбиент, чтобы видеть источники
    //m_ambientColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.1f, 1.0f); //светлый эмбиент
    UpdateLightIntensities();  // установит intensity в зависимости от текущего m_flowMode

    MSG msg = {};
    bool exit = false;

    // Главный цикл сообщений
    while (!exit)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                exit = true;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // Если нет сообщений, рендерим кадр
            g_ibl.Update();   // генерирует по одному шагу за кадр
            Render();
        }
    }

    Cleanup();
    return (int)msg.wParam;
}

// Создание окна
bool InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;  // оконная процедура
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"DirectX11Window";

    if (!RegisterClassEx(&wc))
        return false;

    RECT rc = {};
    rc.left = 0;
    rc.right = m_width;
    rc.top = 0;
    rc.bottom = m_height;

    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    // Создание окна с заданными размерами
    g_hWnd = CreateWindow(
        L"DirectX11Window",
        L"DirectX 11 - Landscape",
        WS_OVERLAPPEDWINDOW,
        100, 100,
        rc.right - rc.left,
        rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd)
        return false;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    return true;
}

// Оконная процедура (обработка сообщений)
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Передаем сообщения в ImGui
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_SIZE:
    {
        UINT newWidth = LOWORD(lParam);
        UINT newHeight = HIWORD(lParam);

        // Если цепочка обмена существует и размеры валидны, изменяем размер
        if (m_pSwapChain && newWidth > 0 && newHeight > 0)
        {
            ResizeSwapChain(newWidth, newHeight);
        }
    }
    break;

    case WM_RBUTTONDOWN:
        m_rbPressed = true;
        m_prevMouseX = GET_X_LPARAM(lParam);  // Используем GET_X_LPARAM
        m_prevMouseY = GET_Y_LPARAM(lParam);  // Используем GET_Y_LPARAM
        SetCapture(hWnd);  // захватываем мышь для получения сообщений даже вне окна
        break;

    case WM_RBUTTONUP:
        m_rbPressed = false;
        ReleaseCapture();
        break;

    case WM_LBUTTONDOWN:
        m_lbPressed = true;
        m_prevMouseX = GET_X_LPARAM(lParam);
        m_prevMouseY = GET_Y_LPARAM(lParam);
        SetCapture(hWnd);
        break;

    case WM_LBUTTONUP:
        m_lbPressed = false;
        ReleaseCapture();
        break;

    case WM_MOUSEMOVE:
        if (m_rbPressed || m_lbPressed)
        {
            int currentX = GET_X_LPARAM(lParam);
            int currentY = GET_Y_LPARAM(lParam);

            float dx, dy;

            if (m_rbPressed)
            {
                // Правая кнопка: инвертированное управление (как было)
                dx = -(float)(currentX - m_prevMouseX) / m_width * CameraRotationSpeed;
                dy = (float)(currentY - m_prevMouseY) / m_width * CameraRotationSpeed;
            }
            else // m_lbPressed
            {
                // Левая кнопка: обычное управление (не инвертированное)
                dx = (float)(currentX - m_prevMouseX) / m_width * CameraRotationSpeed;
                dy = -(float)(currentY - m_prevMouseY) / m_width * CameraRotationSpeed;
            }

            m_yaw += dx;
            m_pitch += dy;

            // Ограничиваем pitch, чтобы не перевернуться
            m_pitch = std::min<float>(std::max<float>(m_pitch, -(float)DirectX::XM_PIDIV2 + 0.001f), (float)DirectX::XM_PIDIV2 - 0.001f);

            m_prevMouseX = currentX;
            m_prevMouseY = currentY;
        }
        break;

    case WM_MOUSEWHEEL:
    {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        m_heightOffset -= delta / 100.0f;
        if (m_heightOffset < 0.1f)
            m_heightOffset = 0.1f;
    }
    break;

    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_TAB:
            m_showImGui = !m_showImGui;  // переключение интерфейса по Tab
            break;
        case 'W': m_keyW = true; break;
        case 'A': m_keyA = true; break;
        case 'S': m_keyS = true; break;
        case 'D': m_keyD = true; break;
        case 'Q': m_keyQ = true; break;
        case 'E': m_keyE = true; break;
        default: break;
        }
        break;

    case WM_KEYUP:
        switch (wParam)
        {
        case 'W': m_keyW = false; break;
        case 'A': m_keyA = false; break;
        case 'S': m_keyS = false; break;
        case 'D': m_keyD = false; break;
        case 'Q': m_keyQ = false; break;
        case 'E': m_keyE = false; break;
        default: break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

// Инициализация Direct3D
bool InitDirectX()
{
    HRESULT result = S_OK;

    // Описание цепочки обмена
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 2;  // двойная буферизация
    swapChainDesc.BufferDesc.Width = m_width;
    swapChainDesc.BufferDesc.Height = m_height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = g_hWnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // современный режим переключения

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;  // включаем отладочный слой в Debug
#endif

    // Создаем устройство и цепочку обмена
    result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &swapChainDesc,
        &m_pSwapChain,
        &m_pDevice,
        nullptr,
        &m_pDeviceContext
    );

    if (FAILED(result))
    {
        // Если не получилось с аппаратным драйвером, пробуем WARP (программный рендерер)
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createFlags,
            featureLevels,
            1,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &m_pSwapChain,
            &m_pDevice,
            nullptr,
            &m_pDeviceContext
        );
    }

    if (FAILED(result))
        return false;

    // Настройка заднего буфера (создание RTV и буфера глубины)
    if (!SetupBackBuffer())
        return false;

    // Создание промежуточного буфера цвета для постпроцессинга
    if (!InitColorBuffer())
        return false;

    // Состояние растеризатора (без отсечения задних граней)
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;  // не отсекаем грани
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = 0;
    rasterDesc.SlopeScaledDepthBias = 0.0f;
    rasterDesc.DepthBiasClamp = 0.0f;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.ScissorEnable = FALSE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.AntialiasedLineEnable = FALSE;

    result = m_pDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerState);
    if (FAILED(result)) return false;

    // РАСТЕРРАЙЗЕР ДЛЯ СКАЙБОКСА
    rasterDesc.CullMode = D3D11_CULL_FRONT;   // отсекаем передние грани
    result = m_pDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerCullFront);
    if (FAILED(result)) return false;

    // === СОЗДАНИЕ СОСТОЯНИЙ ГЛУБИНЫ ДЛЯ REVERSED DEPTH ===

    // Для непрозрачных объектов - reversed depth
    D3D11_DEPTH_STENCIL_DESC opaqueDepthDesc = {};
    opaqueDepthDesc.DepthEnable = TRUE;
    opaqueDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    opaqueDepthDesc.DepthFunc = D3D11_COMPARISON_LESS;  // REVERSED DEPTH: ближние объекты имеют большие значения глубины
    opaqueDepthDesc.StencilEnable = FALSE;

    result = m_pDevice->CreateDepthStencilState(&opaqueDepthDesc, &m_pNormalDepthState);
    if (FAILED(result)) return false;

    // Depth-stencil state для скайбокса (тест включён, запись выключена, сравнение LESS_EQUAL)
    D3D11_DEPTH_STENCIL_DESC skyboxDepthDesc = {};
    skyboxDepthDesc.DepthEnable = TRUE;
    skyboxDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;   // не пишем в глубину
    skyboxDepthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;        // отображаем, даже если глубина равна
    skyboxDepthDesc.StencilEnable = FALSE;

    result = m_pDevice->CreateDepthStencilState(&skyboxDepthDesc, &m_pSkyboxDepthState);
    if (FAILED(result)) return false;

    // === СОЗДАНИЕ BLEND STATES ===
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;          // отключаем смешивание (непрозрачные объекты)
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    result = m_pDevice->CreateBlendState(&blendDesc, &m_pOpaqueBlendState);
    if (FAILED(result)) return false;

    // === СОЗДАНИЕ СЭМПЛЕРОВ ДЛЯ СКАЙБОКСА И IBL ===

    // Сэмплер для скайбокса (Clamp, линейная фильтрация)
    D3D11_SAMPLER_DESC skyboxSampDesc = {};
    skyboxSampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    //skyboxSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    skyboxSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    skyboxSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    skyboxSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    skyboxSampDesc.MinLOD = -FLT_MAX;
    skyboxSampDesc.MaxLOD = FLT_MAX;
    skyboxSampDesc.MipLODBias = 0.0f;
    skyboxSampDesc.MaxAnisotropy = 1;
    skyboxSampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    result = m_pDevice->CreateSamplerState(&skyboxSampDesc, &g_pSkyboxSampler);
    if (FAILED(result)) return false;

    // Сэмплер для IBL (диффузная irradiance – линейная, clamp)
    D3D11_SAMPLER_DESC linearSampDesc = {};
    linearSampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    linearSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearSampDesc.MinLOD = -FLT_MAX;
    linearSampDesc.MaxLOD = 0;
    //linearSampDesc.MaxLOD = FLT_MAX;
    linearSampDesc.MipLODBias = 0.0f;
    linearSampDesc.MaxAnisotropy = 1;
    linearSampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    result = m_pDevice->CreateSamplerState(&linearSampDesc, &g_pLinearSampler);
    if (FAILED(result)) return false;

    // Сэмплер для prefiltered env map (с полной поддержкой mip-уровней)
    D3D11_SAMPLER_DESC linearMipSampDesc = {};
    linearMipSampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    linearMipSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearMipSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearMipSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    linearMipSampDesc.MinLOD = -FLT_MAX;
    linearMipSampDesc.MaxLOD = D3D11_FLOAT32_MAX;   // ключевая строка!
    linearMipSampDesc.MipLODBias = 0.0f;
    linearMipSampDesc.MaxAnisotropy = 1;
    linearMipSampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    result = m_pDevice->CreateSamplerState(&linearMipSampDesc, &g_pLinearMipSampler);
    if (FAILED(result)) return false;

    // Инициализация IBL (HDR окружение)
    if (!g_ibl.Init(m_pDevice, m_pDeviceContext))
    {
        OutputDebugString(L"IBL::Init failed\n");
        return false;
    }

    // Инициализация скайбокса
    {
        std::vector<float> skyboxVertices;
        std::vector<UINT> skyboxIndices;
        CreateSkyboxSphere(64, 32, skyboxVertices, skyboxIndices);  // 64x32 – достаточно для плавного отображения

        g_skyboxIndexCount = (UINT)skyboxIndices.size();

        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = (UINT)(skyboxVertices.size() * sizeof(float));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData = { skyboxVertices.data(), 0, 0 };
        HRESULT hr = m_pDevice->CreateBuffer(&vbDesc, &vbData, &g_pSkyboxVertexBuffer);
        if (FAILED(hr)) return false;

        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.ByteWidth = (UINT)(skyboxIndices.size() * sizeof(UINT));
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibData = { skyboxIndices.data(), 0, 0 };
        hr = m_pDevice->CreateBuffer(&ibDesc, &ibData, &g_pSkyboxIndexBuffer);
        if (FAILED(hr)) return false;

        // Константный буфер
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(SkyboxConstants);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = m_pDevice->CreateBuffer(&cbDesc, nullptr, &g_pSkyboxConstantBuffer);
        if (FAILED(hr)) return false;
    }

    // Компиляция шейдеров скайбокса (SkyboxVS.hlsl и SkyboxPS.hlsl)
    ID3DBlob* pSkyboxVSBlob = nullptr;
    ID3DBlob* pSkyboxPSBlob = nullptr;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    result = D3DCompileFromFile(L"SkyboxVS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main", "vs_5_0", flags, 0, &pSkyboxVSBlob, nullptr);
    if (FAILED(result))
    {
        OutputDebugString(L"Ошибка компиляции SkyboxVS.hlsl\n");
        return false;
    }
    result = m_pDevice->CreateVertexShader(pSkyboxVSBlob->GetBufferPointer(),
        pSkyboxVSBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);
    if (FAILED(result)) {
        OutputDebugString(L"Ошибка создания Vertex Shader\n");
        pSkyboxVSBlob->Release();
        return false;
    }
    // Проверка привязки шейдера
    if (!g_pSkyboxVS) {
        OutputDebugString(L"Не удается привязать Skybox Vertex Shader к пайплайну\n");
        return false;
    }

    result = D3DCompileFromFile(L"SkyboxPS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main", "ps_5_0", flags, 0, &pSkyboxPSBlob, nullptr);
    if (FAILED(result))
    {
        OutputDebugString(L"Ошибка компиляции SkyboxPS.hlsl\n");
        pSkyboxVSBlob->Release();
        return false;
    }
    result = m_pDevice->CreatePixelShader(pSkyboxPSBlob->GetBufferPointer(),
        pSkyboxPSBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);
    if (FAILED(result)) {
        OutputDebugString(L"Ошибка создания Pixel Shader\n");
        pSkyboxVSBlob->Release();
        pSkyboxPSBlob->Release();
        return false;
    }
    // Проверка привязки пиксельного шейдера
    if (!g_pSkyboxPS) {
        OutputDebugString(L"Не удается привязать Skybox Pixel Shader к пайплайну\n");
        pSkyboxPSBlob->Release();
        return false;
    }

    // Input layout: только позиция (3 float)
    D3D11_INPUT_ELEMENT_DESC layoutDesc = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
        0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    result = m_pDevice->CreateInputLayout(&layoutDesc, 1,
        pSkyboxVSBlob->GetBufferPointer(), pSkyboxVSBlob->GetBufferSize(),
        &g_pSkyboxInputLayout);
    pSkyboxVSBlob->Release();
    pSkyboxPSBlob->Release();
    if (FAILED(result)) {
        OutputDebugString(L"Ошибка создания Input Layout\n");
        return false;
    }
    // Проверка привязки input layout
    if (!g_pSkyboxInputLayout) {
        OutputDebugString(L"Не удается привязать Input Layout к пайплайну\n");
        return false;
    }

    // Привязка шейдеров и input layout к пайплайну
    m_pDeviceContext->IASetInputLayout(g_pSkyboxInputLayout);
    m_pDeviceContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(g_pSkyboxPS, nullptr, 0);

    // Проверка привязки шейдеров
    ID3D11VertexShader* boundVS = nullptr;
    ID3D11PixelShader* boundPS = nullptr;
    m_pDeviceContext->VSGetShader(&boundVS, nullptr, nullptr);
    m_pDeviceContext->PSGetShader(&boundPS, nullptr, nullptr);

    if (boundVS != g_pSkyboxVS || boundPS != g_pSkyboxPS) {
        OutputDebugString(L"Ошибка привязки шейдеров SkyboxVS и SkyboxPS к пайплайну\n");
        return false;
    }

    return SUCCEEDED(result);
}

// Настройка заднего буфера и буфера глубины
bool SetupBackBuffer()
{
    HRESULT result = S_OK;

    // Освобождаем предыдущие ресурсы 
    SAFE_RELEASE(m_pBackBufferRTV);
    SAFE_RELEASE(m_pDepthStencilView);
    SAFE_RELEASE(m_pDepthBuffer);

    // Получаем задний буфер из цепочки обмена
    ID3D11Texture2D* pBackBuffer = nullptr;
    result = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(result)) return false;

    // Создаем render target view для заднего буфера
    result = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pBackBufferRTV);
    SAFE_RELEASE(pBackBuffer);
    if (FAILED(result)) return false;

    // === СОЗДАНИЕ БУФЕРА ГЛУБИНЫ D32_FLOAT ===
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;  // 32-битный float для глубины
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthDesc.CPUAccessFlags = 0;
    depthDesc.MiscFlags = 0;

    result = m_pDevice->CreateTexture2D(&depthDesc, nullptr, &m_pDepthBuffer);
    if (FAILED(result)) return false;

    // Создаем depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc = {};
    depthViewDesc.Format = depthDesc.Format;
    depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    depthViewDesc.Texture2D.MipSlice = 0;

    result = m_pDevice->CreateDepthStencilView(m_pDepthBuffer, &depthViewDesc, &m_pDepthStencilView);
    if (FAILED(result)) return false;

    return true;
}

// Инициализация промежуточного буфера цвета для постпроцессинга
bool InitColorBuffer()
{
    HRESULT result = S_OK;

    SAFE_RELEASE(m_pColorBuffer);
    SAFE_RELEASE(m_pColorBufferRTV);
    SAFE_RELEASE(m_pColorBufferSRV);

    // Описание текстуры, которая будет использоваться как рендер-таргет и как шейдерный ресурс
    D3D11_TEXTURE2D_DESC colorBufferDesc = {};
    colorBufferDesc.Width = m_width;
    colorBufferDesc.Height = m_height;
    colorBufferDesc.MipLevels = 1;
    colorBufferDesc.ArraySize = 1;
    colorBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorBufferDesc.SampleDesc.Count = 1;
    colorBufferDesc.SampleDesc.Quality = 0;
    colorBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    colorBufferDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    colorBufferDesc.CPUAccessFlags = 0;
    colorBufferDesc.MiscFlags = 0;

    colorBufferDesc.SampleDesc.Count = 1;
    colorBufferDesc.SampleDesc.Quality = 0;

    result = m_pDevice->CreateTexture2D(&colorBufferDesc, nullptr, &m_pColorBuffer);
    if (FAILED(result)) return false;

    result = m_pDevice->CreateRenderTargetView(m_pColorBuffer, nullptr, &m_pColorBufferRTV);
    if (FAILED(result)) return false;

    result = m_pDevice->CreateShaderResourceView(m_pColorBuffer, nullptr, &m_pColorBufferSRV);
    return SUCCEEDED(result);
}

// Создание константных буферов (сцены)
bool InitBuffers()
{
    HRESULT result = S_OK;

    // Константный буфер сцены (будет обновляться каждый кадр)
    D3D11_BUFFER_DESC sceneBufferDesc = {};
    sceneBufferDesc.ByteWidth = sizeof(SceneBuffer);
    sceneBufferDesc.Usage = D3D11_USAGE_DYNAMIC;  // будет часто обновляться
    sceneBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    sceneBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    sceneBufferDesc.MiscFlags = 0;
    sceneBufferDesc.StructureByteStride = 0;

    result = m_pDevice->CreateBuffer(&sceneBufferDesc, nullptr, &m_pSceneBuffer);
    return SUCCEEDED(result);
}

// Инициализация ландшафта (загрузка карты высот, создание вершин и индексов)
bool InitTerrain()
{
    HRESULT result = S_OK;

    // 1. Загрузка карты высот из EXR
    const char* exrFilename = "landscape/Terrain003_2K.exr";
    float* out_rgba = nullptr;
    int width, height;
    const char* err = nullptr;

    int ret = LoadEXR(&out_rgba, &width, &height, exrFilename, &err);
    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            OutputDebugStringA(err);
            FreeEXRErrorMessage(err);
        }
        MessageBox(NULL, L"Не удалось загрузить карту высот EXR!", L"Ошибка", MB_OK);
        return false;
    }

    // 2. Определяем размер сетки (уменьшаем шаг для производительности)
    const int step = 2; // берём каждый 2-й пиксель (уменьшаем количество вершин)
    m_terrainGridSizeX = (width + step - 1) / step;
    m_terrainGridSizeZ = (height + step - 1) / step;

    UINT vertexCount = m_terrainGridSizeX * m_terrainGridSizeZ;
    std::vector<TextureTangentVertex> vertices(vertexCount);

    // Диапазон координат ландшафта
    float xMin = -m_terrainWidth / 2.0f;
    float xMax = m_terrainWidth / 2.0f;
    float zMin = -m_terrainDepth / 2.0f;
    float zMax = m_terrainDepth / 2.0f;
    float stepX = (xMax - xMin) / (m_terrainGridSizeX - 1);
    float stepZ = (zMax - zMin) / (m_terrainGridSizeZ - 1);

    // Заполняем позиции и текстурные координаты
    for (UINT j = 0; j < m_terrainGridSizeZ; j++)
    {
        for (UINT i = 0; i < m_terrainGridSizeX; i++)
        {
            UINT index = j * m_terrainGridSizeX + i;

            int mapX = i * step;
            int mapY = j * step;
            if (mapX >= width) mapX = width - 1;
            if (mapY >= height) mapY = height - 1;

            float heightVal = out_rgba[(mapY * width + mapX) * 4]; // красный канал (высота)

            float x = xMin + i * stepX;
            float z = zMin + j * stepZ;
            float y = heightVal * m_terrainHeightScale;

            vertices[index].x = x;
            vertices[index].y = y;
            vertices[index].z = z;

            vertices[index].u = (float)i / (m_terrainGridSizeX - 1);
            vertices[index].v = (float)j / (m_terrainGridSizeZ - 1);

            // Пока нули, т.к. вычисляется позже
            vertices[index].nx = vertices[index].ny = vertices[index].nz = 0.0f;
            vertices[index].tx = vertices[index].ty = vertices[index].tz = 0.0f;
        }
    }

    free(out_rgba); // данные EXR больше не нужны

    // 3. Создание индексов (два треугольника на ячейку)
    std::vector<UINT32> indices;
    indices.reserve((m_terrainGridSizeX - 1) * (m_terrainGridSizeZ - 1) * 6);

    for (UINT j = 0; j < m_terrainGridSizeZ - 1; j++)
    {
        for (UINT i = 0; i < m_terrainGridSizeX - 1; i++)
        {
            UINT topLeft = j * m_terrainGridSizeX + i;
            UINT topRight = j * m_terrainGridSizeX + i + 1;
            UINT bottomLeft = (j + 1) * m_terrainGridSizeX + i;
            UINT bottomRight = (j + 1) * m_terrainGridSizeX + i + 1;

            // Первый треугольник (левая верхняя половина)
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Второй треугольник (правая нижняя половина)
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    m_terrainIndexCount = (UINT)indices.size();

    // 4. Вычисление нормалей и касательных
    // Сначала обнулим
    for (auto& v : vertices)
    {
        v.nx = v.ny = v.nz = 0.0f;
        v.tx = v.ty = v.tz = 0.0f;
    }

    // Для каждого треугольника вычисляем нормаль и касательную и накапливаем в вершинах
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        UINT32 i0 = indices[i];
        UINT32 i1 = indices[i + 1];
        UINT32 i2 = indices[i + 2];

        auto& v0 = vertices[i0];
        auto& v1 = vertices[i1];
        auto& v2 = vertices[i2];

        // Позиции
        DirectX::XMFLOAT3 p0(v0.x, v0.y, v0.z);
        DirectX::XMFLOAT3 p1(v1.x, v1.y, v1.z);
        DirectX::XMFLOAT3 p2(v2.x, v2.y, v2.z);

        // Текстурные координаты
        DirectX::XMFLOAT2 uv0(v0.u, v0.v);
        DirectX::XMFLOAT2 uv1(v1.u, v1.v);
        DirectX::XMFLOAT2 uv2(v2.u, v2.v);

        using namespace DirectX;

        XMVECTOR p0v = XMLoadFloat3(&p0);
        XMVECTOR p1v = XMLoadFloat3(&p1);
        XMVECTOR p2v = XMLoadFloat3(&p2);
        XMVECTOR e1 = XMVectorSubtract(p1v, p0v);
        XMVECTOR e2 = XMVectorSubtract(p2v, p0v);

        // Нормаль треугольника (векторное произведение)
        XMVECTOR normal = XMVector3Cross(e1, e2);
        normal = XMVector3Normalize(normal);

        // Касательная (вычисление касательного вектора по формуле с текстурными координатами)
        float deltaU1 = uv1.x - uv0.x;
        float deltaV1 = uv1.y - uv0.y;
        float deltaU2 = uv2.x - uv0.x;
        float deltaV2 = uv2.y - uv0.y;
        float f = 1.0f / (deltaU1 * deltaV2 - deltaU2 * deltaV1);
        XMVECTOR tangent;
        if (isfinite(f))
        {
            tangent = XMVectorScale(XMVectorSubtract(XMVectorScale(e1, deltaV2), XMVectorScale(e2, deltaV1)), f);
            tangent = XMVector3Normalize(tangent);
        }
        else
        {
            tangent = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        }

        // Прибавляем к вершинам
        XMStoreFloat3((XMFLOAT3*)&v0.nx, XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&v0.nx), normal));
        XMStoreFloat3((XMFLOAT3*)&v1.nx, XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&v1.nx), normal));
        XMStoreFloat3((XMFLOAT3*)&v2.nx, XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&v2.nx), normal));

        XMStoreFloat3((XMFLOAT3*)&v0.tx, XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&v0.tx), tangent));
        XMStoreFloat3((XMFLOAT3*)&v1.tx, XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&v1.tx), tangent));
        XMStoreFloat3((XMFLOAT3*)&v2.tx, XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&v2.tx), tangent));
    }

    // Нормализуем нормали и касательные после усреднения
    for (auto& v : vertices)
    {
        DirectX::XMStoreFloat3((DirectX::XMFLOAT3*)&v.nx, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3((DirectX::XMFLOAT3*)&v.nx)));
        DirectX::XMStoreFloat3((DirectX::XMFLOAT3*)&v.tx, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3((DirectX::XMFLOAT3*)&v.tx)));
    }

    // Сохраняем высоты для интерполяции камеры
    m_terrainHeights.resize(m_terrainGridSizeX * m_terrainGridSizeZ);
    for (UINT i = 0; i < vertexCount; ++i)
        m_terrainHeights[i] = vertices[i].y;

    // 5. Создание вершинного буфера
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = (UINT)(vertices.size() * sizeof(TextureTangentVertex));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = 0;
    vbDesc.StructureByteStride = sizeof(TextureTangentVertex);

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = vertices.data();

    result = m_pDevice->CreateBuffer(&vbDesc, &vbData, &m_pTerrainVertexBuffer);
    if (FAILED(result)) return false;

    // 6. Создание индексного буфера
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = (UINT)(indices.size() * sizeof(UINT32));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = indices.data();

    result = m_pDevice->CreateBuffer(&ibDesc, &ibData, &m_pTerrainIndexBuffer);
    if (FAILED(result)) return false;

    // 7. Константный буфер для геометрии ландшафта
    D3D11_BUFFER_DESC geomBufferDesc = {};
    geomBufferDesc.ByteWidth = sizeof(GeomBuffer);
    geomBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    geomBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    geomBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    geomBufferDesc.MiscFlags = 0;
    geomBufferDesc.StructureByteStride = 0;

    GeomBuffer geomData;
    DirectX::XMMATRIX model = DirectX::XMMatrixIdentity();  // ландшафт в центре
    DirectX::XMStoreFloat4x4(&geomData.m, DirectX::XMMatrixTranspose(model));
    DirectX::XMStoreFloat4x4(&geomData.normalM, DirectX::XMMatrixIdentity()); // обратная транспонированная  = единичная
    geomData.pbrParams = DirectX::XMFLOAT4(0.5f, 0.0f, 0.0f, 1.0f); // hasNormalMap = 1
    geomData.posAngle = DirectX::XMFLOAT4(0, 0, 0, 0);

    // Создаём буфер без начальных данных
    D3D11_SUBRESOURCE_DATA geomInitData = { &geomData, 0, 0 };
    result = m_pDevice->CreateBuffer(&geomBufferDesc, &geomInitData, &m_pTerrainGeomBuffer);
    if (FAILED(result)) return false;

    // Заполняем буфер через Map/Unmap
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_pDeviceContext->Map(m_pTerrainGeomBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &geomData, sizeof(GeomBuffer));
        m_pDeviceContext->Unmap(m_pTerrainGeomBuffer, 0);
    }

    return true;
}

float GetTerrainHeight(float x, float z)
{
    float xMin = -m_terrainWidth / 2.0f;
    float xMax = m_terrainWidth / 2.0f;
    float zMin = -m_terrainDepth / 2.0f;
    float zMax = m_terrainDepth / 2.0f;

    // Прижимаем к границам
    x = std::max(xMin, std::min(x, xMax));
    z = std::max(zMin, std::min(z, zMax));

    float stepX = (xMax - xMin) / (m_terrainGridSizeX - 1);
    float stepZ = (zMax - zMin) / (m_terrainGridSizeZ - 1);

    float u = (x - xMin) / stepX;
    float v = (z - zMin) / stepZ;

    int i0 = (int)floor(u);
    int i1 = i0 + 1;
    int j0 = (int)floor(v);
    int j1 = j0 + 1;

    i0 = std::max(0, std::min(i0, (int)m_terrainGridSizeX - 1));
    i1 = std::max(0, std::min(i1, (int)m_terrainGridSizeX - 1));
    j0 = std::max(0, std::min(j0, (int)m_terrainGridSizeZ - 1));
    j1 = std::max(0, std::min(j1, (int)m_terrainGridSizeZ - 1));

    float fx = u - i0;
    float fz = v - j0;

    int idx00 = j0 * m_terrainGridSizeX + i0;
    int idx10 = j0 * m_terrainGridSizeX + i1;
    int idx01 = j1 * m_terrainGridSizeX + i0;
    int idx11 = j1 * m_terrainGridSizeX + i1;

    float h00 = m_terrainHeights[idx00];
    float h10 = m_terrainHeights[idx10];
    float h01 = m_terrainHeights[idx01];
    float h11 = m_terrainHeights[idx11];

    float h0 = (1 - fx) * h00 + fx * h10;
    float h1 = (1 - fx) * h01 + fx * h11;
    float h = (1 - fz) * h0 + fz * h1;
    return h;
}


// Обновление камеры и константного буфера сцены
void UpdateCamera()
{
    // Базовая скорость (метров в секунду)
    float baseSpeed = 2.0f; // можно подобрать под желаемую скорость
    float speed = baseSpeed * g_deltaTime;

    // Направление взгляда из углов yaw/pitch
    DirectX::XMVECTOR lookDir = DirectX::XMVectorSet(
        cosf(m_pitch) * sinf(m_yaw),
        sinf(m_pitch),
        cosf(m_pitch) * cosf(m_yaw),
        0.0f
    );

    // Вектор вправо (перпендикулярно направлению взгляда и глобальному up)
    DirectX::XMVECTOR rightDir = DirectX::XMVector3Cross(
        DirectX::XMVectorSet(0, 1, 0, 0), lookDir);
    rightDir = DirectX::XMVector3Normalize(rightDir);

    // Вектор вперёд в горизонтальной плоскости (для движения W/S)
    DirectX::XMVECTOR forwardDir = DirectX::XMVector3Cross(
        rightDir, DirectX::XMVectorSet(0, 1, 0, 0));
    forwardDir = DirectX::XMVector3Normalize(forwardDir);

    // Суммарное перемещение
    DirectX::XMVECTOR delta = DirectX::XMVectorZero();
    if (m_keyW) delta = DirectX::XMVectorAdd(delta, forwardDir);
    if (m_keyS) delta = DirectX::XMVectorSubtract(delta, forwardDir);
    if (m_keyD) delta = DirectX::XMVectorAdd(delta, rightDir);
    if (m_keyA) delta = DirectX::XMVectorSubtract(delta, rightDir);

    // Изменение высоты над поверхностью (Q/E)
    if (m_keyQ) m_heightOffset += speed;
    if (m_keyE) m_heightOffset -= speed;
    if (m_heightOffset < 0.1f) m_heightOffset = 0.1f;

    // Применяем горизонтальное перемещение
    if (!DirectX::XMVector3Equal(delta, DirectX::XMVectorZero()))
    {
        delta = DirectX::XMVectorScale(delta, speed);
        DirectX::XMFLOAT3 deltaF;
        DirectX::XMStoreFloat3(&deltaF, delta);
        m_camPos.x += deltaF.x;
        m_camPos.z += deltaF.z;
    }

    // Ограничиваем координаты границами ландшафта
    float xMin = -m_terrainWidth / 2.0f;
    float xMax = m_terrainWidth / 2.0f;
    float zMin = -m_terrainDepth / 2.0f;
    float zMax = m_terrainDepth / 2.0f;
    m_camPos.x = std::max(xMin, std::min(m_camPos.x, xMax));
    m_camPos.z = std::max(zMin, std::min(m_camPos.z, zMax));

    // Вычисляем высоту камеры над рельефом
    m_camPos.y = GetTerrainHeight(m_camPos.x, m_camPos.z) + m_heightOffset;

    // Матрица вида
    DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&m_camPos);
    DirectX::XMVECTOR at = DirectX::XMVectorAdd(eye, lookDir);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0, 1, 0, 0);
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, at, up);

    // Проекция (reversed depth)
    float f = 100.0f;
    float n = 0.1f;
    float fov = DirectX::XM_PI / 3; //сейчас 60°, но если "/ 4", то меняем FOV на 45°
    float aspectRatio = (float)m_width / m_height;
    float halfW = tanf(fov / 2) * f;
    float halfH = halfW / aspectRatio;
   //DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveLH(halfW * 2.0f, halfH * 2.0f, f, n);
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(fov, aspectRatio, n, f);

    // Сохраняем матрицы для скайбокса
    DirectX::XMStoreFloat4x4(&m_viewMatrix, view);
    DirectX::XMStoreFloat4x4(&m_projMatrix, proj);
    
    DirectX::XMMATRIX vp = DirectX::XMMatrixMultiply(view, proj);

    // Заполнение константного буфера сцены
    SceneBuffer sceneBuffer;
    DirectX::XMStoreFloat4x4(&sceneBuffer.vp, DirectX::XMMatrixTranspose(vp));
    sceneBuffer.cameraPos = DirectX::XMFLOAT4(m_camPos.x, m_camPos.y, m_camPos.z, 1.0f);
    sceneBuffer.lightInfo = DirectX::XMFLOAT4(
        (float)m_lightCount,
        m_useNormalMaps ? 1.0f : 0.0f,
        m_showNormals ? 1.0f : 0.0f,
        m_detailStrength
    );

    for (int i = 0; i < 10; ++i)
        sceneBuffer.lights[i] = m_lights[i];

    sceneBuffer.ambientColor = m_ambientColor;
    float flowStrength = 0.0f; // например, 0.5 – половина силы
    sceneBuffer.flowInfo = DirectX::XMFLOAT4((float)m_flowModeIndex, 0.0f, 0.0f, 0.0f);
    //sceneBuffer.flowInfo = DirectX::XMFLOAT4(m_flowMode ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);

    sceneBuffer.renderModeInfo = DirectX::XMFLOAT4((float)g_renderMode, 0.0f, 0.0f, 0.0f);

    // Направленный свет – берём направление из IBL (если готово), иначе fallback
    DirectX::XMFLOAT3 sunDir = g_ibl.GetSunDirection();
    sceneBuffer.dirLightDir = DirectX::XMFLOAT4(sunDir.x, sunDir.y, sunDir.z, 0.0f);
    // Интенсивность и цвет подберите под свою сцену (для луны – холодный, яркость 1.5..2.0)
    sceneBuffer.dirLightColor = DirectX::XMFLOAT4(0.9f, 0.95f, 1.0f, 1.5f);

    // ручное управление шероховатостью/металличностью
    //sceneBuffer.manualPBRParams.x = m_useManualRoughnessMetalness ? 1.0f : 0.0f;
    //sceneBuffer.manualPBRParams.y = m_manualRoughness;
    //sceneBuffer.manualPBRParams.z = m_manualMetalness;
    //sceneBuffer.manualPBRParams.w = 0.0f;

    // Обновление буфера
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_pDeviceContext->Map(m_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &sceneBuffer, sizeof(SceneBuffer));
        m_pDeviceContext->Unmap(m_pSceneBuffer, 0);
    }
}

// Обновление константного буфера постпроцессинга
void UpdatePostProcessBuffer()
{
    if (!m_pPostProcessBuffer) return;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT result = m_pDeviceContext->Map(m_pPostProcessBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(result))
    {
        PostProcessBuffer* postProcessData = (PostProcessBuffer*)mappedResource.pData;
        postProcessData->effectType = m_postProcessEffect;
        postProcessData->padding[0] = postProcessData->padding[1] = postProcessData->padding[2] = 0;
        m_pDeviceContext->Unmap(m_pPostProcessBuffer, 0);
    }
}

// === ФУНКЦИЯ РЕНДЕРИНГА ПОСТПРОЦЕССИНГА ===
void RenderPostProcess()
{
    // Сбрасываем шейдерный ресурс, который может быть ещё привязан
    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

    // Переключаемся на back buffer как рендер-таргет
    ID3D11RenderTargetView* views[] = { m_pBackBufferRTV };
    m_pDeviceContext->OMSetRenderTargets(1, views, nullptr);

    // Устанавливаем состояния по умолчанию
    m_pDeviceContext->OMSetDepthStencilState(nullptr, 0);
    m_pDeviceContext->RSSetState(nullptr);
    m_pDeviceContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    // Устанавливаем сэмплер
    ID3D11SamplerState* samplers[] = { m_pSampler };
    m_pDeviceContext->PSSetSamplers(0, 1, samplers);

    // Устанавливаем сэмплеры для IBL (слоты 1 и 2)
    //ID3D11SamplerState* iblSamplers[] = { g_pLinearSampler, g_pLinearMipSampler };
    //m_pDeviceContext->PSSetSamplers(1, 2, iblSamplers);

    // Устанавливаем текстуру (промежуточный буфер цвета) как шейдерный ресурс
    ID3D11ShaderResourceView* resources[] = { m_pColorBufferSRV };
    m_pDeviceContext->PSSetShaderResources(0, 1, resources);

    // Настраиваем пайплайн для постпроцессинга
    m_pDeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    m_pDeviceContext->IASetInputLayout(nullptr);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pDeviceContext->VSSetShader(m_pPostProcessVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pPostProcessPixelShader, nullptr, 0);

    // Устанавливаем константный буфер постпроцессинга
    ID3D11Buffer* postProcessConstantBuffers[] = { m_pPostProcessBuffer };
    m_pDeviceContext->PSSetConstantBuffers(0, 1, postProcessConstantBuffers);

    // Устанавливаем viewport на весь экран
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)m_width;
    vp.Height = (FLOAT)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &vp);

    // Рисуем 3 вершины (один треугольник, покрывающий экран)
    m_pDeviceContext->Draw(3, 0);
}

// Изменение размера цепочки обмена при изменении окна
void ResizeSwapChain(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;

    m_width = width;
    m_height = height;

    // Отвязываем все render target'ы
    if (m_pDeviceContext)
    {
        ID3D11RenderTargetView* nullRTV[1] = { nullptr };
        m_pDeviceContext->OMSetRenderTargets(1, nullRTV, nullptr);
    }

    // Освобождаем все ресурсы, зависящие от размера окна
    SAFE_RELEASE(m_pBackBufferRTV);
    SAFE_RELEASE(m_pDepthStencilView);
    SAFE_RELEASE(m_pDepthBuffer);
    SAFE_RELEASE(m_pColorBufferRTV);
    SAFE_RELEASE(m_pColorBufferSRV);
    SAFE_RELEASE(m_pColorBuffer);

    // Изменяем размер цепочки обмена
    if (m_pSwapChain)
    {
        HRESULT hr = m_pSwapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            // Здесь можно добавить логирование ошибки (??)
            return;
        }
    }

    // Пересоздаём back buffer RTV и depth buffer
    if (!SetupBackBuffer())
    {
        // Обработка ошибки (??)
        return;
    }

    // Пересоздаём color buffer для постпроцессинга
    if (!InitColorBuffer())
    {
        // Обработка ошибки (??)
        return;
    }
}

// Рендеринг маленьких сфер (источников света)
void RenderSmallSpheres()
{
    if (!m_showLightBulbs || m_lightCount == 0)
        return;

    // Устанавливаем состояние для непрозрачных объектов
    m_pDeviceContext->OMSetDepthStencilState(m_pNormalDepthState, 0);
    m_pDeviceContext->OMSetBlendState(m_pOpaqueBlendState, nullptr, 0xFFFFFFFF);

    // Настраиваем пайплайн для маленьких сфер
    m_pDeviceContext->IASetIndexBuffer(m_pSmallSphereIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    ID3D11Buffer* vertexBuffers[] = { m_pSmallSphereVertexBuffer };
    UINT strides[] = { sizeof(DirectX::XMFLOAT3) };
    UINT offsets[] = { 0 };
    m_pDeviceContext->IASetVertexBuffers(0, 1, vertexBuffers, strides, offsets);
    m_pDeviceContext->IASetInputLayout(m_pSmallSphereInputLayout);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pDeviceContext->VSSetShader(m_pSmallSphereVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pSmallSpherePixelShader, nullptr, 0);

    // Рисуем каждую маленькую сферу (источник света)
    for (int i = 0; i < m_lightCount; i++)
    {
        // Обновляем матрицу трансформации для этой сферы (перенос в позицию источника)
        SmallSphereGeomBuffer geomData;
        DirectX::XMMATRIX model = DirectX::XMMatrixTranslation(
            m_lights[i].pos.x,
            m_lights[i].pos.y,
            m_lights[i].pos.z
        );
        DirectX::XMMATRIX modelT = DirectX::XMMatrixTranspose(model);
        DirectX::XMStoreFloat4x4(&geomData.m, modelT);
        geomData.color = m_lights[i].color;

        m_pDeviceContext->UpdateSubresource(m_pSmallSphereGeomBuffers[i], 0, nullptr, &geomData, 0, 0);

        // Устанавливаем константные буферы
        ID3D11Buffer* constantBuffers[] = { m_pSmallSphereGeomBuffers[i], m_pSceneBuffer };
        m_pDeviceContext->VSSetConstantBuffers(0, 2, constantBuffers);
        m_pDeviceContext->PSSetConstantBuffers(0, 2, constantBuffers);

        // Рисуем сферу
        m_pDeviceContext->DrawIndexed(m_smallSphereIndexCount, 0, 0);
    }
}

void UpdateLightIntensities()
{
    // Основной источник (индекс 0)
    if (m_lightCount > 0)
    {
        m_lights[0].intensity = (m_flowModeIndex > 0) ? 100.0f : 80.0f;
    }
    // Остальные источники (индексы 1..m_lightCount-1)
    for (int i = 1; i < m_lightCount; i++)
    {
        m_lights[i].intensity = (m_flowModeIndex > 0) ? 20.0f : 10.0f;
    }
}

// Основная функция рендеринга (вызывается каждый кадр)
void Render()
{
    if (!m_pDeviceContext || !m_pBackBufferRTV)
        return;

    // Вычисление времени кадра
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    g_deltaTime = (float)(currentTime.QuadPart - g_prevTime.QuadPart) / (float)g_freq.QuadPart;
    // Ограничение, чтобы избежать рывков при зависании
    if (g_deltaTime > 0.1f) g_deltaTime = 0.1f;
    g_prevTime = currentTime;

    g_ibl.Update();   // если ещё не добавили выше
    const IBLResources& iblRes = g_ibl.GetResources();

    // Сбрасываем шейдерные ресурсы перед началом (на всякий случай)
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    m_pDeviceContext->PSSetShaderResources(0, 16, nullSRVs);
    //ID3D11ShaderResourceView* nullSRV = nullptr;
    //m_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

    // Рендерим сцену в промежуточную текстуру для постпроцессинга
    ID3D11RenderTargetView* views[] = { m_pColorBufferRTV };
    m_pDeviceContext->OMSetRenderTargets(1, views, m_pDepthStencilView);

    // === REVERSED DEPTH: очистка цветом ===
    static const FLOAT BackColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // чёрный
    m_pDeviceContext->ClearRenderTargetView(m_pColorBufferRTV, BackColor);
    if (m_pDepthStencilView)
        // Для reversed depth очищаем глубину на 0.0 (дальняя плоскость)
        //m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH, 0.0f, 0);
        m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Устанавливаем viewport
    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (FLOAT)m_width;
    viewport.Height = (FLOAT)m_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &viewport);

    m_pDeviceContext->RSSetState(m_pRasterizerState);


    // Обновление константных буферов (камера и постпроцессинг)
    UpdateCamera();
    UpdatePostProcessBuffer();
   
    // 1. РЕНДЕРИМ СКАЙБОКС (ДО ЛАНДШАФТА)
    // if (iblRes.skyboxSRV)
    if (g_ibl.IsReady() && iblRes.skyboxSRV)
    {
        // Сохраняем текущие состояния
        ID3D11RenderTargetView* pOldRTV = nullptr;
        ID3D11DepthStencilView* pOldDSV = nullptr;
        m_pDeviceContext->OMGetRenderTargets(1, &pOldRTV, &pOldDSV);

        ID3D11RasterizerState* pOldRS = nullptr;
        m_pDeviceContext->RSGetState(&pOldRS);

        // Depth-стейт для скайбокса (уже создан в InitDirectX)
        m_pDeviceContext->OMSetDepthStencilState(m_pSkyboxDepthState, 0);

        // Устанавливаем render target (тот же, что и для ландшафта)
        m_pDeviceContext->OMSetRenderTargets(1, &m_pColorBufferRTV, m_pDepthStencilView);

        // Устанавливаем растеризатор без отсечения граней (можно использовать m_pRasterizerState)
        m_pDeviceContext->RSSetState(m_pRasterizerState);

        // Матрица вида-проекции для скайбокса (без переноса камеры)
        // Загружаем матрицы
        DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&m_viewMatrix);
        DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(&m_projMatrix);

        // Убираем перенос (трансляцию) из матрицы вида
        DirectX::XMMATRIX viewNoTranslation = view;
        viewNoTranslation.r[3] = DirectX::XMVectorSet(0, 0, 0, 1); // обнуляем последнюю строку

        // Вычисляем матрицу вида-проекции для скайбокса
        DirectX::XMMATRIX vpSky = viewNoTranslation * proj;

        // Константный буфер для скайбокса
        struct SkyboxConstants
        {
            DirectX::XMMATRIX world;
            DirectX::XMMATRIX viewProj;
            DirectX::XMFLOAT4 cameraPosAndMode;
        };
        SkyboxConstants skyboxCB;
        skyboxCB.world = DirectX::XMMatrixIdentity();
        skyboxCB.viewProj = DirectX::XMMatrixTranspose(vpSky);
        skyboxCB.cameraPosAndMode = DirectX::XMFLOAT4(m_camPos.x, m_camPos.y, m_camPos.z, 0.0f);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_pDeviceContext->Map(g_pSkyboxConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &skyboxCB, sizeof(SkyboxConstants));
            m_pDeviceContext->Unmap(g_pSkyboxConstantBuffer, 0);
        }

        // Устанавливаем константные буферы
        ID3D11Buffer* skyboxCBs[] = { g_pSkyboxConstantBuffer, g_pSkyboxConstantBuffer };
        m_pDeviceContext->VSSetConstantBuffers(0, 2, skyboxCBs);

        // Устанавливаем текстуру скайбокса и сэмплер
        m_pDeviceContext->PSSetShaderResources(9, 1, &iblRes.skyboxSRV);  // Используется skyboxSRV
        m_pDeviceContext->PSSetSamplers(0, 1, &g_pSkyboxSampler);

        // Настройка вершинных буферов
        UINT stride = sizeof(float) * 3;
        UINT offset = 0;
        m_pDeviceContext->IASetVertexBuffers(0, 1, &g_pSkyboxVertexBuffer, &stride, &offset);
        m_pDeviceContext->IASetIndexBuffer(g_pSkyboxIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
        m_pDeviceContext->IASetInputLayout(g_pSkyboxInputLayout);
        m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        m_pDeviceContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
        m_pDeviceContext->PSSetShader(g_pSkyboxPS, nullptr, 0);

        // ПЕРЕКЛЮЧАЕМ РАСТЕРРАЙЗЕР
        m_pDeviceContext->RSSetState(m_pRasterizerCullFront);

        m_pDeviceContext->DrawIndexed(g_skyboxIndexCount, 0, 0);

        // ВОЗВРАЩАЕМ ОБЫЧНЫЙ РАСТЕРРАЙЗЕР
        m_pDeviceContext->RSSetState(m_pRasterizerState);

        // Восстановление состояний
        m_pDeviceContext->RSSetState(pOldRS);
        m_pDeviceContext->OMSetRenderTargets(1, &pOldRTV, pOldDSV);

        if (pOldRTV) pOldRTV->Release();
        if (pOldDSV) pOldDSV->Release();
        if (pOldRS) pOldRS->Release();
    }

    // 2. РЕНДЕРИМ ЛАНДШАФТ
    m_pDeviceContext->OMSetDepthStencilState(m_pNormalDepthState, 0);
    m_pDeviceContext->OMSetBlendState(m_pOpaqueBlendState, nullptr, 0xFFFFFFFF);

    // Устанавливаем текстуры: основную, карту нормалей, детали, flow, roughness, metalness
    ID3D11ShaderResourceView* terrainResources[] = {
        m_pTextureView,
        m_pTextureViewNM,
        m_pDetailTextureView,
        m_pFlowTextureView,
        m_pRoughnessTextureView,
        m_pMetalnessTextureView  // пока nullptr, но слот зарезервирован
    };
    m_pDeviceContext->PSSetShaderResources(0, 6, terrainResources);

    // Устанавливаем IBL текстуры 
    if (g_ibl.IsReady()) // чтобы не биндить IBL текстуры до завершения pipline
    {
        ID3D11ShaderResourceView* iblResources[] = {
            iblRes.irradianceSRV,
            iblRes.prefilteredSRV,
            iblRes.brdfLUTSRV
        };
        m_pDeviceContext->PSSetShaderResources(6, 3, iblResources);
    }
    else
    {
        ID3D11ShaderResourceView* nullIBL[3] = {};
        m_pDeviceContext->PSSetShaderResources(6, 3, nullIBL);
    }
    /*
    ID3D11ShaderResourceView* iblResources[] = {
    iblRes.irradianceSRV,
    iblRes.prefilteredSRV,
    iblRes.brdfLUTSRV
    };
    m_pDeviceContext->PSSetShaderResources(6, 3, iblResources);
    */

    // Устанавливаем сэмплер для основной текстуры (slot 0)
    ID3D11SamplerState* mainSampler = m_pSampler;
    m_pDeviceContext->PSSetSamplers(0, 1, &mainSampler);

    // Устанавливаем сэмплеры: s0 - основной (уже есть), s1 - linearSampler, s2 - linearMipSampler
    ID3D11SamplerState* samplersIBL[] = {
        g_pLinearSampler,     // s1
        g_pLinearMipSampler   // s2
    };
    m_pDeviceContext->PSSetSamplers(1, 2, samplersIBL);

    // Настраиваем пайплайн
    ID3D11Buffer* vertexBuffers[] = { m_pTerrainVertexBuffer };
    UINT strides[] = { sizeof(TextureTangentVertex) };
    UINT offsets[] = { 0 };
    m_pDeviceContext->IASetVertexBuffers(0, 1, vertexBuffers, strides, offsets);
    m_pDeviceContext->IASetIndexBuffer(m_pTerrainIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);

    // Устанавливаем сэмплер
    ID3D11SamplerState* samplers[] = { m_pSampler };
    m_pDeviceContext->PSSetSamplers(0, 1, samplers);

    // Константные буферы: геометрический (b0) и сцены (b1)
    ID3D11Buffer* constantBuffers[] = { m_pTerrainGeomBuffer, m_pSceneBuffer };
    m_pDeviceContext->VSSetConstantBuffers(0, 2, constantBuffers);
    m_pDeviceContext->PSSetConstantBuffers(0, 2, constantBuffers);

    // Рисуем ландшафт
    m_pDeviceContext->DrawIndexed(m_terrainIndexCount, 0, 0);

    // 3. Применяем постпроцессинг (рисуем на заднем буфере с эффектом)
    RenderPostProcess();

    // 4. РЕНДЕРИМ ImGui (поверх всего)
    if (m_showImGui)
    {
        // Начало нового кадра ImGui
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Единое окно управления
        ImGui::Begin("Control Panel", &m_showImGui, ImGuiWindowFlags_AlwaysAutoResize);

        // Секция постпроцессинга
        if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* effectNames[] = {
                "No Effect",     // 0
                "Sepia",         // 1
                "Cold Tint",     // 2
                "Night Vision"   // 3
            };

            ImGui::Text("Post-Process Effect:");
            ImGui::Combo("Effect", &m_postProcessEffect, effectNames, IM_ARRAYSIZE(effectNames));

            // Отображение текущего выбранного эффекта
            ImGui::Text("Current Effect:");
            ImGui::SameLine();
            switch (m_postProcessEffect)
            {
            case 0: ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "None"); break;
            case 1: ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Sepia"); break;
            case 2: ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Cold Tint"); break;
            case 3: ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "Night Vision"); break;
            }
        }

        // Секция освещения
        if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Use normal maps", &m_useNormalMaps); // Использовать карты нормалей
            //ImGui::ColorEdit3("Ambient light", &m_ambientColor.x); // Окружающий цвет
        }

        // Секция детализации и PBR
        if (ImGui::CollapsingHeader("Details & PBR", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // ImGui::SliderFloat("Detail Strength", &m_detailStrength, 0.0f, 1.5f, "%.2f");
            const char* flowModes[] = { u8"Обычный", u8"Влажные ущелья", u8"Каменистое дно каньона", u8"Горные ручьи" };
            ImGui::Combo("Flow Mode", &m_flowModeIndex, flowModes, IM_ARRAYSIZE(flowModes));

            /*
            if (ImGui::Checkbox("Manual Roughness/Metalness", &m_useManualRoughnessMetalness))
            {
                // при переключении флага автоматически обновим буфер при следующем кадре
            }

            if (m_useManualRoughnessMetalness)
            {
                ImGui::SliderFloat("Roughness", &m_manualRoughness, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("Metalness", &m_manualMetalness, 0.0f, 1.0f, "%.3f");
            }
            else
            {
                ImGui::Text("Using texture values");
            }
            */
            /*
            if (ImGui::IsItemEdited()) // если значение изменилось
            {
                UpdateLightIntensities();
            }*/

            // Настройки PBR
            const char* renderModes[] = {
                "Full Lightning (PBR + IBL)",
                "Normal Distribution (NDF)",
                "Geometry Function (G)",
                "Fresnel Function (F)",
                "Diffuse Only",
                "Specular Only",
                "Diffuse IBL",
                "Specular IBL",
                "Fresnel IBL",
                "BRDF LUT",
                "Irradiance Map"
            };
            ImGui::Combo("Render Mode", &g_renderMode, renderModes, IM_ARRAYSIZE(renderModes));
        }

        ImGui::End();

        // Рендеринг ImGui
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // Вывод на экран с вертикальной синхронизацией (1 - включена)
    HRESULT result = m_pSwapChain->Present(1, 0);
    if (FAILED(result))
    {
        OutputDebugString(L"Present failed\n");
    }
}

// Очистка ресурсов
void Cleanup()
{
    // Освобождаем ресурсы маленьких сфер
    for (int i = 0; i < 10; i++)
    {
        SAFE_RELEASE(m_pSmallSphereGeomBuffers[i]);
    }
    SAFE_RELEASE(m_pSmallSphereInputLayout);
    SAFE_RELEASE(m_pSmallSpherePixelShader);
    SAFE_RELEASE(m_pSmallSphereVertexShader);
    SAFE_RELEASE(m_pSmallSphereIndexBuffer);
    SAFE_RELEASE(m_pSmallSphereVertexBuffer);

    // Освобождаем карту нормалей
    SAFE_RELEASE(m_pTextureViewNM);
    SAFE_RELEASE(m_pTextureNM);

    // Освобождаем ресурсы детализации
    SAFE_RELEASE(m_pDetailTextureView);
    SAFE_RELEASE(m_pFlowTextureView);

    //Освобождаем ресурсы карт PBR
    SAFE_RELEASE(m_pRoughnessTextureView);
    SAFE_RELEASE(m_pMetalnessTextureView);

    // Освобождаем ресурсы постпроцессинга
    SAFE_RELEASE(m_pPostProcessBuffer);
    SAFE_RELEASE(m_pPostProcessPixelShader);
    SAFE_RELEASE(m_pPostProcessVertexShader);
    SAFE_RELEASE(m_pColorBufferSRV);
    SAFE_RELEASE(m_pColorBufferRTV);
    SAFE_RELEASE(m_pColorBuffer);

    // Освобождаем IBL ресурсы
    g_ibl.Shutdown();

    // Завершаем работу ImGui
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // Освобождаем blend states
    SAFE_RELEASE(m_pOpaqueBlendState);

    // Освобождаем состояния глубины
    SAFE_RELEASE(m_pNormalDepthState);
    SAFE_RELEASE(m_pSkyboxDepthState);

    SAFE_RELEASE(m_pSampler);
    SAFE_RELEASE(g_pSkyboxSampler);
    SAFE_RELEASE(g_pLinearSampler);
    SAFE_RELEASE(g_pLinearMipSampler);
    SAFE_RELEASE(m_pTextureView);
    SAFE_RELEASE(m_pTexture);

    SAFE_RELEASE(m_pRasterizerState);
    SAFE_RELEASE(m_pRasterizerCullFront);
    SAFE_RELEASE(m_pDepthStencilView);
    SAFE_RELEASE(m_pDepthBuffer);

    SAFE_RELEASE(m_pTerrainVertexBuffer);
    SAFE_RELEASE(m_pTerrainIndexBuffer);
    SAFE_RELEASE(m_pTerrainGeomBuffer);

    SAFE_RELEASE(m_pSceneBuffer);
    SAFE_RELEASE(m_pInputLayout);
    SAFE_RELEASE(m_pVertexShader);
    SAFE_RELEASE(m_pPixelShader);
    SAFE_RELEASE(m_pIndexBuffer);
    SAFE_RELEASE(m_pVertexBuffer);
    SAFE_RELEASE(m_pBackBufferRTV);
    SAFE_RELEASE(m_pSwapChain);
    SAFE_RELEASE(m_pDeviceContext);
    SAFE_RELEASE(m_pDevice);

    // Освобождаем ресурсы скайбокса
    SAFE_RELEASE(g_pSkyboxVS);
    SAFE_RELEASE(g_pSkyboxPS);
    SAFE_RELEASE(g_pSkyboxInputLayout);
    SAFE_RELEASE(g_pSkyboxVertexBuffer);
    SAFE_RELEASE(g_pSkyboxIndexBuffer);
    SAFE_RELEASE(g_pSkyboxConstantBuffer);

    // Завершаем COM
    CoUninitialize();
}