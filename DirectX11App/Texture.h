#pragma once

#include <string>
#include <cstdint>
#include <dxgi.h>
#include <d3d11.h>

struct TextureDesc
{
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

// Функции для работы с DDS
bool LoadDDS(const std::wstring& filepath, TextureDesc& desc, bool singleMip = false);
UINT32 GetBytesPerBlock(const DXGI_FORMAT& fmt);
UINT32 DivUp(UINT32 a, UINT32 b);
std::string WCSToMBS(const std::wstring& wstr);

// Функция для загрузки PNG через WIC (с генерацией MIP-уровней)
bool LoadPNG(const std::wstring& filepath, TextureDesc& desc, bool singleMip = false);