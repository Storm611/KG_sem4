#include "RenderingSystem.h"
#include <d3dx12.h> // user-provided helper header, place in external/
#include <DirectXColors.h>

void RenderingSystem::Init(HWND hwnd, UINT width, UINT height)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    CreateDeviceAndQueue();
    CreateCommandObjects();
    CreateSwapChain(hwnd);
    CreateDescriptorHeaps();
    CreateBackBufferRTVs();
    CreateRootSignatures();
    CreatePipelineStates();

    m_gbuffer.Init(m_device.Get(), width, height);
    CreateGBufferSRVs();

    // Camera
    m_camera.SetLens(XM_PIDIV4, (float)width / (float)height, 0.1f, 500.0f);
    m_camera.SetPosition({ 0.0f, 3.0f, -8.0f });
    m_camera.UpdateViewMatrix();

    // Open the command list once so scene loading can record GPU upload commands.
    ThrowIfFailedM(m_commandAllocators[m_frameIndex]->Reset());
    ThrowIfFailedM(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

    LoadScene();
    CreateConstantBuffers();

    ThrowIfFailedM(m_commandList->Close());
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    WaitForGPU(); // make sure geometry upload finished before first Render()
}

void RenderingSystem::CreateDeviceAndQueue()
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();

        // NOTE: GPU-based validation was used temporarily to diagnose the TDR
        // (confirmed: DXGI_ERROR_DEVICE_HUNG, no page fault -> genuine perf issue,
        // not memory corruption). Left disabled by default since it adds heavy
        // overhead on its own and can make marginal frame times worse.
        // ComPtr<ID3D12Debug1> debugController1;
        // if (SUCCEEDED(debugController.As(&debugController1)))
        //     debugController1->SetEnableGPUBasedValidation(TRUE);
    }

    // DRED: if the device is still removed/hung, this lets us report *which*
    // GPU operation (draw call / resource) caused it instead of just the
    // generic DXGI_ERROR_DEVICE_REMOVED code.
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))))
    {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
#endif

    ThrowIfFailedM(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            break;
        adapter.Reset();
    }
    if (!m_device)
        ThrowIfFailedM(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailedM(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    ThrowIfFailedM(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void RenderingSystem::CreateCommandObjects()
{
    for (UINT i = 0; i < kFrameCount; i++)
        ThrowIfFailedM(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocators[i])));

    ThrowIfFailedM(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    ThrowIfFailedM(m_commandList->Close());
}

void RenderingSystem::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = kFrameCount;
    scDesc.Width = m_width;
    scDesc.Height = m_height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailedM(m_factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &swapChain1));
    ThrowIfFailedM(m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailedM(swapChain1.As(&m_swapChain));

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void RenderingSystem::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = kFrameCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailedM(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_backBufferRTVHeap)));
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Shader-visible heap for the 3 GBuffer SRVs sampled by the lighting pass.
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = GBuffer::kNumRenderTargets;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailedM(m_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)));
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc = {};
    samplerDesc.NumDescriptors = 1;
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailedM(m_device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(&m_samplerHeap)));
}

void RenderingSystem::CreateBackBufferRTVs()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_backBufferRTVHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kFrameCount; i++)
    {
        ThrowIfFailedM(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, handle);
        handle.Offset(1, m_rtvDescriptorSize);
    }
}

void RenderingSystem::CreateGBufferSRVs()
{
    m_gbuffer.CreateSRVs(m_device.Get(), m_srvHeap->GetCPUDescriptorHandleForHeapStart(), m_srvDescriptorSize);
}

// ---------------------------------------------------------------------------
// Root signatures
// ---------------------------------------------------------------------------
void RenderingSystem::CreateRootSignatures()
{
    // --- GBuffer geometry pass: b0 object constants, b1 material constants ---
    {
        CD3DX12_ROOT_PARAMETER params[2];
        params[0].InitAsConstantBufferView(0); // b0
        params[1].InitAsConstantBufferView(1); // b1

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sig, error;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &error);
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailedM(hr);
        ThrowIfFailedM(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(&m_gbufferRootSig)));
    }

    // --- Deferred lighting pass: b0 frame constants, t0-t2 GBuffer table,
    //     t3 structured light buffer (root SRV), s0 static point sampler ---
    {
        CD3DX12_DESCRIPTOR_RANGE gbufferRange;
        gbufferRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBuffer::kNumRenderTargets, /*t0*/0);

        CD3DX12_ROOT_PARAMETER params[3];
        params[0].InitAsConstantBufferView(0);                 // b0
        params[1].InitAsDescriptorTable(1, &gbufferRange);     // t0..t2
        params[2].InitAsShaderResourceView(3);                 // t3 (light structured buffer)

        CD3DX12_STATIC_SAMPLER_DESC sampler(
            0, D3D12_FILTER_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(3, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sig, error;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &error);
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailedM(hr);
        ThrowIfFailedM(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(&m_deferredRootSig)));
    }

    // --- Light marker billboards: b0 marker constants, t0 light structured buffer (root SRV) ---
    {
        CD3DX12_ROOT_PARAMETER params[2];
        params[0].InitAsConstantBufferView(0); // b0
        params[1].InitAsShaderResourceView(0); // t0

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sig, error;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &error);
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        ThrowIfFailedM(hr);
        ThrowIfFailedM(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(&m_markerRootSig)));
    }
}

// ---------------------------------------------------------------------------
// PSOs
// ---------------------------------------------------------------------------
void RenderingSystem::CreatePipelineStates()
{
    // GBuffer PSO
    {
        auto vs = CompileShader(L"shaders/GBuffer_VS.hlsl", nullptr, "main", "vs_5_1");
        auto ps = CompileShader(L"shaders/GBuffer_PS.hlsl", nullptr, "main", "ps_5_1");

        // D3D12_INPUT_ELEMENT_DESC fields, in order:
        // SemanticName, SemanticIndex, Format, InputSlot, AlignedByteOffset, InputSlotClass, InstanceDataStepRate
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { layout, _countof(layout) };
        psoDesc.pRootSignature = m_gbufferRootSig.Get();
        psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = GBuffer::kNumRenderTargets;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.RTVFormats[2] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailedM(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_gbufferPSO)));
    }

    // Deferred lighting PSO (fullscreen triangle, no depth test, no input layout)
    {
        auto vs = CompileShader(L"shaders/Deferred_VS.hlsl", nullptr, "main", "vs_5_1");
        auto ps = CompileShader(L"shaders/Deferred_PS.hlsl", nullptr, "main", "ps_5_1");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { nullptr, 0 };
        psoDesc.pRootSignature = m_deferredRootSig.Get();
        psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailedM(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_deferredPSO)));
    }

    // Light marker PSO: additive-blended billboards, depth-tested against the
    // GBuffer depth (so markers hide behind walls) but never depth-written.
    {
        auto vs = CompileShader(L"shaders/LightMarkers_VS.hlsl", nullptr, "main", "vs_5_1");
        auto ps = CompileShader(L"shaders/LightMarkers_PS.hlsl", nullptr, "main", "ps_5_1");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { nullptr, 0 };
        psoDesc.pRootSignature = m_markerRootSig.Get();
        psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailedM(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_markerPSO)));
    }
}
