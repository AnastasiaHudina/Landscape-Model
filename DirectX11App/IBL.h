#pragma once

#include <d3d11.h>
#include <DirectXMath.h>

#include "stb_image.h"

// Простая структура для хранения всех IBL ресурсов
struct IBLResources
{
    ID3D11ShaderResourceView* hdrSRV = nullptr;
    ID3D11ShaderResourceView* skyboxSRV;  // для текстуры скайбокса

    ID3D11Texture2D* cubeMap = nullptr;
    ID3D11ShaderResourceView* cubeMapSRV = nullptr;

    ID3D11Texture2D* irradianceMap = nullptr;
    ID3D11ShaderResourceView* irradianceSRV = nullptr;

    ID3D11Texture2D* prefilteredMap = nullptr;
    ID3D11ShaderResourceView* prefilteredSRV = nullptr;

    ID3D11Texture2D* brdfLUT = nullptr;
    ID3D11ShaderResourceView* brdfLUTSRV = nullptr;
};

// Стадии генерации IBL (чтобы разбить нагрузку на кадры)
enum IBLStage
{
    IBL_STAGE_NONE = 0,
    IBL_STAGE_CUBEMAP,
    IBL_STAGE_IRRADIANCE,
    IBL_STAGE_PREFILTER,
    IBL_STAGE_BRDF,
    IBL_STAGE_DONE
};

class IBL
{
public:
    IBL();
    ~IBL();

    // Инициализация (создание ресурсов и загрузка HDR)
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

    // Обновление (вызывается каждый кадр)
    void Update();
    void Shutdown();

    // Доступ к результатам
    const IBLResources& GetResources() const { return m_resources; }

    bool IsReady() const { return m_stage == IBL_STAGE_DONE; }
    DirectX::XMFLOAT3 GetSunDirection() const { return m_sunDirection; }

private:

    DirectX::XMMATRIX m_projCubemap; // projection матрица для генерации cubemap
    // Направление на самый яркий источник (солнце/луна) в мировых координатах (нормализовано)
    DirectX::XMFLOAT3 m_sunDirection = { 0.0f, -1.0f, 0.0f };

    // Указатели на device/context (теперь не глобальные)
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11Buffer* m_irradianceCB = nullptr;
    ID3D11Buffer* m_prefilterCB = nullptr;
    ID3D11Buffer* m_pCubeMapCB = nullptr;

    // Ресурсы
    IBLResources m_resources;

    // Текущая стадия
    IBLStage m_stage = IBL_STAGE_NONE;

    // Счётчики для постепенной генерации
    int m_currentFace = 0;
    int m_currentMip = 0;

    // Константы разрешений (должны совпадать с объявленными в шейдерах)
    //const UINT CUBEMAP_RES = 512;      // разрешение исходного кубмапа и HDR текстуры
    //const UINT PREFILTERED_RES = 128;      // разрешение prefiltered карты (максимальный mip)
    //const UINT IRRADIANCE_RES = 64;       // разрешение irradiance карты
    //const UINT BRDF_LUT_RES = 512;      // разрешение BRDF LUT

    // Размеры (снижены для избежания TDR)
    static const UINT CUBEMAP_RES = 256;
    static const UINT IRRADIANCE_RES = 128;   // было 64
    static const UINT IRRADIANCE_MIPS = 1;
    static const UINT PREFILTERED_RES = 256;
    const UINT PREFILTERED_MIPS = 6;        // количество mip-уровней в prefiltered карте
    static const UINT BRDF_LUT_RES = 128;


    // === Внутренние шаги генерации ===

    void RenderCubemapFace();
    void RenderIrradianceFace();
    void RenderPrefilterStep();
    void RenderBRDF();
    bool LoadHDRTexture(const wchar_t* filename);
    bool CreateSkyboxFromHDRTexture();
    bool LoadSkyboxTexture(const wchar_t* filename);

    // Переход к следующей стадии
    void NextStage();
};



