#pragma once
#include "DXHelpers.h"


class GBuffer
{
public:
    static const UINT kNumRenderTargets = 3;

    void Init(ID3D12Device* device, UINT width, UINT height);
    void Resize(ID3D12Device* device, UINT width, UINT height);


    void TransitionToWrite(ID3D12GraphicsCommandList* cmdList);


    void TransitionToRead(ID3D12GraphicsCommandList* cmdList);

    void Clear(ID3D12GraphicsCommandList* cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE RTVHandle(UINT index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE DSVHandle() const;


    void CreateSRVs(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destStart, UINT descriptorSize);

    ID3D12Resource* GetRenderTarget(UINT index) const { return m_renderTargets[index].Get(); }

private:
    void CreateResources(ID3D12Device* device, UINT width, UINT height);

    ComPtr<ID3D12Resource> m_renderTargets[kNumRenderTargets];
    ComPtr<ID3D12Resource> m_depthStencil;

    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    UINT m_rtvDescriptorSize = 0;

    UINT m_width = 0;
    UINT m_height = 0;

    bool m_currentlyWritable = true;

    static const DXGI_FORMAT kFormats[kNumRenderTargets];
};
