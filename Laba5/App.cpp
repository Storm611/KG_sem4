#include "App.h"
#include <cassert>
#include <cmath>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static void ThrowIfFailed(HRESULT hr, const char* msg = "D3D12 call failed") {
    if (FAILED(hr)) throw std::runtime_error(msg);
}
static UINT Align(UINT size, UINT alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

App::App(UINT width, UINT height, HWND hwnd)
    : m_width(width), m_height(height), m_hwnd(hwnd),
      m_aspectRatio((float)width / height),
      m_frameIndex(0), m_fenceEvent(nullptr),
      m_rtvDescSize(0), m_srvUavDescSize(0),
      m_pingPong(false), m_totalTime(0.f),
      m_camYaw(0.f), m_camPitch(0.35f), m_camDist(22.f)
{
    m_fenceValues[0] = m_fenceValues[1] = 0;
    m_emitterPos   = { 0.f, -6.f, 0.f };   
    m_sphereCenter = { 0.f,  1.f, 0.f };   
    m_sphereRadius = 6.f;              
    m_cameraPos    = { 0.f, 2.f, -m_camDist };
}
App::~App() { OnDestroy(); }

void App::OnInit() {
    CreateDevice();
    CreateCommandObjects();
    CreateSwapChain();
    CreateDescriptorHeaps();
    CreateDepthStencil();
    CreateParticleBuffers();
    CreateConstantBuffers();
    CreateComputePipelines();
    CreateRenderPipeline();
    CreateSpherePipeline();
    CreateIndirectArgs();

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* pp[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, pp);
    WaitForGpu();

    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));
}

void App::CreateDevice() {
#ifdef _DEBUG
    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
#endif
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)));
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d; adapter->GetDesc1(&d);
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) break;
    }
    if (!m_device)
        ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
}

void App::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_commandQueue)));
    for (UINT i = 0; i < FRAME_COUNT; i++)
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])));
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValues[m_frameIndex]++;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void App::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = m_width; sd.Height = m_height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferCount = FRAME_COUNT;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc  = { 1, 0 };
    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hwnd, &sd, nullptr, nullptr, &sc1));
    ThrowIfFailed(sc1.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rhd = {};
    rhd.NumDescriptors = FRAME_COUNT;
    rhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rhd, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, h);
        h.Offset(1, m_rtvDescSize);
    }
}

void App::CreateDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC dsvd = {};
    dsvd.NumDescriptors = 1;
    dsvd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvd, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC shd = {};
    shd.NumDescriptors = HEAP_SIZE;
    shd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    shd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&shd, IID_PPV_ARGS(&m_srvUavHeap)));
    m_srvUavDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void App::CreateDepthStencil() {
    D3D12_RESOURCE_DESC d = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE cv = {}; cv.Format = DXGI_FORMAT_D32_FLOAT; cv.DepthStencil = { 1.f, 0 };
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&m_depthStencil)));
    D3D12_DEPTH_STENCIL_VIEW_DESC dvd = {};
    dvd.Format = DXGI_FORMAT_D32_FLOAT; dvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(m_depthStencil.Get(), &dvd, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void App::CreateParticleBuffers() {
    auto MakeBuf = [&](ComPtr<ID3D12Resource>& buf, ComPtr<ID3D12Resource>& ctr,
                        const wchar_t* bn, const wchar_t* cn) {
        UINT64 sz = (UINT64)sizeof(Particle) * MAX_PARTICLES;
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(sz, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&buf)));
        buf->SetName(bn);

        CD3DX12_RESOURCE_DESC crd = CD3DX12_RESOURCE_DESC::Buffer(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &crd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&ctr)));
        ctr->SetName(cn);
    };
    MakeBuf(m_particleBufferA, m_particleCounterA, L"BufA", L"CtrA");
    MakeBuf(m_particleBufferB, m_particleCounterB, L"BufB", L"CtrB");

    {
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(4);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_counterReset)));
        UINT zero = 0; UINT8* p; CD3DX12_RANGE r(0,0);
        m_counterReset->Map(0, &r, (void**)&p);
        memcpy(p, &zero, 4);
        m_counterReset->Unmap(0, nullptr);
    }

    auto MakeDescs = [&](ID3D12Resource* buf, ID3D12Resource* ctr, UINT srvIdx, UINT uavIdx) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = DXGI_FORMAT_UNKNOWN;
        srvd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvd.Buffer.NumElements = MAX_PARTICLES;
        srvd.Buffer.StructureByteStride = sizeof(Particle);
        m_device->CreateShaderResourceView(buf, &srvd, SrvUavCpuHandle(srvIdx));

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format = DXGI_FORMAT_UNKNOWN;
        uavd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavd.Buffer.NumElements = MAX_PARTICLES;
        uavd.Buffer.StructureByteStride = sizeof(Particle);
        uavd.Buffer.CounterOffsetInBytes = 0;
        m_device->CreateUnorderedAccessView(buf, ctr, &uavd, SrvUavCpuHandle(uavIdx));
    };
    MakeDescs(m_particleBufferA.Get(), m_particleCounterA.Get(), SRV_A, UAV_A);
    MakeDescs(m_particleBufferB.Get(), m_particleCounterB.Get(), SRV_B, UAV_B);
}

void App::CreateConstantBuffers() {
    auto MakeCB = [&](ComPtr<ID3D12Resource>& res, UINT size, UINT8** mapped) {
        UINT aligned = Align(size, 256);
        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(aligned);
        ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)));
        CD3DX12_RANGE r(0,0);
        res->Map(0, &r, (void**)mapped);
    };
    MakeCB(m_emitCB,        sizeof(ParticleEmitConstants), &m_pEmitCBData);
    MakeCB(m_cameraCB,      sizeof(CameraConstants),       &m_pCameraCBData);
    MakeCB(m_sphereParamsCB,sizeof(SphereParams),          &m_pSphereParamsData);
}

ComPtr<ID3DBlob> App::CompileShader(const std::wstring& fn, const std::string& entry, const std::string& target) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> code, err;
    HRESULT hr = D3DCompileFromFile(fn.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry.c_str(), target.c_str(), flags, 0, &code, &err);
    if (FAILED(hr)) {
        std::string msg = "Shader compile failed: ";
        if (err) msg += (char*)err->GetBufferPointer();
        throw std::runtime_error(msg);
    }
    return code;
}

void App::CreateComputePipelines() {
    CD3DX12_DESCRIPTOR_RANGE ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsDescriptorTable(1, &ranges[0]);
    params[2].InitAsDescriptorTable(1, &ranges[1]);
    CD3DX12_ROOT_SIGNATURE_DESC rsd(3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSig)));

    auto emitBlob   = CompileShader(L"shaders/ParticleCS.hlsl", "EmitCS",   "cs_5_0");
    auto updateBlob = CompileShader(L"shaders/ParticleCS.hlsl", "UpdateCS", "cs_5_0");
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = {};
    cpsd.pRootSignature = m_computeRootSig.Get();
    cpsd.CS = { emitBlob->GetBufferPointer(), emitBlob->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&m_emitPSO)));
    cpsd.CS = { updateBlob->GetBufferPointer(), updateBlob->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&m_updatePSO)));
}

void App::CreateRenderPipeline() {
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER params[2];
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsDescriptorTable(1, &srvRange);
    CD3DX12_ROOT_SIGNATURE_DESC rsd(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_renderRootSig)));

    auto vsBlob = CompileShader(L"shaders/ParticleVS.hlsl", "main", "vs_5_0");
    auto gsBlob = CompileShader(L"shaders/ParticleGS.hlsl", "main", "gs_5_0");
    auto psBlob = CompileShader(L"shaders/ParticlePS.hlsl", "main", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.pRootSignature = m_renderRootSig.Get();
    psd.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psd.GS = { gsBlob->GetBufferPointer(), gsBlob->GetBufferSize() };
    psd.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psd.NumRenderTargets = 1;
    psd.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.DSVFormat        = DXGI_FORMAT_D32_FLOAT;
    psd.SampleDesc       = { 1, 0 };
    psd.SampleMask       = UINT_MAX;
    psd.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psd.BlendState.RenderTarget[0].BlendEnable    = TRUE;
    psd.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_ONE;
    psd.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
    psd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psd.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psd.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psd.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&m_renderPSO)));
}

void App::CreateSpherePipeline() {
   

    CD3DX12_ROOT_PARAMETER params[2];
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    CD3DX12_ROOT_SIGNATURE_DESC rsd(2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_sphereRootSig)));

    auto vsBlob = CompileShader(L"shaders/SphereVS.hlsl", "main", "vs_5_0");
    auto psBlob = CompileShader(L"shaders/SpherePS.hlsl", "main", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.pRootSignature        = m_sphereRootSig.Get();
    psd.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psd.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psd.NumRenderTargets      = 1;
    psd.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psd.SampleDesc            = { 1, 0 };
    psd.SampleMask            = UINT_MAX;
  
    psd.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psd.BlendState.RenderTarget[0].BlendEnable    = TRUE;
    psd.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
    psd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psd.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psd.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psd.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&m_spherePSO)));
}

void App::CreateIndirectArgs() {
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT)*4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, nullptr, IID_PPV_ARGS(&m_indirectArgsBuf)));

    {
        CD3DX12_HEAP_PROPERTIES hpu(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rdu = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT)*4);
        ThrowIfFailed(m_device->CreateCommittedResource(&hpu, D3D12_HEAP_FLAG_NONE, &rdu,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indirectArgsBufUpload)));
        UINT args[4] = { 0, 1, 0, 0 };
        UINT8* p; CD3DX12_RANGE r(0,0);
        m_indirectArgsBufUpload->Map(0, &r, (void**)&p);
        memcpy(p, args, sizeof(args));
        m_indirectArgsBufUpload->Unmap(0, nullptr);
    }

    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC csd = {};
    csd.ByteStride = sizeof(UINT)*4; csd.NumArgumentDescs = 1; csd.pArgumentDescs = &argDesc;
    ThrowIfFailed(m_device->CreateCommandSignature(&csd, nullptr, IID_PPV_ARGS(&m_drawCommandSig)));
}

D3D12_GPU_DESCRIPTOR_HANDLE App::SrvUavGpuHandle(UINT idx) {
    CD3DX12_GPU_DESCRIPTOR_HANDLE h(m_srvUavHeap->GetGPUDescriptorHandleForHeapStart());
    h.Offset(idx, m_srvUavDescSize); return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE App::SrvUavCpuHandle(UINT idx) {
    CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart());
    h.Offset(idx, m_srvUavDescSize); return h;
}

void App::OnUpdate(float dt) {
    m_totalTime += dt;
    m_camYaw += dt * 0.25f;
    float cx = sinf(m_camYaw) * cosf(m_camPitch) * m_camDist;
    float cy = sinf(m_camPitch) * m_camDist;
    float cz = cosf(m_camYaw) * cosf(m_camPitch) * m_camDist;
    m_cameraPos = { cx, cy, cz };

    XMVECTOR eye = XMLoadFloat3(&m_cameraPos);
    XMVECTOR at  = XMVectorSet(0,0,0,1);
    XMVECTOR up  = XMVectorSet(0,1,0,0);
    auto V  = XMMatrixLookAtLH(eye, at, up);
    auto P  = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), m_aspectRatio, 0.1f, 500.f);
    CameraConstants cam;
    cam.View     = XMMatrixTranspose(V);
    cam.Proj     = XMMatrixTranspose(P);
    cam.ViewProj = XMMatrixMultiplyTranspose(V, P);
    XMStoreFloat3(&cam.CameraPos, eye);
    cam.pad = 0;
    memcpy(m_pCameraCBData, &cam, sizeof(cam));

    ParticleEmitConstants ec;
    ec.EmitterPos   = m_emitterPos;
    ec.DeltaTime    = dt;
    ec.SphereCenter = m_sphereCenter;
    ec.SphereRadius = m_sphereRadius;
    ec.MaxParticles = MAX_PARTICLES;
    ec.EmitCount    = 200;
    ec.pad0 = ec.pad1 = 0;
    memcpy(m_pEmitCBData, &ec, sizeof(ec));

    SphereParams sp;
    sp.SphereCenter = m_sphereCenter;
    sp.SphereRadius = m_sphereRadius;
    memcpy(m_pSphereParamsData, &sp, sizeof(sp));
}

void App::OnRender() {
    auto* cmd = m_commandList.Get();
    bool frameOdd = m_pingPong;
    ID3D12Resource* appendBuf     = frameOdd ? m_particleBufferB.Get()  : m_particleBufferA.Get();
    ID3D12Resource* appendCounter = frameOdd ? m_particleCounterB.Get() : m_particleCounterA.Get();
    ID3D12Resource* consumeBuf    = frameOdd ? m_particleBufferA.Get()  : m_particleBufferB.Get();
    UINT uavAppendIdx             = frameOdd ? UAV_B : UAV_A;
    UINT srvConsumeIdx            = frameOdd ? SRV_A : SRV_B;

    
    {
        D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(appendCounter,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->ResourceBarrier(1, &b);
        cmd->CopyBufferRegion(appendCounter, 0, m_counterReset.Get(), 0, 4);
        b = CD3DX12_RESOURCE_BARRIER::Transition(appendCounter,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &b);
    }

    {
        D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(consumeBuf,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(1, &b);
    }

    ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);


    cmd->SetPipelineState(m_updatePSO.Get());
    cmd->SetComputeRootSignature(m_computeRootSig.Get());
    cmd->SetComputeRootConstantBufferView(0, m_emitCB->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(1, SrvUavGpuHandle(uavAppendIdx));
    cmd->SetComputeRootDescriptorTable(2, SrvUavGpuHandle(srvConsumeIdx));
    cmd->Dispatch((MAX_PARTICLES + 63) / 64, 1, 1);


    cmd->SetPipelineState(m_emitPSO.Get());
    cmd->SetComputeRootConstantBufferView(0, m_emitCB->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(1, SrvUavGpuHandle(uavAppendIdx));
    cmd->SetComputeRootDescriptorTable(2, SrvUavGpuHandle(srvConsumeIdx));
    cmd->Dispatch(4, 1, 1);


    {
        D3D12_RESOURCE_BARRIER barriers[2] = {
            CD3DX12_RESOURCE_BARRIER::UAV(appendBuf),
            CD3DX12_RESOURCE_BARRIER::Transition(appendBuf,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        };
        cmd->ResourceBarrier(2, barriers);
    }


    {
        D3D12_RESOURCE_BARRIER barriers[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_indirectArgsBuf.Get(),
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(appendCounter,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE)
        };
        cmd->ResourceBarrier(2, barriers);
        cmd->CopyBufferRegion(m_indirectArgsBuf.Get(), 0, m_indirectArgsBufUpload.Get(), 0, sizeof(UINT)*4);
        cmd->CopyBufferRegion(m_indirectArgsBuf.Get(), 0, appendCounter, 0, sizeof(UINT));
        D3D12_RESOURCE_BARRIER barriers2[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_indirectArgsBuf.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
            CD3DX12_RESOURCE_BARRIER::Transition(appendCounter,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        cmd->ResourceBarrier(2, barriers2);
    }


    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = DXGI_FORMAT_UNKNOWN;
        srvd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvd.Buffer.NumElements = MAX_PARTICLES;
        srvd.Buffer.StructureByteStride = sizeof(Particle);
        m_device->CreateShaderResourceView(appendBuf, &srvd, SrvUavCpuHandle(SRV_RENDER));
    }


    auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescSize);
    auto dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    {
        D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &b);
    }

    float clearColor[4] = { 0.01f, 0.01f, 0.02f, 1.0f };
    cmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT vp = { 0,0,(float)m_width,(float)m_height,0,1 };
    D3D12_RECT     sr = { 0,0,(LONG)m_width,(LONG)m_height };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sr);


    cmd->SetPipelineState(m_renderPSO.Get());
    cmd->SetGraphicsRootSignature(m_renderRootSig.Get());
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootConstantBufferView(0, m_cameraCB->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, SrvUavGpuHandle(SRV_RENDER));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmd->ExecuteIndirect(m_drawCommandSig.Get(), 1, m_indirectArgsBuf.Get(), 0, nullptr, 0);


    if (m_sphereRadius > 0.0f) {
        cmd->SetPipelineState(m_spherePSO.Get());
        cmd->SetGraphicsRootSignature(m_sphereRootSig.Get());
        cmd->SetGraphicsRootConstantBufferView(0, m_cameraCB->GetGPUVirtualAddress());
        cmd->SetGraphicsRootConstantBufferView(1, m_sphereParamsCB->GetGPUVirtualAddress());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

        const int SEGS=32, RINGS=8, MERIDS=8;
        UINT vertCount = (RINGS * SEGS + MERIDS * SEGS) * 2;
        cmd->DrawInstanced(vertCount, 1, 0, 0);
    }


    {
        D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(appendBuf,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &b);
    }
    {
        D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(consumeBuf,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &b);
    }
    {
        D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
            m_renderTargets[m_frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        cmd->ResourceBarrier(1, &b);
    }

    ThrowIfFailed(cmd->Close());
    ID3D12CommandList* pp[] = { cmd };
    m_commandQueue->ExecuteCommandLists(1, pp);
    ThrowIfFailed(m_swapChain->Present(1, 0));
    MoveToNextFrame();
    m_pingPong = !m_pingPong;

    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
    ThrowIfFailed(cmd->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));
}

void App::WaitForGpu() {
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObject(m_fenceEvent, INFINITE);
    m_fenceValues[m_frameIndex]++;
}

void App::MoveToNextFrame() {
    const UINT64 cur = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), cur));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_fenceValues[m_frameIndex] = cur + 1;
}

void App::OnDestroy() {
    WaitForGpu();
    CloseHandle(m_fenceEvent);
}
