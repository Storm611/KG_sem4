#include "GBuffer.h"
#include <d3dx12.h> // user-provided helper header, place in external/

const DXGI_FORMAT GBuffer::kFormats[GBuffer::kNumRenderTargets] = {
    DXGI_FORMAT_R8G8B8A8_UNORM,   // Albedo.rgb + Roughness.a
    DXGI_FORMAT_R16G16B16A16_FLOAT, // Normal.rgb + Metallic.a
    DXGI_FORMAT_R32G32B32A32_FLOAT  // WorldPos.rgb
};

void GBuffer::Init(ID3D12Device* device, UINT width, UINT height)
{
    // RTV heap: 3 render targets, not shader visible
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = kNumRenderTargets;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailedM(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailedM(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    CreateResources(device, width, height);
}

void GBuffer::Resize(ID3D12Device* device, UINT width, UINT height)
{
    if (width == m_width && height == m_height) return;
    CreateResources(device, width, height);
}

void GBuffer::CreateResources(ID3D12Device* device, UINT width, UINT height)
{
    m_width = width;
    m_height = height;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < kNumRenderTargets; i++)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = kFormats[i];
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = kFormats[i];
        clear.Color[0] = clear.Color[1] = clear.Color[2] = clear.Color[3] = 0.0f;

        ThrowIfFailedM(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
            IID_PPV_ARGS(&m_renderTargets[i])));

        device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
    m_currentlyWritable = false; // resources created already in PIXEL_SHADER_RESOURCE state

    // Depth buffer
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailedM(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
        IID_PPV_ARGS(&m_depthStencil)));

    device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void GBuffer::TransitionToWrite(ID3D12GraphicsCommandList* cmdList)
{
    if (m_currentlyWritable) return;

    D3D12_RESOURCE_BARRIER barriers[kNumRenderTargets];
    for (UINT i = 0; i < kNumRenderTargets; i++)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[i].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    cmdList->ResourceBarrier(kNumRenderTargets, barriers);
    m_currentlyWritable = true;
}

void GBuffer::TransitionToRead(ID3D12GraphicsCommandList* cmdList)
{
    if (!m_currentlyWritable) return;

    D3D12_RESOURCE_BARRIER barriers[kNumRenderTargets];
    for (UINT i = 0; i < kNumRenderTargets; i++)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[i].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    cmdList->ResourceBarrier(kNumRenderTargets, barriers);
    m_currentlyWritable = false;
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmdList)
{
    const float clearColor[4] = { 0, 0, 0, 0 };
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kNumRenderTargets; i++)
    {
        cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
    cmdList->ClearDepthStencilView(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
        D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::RTVHandle(UINT index) const
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    handle.Offset(index, m_rtvDescriptorSize);
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::DSVHandle() const
{
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void GBuffer::CreateSRVs(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destStart, UINT descriptorSize)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(destStart);
    for (UINT i = 0; i < kNumRenderTargets; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = kFormats[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(m_renderTargets[i].Get(), &srvDesc, handle);
        handle.Offset(1, descriptorSize);
    }
}
