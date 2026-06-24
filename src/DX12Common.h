#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include "d3dx12.h"

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <stdexcept>
#include <string>
#include <cassert>
#include <vector>
#include <memory>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;


inline void ThrowIfFailed(HRESULT hr,
    const char* file = __builtin_FILE(),
    int         line = __builtin_LINE())
{
    if (FAILED(hr))
    {
        char buf[256];
        sprintf_s(buf, "HRESULT 0x%08X\n%s : %d", (unsigned)hr, file, line);
        MessageBoxA(nullptr, buf, "DX12 Error", MB_OK | MB_ICONERROR);
        throw std::runtime_error(buf);
    }
}


struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
};


struct ObjectConstants
{
    XMFLOAT4X4 world;
    XMFLOAT4   color;
    int        isBox;
    float      pad[3];
};


inline ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size)
{
    ComPtr<ID3D12Resource> buf;
    auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto dsc = CD3DX12_RESOURCE_DESC::Buffer(size);
    ThrowIfFailed(device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &dsc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&buf)));
    return buf;
}


inline ComPtr<ID3D12Resource> CreateDefaultBuffer(
    ID3D12Device*              device,
    ID3D12GraphicsCommandList* cmdList,
    const void*                data,
    UINT64                     byteSize,
    ComPtr<ID3D12Resource>&    uploadBuffer)
{
   
    ComPtr<ID3D12Resource> gpuBuf;
    {
        auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto dsc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &dsc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&gpuBuf)));
    }

    
    {
        auto hp  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto dsc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &dsc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer)));
    }

   
    D3D12_SUBRESOURCE_DATA sub = {};
    sub.pData      = data;
    sub.RowPitch   = (LONG_PTR)byteSize;
    sub.SlicePitch = sub.RowPitch;
    UpdateSubresources(cmdList, gpuBuf.Get(), uploadBuffer.Get(), 0, 0, 1, &sub);

  
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        gpuBuf.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &barrier);

    return gpuBuf;
}
